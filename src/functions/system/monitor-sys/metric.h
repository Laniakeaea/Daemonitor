// metric.h — standardized measurement primitive for all monitors
// Every observable in DAEMONITOR is a named, typed, unit-bearing Metric
// with automatic history tracking, min/max/avg, and threshold alerts.
// Codex §6.4: functions/system/ — cross-cutting system capability

#pragma once
#include "functions/system/monitor-sys/ring-buffer.h"
#include <cstdint>
#include <cstddef>
#include <cstring>

namespace DaemonCore {

// ── Metric priority / severity ─────────────────────────
enum class MetricLevel : uint8_t {
    Info    = 0,  // informational only
    Warning = 1,  // above warn threshold
    Critical= 2,  // above critical threshold
};

// ── Monitor health status ───────────────────────────────
enum class MonitorStatus : uint8_t {
    Uninitialized = 0,
    Ok            = 1,
    Degraded      = 2,     // running but some features unavailable
    Failed        = 3,
};

// ── Monitor info — declared BEFORE MetricRegistry ───────
struct MonitorInfo {
    const char*    name;          // "CPU", "GPU", "RAM", "Network", "Disk"
    MonitorStatus  status;
    uint32_t       errorCode;     // Win32 error or 0
    uint32_t       pollIntervalMs;
    uint32_t       tickCount;     // total ticks since init
};

// ── Metric descriptor — defines a single named measurement ──
struct MetricDef {
    const char* key;        // dot-delimited: "cpu.usage.total"
    const char* label;      // human-readable: "CPU Usage"
    const char* unit;       // "%", "MHz", "°C", "GB", "B/s", "count"
    float       value;      // most recent sample
    float       min;        // all-time min since last reset
    float       max;        // all-time max since last reset
    float       avg;        // running average over history window
};

// ── Per-metric configuration — every knob exposed ──────
struct MetricConfig {
    bool   enabled       = true;
    float  warnThreshold = 0.0f;     // 0 = no warning
    float  critThreshold = 0.0f;     // 0 = no critical
    size_t historySize   = 256;      // ring-buffer depth (power-of-2)

    MetricLevel evaluate(float value) const {
        if (critThreshold > 0 && value >= critThreshold) return MetricLevel::Critical;
        if (warnThreshold > 0 && value >= warnThreshold) return MetricLevel::Warning;
        return MetricLevel::Info;
    }
};

// ── Alert callback type ─────────────────────────────────
// Called when a metric crosses a threshold boundary.
using AlertCallback = void (*)(const char* key, float value, float threshold,
                               MetricLevel level, void* userData);

// ── Metric handle — the kernel returns this to consumers ──
using MetricHandle = uint16_t;
static constexpr MetricHandle INVALID_METRIC = 0xFFFF;

// ── Maximum metrics across all monitors ─────────────────
static constexpr size_t MAX_METRICS       = 256;
static constexpr size_t METRIC_MAX_MONITORS = 16;
static constexpr size_t DEFAULT_HISTORY   = 256;   // power-of-2

// ── Metric registry — central store owned by kernel ─────
class MetricRegistry {
public:
    MetricHandle reg(const char* key, const char* label, const char* unit,
                     float warnThreshold = 0, float critThreshold = 0,
                     size_t historySize = DEFAULT_HISTORY);

    void push(MetricHandle h, float value);
    const MetricDef* get(MetricHandle h) const;
    MetricDef*       getMut(MetricHandle h);
    MetricHandle     find(const char* key) const;
    MetricConfig*    cfg(MetricHandle h);
    const MetricConfig* cfg(MetricHandle h) const;
    void resetStats(MetricHandle h);
    void resetAllStats();
    size_t queryAll(MetricDef* out, size_t max) const;
    size_t count() const { return m_count; }

    // Direct access to internal arrays (for KernelSnapshot)
    const MetricDef*  metricsData()   const { return m_metrics; }
    size_t            monitorCount()  const { return m_monitorCount; }
    const MonitorInfo* monitorsData() const { return m_monitors; }

    void setAlertCallback(AlertCallback cb, void* userData) {
        m_alertCb = cb; m_alertUserData = userData;
    }
    void evaluateAlerts();

    MonitorInfo* monitorInfo(size_t idx) {
        return (idx < m_monitorCount) ? &m_monitors[idx] : nullptr;
    }

