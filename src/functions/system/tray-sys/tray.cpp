// tray.cpp — System tray icon with right-click popup menu (KEYMERA pattern)
// Menu: Polling → 0.25s / 0.5s / 1s / 2s, Position → Left / Right, Auto-hide ✓
//       曲线图 → CPU / GPU / NET / RAM / SSD, Exit
// Codex §6.4: functions/system/tray-sys/

#include "functions/system/tray-sys/tray.h"

#define WIN32_LEAN_AND_MEAN
#include <Windows.h>
#include <shellapi.h>
#include <atomic>

namespace tray {

static constexpr UINT  WM_TRAY_CALLBACK = WM_APP + 42;
static constexpr UINT  TRAY_UID         = 1001;
static constexpr LPCWSTR kClassName      = L"DaemonitorTrayClass";

// ── Menu IDs ───────────────────────────────────────────
static constexpr UINT IDM_POLL_250  = 40101;
static constexpr UINT IDM_POLL_500  = 40102;
static constexpr UINT IDM_POLL_1000 = 40103;
static constexpr UINT IDM_POLL_2000 = 40104;
static constexpr UINT IDM_POS_LEFT  = 40201;
static constexpr UINT IDM_POS_RIGHT = 40202;
static constexpr UINT IDM_AUTOHIDE  = 40301;
static constexpr UINT IDM_EXIT      = 40401;
static constexpr UINT IDM_CHART_OFF = 40500;
static constexpr UINT IDM_CHART_CPU = 40501;
static constexpr UINT IDM_CHART_GPU = 40502;
static constexpr UINT IDM_CHART_NET = 40503;
static constexpr UINT IDM_CHART_RAM = 40504;
static constexpr UINT IDM_CHART_SSD = 40505;

static constexpr UINT IDM_STARTUP  = 40601;

// ── Atomic request bridge ──────────────────────────────
static std::atomic<int>    g_action{0};       // (int)Action
static std::atomic<int>    g_actionVal{0};    // payload
static std::atomic<bool>   g_autoHide{true};
static std::atomic<bool>   g_onLeft{false};
static std::atomic<int>    g_chartMetric{-1}; // -1=off, 0=CPU...
static std::atomic<bool>   g_startup{false};

static constexpr LPCWSTR kMetricNames[] = { L"CPU", L"GPU", L"NET", L"RAM", L"SSD" };

static UINT chartMetricToId(int m) {
    if (m < 0) return IDM_CHART_OFF;
    static constexpr UINT ids[] = { IDM_CHART_CPU, IDM_CHART_GPU, IDM_CHART_NET, IDM_CHART_RAM, IDM_CHART_SSD };
    return (m <= 4) ? ids[m] : IDM_CHART_OFF;
}

// ── Internal window handle ─────────────────────────────
static HWND g_hwnd = nullptr;

// ═════════════════════════════════════════════════════════════
// Window procedure
// ═════════════════════════════════════════════════════════════
static LRESULT CALLBACK trayWndProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
    if (msg == WM_TRAY_CALLBACK) {
        if (lp == WM_RBUTTONUP || lp == WM_LBUTTONUP || lp == WM_CONTEXTMENU) {
            HMENU menu = CreatePopupMenu();

            HMENU subPoll = CreatePopupMenu();
            AppendMenuW(subPoll, MF_STRING | MF_UNCHECKED, IDM_POLL_250,  L"0.25 s");
            AppendMenuW(subPoll, MF_STRING | MF_UNCHECKED, IDM_POLL_500,  L"0.5 s");
            AppendMenuW(subPoll, MF_STRING | MF_UNCHECKED, IDM_POLL_1000, L"1 s");
            AppendMenuW(subPoll, MF_STRING | MF_UNCHECKED, IDM_POLL_2000, L"2 s");
            AppendMenuW(menu, MF_POPUP, (UINT_PTR)subPoll, L"Polling");

            HMENU subPos = CreatePopupMenu();
            AppendMenuW(subPos, MF_STRING | (g_onLeft ? MF_CHECKED : MF_UNCHECKED),
                        IDM_POS_LEFT,  L"Top Left");
            AppendMenuW(subPos, MF_STRING | (g_onLeft ? MF_UNCHECKED : MF_CHECKED),
                        IDM_POS_RIGHT, L"Top Right");
            AppendMenuW(menu, MF_POPUP, (UINT_PTR)subPos, L"Position");

            AppendMenuW(menu, MF_STRING | (g_autoHide ? MF_CHECKED : MF_UNCHECKED),
                        IDM_AUTOHIDE, L"Auto-hide");

            AppendMenuW(menu, MF_STRING | (g_startup ? MF_CHECKED : MF_UNCHECKED),
                        IDM_STARTUP, L"Startup with Windows");

            // ── Chart submenu ─────────────────────────
            HMENU subChart = CreatePopupMenu();
            int curMetric = g_chartMetric;
            AppendMenuW(subChart, MF_STRING | (curMetric < 0 ? MF_CHECKED : MF_UNCHECKED),
                        IDM_CHART_OFF, L"Off");
            AppendMenuW(subChart, MF_SEPARATOR, 0, nullptr);
            for (int i = 0; i < 5; ++i) {
                AppendMenuW(subChart, MF_STRING | (curMetric == i ? MF_CHECKED : MF_UNCHECKED),
                            chartMetricToId(i), kMetricNames[i]);
            }
            AppendMenuW(menu, MF_POPUP, (UINT_PTR)subChart, L"Chart");

            AppendMenuW(menu, MF_SEPARATOR, 0, nullptr);
            AppendMenuW(menu, MF_STRING, IDM_EXIT, L"Exit");

            POINT pt; GetCursorPos(&pt);
            SetForegroundWindow(hwnd);
            UINT cmd = TrackPopupMenu(menu,
                          TPM_RETURNCMD | TPM_NONOTIFY,
                          pt.x, pt.y, 0, hwnd, nullptr);

            DestroyMenu(subPoll);
            DestroyMenu(subPos);
            DestroyMenu(subChart);
            DestroyMenu(menu);

            // ── Handle selection ───────────────────────
            switch (cmd) {
                case IDM_POLL_250:  g_action = (int)Action::SetPollMs; g_actionVal = 250;  break;
                case IDM_POLL_500:  g_action = (int)Action::SetPollMs; g_actionVal = 500;  break;
                case IDM_POLL_1000: g_action = (int)Action::SetPollMs; g_actionVal = 1000; break;
                case IDM_POLL_2000: g_action = (int)Action::SetPollMs; g_actionVal = 2000; break;
                case IDM_POS_LEFT:
                    g_onLeft = true;
                    g_action = (int)Action::ToggleLeft;
                    g_actionVal = 1;
                    break;
                case IDM_POS_RIGHT:
                    g_onLeft = false;
                    g_action = (int)Action::ToggleLeft;
                    g_actionVal = 0;
                    break;
                case IDM_AUTOHIDE: {
                    bool cur = g_autoHide;
                    g_autoHide = !cur;
                    g_action = (int)Action::ToggleAutoHide;
                    g_actionVal = g_autoHide ? 1 : 0;
                    break;
                }
                case IDM_STARTUP: {
                    bool cur = g_startup;
                    g_startup = !cur;
                    g_action = (int)Action::ToggleStartup;
                    g_actionVal = g_startup ? 1 : 0;
                    break;
                }
                case IDM_CHART_OFF:
                    g_chartMetric = -1;
                    g_action = (int)Action::SetChartMetric;
                    g_actionVal = -1;
                    break;
                case IDM_CHART_CPU:
                    g_chartMetric = 0;
                    g_action = (int)Action::SetChartMetric;
                    g_actionVal = 0;
                    break;
                case IDM_CHART_GPU:
                    g_chartMetric = 1;
                    g_action = (int)Action::SetChartMetric;
                    g_actionVal = 1;
                    break;
                case IDM_CHART_NET:
                    g_chartMetric = 2;
                    g_action = (int)Action::SetChartMetric;
                    g_actionVal = 2;
                    break;
                case IDM_CHART_RAM:
                    g_chartMetric = 3;
                    g_action = (int)Action::SetChartMetric;
                    g_actionVal = 3;
                    break;
                case IDM_CHART_SSD:
                    g_chartMetric = 4;
                    g_action = (int)Action::SetChartMetric;
                    g_actionVal = 4;
                    break;
                case IDM_EXIT:
                    g_action = (int)Action::Exit;
                    break;
                default: break;
            }
        }
        return 0;
    }
    return DefWindowProcW(hwnd, msg, wp, lp);
}

