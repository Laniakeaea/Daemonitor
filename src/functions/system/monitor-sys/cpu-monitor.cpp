// cpu-monitor.cpp — PDH per-core CPU usage + CPUID brand + process top-N + WMI temp
#include "functions/system/monitor-sys/cpu-monitor.h"

#define WIN32_LEAN_AND_MEAN
#include <Windows.h>
#include <Pdh.h>
#include <TlHelp32.h>
#include <intrin.h>
#include <comdef.h>
#include <Wbemidl.h>
#include <cstdio>
#include <cstring>

#pragma comment(lib, "pdh.lib")
#pragma comment(lib, "wbemuuid.lib")

namespace cpu {

// ── Internal globals ────────────────────────────────────
static Config   g_cfg;
static Snapshot g_snap;

static PDH_HQUERY   g_query        = nullptr;
static PDH_HCOUNTER g_counterTotal = nullptr;
static PDH_HCOUNTER g_counters[CPU_MAX_CORES]{};
static uint32_t     g_coreCount    = 0;

// ── WMI globals for temperature ─────────────────────────
static IWbemServices*  g_wmiSvc  = nullptr;
static IWbemLocator*   g_wmiLoc  = nullptr;

// ── CPUID + registry ────────────────────────────────────
static void cpuid_info(char brand[CPU_NAME_MAX]) {
    int regs[4] = {0};
    char* dst = brand;
    for (int leaf = 0x80000002; leaf <= 0x80000004; ++leaf) {
        __cpuidex(regs, leaf, 0);
        *reinterpret_cast<int*>(dst)      = regs[0];
        *reinterpret_cast<int*>(dst + 4)  = regs[1];
        *reinterpret_cast<int*>(dst + 8)  = regs[2];
        *reinterpret_cast<int*>(dst + 12) = regs[3];
        dst += 16;
    }
    brand[CPU_NAME_MAX - 1] = '\0';
}

static float freqBaseMHz() {
    DWORD mhz = 0, sz = sizeof(mhz);
    RegGetValueW(HKEY_LOCAL_MACHINE,
        L"HARDWARE\\DESCRIPTION\\System\\CentralProcessor\\0",
        L"~MHz", RRF_RT_REG_DWORD, nullptr, &mhz, &sz);
    return (float)mhz;
}

static float freqCurrentMHz() {
    DWORD mhz = 0, sz = sizeof(mhz);
    RegGetValueW(HKEY_LOCAL_MACHINE,
        L"HARDWARE\\DESCRIPTION\\System\\CentralProcessor\\0",
        L"~MHz", RRF_RT_REG_DWORD, nullptr, &mhz, &sz);
    return (float)mhz;
}

// ── Multi-strategy WMI temperature ──────────────────────
// ROOT\\WMI  MSAcpi_ThermalZoneTemperature → Kelvin×10
// ROOT\\CIMV2  Win32_PerfFormattedData_Counters_ThermalZoneInformation → °C
static float readTempWMI(IWbemServices* rootWmi, IWbemServices* rootCimv2) {
    // Strategy 1: MSAcpi_ThermalZoneTemperature (desktop/laptop)
    if (rootWmi) {
        IEnumWbemClassObject* pEnum = nullptr;
        HRESULT hr = rootWmi->ExecQuery(
            _bstr_t(L"WQL"),
            _bstr_t(L"SELECT CurrentTemperature FROM MSAcpi_ThermalZoneTemperature"),
            WBEM_FLAG_FORWARD_ONLY | WBEM_FLAG_RETURN_IMMEDIATELY,
            nullptr, &pEnum);
        if (SUCCEEDED(hr) && pEnum) {
            float best = 0.0f;
            IWbemClassObject* pObj = nullptr;
            ULONG ret = 0;
            while (pEnum->Next(WBEM_INFINITE, 1, &pObj, &ret) == S_OK) {
                VARIANT vt; VariantInit(&vt);
                if (SUCCEEDED(pObj->Get(L"CurrentTemperature", 0, &vt, nullptr, nullptr))) {
                    float t = 0.0f;
                    if (vt.vt == VT_I4 || vt.vt == VT_UI4)
                        t = (float)vt.uintVal / 10.0f - 273.15f;
                    else if (vt.vt == VT_R8)
                        t = (float)vt.dblVal / 10.0f - 273.15f;
                    if (t > best) best = t;
                    VariantClear(&vt);
                }
                pObj->Release();
            }
            pEnum->Release();
            if (best > 0.0f) return best;
        }
    }
    // Strategy 2: Win32_PerfFormattedData_Counters_ThermalZoneInformation
    if (rootCimv2) {
        IEnumWbemClassObject* pEnum = nullptr;
        HRESULT hr = rootCimv2->ExecQuery(
            _bstr_t(L"WQL"),
            _bstr_t(L"SELECT PercentPassiveLimit, HighPrecisionTemperature FROM Win32_PerfFormattedData_Counters_ThermalZoneInformation"),
            WBEM_FLAG_FORWARD_ONLY | WBEM_FLAG_RETURN_IMMEDIATELY,
            nullptr, &pEnum);
        if (SUCCEEDED(hr) && pEnum) {
            float best = 0.0f;
            IWbemClassObject* pObj = nullptr;
            ULONG ret = 0;
            while (pEnum->Next(WBEM_INFINITE, 1, &pObj, &ret) == S_OK) {
                VARIANT vt; VariantInit(&vt);
                if (SUCCEEDED(pObj->Get(L"HighPrecisionTemperature", 0, &vt, nullptr, nullptr))) {
                    float t = 0.0f;
                    if (vt.vt == VT_I4 || vt.vt == VT_UI4)
                        t = (float)vt.uintVal / 10.0f - 273.15f;
                    VariantClear(&vt);
                    if (t > best) best = t;
                }
                pObj->Release();
            }
            pEnum->Release();
            if (best > 0.0f) return best;
        }
    }
    return -1.0f;  // not available
}
static float readTemp() { return readTempWMI(g_wmiSvc, nullptr); }

// ── Top processes by CPU delta ──────────────────────────
struct ProcCpuData { char name[32]; uint32_t pid; uint64_t prevKernel; uint64_t prevUser; uint64_t prevTotal; };
static ProcCpuData g_procPrev[256]{};
static uint32_t    g_procPrevCnt = 0;

static void sampleTopProcs() {
    if (!g_cfg.processTopEnabled) return;
    HANDLE snap = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
    if (snap == INVALID_HANDLE_VALUE) return;

    PROCESSENTRY32W pe{ sizeof(pe) };
    ProcCpuData cur[256]{};
    int curCnt = 0;

    if (Process32FirstW(snap, &pe)) {
        do {
            if (curCnt >= 256) break;
            HANDLE h = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, pe.th32ProcessID);
            if (h) {
                FILETIME ct, ex, kr, us;
                if (GetProcessTimes(h, &ct, &ex, &kr, &us)) {
                    auto toU64 = [](FILETIME& f) { return ((uint64_t)f.dwHighDateTime << 32) | f.dwLowDateTime; };
                    cur[curCnt].prevKernel = toU64(kr);
                    cur[curCnt].prevUser   = toU64(us);
                    cur[curCnt].prevTotal  = cur[curCnt].prevKernel + cur[curCnt].prevUser;
                }
                WideCharToMultiByte(CP_UTF8, 0, pe.szExeFile, -1, cur[curCnt].name, 32, nullptr, nullptr);
                cur[curCnt].pid = pe.th32ProcessID;
                ++curCnt;
                CloseHandle(h);
            }
        } while (Process32NextW(snap, &pe));
    }
    CloseHandle(snap);

