// kernel.h — DaemonCore: unified metric registry + monitor lifecycle
// Owns all hardware monitors; bridges snapshots → standardized MetricDef stream.
// Consumers query by key/handle. Full config mutation at runtime.
// Codex §6.4: functions/system/ — cross-cutting system capability

#pragma once
#include "functions/system/monitor-sys/metric.h"
#include "functions/system/monitor-sys/cpu-monitor.h"
#include "functions/system/monitor-sys/gpu-monitor.h"
#include "functions/system/monitor-sys/ram-monitor.h"
#include "functions/system/monitor-sys/net-monitor.h"
#include "functions/system/monitor-sys/disk-monitor.h"
#include "functions/system/monitor-sys/power-monitor.h"

namespace DaemonCore {

// ═══════════════════════════════════════════════════════════
// Unified kernel snapshot — all monitor data at one tick
// ═══════════════════════════════════════════════════════════

struct KernelSnapshot {
    const cpu::Snapshot& cpu;
    const gpu::Snapshot& gpu;
    const ram::Snapshot& ram;
    const net::Snapshot& net;
    const disk::Snapshot& disk;
    const pwr::Snapshot& pwr;
    size_t               metricCount;
    const MetricDef*     metrics;       // pointer to registry array
    size_t               monitorCount;
    const MonitorInfo*   monitors;
};

// ── History point — one tick of key metrics for time-series charts ──
struct HistoryPoint {
    float cpuUsage;
    float ramUsage;
    float gpuUsage;
    float netRxMBps;
    float netTxMBps;
    float diskReadMBps;
    float diskWriteMBps;
    float cpuTemp;
    float gpuTemp;
    float pwrBatteryPct;
    float pwrChargeRate;
};

static constexpr size_t HISTORY_DEPTH = 256;   // power-of-2 for ring buffer

// ═══════════════════════════════════════════════════════════
// Lifecycle — call in this order
// ═══════════════════════════════════════════════════════════

bool init();             // register all metrics, start monitors
void shutdown();         // stop all monitors, free resources
void tick();             // drive all monitors once, bridge into registry

// ── Polling control (mutable at runtime) ───────────────

void setPollIntervalMs(uint32_t ms);   // global polling interval
uint32_t getPollIntervalMs();

// ── Per-monitor enable/disable ─────────────────────────

void enableMonitor(const char* name);   // "cpu", "gpu", "ram", "net", "disk", "pwr"
void disableMonitor(const char* name);
bool isMonitorEnabled(const char* name);

// ═══════════════════════════════════════════════════════════
// Standardized metric query API
// ═══════════════════════════════════════════════════════════

// Snapshot — unified view of all monitors + metrics at current tick
KernelSnapshot getSnapshot();

// History — time-series data for sparklines/charts
// Copies up to 'max' most recent points into out[]; returns actual count.
size_t getRecentHistory(HistoryPoint* out, size_t max);

// Query by handle (fast path — use after find())
const MetricDef*  getMetric(MetricHandle h);
MetricDef*        getMetricMut(MetricHandle h);

// Lookup: find handle by dot-delimited key string
MetricHandle      findMetric(const char* key);

// Direct value query by key (convenience, slower than handle)
float             metricValue(const char* key, float defaultVal = 0.0f);

// Bulk: fill caller array with all metrics, returns count
size_t            queryAllMetrics(MetricDef* out, size_t max);

// ═══════════════════════════════════════════════════════════
// Configuration API — every knob tunable at runtime
// ═══════════════════════════════════════════════════════════

// Per-metric configuration
MetricConfig*     getMetricConfig(MetricHandle h);
const MetricConfig* getMetricConfigConst(MetricHandle h);
MetricConfig*     getMetricConfigByKey(const char* key);

// Set per-metric warn/critical thresholds at runtime
void setMetricWarnThreshold(const char* key, float value);
void setMetricCritThreshold(const char* key, float value);
void setMetricHistorySize(const char* key, size_t size);
void setMetricEnabled(const char* key, bool enabled);

// Monitor-level config access (returns mutable struct)
cpu::Config*      getCpuConfig();
gpu::Config*      getGpuConfig();
ram::Config*      getRamConfig();
net::Config*      getNetConfig();
disk::Config*     getDiskConfig();

// ═══════════════════════════════════════════════════════════
// Alert system
// ═══════════════════════════════════════════════════════════

void              setAlertCallback(AlertCallback cb, void* userData = nullptr);
void              evaluateAlerts();

// ═══════════════════════════════════════════════════════════
// Statistics control
// ═══════════════════════════════════════════════════════════

void              resetAllStats();
void              resetMetricStats(const char* key);

// ═══════════════════════════════════════════════════════════
// Introspection
// ═══════════════════════════════════════════════════════════

size_t            getMetricCount();
size_t            getMonitorCount();
const MonitorInfo* getMonitorInfo(size_t idx);
MetricRegistry*   getRegistry();       // direct access for advanced use

} // namespace DaemonCore

