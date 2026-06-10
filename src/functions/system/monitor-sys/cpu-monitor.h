#pragma once
#include "functions/system/monitor-sys/metric.h"
#include <cstdint>

#define CPU_MAX_CORES     64
#define CPU_MAX_TOP_PROCS 16
#define CPU_NAME_MAX      64

namespace cpu {

// ── All config exposed — every knob tunable at runtime ──
struct Config {
    bool     enabled         = true;
    uint32_t pollIntervalMs  = 1000;
    bool     perCoreEnabled  = true;
    bool     processTopEnabled = true;
    uint32_t processTopN     = 8;       // top-N processes by CPU
    size_t   historySize     = 256;
    // Thresholds
    float    warnCpuPercent  = 85.0f;
    float    critCpuPercent  = 95.0f;
    float    warnTempC       = 80.0f;
    float    critTempC       = 95.0f;
};

// ── Per-core data ───────────────────────────────────────
struct CoreInfo {
    float usagePercent;
    float freqMHz;
};

// ── Top process by CPU ──────────────────────────────────
struct ProcInfo {
    char   name[32];
    uint32_t pid;
    float    cpuPercent;
};

// ── Full snapshot ───────────────────────────────────────
struct Snapshot {
    // Totals
    float    usagePercent;
    float    temperature;
    uint32_t coreCount;
    float    freqCurrentMHz;
    float    freqBaseMHz;
    char     brand[CPU_NAME_MAX];

    // Per-core
    CoreInfo cores[CPU_MAX_CORES];

    // Top processes
    ProcInfo topProcs[CPU_MAX_TOP_PROCS];
    uint32_t topProcCount;

    // PDH monotonic counters (internal use)
    uint64_t prevIdleTime;
    uint64_t prevKernelTime;
    uint64_t prevUserTime;
};

// ── Lifecycle ───────────────────────────────────────────
bool   init(Config* cfg);
void   shutdown();
void   tick();
const Snapshot& snapshot();
Config* getConfig();
void   register_metrics(class MetricRegistry* reg);

} // namespace cpu
