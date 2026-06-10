// kernel.cpp — DaemonCore: metric registry + bridge + lifecycle
// Pure data collection — no I/O, no formatting.
// Consumer (print-sys) handles all display.
#include "functions/system/app-core-sys/kernel.h"
#include <cstdio>
#include <cstring>

namespace DaemonCore {

static MetricRegistry s_registry;

// ── History ring buffer for time-series charts ─────────
static HistoryPoint s_history[HISTORY_DEPTH]{};
static size_t       s_historyHead  = 0;
static size_t       s_historyCount = 0;

static void pushHistory() {
    auto& c = cpu::snapshot();
    auto& r = ram::snapshot();
    auto& g = gpu::snapshot();
    auto& n = net::snapshot();
    auto& d = disk::snapshot();
    auto& p = pwr::snapshot();

    HistoryPoint pt = {
        c.usagePercent,
        r.usagePercent,
        g.usagePercent,
        n.rxBytesPerSec / 1048576.0f,
        n.txBytesPerSec / 1048576.0f,
        d.readBytesPerSec / 1048576.0f,
        d.writeBytesPerSec / 1048576.0f,
        c.temperature,
        g.temperature,
        p.batteryPercent,
        p.chargeRateWatts
    };
    s_history[s_historyHead] = pt;
    s_historyHead = (s_historyHead + 1) & (HISTORY_DEPTH - 1);
    if (s_historyCount < HISTORY_DEPTH) ++s_historyCount;
}

// ── Register all known metrics (called once at init) ────
static void registerAllMetrics() {
    // CPU
    s_registry.reg("cpu.usage.total",   "CPU Usage",          "%",       85, 95);
    s_registry.reg("cpu.temp.package",   "CPU Package Temp",   "°C",      80, 95);
    s_registry.reg("cpu.freq.current",   "CPU Current Freq",   "MHz",     0,  0);
    s_registry.reg("cpu.freq.base",      "CPU Base Freq",      "MHz",     0,  0);
    s_registry.reg("cpu.cores.logical",  "CPU Logical Cores",  "count",   0,  0);
    for (uint32_t i = 0; i < CPU_MAX_CORES; ++i) {
        char k[64], l[64];
        snprintf(k, 64, "cpu.usage.core.%u", i);
        snprintf(l, 64, "Core %u Usage", i);
        s_registry.reg(k, l, "%", 85, 95);
    }
    // GPU
    s_registry.reg("gpu.usage.total",   "GPU Usage",          "%",      90, 98);
    s_registry.reg("gpu.temp.core",      "GPU Core Temp",      "°C",     80, 95);
    s_registry.reg("gpu.clock.core",     "GPU Core Clock",     "MHz",    0,  0);
    s_registry.reg("gpu.clock.mem",      "GPU Mem Clock",      "MHz",    0,  0);
    s_registry.reg("gpu.mem.used",       "VRAM Used",          "MB",     0,  0);
    s_registry.reg("gpu.mem.total",      "VRAM Total",         "MB",     0,  0);
    s_registry.reg("gpu.mem.percent",    "VRAM Usage",         "%",      90, 98);
    for (uint32_t i = 0; i < GPU_MAX_ENGINES; ++i) {
        char k[64], l[64];
        snprintf(k, 64, "gpu.usage.engine.%u", i);
        snprintf(l, 64, "GPU Engine %u", i);
        s_registry.reg(k, l, "%", 0, 0);
    }
    // RAM
    s_registry.reg("ram.phys.total",     "RAM Total",          "GB",     0,  0);
    s_registry.reg("ram.phys.used",      "RAM Used",           "GB",     0,  0);
    s_registry.reg("ram.phys.available", "RAM Available",      "GB",     0,  0);
    s_registry.reg("ram.phys.percent",   "RAM Usage",          "%",      85, 95);
    s_registry.reg("ram.commit.total",   "Commit Limit",       "GB",     0,  0);
    s_registry.reg("ram.commit.used",    "Commit Used",        "GB",     0,  0);
    s_registry.reg("ram.commit.percent", "Commit Usage",       "%",      85, 95);
    s_registry.reg("ram.paged.used",     "Paged Pool",         "GB",     0,  0);
    s_registry.reg("ram.nonpaged.used",  "Non-Paged Pool",     "GB",     0,  0);
    s_registry.reg("ram.cache",          "System Cache",       "GB",     0,  0);
    s_registry.reg("ram.pagefaults",     "Page Faults/sec",    "/s",     0,  0);
    // Network
    s_registry.reg("net.rx.total",       "Network RX",         "B/s",    0,  0);
    s_registry.reg("net.tx.total",       "Network TX",         "B/s",    0,  0);
    s_registry.reg("net.rx.mbps",        "Network RX Mbps",    "Mbps",   800, 950);
    s_registry.reg("net.tx.mbps",        "Network TX Mbps",    "Mbps",   800, 950);
    for (uint32_t i = 0; i < NET_MAX_IFACES; ++i) {
        char k[64], l[64];
        snprintf(k, 64, "net.iface.%u.rx", i); snprintf(l, 64, "Iface %u RX", i);
        s_registry.reg(k, l, "B/s", 0, 0);
        snprintf(k, 64, "net.iface.%u.tx", i); snprintf(l, 64, "Iface %u TX", i);
        s_registry.reg(k, l, "B/s", 0, 0);
    }
    // Disk
    s_registry.reg("disk.read.total",    "Disk Read",          "B/s",    0,  0);
    s_registry.reg("disk.write.total",   "Disk Write",         "B/s",    0,  0);
    s_registry.reg("disk.read.mbps",     "Disk Read MB/s",     "MB/s",   500, 800);
    s_registry.reg("disk.write.mbps",    "Disk Write MB/s",    "MB/s",   500, 800);
    for (uint32_t i = 0; i < DISK_MAX_DISKS; ++i) {
        char k[64], l[64];
        snprintf(k, 64, "disk.%u.read", i);  snprintf(l, 64, "Disk %u Read", i);
        s_registry.reg(k, l, "B/s", 0, 0);
        snprintf(k, 64, "disk.%u.write", i); snprintf(l, 64, "Disk %u Write", i);
        s_registry.reg(k, l, "B/s", 0, 0);
        snprintf(k, 64, "disk.%u.queue", i); snprintf(l, 64, "Disk %u Queue", i);
        s_registry.reg(k, l, "", 10, 20);
    }
    // Power
    pwr::register_metrics(&s_registry);
}

// ── Bridge snapshots → registry ────────────────────────
static void bridgeAllSnapshots() {
    auto& c = cpu::snapshot();
    s_registry.push(s_registry.find("cpu.usage.total"),  c.usagePercent);
    s_registry.push(s_registry.find("cpu.temp.package"),  c.temperature);
    s_registry.push(s_registry.find("cpu.freq.current"),  c.freqCurrentMHz);
    s_registry.push(s_registry.find("cpu.freq.base"),     c.freqBaseMHz);
    s_registry.push(s_registry.find("cpu.cores.logical"), (float)c.coreCount);
    for (uint32_t i = 0; i < c.coreCount && i < CPU_MAX_CORES; ++i) {
        char k[64]; snprintf(k, 64, "cpu.usage.core.%u", i);
        s_registry.push(s_registry.find(k), c.cores[i].usagePercent);
    }

    auto& g = gpu::snapshot();
    s_registry.push(s_registry.find("gpu.usage.total"),   g.usagePercent);
    s_registry.push(s_registry.find("gpu.temp.core"),      g.temperature);
    s_registry.push(s_registry.find("gpu.clock.core"),     g.coreClockMHz);
    s_registry.push(s_registry.find("gpu.clock.mem"),      g.memClockMHz);
    s_registry.push(s_registry.find("gpu.mem.used"),       g.memUsedMB);
    s_registry.push(s_registry.find("gpu.mem.total"),      g.memTotalMB);
    s_registry.push(s_registry.find("gpu.mem.percent"),    g.memUsagePercent);
    for (uint32_t i = 0; i < g.engineCount && i < GPU_MAX_ENGINES; ++i) {
        char k[64]; snprintf(k, 64, "gpu.usage.engine.%u", i);
        s_registry.push(s_registry.find(k), g.engines[i].usagePercent);
    }

    auto& r = ram::snapshot();
    s_registry.push(s_registry.find("ram.phys.total"),    r.totalGB);
    s_registry.push(s_registry.find("ram.phys.used"),     r.usedGB);
    s_registry.push(s_registry.find("ram.phys.available"),r.availableGB);
    s_registry.push(s_registry.find("ram.phys.percent"),  r.usagePercent);
    s_registry.push(s_registry.find("ram.commit.total"),  r.commitTotalGB);
    s_registry.push(s_registry.find("ram.commit.used"),   r.commitUsedGB);
    s_registry.push(s_registry.find("ram.commit.percent"),r.commitPercent);
    s_registry.push(s_registry.find("ram.paged.used"),    r.pageUsedGB);
    s_registry.push(s_registry.find("ram.nonpaged.used"), r.nonPagedUsedGB);
    s_registry.push(s_registry.find("ram.cache"),         r.cacheGB);
    s_registry.push(s_registry.find("ram.pagefaults"),    (float)r.pageFaultsPerSec);

    auto& n = net::snapshot();
    s_registry.push(s_registry.find("net.rx.total"),      n.rxBytesPerSec);
    s_registry.push(s_registry.find("net.tx.total"),      n.txBytesPerSec);
    s_registry.push(s_registry.find("net.rx.mbps"),       n.rxMbps);
    s_registry.push(s_registry.find("net.tx.mbps"),       n.txMbps);
    for (uint32_t i = 0; i < n.ifaceCount && i < NET_MAX_IFACES; ++i) {
        char k[64];
        snprintf(k, 64, "net.iface.%u.rx", i); s_registry.push(s_registry.find(k), n.ifaces[i].rxBytesPerSec);
        snprintf(k, 64, "net.iface.%u.tx", i); s_registry.push(s_registry.find(k), n.ifaces[i].txBytesPerSec);
    }

    auto& d = disk::snapshot();
    s_registry.push(s_registry.find("disk.read.total"),   d.readBytesPerSec);
    s_registry.push(s_registry.find("disk.write.total"),  d.writeBytesPerSec);
    s_registry.push(s_registry.find("disk.read.mbps"),    d.readMBps);
    s_registry.push(s_registry.find("disk.write.mbps"),   d.writeMBps);
    for (uint32_t i = 0; i < d.diskCount && i < DISK_MAX_DISKS; ++i) {
        char k[64];
        snprintf(k, 64, "disk.%u.read", i);  s_registry.push(s_registry.find(k), d.disks[i].readBytesPerSec);
        snprintf(k, 64, "disk.%u.write", i); s_registry.push(s_registry.find(k), d.disks[i].writeBytesPerSec);
        snprintf(k, 64, "disk.%u.queue", i); s_registry.push(s_registry.find(k), d.disks[i].queueDepth);
    }

    auto& p = pwr::snapshot();
    s_registry.push(s_registry.find("pwr.battery.percent"),     p.batteryPercent);
    s_registry.push(s_registry.find("pwr.battery.remaining"),   p.remainingTimeMin);
    s_registry.push(s_registry.find("pwr.battery.charge_rate"), p.chargeRateWatts);
    s_registry.push(s_registry.find("pwr.ac_connected"),        p.acConnected ? 1.0f : 0.0f);
    s_registry.push(s_registry.find("pwr.charging"),            p.charging ? 1.0f : 0.0f);
}

// ── Global polling interval ────────────────────────────
static uint32_t g_pollIntervalMs = 1000;

// ═══════════════════════════════════════════════════════
// Public
// ═══════════════════════════════════════════════════════

bool init() {
    if (!cpu::init(nullptr))  return false;
    gpu::init(nullptr);
    if (!ram::init(nullptr))  return false;
    net::init(nullptr);
    disk::init(nullptr);
    pwr::init(nullptr);
    registerAllMetrics();

    // Init monitor info array
    s_registry.addMonitor("CPU",     MonitorStatus::Ok);
    s_registry.addMonitor("GPU",     MonitorStatus::Ok);
    s_registry.addMonitor("RAM",     MonitorStatus::Ok);
    s_registry.addMonitor("Network", MonitorStatus::Ok);
    s_registry.addMonitor("Disk",    MonitorStatus::Ok);
    s_registry.addMonitor("Power",   MonitorStatus::Ok);
    return true;
}

void shutdown() { cpu::shutdown(); gpu::shutdown(); ram::shutdown(); net::shutdown(); disk::shutdown(); pwr::shutdown(); }

void tick() {
    cpu::tick(); gpu::tick(); ram::tick(); net::tick(); disk::tick(); pwr::tick();
    bridgeAllSnapshots();
    pushHistory();
    evaluateAlerts();
}

// ── Polling ────────────────────────────────────────────

void setPollIntervalMs(uint32_t ms) { g_pollIntervalMs = ms; }
uint32_t getPollIntervalMs() { return g_pollIntervalMs; }

// ── Per-monitor enable/disable ─────────────────────────

void enableMonitor(const char* name) {
    if (strcmp(name, "cpu") == 0) cpu::getConfig()->enabled = true;
    else if (strcmp(name, "gpu") == 0) gpu::getConfig()->enabled = true;
    else if (strcmp(name, "ram") == 0) ram::getConfig()->enabled = true;
    else if (strcmp(name, "net") == 0) net::getConfig()->enabled = true;
    else if (strcmp(name, "disk") == 0) disk::getConfig()->enabled = true;
    else if (strcmp(name, "pwr") == 0) pwr::getConfig()->enabled = true;
}

void disableMonitor(const char* name) {
    if (strcmp(name, "cpu") == 0) cpu::getConfig()->enabled = false;
    else if (strcmp(name, "gpu") == 0) gpu::getConfig()->enabled = false;
    else if (strcmp(name, "ram") == 0) ram::getConfig()->enabled = false;
    else if (strcmp(name, "net") == 0) net::getConfig()->enabled = false;
    else if (strcmp(name, "disk") == 0) disk::getConfig()->enabled = false;
    else if (strcmp(name, "pwr") == 0) pwr::getConfig()->enabled = false;
}

bool isMonitorEnabled(const char* name) {
    if (strcmp(name, "cpu") == 0) return cpu::getConfig()->enabled;
    if (strcmp(name, "gpu") == 0) return gpu::getConfig()->enabled;
    if (strcmp(name, "ram") == 0) return ram::getConfig()->enabled;
    if (strcmp(name, "net") == 0) return net::getConfig()->enabled;
    if (strcmp(name, "disk") == 0) return disk::getConfig()->enabled;
    if (strcmp(name, "pwr") == 0) return pwr::getConfig()->enabled;
    return false;
}

// ── Snapshot ───────────────────────────────────────────

KernelSnapshot getSnapshot() {
    return {
        cpu::snapshot(), gpu::snapshot(), ram::snapshot(),
        net::snapshot(), disk::snapshot(), pwr::snapshot(),
        s_registry.count(), s_registry.metricsData(),
        s_registry.monitorCount(), s_registry.monitorsData()
    };
}

size_t getRecentHistory(HistoryPoint* out, size_t max) {
    size_t n = (s_historyCount < max) ? s_historyCount : max;
    for (size_t i = 0; i < n; ++i) {
        // Read backward: (head - 1 - i) wraps around
        size_t idx = (s_historyHead - 1 - i) & (HISTORY_DEPTH - 1);
        out[n - 1 - i] = s_history[idx];  // oldest → newest
    }
    return n;
}

// ── Metric query ───────────────────────────────────────

const MetricDef* getMetric(MetricHandle h)        { return s_registry.get(h); }
MetricDef*       getMetricMut(MetricHandle h)     { return s_registry.getMut(h); }
MetricHandle     findMetric(const char* key)      { return s_registry.find(key); }
size_t           queryAllMetrics(MetricDef* o, size_t m) { return s_registry.queryAll(o, m); }

float metricValue(const char* key, float defVal) {
    MetricHandle h = s_registry.find(key);
    if (h == INVALID_METRIC) return defVal;
    const MetricDef* d = s_registry.get(h);
    return d ? d->value : defVal;
}

// ── Configuration ──────────────────────────────────────

MetricConfig*       getMetricConfig(MetricHandle h) { return s_registry.cfg(h); }
const MetricConfig* getMetricConfigConst(MetricHandle h) { return s_registry.cfg(h); }
MetricConfig*       getMetricConfigByKey(const char* key) { return s_registry.cfg(s_registry.find(key)); }

void setMetricWarnThreshold(const char* key, float value) {
    MetricConfig* c = s_registry.cfg(s_registry.find(key));
    if (c) c->warnThreshold = value;
}
void setMetricCritThreshold(const char* key, float value) {
    MetricConfig* c = s_registry.cfg(s_registry.find(key));
    if (c) c->critThreshold = value;
}
void setMetricHistorySize(const char* key, size_t size) {
    MetricConfig* c = s_registry.cfg(s_registry.find(key));
    if (c) c->historySize = size;
}
void setMetricEnabled(const char* key, bool enabled) {
    MetricConfig* c = s_registry.cfg(s_registry.find(key));
    if (c) c->enabled = enabled;
}

cpu::Config*  getCpuConfig()  { return cpu::getConfig(); }
gpu::Config*  getGpuConfig()  { return gpu::getConfig(); }
ram::Config*  getRamConfig()  { return ram::getConfig(); }
net::Config*  getNetConfig()  { return net::getConfig(); }
disk::Config* getDiskConfig() { return disk::getConfig(); }

// ── Alerts ─────────────────────────────────────────────

void            setAlertCallback(AlertCallback cb, void* ud) { s_registry.setAlertCallback(cb, ud); }
void            evaluateAlerts()   { s_registry.evaluateAlerts(); }
void            resetAllStats()    { s_registry.resetAllStats(); }
void            resetMetricStats(const char* key) { s_registry.resetStats(s_registry.find(key)); }
size_t          getMetricCount() { return s_registry.count(); }
size_t          getMonitorCount() { return s_registry.monitorCount(); }
const MonitorInfo* getMonitorInfo(size_t i) { return s_registry.monitorInfo(i); }
MetricRegistry* getRegistry()    { return &s_registry; }

} // namespace DaemonCore
