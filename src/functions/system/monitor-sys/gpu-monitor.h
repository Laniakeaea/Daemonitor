#pragma once
#include "functions/system/monitor-sys/metric.h"
#include <cstdint>

#define GPU_MAX_ENGINES  8
#define GPU_NAME_MAX     64

namespace gpu {

// ── Config ──────────────────────────────────────────────
struct Config {
    bool     enabled          = true;
    uint32_t pollIntervalMs   = 1000;
    bool     nvapiEnabled     = true;   // dynamic NVAPI for temp/clocks
    bool     perEngineEnabled = true;
    size_t   historySize      = 256;
    float    warnTempC        = 80.0f;
    float    critTempC        = 95.0f;
    float    warnVramPercent  = 90.0f;
    float    critVramPercent  = 98.0f;
};

// ── Engine type ─────────────────────────────────────────
struct EngineInfo {
    char  name[32];     // "3D", "Copy", "VideoDecode", etc.
    float usagePercent;
};

// ── Full snapshot ───────────────────────────────────────
struct Snapshot {
    // Totals
    float  usagePercent;
    float  temperature;
    float  memUsedMB;
    float  memTotalMB;
    float  memUsagePercent;
    float  coreClockMHz;
    float  memClockMHz;
    char   name[GPU_NAME_MAX];

    // Per-engine
    EngineInfo engines[GPU_MAX_ENGINES];
    uint32_t    engineCount;
};

// ── Lifecycle ───────────────────────────────────────────
bool   init(Config* cfg);
void   shutdown();
void   tick();
const Snapshot& snapshot();
Config* getConfig();
void   register_metrics(class MetricRegistry* reg);

} // namespace gpu
