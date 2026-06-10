// print.cpp — Terminal output formatting. Pure display — no state.
#include "functions/system/print-sys/print.h"
#include <cstdio>
#include <string>

namespace print {

// ── Bandwidth formatter ────────────────────────────────
std::string formatBandwidth(float v) {
    char b[32];
    if (v >= 1e9f)      snprintf(b, sizeof(b), "%.1f GB/s", v / 1e9f);
    else if (v >= 1e6f) snprintf(b, sizeof(b), "%.1f MB/s", v / 1e6f);
    else if (v >= 1e3f) snprintf(b, sizeof(b), "%.1f KB/s", v / 1e3f);
    else                snprintf(b, sizeof(b), "%.0f  B/s", v);
    return b;
}

// ── Temperature formatter ──────────────────────────────
const char* formatTemp(float c) {
    static char b[16];
    if (c > 0) snprintf(b, sizeof(b), "%7.1f\xC2\xB0""C", (double)c);
    else       snprintf(b, sizeof(b), "%7s", "N/A");
    return b;
}

// ── Battery formatter ──────────────────────────────────
const char* formatBattery(const pwr::Snapshot& p) {
    static char b[16];
    if (p.batteryPresent) snprintf(b, sizeof(b), "%.0f%%", (double)p.batteryPercent);
    else                  snprintf(b, sizeof(b), "---");
    return b;
}

// ═════════════════════════════════════════════════════════════
// Usage
// ═════════════════════════════════════════════════════════════
void usage() {
    printf("DAEMONITOR v0.1.0 — Hardware Monitor Kernel\n\n");
    printf("Usage: Daemonitor.exe [mode] [options]\n\n");
    printf("Modes:\n");
    printf("  (default)   Continuous monitoring (Ctrl+C to exit)\n");
    printf("  --dump      Dump all metrics once and exit\n");
    printf("  --help      This message\n\n");
    printf("Options:\n");
    printf("  --ticks=N    Run N ticks then exit (default: unlimited)\n");
    printf("  --interval=N Poll interval in ms (default: 1000)\n");
}

// ═════════════════════════════════════════════════════════════
// Continuous table
// ═════════════════════════════════════════════════════════════
void tableHeader() {
    printf("DAEMONITOR v0.1.0 — Kernel Mode  (Ctrl+C to exit)\n");
    printf("%-6s | %8s | %8s | %8s | %8s | %16s | %16s | %8s\n",
           "Tick", "CPU%", "GPU%", "RAM%", "TempC", "NetRX", "NetTX", "Battery");
    printf("-------|----------|----------|----------|----------|------------------|------------------|----------\n");
}

void tableRow(int tick, const DaemonCore::KernelSnapshot& s) {
    float rp = s.ram.totalGB > 0 ? (s.ram.usedGB / s.ram.totalGB) * 100.0f : 0.0f;
    float tp = s.cpu.temperature > 0 ? s.cpu.temperature : s.gpu.temperature;
    printf("%-6d | %7.1f%% | %7.1f%% | %7.1f%% | %s | %16s | %16s | %8s\n",
           tick,
           (double)s.cpu.usagePercent,
           (double)s.gpu.usagePercent,
           (double)rp,
           formatTemp(tp),
           formatBandwidth(s.net.rxBytesPerSec).c_str(),
           formatBandwidth(s.net.txBytesPerSec).c_str(),
           formatBattery(s.pwr));
}

// ═════════════════════════════════════════════════════════════
// Metric dump
// ═════════════════════════════════════════════════════════════
void dumpHeader(const DaemonCore::KernelSnapshot& s) {
    printf("=== DAEMONITOR Metric Dump ===\n");
    printf("Metrics: %zu | Monitors: %zu\n\n", s.metricCount, s.monitorCount);
}

void dumpRow(const DaemonCore::MetricDef& m) {
    printf("  %-32s  %-24s  %8.2f %-6s  [min=%.2f max=%.2f avg=%.2f]\n",
           m.key ? m.key : "(null)", m.label ? m.label : "",
           (double)m.value, m.unit ? m.unit : "",
           (double)m.min, (double)m.max, (double)m.avg);
}

} // namespace print
