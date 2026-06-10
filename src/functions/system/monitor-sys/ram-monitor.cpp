// ram-monitor.cpp — GlobalMemoryStatusEx + GetPerformanceInfo + top-N by working set
#include "functions/system/monitor-sys/ram-monitor.h"

#define WIN32_LEAN_AND_MEAN
#include <Windows.h>
#include <Psapi.h>
#include <Pdh.h>
#include <TlHelp32.h>
#include <cstring>

#pragma comment(lib, "pdh.lib")

#define GB(X) ((X) / (1024.0f * 1024.0f * 1024.0f))

namespace ram {

static Config   g_cfg;
static Snapshot g_snap;

static PDH_HQUERY   g_query   = nullptr;
static PDH_HCOUNTER g_ctrPF   = nullptr;   // Page Faults/sec

// ── Top processes by working set ────────────────────────
static void sampleTopProcs() {
    if (!g_cfg.processTopEnabled) { g_snap.topProcCount = 0; return; }

    HANDLE snap = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
    if (snap == INVALID_HANDLE_VALUE) return;

    PROCESSENTRY32W pe{ sizeof(pe) };
    struct ProcWS { char name[32]; uint32_t pid; SIZE_T ws; };
    ProcWS list[512]{};
    int cnt = 0;

    if (Process32FirstW(snap, &pe)) {
        do {
            if (cnt >= 512) break;
            HANDLE h = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION | PROCESS_VM_READ, FALSE, pe.th32ProcessID);
            if (h) {
                PROCESS_MEMORY_COUNTERS_EX pmc{};
                pmc.cb = sizeof(pmc);
                if (GetProcessMemoryInfo(h, (PROCESS_MEMORY_COUNTERS*)&pmc, sizeof(pmc))) {
                    list[cnt].ws  = pmc.WorkingSetSize;
                    list[cnt].pid = pe.th32ProcessID;
                    WideCharToMultiByte(CP_UTF8, 0, pe.szExeFile, -1, list[cnt].name, 32, nullptr, nullptr);
                    ++cnt;
                }
                CloseHandle(h);
            }
        } while (Process32NextW(snap, &pe));
    }
    CloseHandle(snap);

    // Sort by WS desc
    for (int i = 0; i < cnt - 1; ++i)
        for (int j = i + 1; j < cnt; ++j)
            if (list[j].ws > list[i].ws) { ProcWS t = list[i]; list[i] = list[j]; list[j] = t; }

    uint32_t n = g_cfg.processTopN;
    if (n > RAM_MAX_TOP_PROCS) n = RAM_MAX_TOP_PROCS;
    if ((uint32_t)cnt < n) n = (uint32_t)cnt;
    g_snap.topProcCount = n;

    for (uint32_t i = 0; i < n; ++i) {
        strncpy_s(g_snap.topProcs[i].name, list[i].name, 31);
        g_snap.topProcs[i].pid = list[i].pid;
        g_snap.topProcs[i].workingSetMB = (float)list[i].ws / (1024.0f * 1024.0f);
    }
}

// ═══════════════════════════════════════════════════════
bool init(Config* cfg) {
    if (cfg) g_cfg = *cfg;
    if (!g_cfg.enabled) return true;

    PdhOpenQueryW(nullptr, 0, &g_query);
    PdhAddEnglishCounterW(g_query, L"\\Memory\\Page Faults/sec", 0, &g_ctrPF);
    PdhCollectQueryData(g_query);
    return true;
}

void shutdown() {
    if (g_query) { PdhCloseQuery(g_query); g_query = nullptr; }
}

void tick() {
    if (!g_cfg.enabled) return;
    PdhCollectQueryData(g_query);

    // Physical
    MEMORYSTATUSEX msx{ sizeof(msx) };
    GlobalMemoryStatusEx(&msx);
    g_snap.totalGB     = GB((float)msx.ullTotalPhys);
    g_snap.availableGB = GB((float)msx.ullAvailPhys);
    g_snap.usedGB      = g_snap.totalGB - g_snap.availableGB;
    g_snap.usagePercent = g_snap.totalGB > 0 ? (g_snap.usedGB / g_snap.totalGB) * 100.0f : 0;

    // Commit
    g_snap.commitTotalGB = GB((float)msx.ullTotalPageFile);
    float commitUsedGBf  = GB((float)(msx.ullTotalPageFile - msx.ullAvailPageFile));
    g_snap.commitUsedGB  = commitUsedGBf > 0 ? commitUsedGBf : 0;
    g_snap.commitPercent = g_snap.commitTotalGB > 0
        ? (g_snap.commitUsedGB / g_snap.commitTotalGB) * 100.0f : 0;

    // Paged/nonpaged pool via PDH (portable across MSVC/MinGW)
    // These are optional — if PDH fails, values stay at 0
    // NOTE: For simplicity, pool data is derived from commit counters.
    // Full implementation would add PDH counters for \Memory\Pool Paged Bytes etc.
    g_snap.pageUsedGB    = 0;  // placeholder — add PDH counter if needed
    g_snap.nonPagedUsedGB = 0; // placeholder — add PDH counter if needed

    // System cache via PERFORMANCE_INFORMATION
    PERFORMANCE_INFORMATION pi{};
    pi.cb = sizeof(pi);
    if (GetPerformanceInfo(&pi, sizeof(pi))) {
        g_snap.cacheGB = GB((float)((uint64_t)pi.SystemCache * pi.PageSize));
    }

    // Page faults/sec
    if (g_ctrPF) {
        DWORD type = 0;
        PDH_FMT_COUNTERVALUE val{};
        PdhGetFormattedCounterValue(g_ctrPF, PDH_FMT_LARGE, &type, &val);
        g_snap.pageFaultsPerSec = val.largeValue;
    }

    sampleTopProcs();
}

const Snapshot& snapshot() { return g_snap; }
Config*         getConfig() { return &g_cfg; }
void            register_metrics(MetricRegistry*) {}

} // namespace ram
