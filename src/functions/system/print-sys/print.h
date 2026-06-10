// print.h — Pure terminal formatting layer. Calls kernel API only.
// No data collection logic. No side effects beyond stdout.
// Codex §6.4: functions/system/print-sys/

#pragma once
#include <string>
#include "functions/system/app-core-sys/kernel.h"

namespace print {

// ── Helpers ────────────────────────────────────────────
std::string  formatBandwidth(float bytesPerSec);       // "1.2 MB/s" etc.
const char*  formatTemp(float celsius);                // "42.3°C" or "N/A"
const char*  formatBattery(const pwr::Snapshot& p);    // "87%" or "---"

// ── Usage ──────────────────────────────────────────────
void usage();

// ── Continuous monitoring table ────────────────────────
void tableHeader();                                    // column titles
void tableRow(int tick, const DaemonCore::KernelSnapshot& s);

// ── One-shot metric dump ──────────────────────────────
void dumpHeader(const DaemonCore::KernelSnapshot& s);
void dumpRow(const DaemonCore::MetricDef& m);

} // namespace print