// ═════════════════════════════════════════════════════════════
// Init
// ═════════════════════════════════════════════════════════════
bool init() {
    HINSTANCE hInst = GetModuleHandleW(nullptr);

    WNDCLASSEXW wc{};
    wc.cbSize        = sizeof(WNDCLASSEXW);
    wc.lpfnWndProc   = trayWndProc;
    wc.hInstance     = hInst;
    wc.lpszClassName = kClassName;
    if (!RegisterClassExW(&wc) && GetLastError() != ERROR_CLASS_ALREADY_EXISTS)
        return false;

    g_hwnd = CreateWindowExW(
        0, kClassName, L"", WS_OVERLAPPED,
        0, 0, 1, 1, nullptr, nullptr, hInst, nullptr);
    if (!g_hwnd) return false;

    HICON hIcon = (HICON)LoadImageW(hInst, MAKEINTRESOURCEW(101),
                     IMAGE_ICON, GetSystemMetrics(SM_CXSMICON),
                     GetSystemMetrics(SM_CYSMICON), LR_DEFAULTCOLOR);

    NOTIFYICONDATAW nid{};
    nid.cbSize           = sizeof(NOTIFYICONDATAW);
    nid.hWnd             = g_hwnd;
    nid.uID              = TRAY_UID;
    nid.uFlags           = NIF_ICON | NIF_MESSAGE | NIF_TIP;
    nid.uCallbackMessage = WM_TRAY_CALLBACK;
    nid.hIcon            = hIcon;
    wcscpy(nid.szTip, L"Daemonitor");
    if (!Shell_NotifyIconW(NIM_ADD, &nid)) return false;

    return true;
}

void close() {
    if (g_hwnd) {
        NOTIFYICONDATAW nid{};
        nid.cbSize = sizeof(NOTIFYICONDATAW);
        nid.hWnd   = g_hwnd;
        nid.uID    = TRAY_UID;
        Shell_NotifyIconW(NIM_DELETE, &nid);
        DestroyWindow(g_hwnd);
        g_hwnd = nullptr;
    }
}

// ── Consume bridge ─────────────────────────────────────
Action consumeAction() {
    return (Action)g_action.exchange((int)Action::None);
}
int getActionValue() {
    return g_actionVal;
}

void syncState(bool onLeft, bool autoHide, int chartMetric, bool startup) {
    g_onLeft      = onLeft;
    g_autoHide    = autoHide;
    g_chartMetric = (chartMetric >= 0 && chartMetric <= 4) ? chartMetric : -1;
    g_startup     = startup;
}

} // namespace tray
