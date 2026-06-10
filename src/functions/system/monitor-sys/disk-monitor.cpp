// disk-monitor.cpp — per-physical-disk I/O via PDH counters
#include "functions/system/monitor-sys/disk-monitor.h"

#define WIN32_LEAN_AND_MEAN
#include <Windows.h>
#include <Pdh.h>
#include <cstdio>
#include <cstring>

#pragma comment(lib, "pdh.lib")

namespace disk {

static Config   g_cfg;
static Snapshot g_snap;

static PDH_HQUERY   g_query      = nullptr;
static PDH_HCOUNTER g_ctrRead[DISK_MAX_DISKS]{};
static PDH_HCOUNTER g_ctrWrite[DISK_MAX_DISKS]{};
static uint32_t     g_diskCount  = 0;
static bool         g_firstSample = true;  // need 2 PDH samples for rate counters

// ── Discover physical disks by probing PDH paths ────────
static uint32_t discoverDisks() {
    g_diskCount = 0;

    // Always add _Total as reliable fallback
    {
        PDH_HCOUNTER rc = nullptr, wc = nullptr;
        if (PdhAddCounterW(g_query,
                L"\\PhysicalDisk(_Total)\\Disk Read Bytes/sec", 0, &rc) == ERROR_SUCCESS &&
            PdhAddCounterW(g_query,
                L"\\PhysicalDisk(_Total)\\Disk Write Bytes/sec", 0, &wc) == ERROR_SUCCESS) {
            g_ctrRead[0]  = rc;
            g_ctrWrite[0] = wc;
            snprintf(g_snap.disks[0].name,  DISK_NAME_MAX, "Total");
            snprintf(g_snap.disks[0].model, DISK_NAME_MAX, "All Disks");
            g_diskCount = 1;
        }
    }

    // Try individual disks as well (up to DISK_MAX_DISKS - 1 to leave room for _Total)
    for (uint32_t d = 0; d < 4 && g_diskCount < DISK_MAX_DISKS; ++d) {
        wchar_t readPath[256], writePath[256];
        _snwprintf_s(readPath,  _TRUNCATE,
            L"\\PhysicalDisk(%u)\\Disk Read Bytes/sec", d);
        _snwprintf_s(writePath, _TRUNCATE,
            L"\\PhysicalDisk(%u)\\Disk Write Bytes/sec", d);

        PDH_HCOUNTER rc = nullptr, wc = nullptr;
        PDH_STATUS sR = PdhAddCounterW(g_query, readPath,  0, &rc);
        PDH_STATUS sW = PdhAddCounterW(g_query, writePath, 0, &wc);

        if (sR == ERROR_SUCCESS && sW == ERROR_SUCCESS) {
            g_ctrRead[g_diskCount]  = rc;
            g_ctrWrite[g_diskCount] = wc;

            snprintf(g_snap.disks[g_diskCount].name,
                DISK_NAME_MAX, "PhysicalDrive%u", d);
            snprintf(g_snap.disks[g_diskCount].model,
                DISK_NAME_MAX, "Disk %u", d);

            ++g_diskCount;
        }
    }

    g_snap.diskCount = g_diskCount;
    return g_diskCount;
}

// ═══════════════════════════════════════════════════════
bool init(Config* cfg) {
    if (cfg) g_cfg = *cfg;
    if (!g_cfg.enabled) return true;

    PdhOpenQueryW(nullptr, 0, &g_query);
    discoverDisks();
    if (g_diskCount > 0) {
        PdhCollectQueryData(g_query);  // prime first sample
        g_firstSample = true;
    }
    return true;
}

void shutdown() {
    if (g_query) { PdhCloseQuery(g_query); g_query = nullptr; }
}

void tick() {
    if (!g_cfg.enabled || g_diskCount == 0) return;

    PDH_STATUS s = PdhCollectQueryData(g_query);
    // Don't abort on collection warnings — check each counter individually
    (void)s;

    // PDH rate counters need two samples — skip first tick after init
    if (g_firstSample) { g_firstSample = false; return; }

    float totalRead = 0, totalWrite = 0;

    for (uint32_t i = 0; i < g_diskCount; ++i) {
        DiskInfo& di = g_snap.disks[i];
        di.readBytesPerSec  = 0;
        di.writeBytesPerSec = 0;
        di.active = false;

        if (g_ctrRead[i]) {
            PDH_FMT_COUNTERVALUE val{};
            if (PdhGetFormattedCounterValue(g_ctrRead[i],
                    PDH_FMT_DOUBLE | PDH_FMT_NOCAP100, nullptr, &val) == ERROR_SUCCESS) {
                di.readBytesPerSec = (float)val.doubleValue;
                di.readMBps = di.readBytesPerSec / 1048576.0f;
                if (di.readBytesPerSec > 10.0f) di.active = true;
            }
        }

        if (g_ctrWrite[i]) {
            PDH_FMT_COUNTERVALUE val{};
            if (PdhGetFormattedCounterValue(g_ctrWrite[i],
                    PDH_FMT_DOUBLE | PDH_FMT_NOCAP100, nullptr, &val) == ERROR_SUCCESS) {
                di.writeBytesPerSec = (float)val.doubleValue;
                di.writeMBps = di.writeBytesPerSec / 1048576.0f;
                if (di.writeBytesPerSec > 10.0f) di.active = true;
            }
        }

        totalRead  += di.readBytesPerSec;
        totalWrite += di.writeBytesPerSec;
    }

    g_snap.readBytesPerSec  = totalRead;
    g_snap.writeBytesPerSec = totalWrite;
    g_snap.readMBps  = totalRead  / 1048576.0f;
    g_snap.writeMBps = totalWrite / 1048576.0f;
}

const Snapshot& snapshot() { return g_snap; }
Config*         getConfig() { return &g_cfg; }
void            register_metrics(MetricRegistry*) {}

} // namespace disk
