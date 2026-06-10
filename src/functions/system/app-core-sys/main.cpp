// main.cpp — DAEMONITOR entry point. Kernel + Display + Tray + Settings.
// Registry HKCU\Software\Daemonitor stores PollMs, OnLeft, AutoHide, ChartMetric, Startup.
// Codex §6.4: functions/system/app-core-sys/

#include "functions/system/app-core-sys/kernel.h"
#include "functions/system/display-sys/display.h"
#include "functions/system/tray-sys/tray.h"

#define WIN32_LEAN_AND_MEAN
#include <Windows.h>
#include <cstdio>
#include <cstdlib>
#include <string>

// ═════════════════════════════════════════════════════════════
// Registry persistence
// ═════════════════════════════════════════════════════════════
static constexpr LPCWSTR kRegKey    = L"Software\\Daemonitor";
static constexpr LPCWSTR kValPoll   = L"PollMs";
static constexpr LPCWSTR kValLeft   = L"OnLeft";
static constexpr LPCWSTR kValHide   = L"AutoHide";
static constexpr LPCWSTR kValChart  = L"ChartMetric";
static constexpr LPCWSTR kValStart  = L"Startup";

// Windows Run key for auto-start
static constexpr LPCWSTR kRegRunKey   = L"Software\\Microsoft\\Windows\\CurrentVersion\\Run";
static constexpr LPCWSTR kRegRunValue = L"Daemonitor";

static int   g_pollMs      = display::kDefaultPollMs;
static bool  g_onLeft      = false;
static bool  g_autoHide    = true;
static int   g_chartMetric = -1;
static bool  g_startup     = false;

static void loadSettings() {
    HKEY hk;
    if (RegCreateKeyExW(HKEY_CURRENT_USER, kRegKey, 0, nullptr, 0,
                        KEY_READ, nullptr, &hk, nullptr) == ERROR_SUCCESS) {
        DWORD v, sz = sizeof(DWORD);
        if (RegQueryValueExW(hk, kValPoll, nullptr, nullptr, (LPBYTE)&v, &sz) == ERROR_SUCCESS && v >= 250 && v <= 10000)
            g_pollMs = (int)v;
        sz = sizeof(DWORD);
        if (RegQueryValueExW(hk, kValLeft, nullptr, nullptr, (LPBYTE)&v, &sz) == ERROR_SUCCESS)
            g_onLeft = (v != 0);
        sz = sizeof(DWORD);
        if (RegQueryValueExW(hk, kValHide, nullptr, nullptr, (LPBYTE)&v, &sz) == ERROR_SUCCESS)
            g_autoHide = (v != 0);
        sz = sizeof(DWORD);
        if (RegQueryValueExW(hk, kValChart, nullptr, nullptr, (LPBYTE)&v, &sz) == ERROR_SUCCESS) {
            g_chartMetric = ((int)v >= 0 && (int)v <= 4) ? (int)v : -1;
        }
        sz = sizeof(DWORD);
        if (RegQueryValueExW(hk, kValStart, nullptr, nullptr, (LPBYTE)&v, &sz) == ERROR_SUCCESS)
            g_startup = (v != 0);
        RegCloseKey(hk);
    }
}

// ═════════════════════════════════════════════════════════════
// Auto-start with Windows (HKCU\...\Run key)
// ═════════════════════════════════════════════════════════════
static void setStartWithWindows(bool enabled) {
    HKEY hk;
    if (RegCreateKeyExW(HKEY_CURRENT_USER, kRegRunKey, 0, nullptr,
                        REG_OPTION_NON_VOLATILE, KEY_SET_VALUE,
                        nullptr, &hk, nullptr) == ERROR_SUCCESS) {
        if (enabled) {
            wchar_t exePath[MAX_PATH];
            if (GetModuleFileNameW(nullptr, exePath, MAX_PATH) > 0) {
                std::wstring cmd = L"\"";
                cmd += exePath;
                cmd += L"\"";
                RegSetValueExW(hk, kRegRunValue, 0, REG_SZ,
                    (const BYTE*)cmd.c_str(),
                    (DWORD)((cmd.size() + 1) * sizeof(wchar_t)));
            }
        } else {
            RegDeleteValueW(hk, kRegRunValue);
        }
        RegCloseKey(hk);
    }
}

