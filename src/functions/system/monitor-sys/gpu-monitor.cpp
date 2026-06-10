// gpu-monitor.cpp — DXGI VRAM + PDH engine usage + NVAPI temp/clocks + WMI fallback
#include "functions/system/monitor-sys/gpu-monitor.h"

#define WIN32_LEAN_AND_MEAN
#include <Windows.h>
#include <Pdh.h>
#include <dxgi.h>
#include <comdef.h>
#include <Wbemidl.h>
#include <cstdio>
#include <cstring>

#pragma comment(lib, "pdh.lib")
#pragma comment(lib, "dxgi.lib")
#pragma comment(lib, "wbemuuid.lib")

// Forward — WMI thermal fallback (defined after namespace)
static float readWMI(IWbemServices* svc);

namespace gpu {

// ── NVAPI runtime-dynamic loading (no SDK needed) ───────
// WINAPI (__stdcall) required — MinGW defaults to __cdecl
typedef void* (WINAPI *NvQIF)(unsigned int);
typedef int   (WINAPI *NvInit)();
typedef int   (WINAPI *NvEnumGpus)(int32_t* handles, int32_t* count);

#pragma pack(push,1)
struct NvTherm { uint32_t ver; uint32_t cnt; struct { int32_t ctlr,target,curTemp,defMin,defMax; } s[3]; };
struct NvClk   { uint32_t ver; uint32_t rsvd[32]; struct { uint32_t present; uint32_t freqKHz; } d[32]; };
#pragma pack(pop)

typedef int (WINAPI *NvThermFn)(int32_t, int32_t, NvTherm*);
typedef int (WINAPI *NvClkFn)(int32_t, NvClk*);

static HMODULE    g_nvDll     = nullptr;
static NvInit     g_nvInit    = nullptr;
static NvEnumGpus g_nvEnum    = nullptr;
static NvThermFn  g_nvTherm   = nullptr;
static NvClkFn    g_nvClk     = nullptr;
static bool       g_nvReady   = false;
static int32_t    g_nvHandle  = 0;

// ── PDH ─────────────────────────────────────────────────
static PDH_HQUERY   g_query     = nullptr;
static PDH_HCOUNTER g_ctrTotal  = nullptr;
static PDH_HCOUNTER g_ctrEngine[GPU_MAX_ENGINES]{};
static uint32_t     g_engCount  = 0;

// ── WMI fallback for temperature (AMD / Intel GPUs) ──
static IWbemServices* g_gpuWmiSvc = nullptr;
static IWbemLocator*  g_gpuWmiLoc = nullptr;

// ── Internal state ──────────────────────────────────────
static Config   g_cfg;
static Snapshot g_snap;

// Engine name mapping (index matches PDH wildcard discovery order)
static const char* kEngineNames[] = {"3D","Copy","VideoDecode","VideoEncode",
                                     "Compute","Graphics","Overlay","Unknown"};

// ═══════════════════════════════════════════════════════
bool init(Config* cfg) {
    if (cfg) g_cfg = *cfg;
    if (!g_cfg.enabled) return true;

    // DXGI: name + VRAM
    IDXGIFactory* fac = nullptr;
    if (SUCCEEDED(CreateDXGIFactory(__uuidof(IDXGIFactory), (void**)&fac))) {
        IDXGIAdapter* adp = nullptr;
        if (SUCCEEDED(fac->EnumAdapters(0, &adp))) {
            DXGI_ADAPTER_DESC desc{};
            if (SUCCEEDED(adp->GetDesc(&desc))) {
                WideCharToMultiByte(CP_UTF8, 0, desc.Description, -1,
                    g_snap.name, GPU_NAME_MAX, nullptr, nullptr);
                g_snap.memTotalMB = desc.DedicatedVideoMemory / (1024.0f * 1024.0f);
            }
            adp->Release();
        }
        fac->Release();
    }

    // NVAPI: dynamic load from System32
    if (g_cfg.nvapiEnabled) {
        g_nvDll = LoadLibraryW(L"nvapi64.dll");
        if (g_nvDll) {
            auto qif = (NvQIF)GetProcAddress(g_nvDll, "nvapi_QueryInterface");
            if (qif) {
                g_nvInit  = (NvInit)   qif(0x0150E828);
                g_nvEnum  = (NvEnumGpus)qif(0xE5AC921F);
                g_nvTherm = (NvThermFn)qif(0xE3640A56);
                g_nvClk   = (NvClkFn)  qif(0x1F3C5E10);
                if (g_nvInit && g_nvInit() == 0) {
                    int32_t gpus[4], cnt = 4;
                    if (g_nvEnum(gpus, &cnt) == 0 && cnt > 0) {
                        g_nvHandle = gpus[0];
                        g_nvReady  = true;
                    }
                }
            }
        }
    }

    // PDH: GPU total usage
    PdhOpenQueryW(nullptr, 0, &g_query);
    PdhAddEnglishCounterW(g_query,
        L"\\GPU Engine(pid_*_luid_*_phys_*_engtype_3D)\\Utilization Percentage",
        0, &g_ctrTotal);

    // Per-engine: enumerate available engines via wildcard
    if (g_cfg.perEngineEnabled) {
        const char* engKeys[] = {"3D","Copy","VideoDecode","VideoEncode","Compute"};
        for (int e = 0; e < 5 && g_engCount < GPU_MAX_ENGINES; ++e) {
            wchar_t path[256];
            _snwprintf_s(path, _TRUNCATE,
                L"\\GPU Engine(pid_*_luid_*_phys_*_engtype_%hs)\\Utilization Percentage", engKeys[e]);
            PDH_HCOUNTER c = nullptr;
            if (PdhAddEnglishCounterW(g_query, path, 0, &c) == ERROR_SUCCESS) {
                g_ctrEngine[g_engCount] = c;
                strncpy_s(g_snap.engines[g_engCount].name, engKeys[e], 31);
                ++g_engCount;
            }
        }
        g_snap.engineCount = g_engCount;
    }

    PdhCollectQueryData(g_query);

    // ── Init WMI for temperature fallback ──────────────
    HRESULT hr = CoInitializeEx(nullptr, COINIT_MULTITHREADED);
    if (SUCCEEDED(hr) || hr == S_FALSE || hr == RPC_E_CHANGED_MODE) {
        hr = CoCreateInstance(CLSID_WbemLocator, nullptr, CLSCTX_INPROC_SERVER,
                              IID_IWbemLocator, (void**)&g_gpuWmiLoc);
        if (SUCCEEDED(hr) && g_gpuWmiLoc) {
            hr = g_gpuWmiLoc->ConnectServer(
                _bstr_t(L"ROOT\\WMI"), nullptr, nullptr, nullptr,
                0, nullptr, nullptr, &g_gpuWmiSvc);
            if (SUCCEEDED(hr) && g_gpuWmiSvc) {
                CoSetProxyBlanket(g_gpuWmiSvc, RPC_C_AUTHN_WINNT, RPC_C_AUTHZ_NONE,
                                  nullptr, RPC_C_AUTHN_LEVEL_CALL,
                                  RPC_C_IMP_LEVEL_IMPERSONATE, nullptr, EOAC_NONE);
            }
        }
    }

    return true;
}

void shutdown() {
    if (g_nvDll) { FreeLibrary(g_nvDll); g_nvDll = nullptr; }
    if (g_query) { PdhCloseQuery(g_query); g_query = nullptr; }
    if (g_gpuWmiSvc) { g_gpuWmiSvc->Release(); g_gpuWmiSvc = nullptr; }
    if (g_gpuWmiLoc) { g_gpuWmiLoc->Release(); g_gpuWmiLoc = nullptr; }
    CoUninitialize();
}

void tick() {
    if (!g_cfg.enabled) return;
    if (!g_query) return;  // PDH not initialized, skip quietly

    PdhCollectQueryData(g_query);

    DWORD type = 0;
    PDH_FMT_COUNTERVALUE val{};

    // Total GPU usage
    if (g_ctrTotal && PdhGetFormattedCounterValue(g_ctrTotal, PDH_FMT_DOUBLE, &type, &val) == ERROR_SUCCESS)
        g_snap.usagePercent = (float)val.doubleValue;

    // VRAM snapshot — use DXGI QueryVideoMemoryInfo if available
    // Falls back gracefully on older SDKs / MinGW
#if defined(NTDDI_WIN10_RS2) && (NTDDI_VERSION >= NTDDI_WIN10_RS2)
    IDXGIFactory* fac = nullptr;
    if (SUCCEEDED(CreateDXGIFactory(__uuidof(IDXGIFactory), (void**)&fac))) {
        IDXGIAdapter* adp = nullptr;
        if (SUCCEEDED(fac->EnumAdapters(0, &adp))) {
            DXGI_QUERY_VIDEO_MEMORY_INFO info{};
            if (SUCCEEDED(adp->QueryVideoMemoryInfo(0, DXGI_MEMORY_SEGMENT_GROUP_LOCAL, &info))) {
                g_snap.memUsedMB = info.CurrentUsage / (1024.0f * 1024.0f);
                g_snap.memUsagePercent = g_snap.memTotalMB > 0
                    ? (g_snap.memUsedMB / g_snap.memTotalMB) * 100.0f : 0;
            }
            adp->Release();
        }
        fac->Release();
    }
#else
    // MinGW fallback: use DXGI_ADAPTER_DESC only for total VRAM
    // memUsedMB stays 0 unless we use PDH GPU memory counters
    g_snap.memUsedMB = 0;
    g_snap.memUsagePercent = 0;
#endif

    // Per-engine
    for (uint32_t i = 0; i < g_engCount; ++i) {
        PDH_FMT_COUNTERVALUE cv{};
        if (PdhGetFormattedCounterValue(g_ctrEngine[i], PDH_FMT_DOUBLE, &type, &cv) == ERROR_SUCCESS)
            g_snap.engines[i].usagePercent = (float)cv.doubleValue;
    }

    // NVAPI (null-guard: NVAPI 函数指针在 MinGW 下可能为 NULL)
    if (g_nvReady) {
        NvTherm t{}; t.ver = 0x20004;
        if (g_nvTherm && g_nvTherm(g_nvHandle, 0, &t) == 0)
            g_snap.temperature = (float)t.s[0].curTemp;

        NvClk clk{}; clk.ver = 0x20007;
        if (g_nvClk && g_nvClk(g_nvHandle, &clk) == 0) {
            g_snap.coreClockMHz = clk.d[0].freqKHz / 1000.0f;
            g_snap.memClockMHz  = clk.d[1].freqKHz / 1000.0f;
        }
    }

    // ── WMI temperature fallback (AMD / Intel GPUs) ──
    if (g_snap.temperature <= 0.0f && g_gpuWmiSvc) {
        g_snap.temperature = readWMI(g_gpuWmiSvc);
    }
}

const Snapshot& snapshot() { return g_snap; }
Config*         getConfig() { return &g_cfg; }
void            register_metrics(MetricRegistry*) {}

} // namespace gpu

// ── Shared WMI thermal zone query (used by GPU WMI fallback) ──
static float readWMI(IWbemServices* svc) {
    if (!svc) return 0.0f;
    IEnumWbemClassObject* pEnum = nullptr;
    float best = 0.0f;

    HRESULT hr = svc->ExecQuery(
        _bstr_t(L"WQL"),
        _bstr_t(L"SELECT CurrentTemperature FROM MSAcpi_ThermalZoneTemperature"),
        WBEM_FLAG_FORWARD_ONLY | WBEM_FLAG_RETURN_IMMEDIATELY,
        nullptr, &pEnum);
    if (FAILED(hr) || !pEnum) return 0.0f;

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
    return best;
}
