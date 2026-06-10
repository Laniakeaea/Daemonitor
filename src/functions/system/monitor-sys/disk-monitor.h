#pragma once
#include "functions/system/monitor-sys/metric.h"
#include <cstdint>

#define DISK_MAX_DISKS  8
#define DISK_NAME_MAX   64

namespace disk {

// ── Config ──────────────────────────────────────────────
struct Config {
    bool     enabled         = true;
    uint32_t pollIntervalMs  = 1000;
    size_t   historySize     = 256;
    float    warnQueueDepth  = 10.0f;
    float    warnReadMBps    = 500.0f;
    float    warnWriteMBps   = 500.0f;
    // Filter: comma-separated physical drive numbers, e.g. "0,1"
    char     diskFilter[64]  = "";
};

// ── Per-disk data ───────────────────────────────────────
struct DiskInfo {
    char    name[DISK_NAME_MAX];      // "PhysicalDrive0"
    char    model[DISK_NAME_MAX];     // "Samsung SSD 980 PRO"
    float   readBytesPerSec;
    float   writeBytesPerSec;
    float   readMBps;
    float   writeMBps;
    float   queueDepth;
    bool    active;
};

// ── Full snapshot ───────────────────────────────────────
struct Snapshot {
    float    readBytesPerSec;          // total
    float    writeBytesPerSec;         // total
    float    readMBps;
    float    writeMBps;
    uint32_t diskCount;
    DiskInfo disks[DISK_MAX_DISKS];
};

// ── Lifecycle ───────────────────────────────────────────
bool   init(Config* cfg);
void   shutdown();
void   tick();
const Snapshot& snapshot();
Config* getConfig();
void   register_metrics(class MetricRegistry* reg);

} // namespace disk
