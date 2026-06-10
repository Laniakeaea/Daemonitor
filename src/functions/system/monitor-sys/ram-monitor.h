#pragma once
#include "functions/system/monitor-sys/metric.h"
#include <cstdint>

#define RAM_MAX_TOP_PROCS 16

namespace ram {

// ── Config ──────────────────────────────────────────────
struct Config {
    bool     enabled          = true;
    uint32_t pollIntervalMs   = 1000;
    bool     processTopEnabled = true;
    uint32_t processTopN      = 8;
    size_t   historySize      = 256;
    float    warnPhysPercent  = 85.0f;
    float    critPhysPercent  = 95.0f;
    float    warnCommitPercent = 85.0f;
};

// ── Top process by working set ──────────────────────────
struct RamProcInfo {
    char     name[32];
    uint32_t pid;
    float    workingSetMB;
};

// ── Full snapshot ───────────────────────────────────────
struct Snapshot {
    float    totalGB;
    float    usedGB;
    float    availableGB;
    float    usagePercent;
    float    commitTotalGB;
    float    commitUsedGB;
    float    commitPercent;
    float    pageUsedGB;
    float    nonPagedUsedGB;
    float    cacheGB;
    uint64_t pageFaultsPerSec;

    // Per-process
    RamProcInfo topProcs[RAM_MAX_TOP_PROCS];
    uint32_t    topProcCount;
};

// ── Lifecycle ───────────────────────────────────────────
bool   init(Config* cfg);
void   shutdown();
void   tick();
const Snapshot& snapshot();
Config* getConfig();
void   register_metrics(class MetricRegistry* reg);

} // namespace ram