    // For each current process, compute delta against previous
    uint64_t deltas[256]{};
    uint64_t totalDelta = 0;
    for (int i = 0; i < curCnt; ++i) {
        for (uint32_t j = 0; j < g_procPrevCnt; ++j) {
            if (g_procPrev[j].pid == cur[i].pid) {
                uint64_t dt = (cur[i].prevKernel + cur[i].prevUser)
                            - (g_procPrev[j].prevKernel + g_procPrev[j].prevUser);
                // Clamp small negatives
                deltas[i] = (cur[i].prevKernel >= g_procPrev[j].prevKernel
                          && cur[i].prevUser >= g_procPrev[j].prevUser) ? dt : 0;
                totalDelta += deltas[i];
                break;
            }
        }
    }

    // Sort by delta desc (simple bubble, max 256 items)
    for (int i = 0; i < curCnt - 1; ++i)
        for (int j = i + 1; j < curCnt; ++j)
            if (deltas[j] > deltas[i]) {
                uint64_t td = deltas[i]; deltas[i] = deltas[j]; deltas[j] = td;
                ProcCpuData tp = cur[i]; cur[i] = cur[j]; cur[j] = tp;
            }

    uint32_t n = g_cfg.processTopN;
    if (n > CPU_MAX_TOP_PROCS) n = CPU_MAX_TOP_PROCS;
    if ((uint32_t)curCnt < n) n = (uint32_t)curCnt;
    g_snap.topProcCount = n;

