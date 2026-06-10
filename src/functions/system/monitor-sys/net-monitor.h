#pragma once
#include "functions/system/monitor-sys/metric.h"
#include <cstdint>

#define NET_MAX_IFACES 166
#define NET_NAME_MAX   64

namespace net {

// ── Config ──────────────────────────────────────────────
struct Config {
    bool     enabled         = true;
    uint32_t pollIntervalMs  = 1000;
    size_t   historySize     = 256;
    float    warnRxMbps      = 800.0f;    // Mbps warning
    float    warnTxMbps      = 800.0f;
    // Filter: if non-empty, only monitor matching ifaces (comma-separated)
    char     ifaceFilter[128] = "";
};

// ── Per-interface data ──────────────────────────────────
struct IfaceInfo {
    char    name[NET_NAME_MAX];
    char    friendlyName[NET_NAME_MAX];
    float   rxBytesPerSec;
    float   txBytesPerSec;
    float   rxMbps;
    float   txMbps;
    bool    active;
};

// ── Full snapshot ───────────────────────────────────────
struct Snapshot {
    float    rxBytesPerSec;
    float    txBytesPerSec;
    float    rxMbps;
    float    txMbps;
    uint32_t ifaceCount;
    IfaceInfo ifaces[NET_MAX_IFACES];
};

// ── Lifecycle ───────────────────────────────────────────
bool   init(Config* cfg);
void   shutdown();
void   tick();
const Snapshot& snapshot();
Config* getConfig();
void   register_metrics(class MetricRegistry* reg);

} // namespace net
