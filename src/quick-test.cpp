// quick-test.cpp — validate net/disk show real data
#define WIN32_LEAN_AND_MEAN
#include <Windows.h>
#include <cstdio>

#include "functions/system/app-core-sys/kernel.h"

int main() {
    if (!DaemonCore::init()) {
        printf("FATAL: init failed\n");
        return 1;
    }

    // Run 5 ticks (need 2+ for rate counters to produce data)
    for (int i = 0; i < 3; ++i) {
        Sleep(1000);
        DaemonCore::tick();
        auto s = DaemonCore::getSnapshot();

        printf("[tick %d] CPU:%5.1f%%  GPU:%5.1f%%  RAM:%5.1f%%\n",
               i+1,
               (double)s.cpu.usagePercent,
               (double)s.gpu.usagePercent,
               (double)s.ram.usagePercent);

        printf("  Net: rx=%.1f tx=%.1f B/s  ifaces=%u\n",
               (double)s.net.rxBytesPerSec, (double)s.net.txBytesPerSec,
               (unsigned)s.net.ifaceCount);
        for (uint32_t n = 0; n < s.net.ifaceCount && n < 16; ++n) {
            auto& ifc = s.net.ifaces[n];
            printf("    [%u] %-24s  rx=%10.1f  tx=%10.1f B/s  active=%d\n",
                   n, ifc.name, (double)ifc.rxBytesPerSec, (double)ifc.txBytesPerSec, ifc.active);
        }

        printf("  Disk: read=%.1f write=%.1f B/s  disks=%u\n",
               (double)s.disk.readBytesPerSec, (double)s.disk.writeBytesPerSec,
               (unsigned)s.disk.diskCount);
    }

    DaemonCore::shutdown();

    // ── History check ──────────────────────────────────
    DaemonCore::HistoryPoint hist[DaemonCore::HISTORY_DEPTH];
    size_t hc = DaemonCore::getRecentHistory(hist, DaemonCore::HISTORY_DEPTH);
    printf("  History: %zu points (first cpu=%.1f%% ram=%.1f%%)\n",
           hc, (double)hist[0].cpuUsage, (double)hist[0].ramUsage);
    return 0;
}