    for (uint32_t i = 0; i < n; ++i) {
        strncpy_s(g_snap.topProcs[i].name, cur[i].name, 31);
        g_snap.topProcs[i].pid       = cur[i].pid;
        g_snap.topProcs[i].cpuPercent = totalDelta > 0
            ? (float)((double)deltas[i] / (double)totalDelta * g_snap.usagePercent)
            : 0.0f;
    }

    // Save for next tick
    g_procPrevCnt = (uint32_t)curCnt;
    for (int i = 0; i < curCnt && i < 256; ++i) g_procPrev[i] = cur[i];
}

// ═══════════════════════════════════════════════════════
// Public API
// ═══════════════════════════════════════════════════════

bool init(Config* cfg) {
    if (cfg) g_cfg = *cfg;
    if (!g_cfg.enabled) return true;

    cpuid_info(g_snap.brand);
    g_snap.freqBaseMHz = freqBaseMHz();

    SYSTEM_INFO si;
    GetSystemInfo(&si);
    g_coreCount = si.dwNumberOfProcessors;
    g_snap.coreCount = g_coreCount;

    PdhOpenQueryW(nullptr, 0, &g_query);
    PdhAddEnglishCounterW(g_query, L"\\Processor(_Total)\\% Processor Time", 0, &g_counterTotal);

    if (g_cfg.perCoreEnabled) {
        for (uint32_t i = 0; i < g_coreCount && i < CPU_MAX_CORES; ++i) {
            wchar_t path[128];
            _snwprintf_s(path, _TRUNCATE, L"\\Processor(%u)\\%% Processor Time", i);
            PdhAddEnglishCounterW(g_query, path, 0, &g_counters[i]);
        }
    }
    PdhCollectQueryData(g_query);

    // ── Init WMI for temperature ──────────────────────────
    HRESULT hr = CoInitializeEx(nullptr, COINIT_MULTITHREADED);
    if (SUCCEEDED(hr) || hr == S_FALSE || hr == RPC_E_CHANGED_MODE) {
        hr = CoCreateInstance(CLSID_WbemLocator, nullptr, CLSCTX_INPROC_SERVER,
                              IID_IWbemLocator, (void**)&g_wmiLoc);
        if (SUCCEEDED(hr) && g_wmiLoc) {
            hr = g_wmiLoc->ConnectServer(
                _bstr_t(L"ROOT\\WMI"), nullptr, nullptr, nullptr,
                0, nullptr, nullptr, &g_wmiSvc);
            if (SUCCEEDED(hr) && g_wmiSvc) {
                CoSetProxyBlanket(g_wmiSvc, RPC_C_AUTHN_WINNT, RPC_C_AUTHZ_NONE,
                                  nullptr, RPC_C_AUTHN_LEVEL_CALL,
                                  RPC_C_IMP_LEVEL_IMPERSONATE, nullptr, EOAC_NONE);
            }
        }
    }

    return true;
}

void shutdown() {
    if (g_query) { PdhCloseQuery(g_query); g_query = nullptr; }
    if (g_wmiSvc) { g_wmiSvc->Release(); g_wmiSvc = nullptr; }
    if (g_wmiLoc) { g_wmiLoc->Release(); g_wmiLoc = nullptr; }
    CoUninitialize();
}

void tick() {
    if (!g_cfg.enabled) return;
    PdhCollectQueryData(g_query);

    DWORD type = 0;
    PDH_FMT_COUNTERVALUE val{};
    if (PdhGetFormattedCounterValue(g_counterTotal, PDH_FMT_DOUBLE, &type, &val) == ERROR_SUCCESS)
        g_snap.usagePercent = (float)val.doubleValue;

    g_snap.freqCurrentMHz = freqCurrentMHz();
    g_snap.temperature = readTemp();

    // Per-core usage
    if (g_cfg.perCoreEnabled) {
        for (uint32_t i = 0; i < g_coreCount && i < CPU_MAX_CORES; ++i) {
            if (g_counters[i]) {
                PDH_FMT_COUNTERVALUE cv{};
                if (PdhGetFormattedCounterValue(g_counters[i], PDH_FMT_DOUBLE, &type, &cv) == ERROR_SUCCESS)
                    g_snap.cores[i].usagePercent = (float)cv.doubleValue;
                g_snap.cores[i].freqMHz = g_snap.freqCurrentMHz;
            }
        }
    }

    sampleTopProcs();
}

const Snapshot& snapshot() { return g_snap; }
Config*         getConfig() { return &g_cfg; }
void            register_metrics(MetricRegistry*) {}

} // namespace cpu
