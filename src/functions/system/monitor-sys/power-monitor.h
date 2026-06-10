#pragma once
#include "functions/system/monitor-sys/metric.h"
#include <cstdint>

#define PWR_NAME_MAX 32

namespace pwr {

// ── Config ──────────────────────────────────────────────
struct Config {
    bool     enabled         = true;
    uint32_t pollIntervalMs  = 2000;
    size_t   historySize     = 256;
    float    warnBatteryPct  = 20.0f;
    float    critBatteryPct  = 10.0f;
};

// ── Full snapshot ───────────────────────────────────────
struct Snapshot {
    float    batteryPercent;        // 0-100, or -1 if no battery
    bool     acConnected;           // true = plugged in
    bool     charging;              // true = actively charging
    bool     batteryPresent;        // false = desktop (no battery)
    float    remainingTimeMin;      // estimated minutes remaining, or -1
    float    chargeRateWatts;       // estimated charge (+) / discharge (-) rate in watts
    char     powerSource[PWR_NAME_MAX]; // "AC Power" or "Battery" or "Unknown"
};

// ── Lifecycle ───────────────────────────────────────────
bool   init(Config* cfg);
void   shutdown();
void   tick();
const Snapshot& snapshot();
Config* getConfig();
void   register_metrics(class DaemonCore::MetricRegistry* reg);

} // namespace pwr