static void saveSettings() {
    HKEY hk;
    if (RegCreateKeyExW(HKEY_CURRENT_USER, kRegKey, 0, nullptr, 0,
                        KEY_WRITE, nullptr, &hk, nullptr) == ERROR_SUCCESS) {
        DWORD v = (DWORD)g_pollMs;
        RegSetValueExW(hk, kValPoll, 0, REG_DWORD, (const BYTE*)&v, sizeof(DWORD));
        v = g_onLeft ? 1 : 0;
        RegSetValueExW(hk, kValLeft, 0, REG_DWORD, (const BYTE*)&v, sizeof(DWORD));
        v = g_autoHide ? 1 : 0;
        RegSetValueExW(hk, kValHide, 0, REG_DWORD, (const BYTE*)&v, sizeof(DWORD));
        v = (DWORD)g_chartMetric;
        RegSetValueExW(hk, kValChart, 0, REG_DWORD, (const BYTE*)&v, sizeof(DWORD));
        v = g_startup ? 1 : 0;
        RegSetValueExW(hk, kValStart, 0, REG_DWORD, (const BYTE*)&v, sizeof(DWORD));
        RegCloseKey(hk);
    }
}

// ═════════════════════════════════════════════════════════════
// Kernel thread
// ═════════════════════════════════════════════════════════════
static volatile bool g_running = true;
static DWORD WINAPI kernelThread(LPVOID) {
    while (g_running) {
        DaemonCore::tick();
        display::updateMetrics();
        Sleep((DWORD)g_pollMs);
    }
    return 0;
}

// ═════════════════════════════════════════════════════════════
// Apply settings to display layer
// ═════════════════════════════════════════════════════════════
static void applySettings() {
    display::setOnLeft(g_onLeft);
    display::setAutoHide(g_autoHide);
    display::setChartMetric(g_chartMetric);
}

// ═════════════════════════════════════════════════════════════
// Entry point
// ═════════════════════════════════════════════════════════════
int WINAPI WinMain(HINSTANCE, HINSTANCE, LPSTR, int) {
    HANDLE hMutex = CreateMutexW(nullptr, TRUE, L"Global\\Daemonitor_SingleInstance");
    if (GetLastError() == ERROR_ALREADY_EXISTS) {
        return 0;
    }

    loadSettings();

    if (!DaemonCore::init()) return 1;
    if (!tray::init()) {
        DaemonCore::shutdown();
        return 1;
    }
    if (!display::init()) {
        tray::close();
        DaemonCore::shutdown();
        return 1;
    }
    applySettings();
    tray::syncState(g_onLeft, g_autoHide, g_chartMetric, g_startup);
    display::startPeekTimer();

    HANDLE hKernel = CreateThread(nullptr, 0, kernelThread, nullptr, 0, nullptr);

    bool running = true;
    while (running) {
        MSG msg;
        while (PeekMessageA(&msg, nullptr, 0, 0, PM_REMOVE)) {
            if (msg.message == WM_QUIT) { running = false; break; }
            TranslateMessage(&msg);
            DispatchMessageA(&msg);
        }

        tray::Action act = tray::consumeAction();
        switch (act) {
            case tray::Action::Exit:
                running = false;
                break;
            case tray::Action::SetPollMs: {
                int v = tray::getActionValue();
                if (v >= 250 && v <= 10000) g_pollMs = v;
                saveSettings();
                break;
            }
            case tray::Action::ToggleLeft:
                g_onLeft = (tray::getActionValue() != 0);
                applySettings();
                saveSettings();
                break;
            case tray::Action::ToggleAutoHide:
                g_autoHide = (tray::getActionValue() != 0);
                applySettings();
                saveSettings();
                break;
            case tray::Action::SetChartMetric:
                g_chartMetric = tray::getActionValue();
                applySettings();
                saveSettings();
                break;
            case tray::Action::ToggleStartup:
                g_startup = (tray::getActionValue() != 0);
                setStartWithWindows(g_startup);
                saveSettings();
                break;
            default: break;
        }
        Sleep(16);
    }

    g_running = false;
    WaitForSingleObject(hKernel, 2000);
    CloseHandle(hKernel);
    display::close();
    tray::close();
    DaemonCore::shutdown();

    return 0;
}
