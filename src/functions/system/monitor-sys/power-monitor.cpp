// power-monitor.cpp — Windows battery / power status monitor
// Uses GetSystemPowerStatus for battery info.
// Codex §6.4: functions/system/ — cross-cutting system capability

#include "functions/system/monitor-sys/power-monitor.h"
#include <windows.h>
#include <cstdio>
#include <cstring>
#include <cmath>

namespace pwr {

static Config  s_cfg;
static Snapshot s_snap;
static bool    s_initialized = false;

// For charge rate estimation
static float  s_prevBatteryPct = -1;
static ULONGLONG s_prevTick = 0;
static float  s_batteryCapacityWh = 50.0f;   // rough estimate, typical laptop battery

bool init(Config* cfg) {
    if (cfg) s_cfg = *cfg;
    memset(&s_snap, 0, sizeof(s_snap));
    s_snap.batteryPercent  = -1;
    s_snap.remainingTimeMin = -1;
    s_snap.chargeRateWatts  = 0;
    strcpy(s_snap.powerSource, "Unknown");
    s_snap.batteryPresent = false;
    s_snap.acConnected    = false;
    s_snap.charging       = false;
    s_initialized = true;
    return true;
}

void shutdown() {
    s_initialized = false;
}

void tick() {
    if (!s_initialized) return;
    if (!s_cfg.enabled) return;

    SYSTEM_POWER_STATUS ps;
    if (!GetSystemPowerStatus(&ps)) {
        s_snap.batteryPercent = -1;
        s_snap.batteryPresent = false;
        strcpy(s_snap.powerSource, "Unknown");
        return;
    }

    // ── AC status ──
    s_snap.acConnected = (ps.ACLineStatus == 1);

    // ── Battery presence ──
    s_snap.batteryPresent = !(ps.BatteryFlag & 128);

    // ── Power source string ──
    if (s_snap.acConnected)
        strcpy(s_snap.powerSource, "AC Power");
    else if (s_snap.batteryPresent)
        strcpy(s_snap.powerSource, "Battery");
    else
        strcpy(s_snap.powerSource, "AC Power");

    // ── Battery percentage ──
    if (ps.BatteryLifePercent != 255)
        s_snap.batteryPercent = (float)ps.BatteryLifePercent;
    else
        s_snap.batteryPercent = -1;

    // ── Charging status ──
    s_snap.charging = (ps.BatteryFlag & 8) != 0;

    // ── Remaining time ──
    if (ps.BatteryLifeTime != (DWORD)-1 && !s_snap.acConnected)
        s_snap.remainingTimeMin = (float)ps.BatteryLifeTime / 60.0f;
    else
        s_snap.remainingTimeMin = -1;

    // ── Charge/discharge rate estimation (from consecutive readings) ──
    ULONGLONG now = GetTickCount64();
    if (s_prevBatteryPct >= 0 && s_prevTick > 0 && s_snap.batteryPercent >= 0) {
        float deltaPct = s_snap.batteryPercent - s_prevBatteryPct;
        float deltaHours = (float)(now - s_prevTick) / 3600000.0f;  // ms → hours
        if (deltaHours > 0.001f) {
            float pctPerHour = deltaPct / deltaHours;
            s_snap.chargeRateWatts = pctPerHour * s_batteryCapacityWh / 100.0f;
        }
    }
    s_prevBatteryPct = s_snap.batteryPercent;
    s_prevTick = now;
}

const Snapshot& snapshot() { return s_snap; }
Config* getConfig() { return &s_cfg; }

void register_metrics(DaemonCore::MetricRegistry* reg) {
    reg->reg("pwr.battery.percent",     "Battery %",         "%",     s_cfg.warnBatteryPct, s_cfg.critBatteryPct);
    reg->reg("pwr.battery.remaining",   "Battery Remaining", "min",   0, 0);
    reg->reg("pwr.battery.charge_rate", "Charge Rate",       "W",     0, 0);
    reg->reg("pwr.ac_connected",        "AC Connected",      "bool",  0, 0);
    reg->reg("pwr.charging",            "Charging",          "bool",  0, 0);
}

} // namespace pwr
