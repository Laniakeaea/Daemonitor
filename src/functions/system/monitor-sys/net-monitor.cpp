// net-monitor.cpp — per-interface network I/O via GetIfTable (stable-index matching + 32-bit wrap detect)
#include "functions/system/monitor-sys/net-monitor.h"

#define WIN32_LEAN_AND_MEAN
#include <Windows.h>
#include <iphlpapi.h>
#include <cstring>
#include <cstdio>

namespace net {

static Config   g_cfg;
static Snapshot g_snap;

// Previous 32-bit counters keyed by dwIndex (stable across calls, survives reordering)
static DWORD    g_prevIndex[NET_MAX_IFACES]{};
static DWORD    g_prevRx[NET_MAX_IFACES]{};      // 32-bit dwInOctets
static DWORD    g_prevTx[NET_MAX_IFACES]{};      // 32-bit dwOutOctets
static uint32_t g_prevCount  = 0;
static uint64_t g_prevTime   = 0;  // ms

// ── Find previous slot by dwIndex ─────────────────────
static int32_t findPrevSlot(DWORD idx) {
    for (uint32_t s = 0; s < g_prevCount; ++s)
        if (g_prevIndex[s] == idx) return (int32_t)s;
    return -1;
}

// ── 32-bit counter delta with wrap-around detection ───
static uint64_t counterDelta32(DWORD cur, DWORD prev) {
    if (cur >= prev)
        return (uint64_t)(cur - prev);
    else
        return (0xFFFFFFFFULL - prev) + cur + 1;
}

// ═══════════════════════════════════════════════════════
bool init(Config* cfg) {
    if (cfg) g_cfg = *cfg;
    if (!g_cfg.enabled) return true;

    ULONG sz = 0;
    if (GetIfTable(nullptr, &sz, FALSE) != ERROR_INSUFFICIENT_BUFFER)
        return true;
    if (sz == 0) return true;

    MIB_IFTABLE* table = (MIB_IFTABLE*)malloc(sz);
    if (!table) return true;
    if (GetIfTable(table, &sz, FALSE) != NO_ERROR) {
        free(table);
        return true;
    }

    // ── Two-pass: collect candidates, sort by traffic, take top N ──
    struct Candidate { DWORD idx; DWORD rx; DWORD tx; };
    const uint32_t MAX_CAND = 128;
    Candidate candidates[MAX_CAND];
    uint32_t candCount = 0;

    for (DWORD i = 0; i < table->dwNumEntries && candCount < MAX_CAND; ++i) {
        MIB_IFROW& row = table->table[i];
        if (row.dwType == MIB_IF_TYPE_LOOPBACK) continue;
        if (row.dwOperStatus == 0) continue;

        // Skip WFP filter layers — they duplicate the base NIC counters
        const char* descr = (const char*)row.bDescr;
        if (strstr(descr, "WFP") || strstr(descr, "QoS") ||
            strstr(descr, "Virtual WiFi") || strstr(descr, "Native WiFi"))
            continue;

        candidates[candCount++] = { row.dwIndex, row.dwInOctets, row.dwOutOctets };
    }

    // Sort descending by total bytes (inOct+outOct)
    for (uint32_t a = 0; a < candCount; ++a) {
        for (uint32_t b = a + 1; b < candCount; ++b) {
            uint64_t ta = (uint64_t)candidates[a].rx + candidates[a].tx;
            uint64_t tb = (uint64_t)candidates[b].rx + candidates[b].tx;
            if (tb > ta) { Candidate t = candidates[a]; candidates[a] = candidates[b]; candidates[b] = t; }
        }
    }

    uint32_t n = 0;
    for (uint32_t c = 0; c < candCount && n < NET_MAX_IFACES; ++c) {
        g_prevIndex[n] = candidates[c].idx;
        g_prevRx[n]    = candidates[c].rx;
        g_prevTx[n]    = candidates[c].tx;
        ++n;
    }
    g_prevCount = n;
    free(table);

    g_prevTime = GetTickCount64();
    return true;
}

void shutdown() { g_prevCount = 0; }

void tick() {
    if (!g_cfg.enabled) return;

    ULONG sz = 0;
    if (GetIfTable(nullptr, &sz, FALSE) != ERROR_INSUFFICIENT_BUFFER)
        return;
    if (sz == 0) return;

    MIB_IFTABLE* table = (MIB_IFTABLE*)malloc(sz);
    if (!table) return;

    if (GetIfTable(table, &sz, FALSE) != NO_ERROR) {
        free(table);
        return;
    }

    uint64_t now  = GetTickCount64();
    float    dt   = (float)(now - g_prevTime) / 1000.0f;
    if (dt <= 0.0f) dt = 1.0f;
    g_prevTime    = now;

    uint32_t n = 0;
    float totalRx = 0, totalTx = 0;

    // Build new prev arrays & snapshot in prev-slot order (sorted by traffic)
    DWORD    newIndex[NET_MAX_IFACES]{};
    DWORD    newRx[NET_MAX_IFACES]{};
    DWORD    newTx[NET_MAX_IFACES]{};
    uint32_t newCount = 0;

    for (uint32_t s = 0; s < g_prevCount; ++s) {
        // Find this prev interface in the current table
        DWORD targetIdx = g_prevIndex[s];
        MIB_IFROW* found = nullptr;
        for (DWORD i = 0; i < table->dwNumEntries; ++i) {
            if (table->table[i].dwIndex == targetIdx) {
                found = &table->table[i];
                break;
            }
        }
        if (!found) continue;

        MIB_IFROW& row = *found;
        DWORD curRx = row.dwInOctets;
        DWORD curTx = row.dwOutOctets;

        uint64_t rxDelta = counterDelta32(curRx, g_prevRx[s]);
        uint64_t txDelta = counterDelta32(curTx, g_prevTx[s]);

        IfaceInfo& ifc = g_snap.ifaces[n];
        ifc.rxBytesPerSec = (float)rxDelta / dt;
        ifc.txBytesPerSec = (float)txDelta / dt;
        ifc.rxMbps = ifc.rxBytesPerSec * 8.0f / 1000000.0f;
        ifc.txMbps = ifc.txBytesPerSec * 8.0f / 1000000.0f;
        ifc.active = (rxDelta > 0 || txDelta > 0);

        if (row.bDescr[0]) {
            snprintf(ifc.friendlyName, NET_NAME_MAX, "%s", row.bDescr);
        } else {
            WideCharToMultiByte(CP_UTF8, 0, row.wszName, -1,
                ifc.friendlyName, NET_NAME_MAX, nullptr, nullptr);
        }

        snprintf(ifc.name, NET_NAME_MAX, "if%lu", (unsigned long)row.dwIndex);

        totalRx += ifc.rxBytesPerSec;
        totalTx += ifc.txBytesPerSec;

        if (newCount < NET_MAX_IFACES) {
            newIndex[newCount] = row.dwIndex;
            newRx[newCount] = curRx;
            newTx[newCount] = curTx;
            ++newCount;
        }
        ++n;
    }

    // Commit new prev arrays
    g_prevCount = newCount;
    for (uint32_t k = 0; k < newCount; ++k) {
        g_prevIndex[k] = newIndex[k];
        g_prevRx[k] = newRx[k];
        g_prevTx[k] = newTx[k];
    }

    g_snap.ifaceCount = n;
    g_snap.rxBytesPerSec = totalRx;
    g_snap.txBytesPerSec = totalTx;
    g_snap.rxMbps = totalRx * 8.0f / 1000000.0f;
    g_snap.txMbps = totalTx * 8.0f / 1000000.0f;

    free(table);
}

const Snapshot& snapshot() { return g_snap; }
Config*         getConfig() { return &g_cfg; }
void            register_metrics(MetricRegistry*) {}

} // namespace net