    MonitorInfo* addMonitor(const char* name, MonitorStatus status = MonitorStatus::Ok) {
        if (m_monitorCount >= METRIC_MAX_MONITORS) return nullptr;
        MonitorInfo* m = &m_monitors[m_monitorCount++];
        m->name = name;
        m->status = status;
        m->errorCode = 0;
        m->pollIntervalMs = 1000;
        m->tickCount = 0;
        return m;
    }

private:
    MetricDef    m_metrics[MAX_METRICS]{};
    MetricConfig m_configs[MAX_METRICS]{};
    MetricLevel  m_levels[MAX_METRICS]{};
    RingBuffer<DEFAULT_HISTORY> m_history[MAX_METRICS];
    size_t       m_count  = 0;
    AlertCallback m_alertCb = nullptr;
    void*         m_alertUserData = nullptr;
    MonitorInfo   m_monitors[METRIC_MAX_MONITORS]{};
    size_t        m_monitorCount = 0;
};

// ── Inline implementations ─────────────────────────────────
inline MetricHandle MetricRegistry::reg(const char* key, const char* label, const char* unit,
    float warn, float crit, size_t histSz) {
    if (m_count >= MAX_METRICS) return INVALID_METRIC;
    MetricHandle h = (MetricHandle)m_count;
    m_metrics[h].key   = key;
    m_metrics[h].label = label;
    m_metrics[h].unit  = unit;
    m_metrics[h].value = 0;
    m_metrics[h].min   = 0;
    m_metrics[h].max   = 0;
    m_metrics[h].avg   = 0;
    m_configs[h].enabled       = true;
    m_configs[h].warnThreshold = warn;
    m_configs[h].critThreshold = crit;
    m_configs[h].historySize   = histSz;
    m_levels[h] = MetricLevel::Info;
    ++m_count;
    return h;
}

inline void MetricRegistry::push(MetricHandle h, float v) {
    if (h >= m_count) return;
    if (!m_configs[h].enabled) return;
    m_metrics[h].value = v;
    m_history[h].push(v);
    auto st = m_history[h].compute();
    m_metrics[h].min = (m_history[h].size() == 1) ? v : st.min;
    m_metrics[h].max = (m_history[h].size() == 1) ? v : st.max;
    m_metrics[h].avg = st.avg;
}

inline const MetricDef* MetricRegistry::get(MetricHandle h) const {
    return (h < m_count) ? &m_metrics[h] : nullptr;
}
inline MetricDef* MetricRegistry::getMut(MetricHandle h) {
    return (h < m_count) ? &m_metrics[h] : nullptr;
}

inline MetricHandle MetricRegistry::find(const char* key) const {
    for (size_t i = 0; i < m_count; ++i)
        if (m_metrics[i].key && strcmp(m_metrics[i].key, key) == 0)
            return (MetricHandle)i;
    return INVALID_METRIC;
}

inline MetricConfig* MetricRegistry::cfg(MetricHandle h) {
    return (h < m_count) ? &m_configs[h] : nullptr;
}
inline const MetricConfig* MetricRegistry::cfg(MetricHandle h) const {
    return (h < m_count) ? &m_configs[h] : nullptr;
}

inline void MetricRegistry::resetStats(MetricHandle h) {
    if (h >= m_count) return;
    m_metrics[h].min = m_metrics[h].value;
    m_metrics[h].max = m_metrics[h].value;
}

inline void MetricRegistry::resetAllStats() {
    for (size_t i = 0; i < m_count; ++i) {
        m_metrics[i].min = m_metrics[i].value;
        m_metrics[i].max = m_metrics[i].value;
    }
}

inline size_t MetricRegistry::queryAll(MetricDef* out, size_t max) const {
    size_t n = m_count < max ? m_count : max;
    for (size_t i = 0; i < n; ++i) out[i] = m_metrics[i];
    return n;
}

inline void MetricRegistry::evaluateAlerts() {
    if (!m_alertCb) return;
    for (size_t i = 0; i < m_count; ++i) {
        if (!m_configs[i].enabled) continue;
        MetricLevel lvl = m_configs[i].evaluate(m_metrics[i].value);
        // Edge-triggered: only fire when severity increases
        if (lvl > m_levels[i]) {
            float th = (lvl == MetricLevel::Critical) ? m_configs[i].critThreshold
                                                      : m_configs[i].warnThreshold;
            m_alertCb(m_metrics[i].key, m_metrics[i].value, th, lvl, m_alertUserData);
        }
        m_levels[i] = lvl;
    }
}

// ── Snapshot of all metrics at a point in time ──────────
struct MetricSample {
    MetricHandle handle;
    float        value;
};

} // namespace DaemonCore
