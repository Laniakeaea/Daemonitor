// test-monitors.cpp — standalone diagnostics for net/disk monitors
#define WIN32_LEAN_AND_MEAN
#include <Windows.h>
#include <iphlpapi.h>
#include <Pdh.h>
#include <cstdio>
#include <cstring>
#include <cstdint>

int main() {
    printf("=== Network Monitor Diagnostic ===\n");

    // Test 1: GetIfTable basic
    ULONG sz = 0;
    DWORD ret = GetIfTable(nullptr, &sz, FALSE);
    printf("[1] GetIfTable(NULL): ret=%lu sz=%lu (expected ERROR_INSUFFICIENT_BUFFER=%lu)\n",
           (unsigned long)ret, (unsigned long)sz, (unsigned long)ERROR_INSUFFICIENT_BUFFER);

    if (ret == ERROR_INSUFFICIENT_BUFFER && sz > 0) {
        MIB_IFTABLE* table = (MIB_IFTABLE*)malloc(sz);
        if (table && GetIfTable(table, &sz, FALSE) == NO_ERROR) {
            printf("[2] Table entries: %lu | LOOPBACK=%d OPER_STATUS=%d\n",
                   (unsigned long)table->dwNumEntries,
                   (int)MIB_IF_TYPE_LOOPBACK, (int)MIB_IF_OPER_STATUS_OPERATIONAL);

            // Find entries with non-zero counters
            printf("[3] Entries with traffic:\n");
            int found = 0;
            for (DWORD i = 0; i < table->dwNumEntries; ++i) {
                MIB_IFROW& row = table->table[i];
                if (row.dwInOctets > 0 || row.dwOutOctets > 0) {
                    printf("    [%lu] idx=%lu type=%lu oper=%lu in=%lu out=%lu '%s'\n",
                           (unsigned long)i, (unsigned long)row.dwIndex,
                           (unsigned long)row.dwType, (unsigned long)row.dwOperStatus,
                           (unsigned long)row.dwInOctets, (unsigned long)row.dwOutOctets,
                           row.bDescr);
                    ++found;
                }
            }
            printf("    => %d with traffic\n", found);

            // Show ALL non-loopback (first 25)
            printf("[4] All non-loopback:\n");
            int shown = 0;
            for (DWORD i = 0; i < table->dwNumEntries && shown < 25; ++i) {
                MIB_IFROW& row = table->table[i];
                if (row.dwType == MIB_IF_TYPE_LOOPBACK) continue;
                printf("    [%lu] idx=%lu type=%lu oper=%lu in=%lu out=%lu '%s'\n",
                       (unsigned long)i, (unsigned long)row.dwIndex,
                       (unsigned long)row.dwType, (unsigned long)row.dwOperStatus,
                       (unsigned long)row.dwInOctets, (unsigned long)row.dwOutOctets,
                       row.bDescr);
                ++shown;
            }
        }
        free(table);
    }

    printf("\n=== Disk Monitor Diagnostic ===\n");

    PDH_HQUERY query = nullptr;
    PDH_STATUS ps = PdhOpenQueryW(nullptr, 0, &query);
    printf("[5] PdhOpenQueryW: %lu (0=SUCCESS)\n", (unsigned long)ps);

    for (uint32_t d = 0; d < 4; ++d) {
        wchar_t readPath[256];
        _snwprintf_s(readPath, 256, _TRUNCATE,
            L"\\PhysicalDisk(%u)\\Disk Read Bytes/sec", d);
        
        PDH_HCOUNTER counter = nullptr;
        ps = PdhAddCounterW(query, readPath, 0, &counter);
        printf("[6] PdhAddCounterW 'PhysicalDisk(%u)\\Disk Read Bytes/sec': %lu (0=SUCCESS)\n",
               d, (unsigned long)ps);

        if (ps == ERROR_SUCCESS) {
            PdhCollectQueryData(query);
            Sleep(1000);
            ps = PdhCollectQueryData(query);
            printf("    PdhCollectQueryData #2: %lu\n", (unsigned long)ps);
            
            if (ps == ERROR_SUCCESS) {
                PDH_FMT_COUNTERVALUE val{};
                ps = PdhGetFormattedCounterValue(counter,
                    PDH_FMT_DOUBLE | PDH_FMT_NOCAP100, nullptr, &val);
                printf("    Value: %.1f B/s (status=%lu, CStatus=%lu)\n",
                       (double)val.doubleValue, (unsigned long)ps, (unsigned long)val.CStatus);
            }
        }
    }

    // Try _Total
    wchar_t totalPath[256];
    _snwprintf_s(totalPath, 256, _TRUNCATE,
        L"\\PhysicalDisk(_Total)\\Disk Read Bytes/sec");
    PDH_HCOUNTER tcounter = nullptr;
    ps = PdhAddCounterW(query, totalPath, 0, &tcounter);
    printf("[7] PdhAddCounterW '_Total': %lu (0=SUCCESS)\n", (unsigned long)ps);
    if (ps == ERROR_SUCCESS) {
        PdhCollectQueryData(query);
        Sleep(1000);
        PdhCollectQueryData(query);
        PDH_FMT_COUNTERVALUE val{};
        ps = PdhGetFormattedCounterValue(tcounter,
            PDH_FMT_DOUBLE | PDH_FMT_NOCAP100, nullptr, &val);
        printf("    _Total value: %.1f B/s (status=%lu)\n",
               (double)val.doubleValue, (unsigned long)ps);
    }

    PdhCloseQuery(query);
    printf("\n=== Done ===\n");
    return 0;
}
