// display.cpp — Direct2D + DirectWrite overlay with auto-hide animation
//   Supports left/right anchor. Chart mode: 1 slot + 284×40 line chart.
//   Codex §6.4: functions/system/display-sys/

#include "functions/system/display-sys/display.h"
#include "functions/system/display-sys/svg-render.h"
#include "functions/system/display-sys/embedded_font.h"
#include "functions/system/app-core-sys/kernel.h"

#define WIN32_LEAN_AND_MEAN
#include <Windows.h>
#include <d2d1.h>
#include <dwrite.h>
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <string>

#pragma comment(lib, "d2d1.lib")
#pragma comment(lib, "dwrite.lib")

namespace display {

// ═════════════════════════════════════════════════════════════
// D2D / DWrite factories
// ═════════════════════════════════════════════════════════════
static ID2D1Factory*         g_d2dFactory = nullptr;
static IDWriteFactory*       g_dwFactory  = nullptr;
static ID2D1DCRenderTarget*  g_dcRt       = nullptr;

// ═════════════════════════════════════════════════════════════
// Font
// ═════════════════════════════════════════════════════════════
static IDWriteTextFormat* g_txtMain  = nullptr;
static IDWriteTextFormat* g_txtSub   = nullptr;
static IDWriteTextFormat* g_txtMainR = nullptr;  // trailing (left anchor)
static IDWriteTextFormat* g_txtSubR  = nullptr;
static bool  g_fontLoaded = false;
static HANDLE g_hFontRes  = nullptr;

static void loadFont() {
    if (g_fontLoaded) return;
    g_fontLoaded = true;

    // Load from embedded subset (AddFontMemResourceEx)
    DWORD nFonts = 0;
    g_hFontRes = AddFontMemResourceEx(
        (void*)kFontData, kFontDataSize, nullptr, &nFonts);
    // Even if it fails (already installed), proceed

    static const wchar_t* kFamily = L"UbuntuMono Nerd Font Mono";
    auto mkFmt = [&](float sz, IDWriteTextFormat** pp) {
        g_dwFactory->CreateTextFormat(kFamily, nullptr,
            DWRITE_FONT_WEIGHT_BOLD, DWRITE_FONT_STYLE_NORMAL, DWRITE_FONT_STRETCH_NORMAL,
            sz, L"", pp);
    };
    mkFmt(14.0f, &g_txtMain);
    mkFmt(10.0f, &g_txtSub);
    mkFmt(14.0f, &g_txtMainR);
    mkFmt(10.0f, &g_txtSubR);

    for (auto* f : {g_txtMain, g_txtSub}) {
        if (!f) continue;
        f->SetWordWrapping(DWRITE_WORD_WRAPPING_NO_WRAP);
        f->SetParagraphAlignment(DWRITE_PARAGRAPH_ALIGNMENT_NEAR);
        f->SetTextAlignment(DWRITE_TEXT_ALIGNMENT_LEADING);
    }
    for (auto* f : {g_txtMainR, g_txtSubR}) {
        if (!f) continue;
        f->SetWordWrapping(DWRITE_WORD_WRAPPING_NO_WRAP);
        f->SetParagraphAlignment(DWRITE_PARAGRAPH_ALIGNMENT_NEAR);
        f->SetTextAlignment(DWRITE_TEXT_ALIGNMENT_TRAILING);
    }
}

static void freeFont() {
    if (g_txtMain)  { g_txtMain->Release();  g_txtMain  = nullptr; }
    if (g_txtSub)   { g_txtSub->Release();   g_txtSub   = nullptr; }
    if (g_txtMainR) { g_txtMainR->Release(); g_txtMainR = nullptr; }
    if (g_txtSubR)  { g_txtSubR->Release();  g_txtSubR  = nullptr; }
    if (g_hFontRes) { RemoveFontMemResourceEx(g_hFontRes); g_hFontRes = nullptr; }
    g_fontLoaded = false;
}

// ═════════════════════════════════════════════════════════════
// Icon cache
// ═════════════════════════════════════════════════════════════
enum class SlotIcon : int { CPU, GPU, NET, RAM, SSD, COUNT };
static constexpr int kIconCount = (int)SlotIcon::COUNT;
static SlotIcon g_slotOrder[5] = { SlotIcon::CPU, SlotIcon::GPU, SlotIcon::NET, SlotIcon::RAM, SlotIcon::SSD };
static ID2D1PathGeometry* g_iconGeom[kIconCount] = {};
static bool g_iconsLoaded = false;
// ═════════════════════════════════════════════════════════════
// Embedded SVG icons (no file I/O at runtime)
// ═════════════════════════════════════════════════════════════
static constexpr const char* kSvgData[] = {
    // CPU
    "<svg width=\"20\" height=\"60\" viewBox=\"0 0 20 60\" fill=\"none\" xmlns=\"http://www.w3.org/2000/svg\">"
    "<path d=\"M3.32 21H16.64V22.1704H20V24.3901H16.64V26.6502H20V28.9103H16.64V31.0897H20V33.3498H16.64V35.6099H20V37.8296H16.64V39H3.32V37.8296H0V35.6099H3.32V33.3498H0V31.0897H3.32V28.9103H0V26.6502H3.32V24.3901H0V22.1704H3.32V21ZM8.92 33.3498V36.7399H10V33.3498H8.92ZM11.08 33.3498V36.7399H12.24V33.3498H11.08ZM13.32 33.3498V36.7399H14.44V33.3498H13.32Z\" fill=\"#D9D9D9\"/>"
    "</svg>",
    // GPU
    "<svg width=\"20\" height=\"60\" viewBox=\"0 0 20 60\" fill=\"none\" xmlns=\"http://www.w3.org/2000/svg\">"
    "<path d=\"M20 27.0531H18.04V29.0442H20V30.9558H18.04V33.0266H20V35.0177H18.04V37.0089C18.04 37.5396 17.84 38.0044 17.44 38.4027C17.04 38.8009 16.56 39 16 39H2C1.46667 39 1 38.8009 0.6 38.4027C0.2 38.0044 0 37.5396 0 23.0708V23.0708C0 22.4865 0.2 21.9956 0.6 21.5973C1 21.1991 1.46667 21 2 21H16C16.56 21 17.04 21.1991 17.44 21.5973C17.84 21.9956 18.04 22.4865 18.04 23.0708V24.9823H20V27.0531ZM16 37.0089V23.0708H2V37.0089H16ZM4 30.9558V35.0177H9.04V30.9558H4ZM10 24.9823V28.0088H14V24.9823H10ZM4 24.9823V30H9.04V24.9823H4ZM10 29.0442V35.0177H14V29.0442H10Z\" fill=\"#D9D9D9\"/>"
    "</svg>",
    // NET
    "<svg width=\"20\" height=\"60\" viewBox=\"0 0 20 60\" fill=\"none\" xmlns=\"http://www.w3.org/2000/svg\">"
    "<path d=\"M10 39L6.68 34.2201C7.64 33.4162 8.74669 33.0144 10 33.0144C11.2533 33.0144 12.3733 33.4162 13.36 34.2201L10 39ZM10 21C11.8667 21 13.64 21.3158 15.32 21.9474C17.0267 22.5789 18.5867 23.4689 20 24.6172L18.36 27.0287C17.16 26.0526 15.8533 25.3062 14.44 24.7895C13.0267 24.2727 11.5467 24.0144 10 24.0144C8.45331 24.0144 6.97331 24.2727 5.56 24.7895C4.14667 25.3062 2.85333 26.0526 1.68 27.0287L0 24.6172C1.41333 23.4689 2.96 22.5789 4.64 21.9474C6.34669 21.3158 8.13331 21 10 21ZM10 27.0287C11.2267 27.0287 12.4133 27.244 13.56 27.6746C14.7067 28.0765 15.76 28.6507 16.72 29.3971L15 31.7656C13.5333 30.5885 11.8667 30 10 30C8.16 30 6.49331 30.5885 5 31.7656L3.32 29.3971C4.28 28.6507 5.32 28.0765 6.44 27.6746C7.58669 27.244 8.77331 27.0287 10 27.0287Z\" fill=\"#D9D9D9\"/>"
    "</svg>",
    // RAM
    "<svg width=\"20\" height=\"60\" viewBox=\"0 0 20 60\" fill=\"none\" xmlns=\"http://www.w3.org/2000/svg\">"
    "<path d=\"M2.2 23H17.76C18.3733 23 18.8933 23.2249 19.32 23.6747C19.7467 24.1245 19.9733 24.6867 20 25.3614V25.6145C20 25.8393 19.88 26.0361 19.64 26.2048C19.1333 26.5422 18.88 27.0201 18.88 27.6386C18.88 28.3133 19.1333 28.8193 19.64 29.1566C19.88 29.3253 20 29.5221 20 29.747V32.3614H0V29.747C0 29.5221 0.12 29.3253 0.36 29.1566C0.866669 28.7631 1.12 28.2571 1.12 27.6386C1.12 27.0201 0.866669 26.5422 0.36 26.2048C0.12 26.0361 0 25.8393 0 25.6145V25.3614C0 24.6867 0.213333 24.1245 0.64 23.6747C1.06667 23.2249 1.58667 23 2.2 23ZM20 33.5422V35.9036C20 36.241 19.8933 36.494 19.68 36.6626C19.4667 36.8875 19.2 37 18.88 37H17.2V35.9036C17.2 35.7349 17.1467 35.5944 17.04 35.4819C16.9333 35.3695 16.8 35.3133 16.64 35.3133C16.48 35.3133 16.36 35.3695 16.28 35.4819C16.1733 35.5944 16.12 35.7349 16.12 35.9036V37H12.76V35.9036C12.76 35.7349 12.7067 35.5944 12.6 35.4819C12.4933 35.3695 12.3733 35.3133 12.24 35.3133C12.08 35.3133 11.9467 35.3695 11.84 35.4819C11.7333 35.5944 11.68 35.7349 11.68 35.9036V37H8.32V35.9036C8.32 35.7349 8.26669 35.5944 8.16 35.4819C8.05331 35.3695 7.92 35.3133 7.76 35.3133C7.6 35.3133 7.48 35.3695 7.4 35.4819C7.29331 35.5944 7.22669 35.7349 7.2 35.9036V37H3.88V35.9036C3.88 35.7349 3.82667 35.5944 3.72 35.4819C3.61333 35.3695 3.48 35.3133 3.32 35.3133C3.16 35.3133 3.02667 35.3695 2.92 35.4819C2.81333 35.5944 2.76 35.7349 2.76 35.9036V37H1.12C0.8 37 0.533333 36.8875 0.32 36.6626C0.106667 36.4378 0 36.1848 0 35.9036V33.5422H20ZM6.64 26.4578C6.64 26.1205 6.53331 25.8675 6.32 25.6988C6.10667 25.4739 5.85333 25.3614 5.56 25.3614C5.24 25.3614 4.97333 25.4739 4.76 25.6988C4.54667 25.9237 4.44 26.1767 4.44 26.4578V28.8193C4.44 29.1566 4.54667 29.4378 4.76 29.6626C4.97333 29.8875 5.24 30 5.56 30C5.88 30 6.13333 29.8875 6.32 29.6626C6.53331 29.4378 6.64 29.1566 6.64 28.8193V26.4578ZM11.12 26.4578C11.12 26.1205 11 25.8675 10.76 25.6988C10.5467 25.4739 10.2933 25.3614 10 25.3614C9.68 25.3614 9.41331 25.4739 9.2 25.6988C8.98669 25.9237 8.88 26.1767 8.88 26.4578V28.8193C8.88 29.1566 8.98669 29.4378 9.2 29.6626C9.41331 29.8875 9.68 30 10 30C10.32 30 10.5733 29.8875 10.76 29.6626C10.9733 29.4378 11.0933 29.1566 11.12 28.8193V26.4578ZM15.56 26.4578C15.56 26.1205 15.4533 25.8675 15.24 25.6988C15.0267 25.4739 14.76 25.3614 14.44 25.3614C14.12 25.3614 13.8533 25.4739 13.64 25.6988C13.4267 25.9237 13.32 26.1767 13.32 26.4578V28.8193C13.32 29.1566 13.4267 29.4378 13.64 29.6626C13.8533 29.8875 14.12 30 14.44 30C14.76 30 15.0267 29.8875 15.24 29.6626C15.4533 29.4378 15.56 29.1566 15.56 28.8193V26.4578Z\" fill=\"#D9D9D9\"/>"
    "</svg>",
    // SSD
    "<svg width=\"20\" height=\"60\" viewBox=\"0 0 20 60\" fill=\"none\" xmlns=\"http://www.w3.org/2000/svg\">"
    "<path d=\"M2.48 21H17.48C18.1733 21 18.76 21.2601 19.24 21.7808C19.72 22.274 19.9733 22.8769 20 23.589V26.137C20 26.8491 19.7467 27.4655 19.24 27.9863C18.76 28.4795 18.1733 28.726 17.48 28.726H2.48C1.78667 28.726 1.2 28.4795 0.72 27.9863C0.24 27.4932 0 26.8767 0 26.137V23.589C0 22.8769 0.24 22.274 0.72 21.7808C1.2 21.2601 1.78667 21 2.48 21ZM14.08 24.2055C13.92 24.0135 13.7067 23.9178 13.44 23.9178C13.1733 23.9178 12.9467 24.0135 12.76 24.2055C12.5733 24.3975 12.48 24.6299 12.48 24.9041C12.48 25.1783 12.5733 25.3973 12.76 25.5616C12.9467 25.7536 13.1733 25.8493 13.44 25.8493C13.7067 25.8493 13.92 25.7536 14.08 25.5616C14.2667 25.3697 14.36 25.1507 14.36 24.9041C14.36 24.6299 14.2667 24.3975 14.08 24.2055ZM15.56 24.2055C15.4 24.3975 15.32 24.6299 15.32 24.9041C15.32 25.1783 15.4 25.3973 15.56 25.5616C15.7467 25.7536 15.9733 25.8493 16.24 25.8493C16.5067 25.8493 16.72 25.7536 16.88 25.5616C17.0667 25.3697 17.16 25.1507 17.16 24.9041C17.16 24.6299 17.0667 24.3975 16.88 24.2055C16.6933 24.0135 16.48 23.9178 16.24 23.9178C15.9733 23.9178 15.7467 24.0135 15.56 24.2055ZM2.48 31.3151H17.48C18.1733 31.3151 18.76 31.5616 19.24 32.0548C19.72 32.5479 19.9733 33.1509 20 33.863V36.4521C20 37.1642 19.7467 37.7671 19.24 38.2603C18.76 38.7534 18.1733 39 17.48 39H2.48C1.78667 39 1.2 38.7534 0.72 38.2603C0.24 37.7671 0 37.1642 0 36.4521V33.863C0 33.1509 0.24 32.5479 0.72 32.0548C1.2 31.5616 1.78667 31.3151 2.48 31.3151ZM14.08 34.4795C13.92 34.2875 13.7067 34.1918 13.44 34.1918C13.1733 34.1918 12.9467 34.2875 12.76 34.4795C12.5733 34.6715 12.48 34.8904 12.48 35.137C12.48 35.4112 12.5733 35.6436 12.76 35.8356C12.9467 36.0276 13.1733 36.1233 13.44 36.1233C13.7067 36.1233 13.92 36.0276 14.08 35.8356C14.2667 35.6436 14.36 35.4112 14.36 35.137C14.36 34.8628 14.2667 34.6438 14.08 34.4795ZM15.88 34.4795C15.6933 34.6438 15.6 34.8628 15.6 35.137C15.6 35.4112 15.6933 35.6436 15.88 35.8356C16.0667 36.0276 16.28 36.1233 16.52 36.1233C16.7867 36.1233 17.0133 36.0276 17.2 35.8356C17.3867 35.6436 17.48 35.4112 17.48 35.137C17.48 34.8628 17.3867 34.6438 17.2 34.4795C17.0133 34.2875 16.7867 34.1918 16.52 34.1918C16.2533 34.1918 16.04 34.2875 15.88 34.4795Z\" fill=\"#D9D9D9\"/>"
    "</svg>",
};

static void loadIcons() {
    if (g_iconsLoaded) return;
    g_iconsLoaded = true;
    for (int i = 0; i < kIconCount; ++i)
        g_iconGeom[i] = svg::loadPath(kSvgData[i], g_d2dFactory);
}

static void freeIcons() {
    for (int i = 0; i < kIconCount; ++i) {
        if (g_iconGeom[i]) { g_iconGeom[i]->Release(); g_iconGeom[i] = nullptr; }
    }
    g_iconsLoaded = false;
}

// ═════════════════════════════════════════════════════════════
// Live metrics
// ═════════════════════════════════════════════════════════════
struct Metrics {
    float cpuUsage, cpuFreq;
    float gpuUsage, gpuCoreClock, gpuMemTotal;
    float netRx, netTx;
    float ramUsage, ramUsed, ramTotal;
    float diskRead, diskWrite;
    float batteryPct;
};
static Metrics g_m;
static bool    g_mValid = false;

// ═════════════════════════════════════════════════════════════
// Chart ring buffer (forward-declared before updateMetrics)
// ═════════════════════════════════════════════════════════════
static constexpr int kChartSamples = 284;
static float g_chartBuf[kChartSamples] = {};
static float g_chartBuf2[kChartSamples] = {};
static int   g_chartIdx   = 0;
static int   g_chartCount = 0;
static bool  g_chartMode  = false;
static int   g_chartMetric = -1;

static void chartPush() {
    float v1 = 0, v2 = 0;
    bool dual = false;
    switch (g_chartMetric) {
        case 0: v1 = g_m.cpuUsage;                              break;
        case 1: v1 = g_m.gpuUsage;                              break;
        case 2: v1 = g_m.netRx;  v2 = g_m.netTx;  dual = true; break;
        case 3: v1 = g_m.ramUsage;                              break;
        case 4: v1 = g_m.diskRead; v2 = g_m.diskWrite; dual = true; break;
        default: return;
    }
    g_chartBuf[g_chartIdx] = v1;
    g_chartBuf2[g_chartIdx] = v2;
    g_chartIdx = (g_chartIdx + 1) % kChartSamples;
    if (g_chartCount < kChartSamples) ++g_chartCount;
    (void)dual;
}

void updateMetrics() {
    auto s = DaemonCore::getSnapshot();
    g_m.cpuUsage     = s.cpu.usagePercent;
    g_m.cpuFreq      = s.cpu.freqCurrentMHz;
    g_m.gpuUsage     = s.gpu.usagePercent;
    g_m.gpuCoreClock = s.gpu.coreClockMHz;
    g_m.gpuMemTotal  = s.gpu.memTotalMB;
    g_m.netRx        = s.net.rxBytesPerSec;
    g_m.netTx        = s.net.txBytesPerSec;
    g_m.ramUsage     = s.ram.usagePercent;
    g_m.ramUsed      = s.ram.usedGB;
    g_m.ramTotal     = s.ram.totalGB;
    g_m.diskRead     = s.disk.readBytesPerSec;
    g_m.diskWrite    = s.disk.writeBytesPerSec;
    g_m.batteryPct   = s.pwr.batteryPercent;
    g_mValid         = true;

    if (g_chartMode) chartPush();
}

// ═════════════════════════════════════════════════════════════
// Text formatting
// ═════════════════════════════════════════════════════════════
static std::wstring fmtPct(float v) {
    if (v < 0) return L"--";
    wchar_t b[16];
    _snwprintf_s(b, 16, L"%.0f%%", (double)v);
    return b;
}
static std::wstring fmtFreq(float mhz) {
    if (mhz <= 0) return L"--";
    wchar_t b[16];
    if (mhz >= 1000) _snwprintf_s(b, 16, L"%.1fG", (double)(mhz / 1000.0));
    else             _snwprintf_s(b, 16, L"%.0fM", (double)mhz);
    return b;
}
static std::wstring fmtBw(float bps) {
    if (bps <= 0) return L"0B";
    wchar_t b[16];
    if      (bps >= 1e9f) _snwprintf_s(b, 16, L"%.1fG", (double)(bps / 1e9f));
    else if (bps >= 1e6f) _snwprintf_s(b, 16, L"%.0fM", (double)(bps / 1e6f));
    else if (bps >= 1e3f) _snwprintf_s(b, 16, L"%.0fK", (double)(bps / 1e3f));
    else                  _snwprintf_s(b, 16, L"%.0fB", (double)bps);
    return b;
}
static std::wstring fmtRamUsed(float usedGB) {
    if (usedGB <= 0) return L"--";
    wchar_t b[16];
    _snwprintf_s(b, 16, L"%.1fG", (double)usedGB);
    return b;
}

// ═════════════════════════════════════════════════════════════
// Window & animation state
// ═════════════════════════════════════════════════════════════
static HINSTANCE  g_hInstance = nullptr;
static HWND       g_hwnd      = nullptr;
static const char* kClassName = "DAEMONITOR_DISPLAY_OVERLAY";
static int  g_baseX     = 0;
static bool g_onLeft    = false;
static bool g_autoHide  = true;

static constexpr int   kTimerId       = 1;
static constexpr int   kTimerMs       = 16;
static constexpr float kLerpSpeed     = 0.22f;
static constexpr int   kTriggerZoneY  = 8;
static constexpr int   kLeaveZoneY    = 80;

static float g_curY    = -(float)kHeight;
static float g_targetY = -(float)kHeight;

static void recalcPosition() {
    int screenW = GetSystemMetrics(SM_CXSCREEN);
    g_baseX = g_onLeft ? 0 : (screenW - kWidth);
    if (g_hwnd) {
        SetWindowPos(g_hwnd, nullptr, g_baseX, (int)g_curY, 0, 0,
                     SWP_NOZORDER | SWP_NOSIZE | SWP_NOACTIVATE);
    }
}

// ═════════════════════════════════════════════════════════════
// Slot text + icon for one slot (used in both normal & chart mode)
// ═════════════════════════════════════════════════════════════
static void drawSlot(int i, ID2D1SolidColorBrush* brIcon,
                     ID2D1SolidColorBrush* brMain, ID2D1SolidColorBrush* brSub,
                     const std::wstring& upper, const std::wstring& lower,
                     IDWriteTextFormat* fmtMain, IDWriteTextFormat* fmtSub) {
    static constexpr float slotX0 = 55.0f;
    static constexpr float slotW  = 76.0f;
    static constexpr float splitL = 20.0f;
    static constexpr float txtPadL = 10.0f;
    static constexpr float txtPadT = 20.0f;
    static constexpr float txtGap  = 4.0f;

    int iconIdx = (int)g_slotOrder[i];
    ID2D1PathGeometry* ico = g_iconGeom[iconIdx];
    if (ico && brIcon) {
        float ix;
        if (g_onLeft)
            ix = (kWidth - slotX0) - slotW * (i + 1) + (slotW - splitL);
        else
            ix = slotX0 + slotW * i;
        D2D1::Matrix3x2F t = D2D1::Matrix3x2F::Translation(ix, 0);
        g_dcRt->SetTransform(t);
        g_dcRt->FillGeometry(ico, brIcon);
        g_dcRt->SetTransform(D2D1::Matrix3x2F::Identity());
    }

    float slotX, rx;
    if (g_onLeft) {
        slotX = (kWidth - slotX0) - slotW * (i + 1);
        rx = slotX + txtPadL;
    } else {
        slotX = slotX0 + slotW * i;
        rx = slotX + splitL + txtPadL;
    }
    float ryTop = txtPadT;
    float ryBot = txtPadT + 10.0f + txtGap;
    // Left anchor: rect right edge = iconLeft - txtPadL, TRAILING snaps with 10px gap
    float rw = g_onLeft ? (slotW - splitL - txtPadL * 2) : 56.0f;

    g_dcRt->DrawText(upper.c_str(), (UINT32)upper.size(),
        fmtSub, D2D1::RectF(rx, ryTop, rx + rw, ryTop + 12), brSub,
        D2D1_DRAW_TEXT_OPTIONS_NONE, DWRITE_MEASURING_MODE_NATURAL);
    g_dcRt->DrawText(lower.c_str(), (UINT32)lower.size(),
        fmtMain, D2D1::RectF(rx, ryBot, rx + rw, ryBot + 16), brMain,
        D2D1_DRAW_TEXT_OPTIONS_NONE, DWRITE_MEASURING_MODE_NATURAL);
}

// ═════════════════════════════════════════════════════════════
// Paint — full frame render
// ═════════════════════════════════════════════════════════════
static void paint() {
    if (!g_hwnd || !g_dcRt) return;
    loadIcons();
    loadFont();

    RECT rc; GetClientRect(g_hwnd, &rc);
    int w = rc.right - rc.left;
    int h = rc.bottom - rc.top;
    if (w <= 0 || h <= 0) return;

    BITMAPINFO bi{};
    bi.bmiHeader.biSize        = sizeof(BITMAPINFOHEADER);
    bi.bmiHeader.biWidth       = w;
    bi.bmiHeader.biHeight      = -h;
    bi.bmiHeader.biPlanes      = 1;
    bi.bmiHeader.biBitCount    = 32;
    bi.bmiHeader.biCompression = BI_RGB;

    HDC screenDC = GetDC(nullptr);
    HDC memDC    = CreateCompatibleDC(screenDC);
    void* bits   = nullptr;
    HBITMAP dib  = CreateDIBSection(memDC, &bi, DIB_RGB_COLORS, &bits, nullptr, 0);
    HGDIOBJ old  = SelectObject(memDC, dib);

    RECT bindRc{0, 0, w, h};
    g_dcRt->BindDC(memDC, &bindRc);
    g_dcRt->BeginDraw();
    g_dcRt->Clear(D2D1::ColorF(0, 0, 0, 0));
    g_dcRt->SetAntialiasMode(D2D1_ANTIALIAS_MODE_PER_PRIMITIVE);

    // ── SVG background shape ───────────────────────────
    {
        ID2D1PathGeometry* bgGeom = nullptr;
        g_d2dFactory->CreatePathGeometry(&bgGeom);
        if (bgGeom) {
            ID2D1GeometrySink* gs = nullptr;
            bgGeom->Open(&gs);
            if (gs) {
                gs->SetFillMode(D2D1_FILL_MODE_WINDING);
                gs->BeginFigure(D2D1::Point2F(435, 60), D2D1_FIGURE_BEGIN_FILLED);
                gs->AddLine(D2D1::Point2F(66.2314f, 60));
                D2D1_BEZIER_SEGMENT bez1{};
                bez1.point1 = {55.5614f, 60};
                bez1.point2 = {45.6937f, 54.3327f};
                bez1.point3 = {40.3174f, 45.1162f};
                gs->AddBezier(bez1);
                gs->AddLine(D2D1::Point2F(22.6826f, 14.8838f));
                D2D1_BEZIER_SEGMENT bez2{};
                bez2.point1 = {17.8538f, 6.60589f};
                bez2.point2 = {9.40211f, 1.19088f};
                bez2.point3 = {0, 0.173828f};
                gs->AddBezier(bez2);
                gs->AddLine(D2D1::Point2F(0, 0));
                gs->AddLine(D2D1::Point2F(435, 0));
                gs->EndFigure(D2D1_FIGURE_END_CLOSED);
                gs->Close(); gs->Release();

                ID2D1SolidColorBrush* brBlack = nullptr;
                g_dcRt->CreateSolidColorBrush(D2D1::ColorF(0, 0, 0, 1), &brBlack);
                if (g_onLeft) {
                    D2D1::Matrix3x2F flip = D2D1::Matrix3x2F::Scale(
                        -1.0f, 1.0f, D2D1::Point2F((float)kWidth / 2, 0));
                    g_dcRt->SetTransform(flip);
                }
                g_dcRt->FillGeometry(bgGeom, brBlack);
                g_dcRt->SetTransform(D2D1::Matrix3x2F::Identity());
                brBlack->Release();
                bgGeom->Release();
            }
        }
    }

    // ── Icons + Text ────────────────────────────────────
    if (g_mValid && g_txtMain && g_txtSub) {
        ID2D1SolidColorBrush *brIcon=nullptr,*brMain=nullptr,*brSub=nullptr;
        g_dcRt->CreateSolidColorBrush(
            D2D1::ColorF(217.0f/255,217.0f/255,217.0f/255,1), &brIcon);
        g_dcRt->CreateSolidColorBrush(
            D2D1::ColorF(0xE8/255.0f,0x70/255.0f,0x40/255.0f,1), &brMain);
        g_dcRt->CreateSolidColorBrush(
            D2D1::ColorF(0x6C/255.0f,0x6C/255.0f,0x6C/255.0f,1), &brSub);

        IDWriteTextFormat* fmtMainA = g_onLeft ? g_txtMainR : g_txtMain;
        IDWriteTextFormat* fmtSubA  = g_onLeft ? g_txtSubR  : g_txtSub;

        using S = std::wstring;
        S upper[5] = {
            fmtFreq(g_m.cpuFreq),
            g_m.gpuCoreClock>0?fmtFreq(g_m.gpuCoreClock):fmtRamUsed(g_m.gpuMemTotal/1024),
            L"\xF062" + fmtBw(g_m.netTx),
            fmtRamUsed(g_m.ramUsed), L"W"+fmtBw(g_m.diskWrite),
        };
        S lower[5] = {
            fmtPct(g_m.cpuUsage), fmtPct(g_m.gpuUsage),
            L"\xF063" + fmtBw(g_m.netRx),
            fmtPct(g_m.ramUsage), L"R"+fmtBw(g_m.diskRead),
        };

        if (g_chartMode) {
            // Chart mode: slot at battery-circle position, icon+text from selected metric
            int keepPos = 0;  // slot nearest battery circle (both anchors)
            int metIdx  = (g_chartMetric >= 0 && g_chartMetric <= 4) ? g_chartMetric : 0;
            static constexpr float slotX0L = 55.0f;
            static constexpr float slotWL  = 76.0f;
            static constexpr float splitLL = 20.0f;
            static constexpr float txtPadLL = 10.0f;
            static constexpr float txtPadTL = 20.0f;
            static constexpr float txtGapL = 4.0f;

            // Icon at keepPos position, metricSlot icon
            int iconIdx = (int)g_slotOrder[metIdx];
            ID2D1PathGeometry* ico = g_iconGeom[iconIdx];
            if (ico && brIcon) {
                float ix = g_onLeft ? (kWidth - slotX0L) - slotWL * (keepPos + 1) + (slotWL - splitLL)
                                    : slotX0L + slotWL * keepPos;
                D2D1::Matrix3x2F t = D2D1::Matrix3x2F::Translation(ix, 0);
                g_dcRt->SetTransform(t);
                g_dcRt->FillGeometry(ico, brIcon);
                g_dcRt->SetTransform(D2D1::Matrix3x2F::Identity());
            }

            float slotX, rx;
            if (g_onLeft) {
                slotX = (kWidth - slotX0L) - slotWL * (keepPos + 1);
                rx = slotX + txtPadLL;
            } else {
                slotX = slotX0L + slotWL * keepPos;
                rx = slotX + splitLL + txtPadLL;
            }
            float ryTop = txtPadTL;
            float ryBot = txtPadTL + 10.0f + txtGapL;
            float rw = g_onLeft ? (slotWL - splitLL - txtPadLL * 2) : 56.0f;

            g_dcRt->DrawText(upper[metIdx].c_str(), (UINT32)upper[metIdx].size(),
                fmtSubA, D2D1::RectF(rx, ryTop, rx + rw, ryTop + 12), brSub,
                D2D1_DRAW_TEXT_OPTIONS_NONE, DWRITE_MEASURING_MODE_NATURAL);
            g_dcRt->DrawText(lower[metIdx].c_str(), (UINT32)lower[metIdx].size(),
                fmtMainA, D2D1::RectF(rx, ryBot, rx + rw, ryBot + 16), brMain,
                D2D1_DRAW_TEXT_OPTIONS_NONE, DWRITE_MEASURING_MODE_NATURAL);
        } else {
            for (int i = 0; i < 5; ++i)
                drawSlot(i, brIcon, brMain, brSub,
                         upper[i], lower[i], fmtMainA, fmtSubA);
        }

        if (brIcon) brIcon->Release();
        if (brMain) brMain->Release();
        if (brSub)  brSub->Release();
    }

    // ── Chart line ──────────────────────────────────────
    if (g_chartMode && g_chartCount > 1) {
        // Chart bounding box
        // Right anchor: chart starts at slotX0+slotW = 55+76 = 131, width = 304
        // Left anchor:  chart ends at kWidth-slotX0-slotW = 435-55-76 = 304, start = 0
        static constexpr float slotX0 = 55.0f;
        static constexpr float slotW  = 76.0f;
        static constexpr float chPad  = 10.0f;
        float chY = 10.0f;
        float chH = 40.0f;
        float chX0, chW = slotW * 4;  // 304

        if (g_onLeft)
            chX0 = 0.0f;                         // left edge of window
        else
            chX0 = slotX0 + slotW;               // 131

        float chInnerX0 = chX0 + chPad;
        float chInnerW  = chW  - chPad * 2;      // 284

        // Find max value (scan both buffers for dual-metric charts)
        bool dual = (g_chartMetric == 2 || g_chartMetric == 4);
        float floorV;
        switch (g_chartMetric) {
            case 0: case 1: case 3: floorV = 100.0f; break;
            case 2: case 4: floorV = 1.0f;    break;
            default:        floorV = 1.0f;    break;
        }
        float maxV = floorV;
        for (int j = 0; j < g_chartCount; ++j) {
            float sv = g_chartBuf[(g_chartIdx - g_chartCount + j + kChartSamples) % kChartSamples];
            if (sv > maxV) maxV = sv;
            if (dual) {
                float sv2 = g_chartBuf2[(g_chartIdx - g_chartCount + j + kChartSamples) % kChartSamples];
                if (sv2 > maxV) maxV = sv2;
            }
        }

        // Clip chart drawing to 40px box — stroke/dot overshoot trimmed, data still scaled
        g_dcRt->PushAxisAlignedClip(
            D2D1::RectF(chX0, chY, chX0 + chW, chY + chH),
            D2D1_ANTIALIAS_MODE_PER_PRIMITIVE);

        // Helper: build geometry from buffer, ratio clamped [0,1] → never leaves box
        auto drawLine = [&](const float* buf, const D2D1::ColorF& color, float width) {
            ID2D1SolidColorBrush* br = nullptr;
            g_dcRt->CreateSolidColorBrush(color, &br);
            if (!br) return;
            ID2D1PathGeometry* cg = nullptr;
            g_d2dFactory->CreatePathGeometry(&cg);
            if (cg) {
                ID2D1GeometrySink* gs = nullptr;
                cg->Open(&gs);
                if (gs) {
                    gs->SetFillMode(D2D1_FILL_MODE_WINDING);
                    for (int j = 0; j < g_chartCount; ++j) {
                        float sv = buf[(g_chartIdx - g_chartCount + j + kChartSamples) % kChartSamples];
                        if (sv < 0) sv = 0;
                        float r = std::min(sv / maxV, 1.0f);
                        float nx = chInnerX0 + (float)j / (float)(kChartSamples-1) * chInnerW;
                        float ny = chY + chH - r * chH;
                        if (j == 0) gs->BeginFigure(D2D1::Point2F(nx,ny), D2D1_FIGURE_BEGIN_HOLLOW);
                        else        gs->AddLine(D2D1::Point2F(nx,ny));
                    }
                    gs->EndFigure(D2D1_FIGURE_END_OPEN);
                    gs->Close(); gs->Release();
                    g_dcRt->DrawGeometry(cg, br, width);
                    cg->Release();
                }
            }
            br->Release();
        };

        auto drawDot = [&](const float* buf, const D2D1::ColorF& fill) {
            int lastPos = g_chartCount - 1;
            float lastVal = buf[(g_chartIdx - 1 + kChartSamples) % kChartSamples];
            if (lastVal < 0) lastVal = 0;
            float r = std::min(lastVal / maxV, 1.0f);
            float lx = chInnerX0 + (float)lastPos / (float)(kChartSamples-1) * chInnerW;
            float ly = chY + chH - r * chH;
            D2D1_ELLIPSE dot{ {lx, ly}, 1.5f, 1.5f };
            ID2D1SolidColorBrush* brB = nullptr;
            g_dcRt->CreateSolidColorBrush(D2D1::ColorF(0,0,0,1), &brB);
            if (brB) { g_dcRt->DrawEllipse(dot, brB, 1.0f); brB->Release(); }
            ID2D1SolidColorBrush* brD = nullptr;
            g_dcRt->CreateSolidColorBrush(fill, &brD);
            if (brD) { g_dcRt->FillEllipse(dot, brD); brD->Release(); }
        };

        // Line 2 first (dark, underneath)
        if (dual) {
            drawLine(g_chartBuf2, D2D1::ColorF(0x0A/255.0f,0x26/255.0f,0x25/255.0f,1), 1.5f);
            drawDot(g_chartBuf2, D2D1::ColorF(0x2E/255.0f,0x16/255.0f,0x0D/255.0f,1));
        }
        // Line 1 on top (primary)
        drawLine(g_chartBuf, D2D1::ColorF(0x33/255.0f,0xBC/255.0f,0xB7/255.0f,1), 1.5f);
        drawDot(g_chartBuf, D2D1::ColorF(0xE8/255.0f,0x70/255.0f,0x40/255.0f,1));

        g_dcRt->PopAxisAlignedClip();
    }

    // ── Battery circle ─────────────────────────────────
    {
        float b = g_m.batteryPct;
        if (b < 0) b = 100.0f;
        float t = b / 100.0f;
        float cr = (0xE8 + (0x33 - 0xE8) * t) / 255.0f;
        float cg = (0x70 + (0xBC - 0x70) * t) / 255.0f;
        float cb = (0x40 + (0xB7 - 0x40) * t) / 255.0f;
        ID2D1SolidColorBrush* brBat = nullptr;
        g_dcRt->CreateSolidColorBrush(D2D1::ColorF(cr, cg, cb, 1), &brBat);
        if (brBat) {
            D2D1_ELLIPSE el{};
            float bx = g_onLeft ? (float)(kWidth - 35 - 4) : (35.0f + 4);
            el.point = {bx, 10.0f + 4};
            el.radiusX = 4; el.radiusY = 4;
            g_dcRt->FillEllipse(el, brBat);
            brBat->Release();
        }
    }

    g_dcRt->EndDraw();

    // ── Composite ──────────────────────────────────────
    BLENDFUNCTION blend{};
    blend.BlendOp             = AC_SRC_OVER;
    blend.SourceConstantAlpha = 255;
    blend.AlphaFormat         = AC_SRC_ALPHA;

    POINT ptSrc{0, 0};
    SIZE  szWnd{w, h};
    POINT ptDst{0, 0};
    ClientToScreen(g_hwnd, &ptDst);

    UpdateLayeredWindow(g_hwnd, screenDC, &ptDst, &szWnd,
                        memDC, &ptSrc, 0, &blend, ULW_ALPHA);

    SelectObject(memDC, old);
    DeleteObject(dib);
    DeleteDC(memDC);
    ReleaseDC(nullptr, screenDC);
}

// ═════════════════════════════════════════════════════════════
// Animation tick
// ═════════════════════════════════════════════════════════════
static void animTick() {
    POINT pt; GetCursorPos(&pt);

    bool inZone = (pt.y <= kTriggerZoneY) &&
                  (pt.x >= g_baseX) && (pt.x <= g_baseX + kWidth);

    if (!g_autoHide) {
        g_targetY = 0.0f;
        g_curY += (g_targetY - g_curY) * kLerpSpeed;
        if (std::fabs(g_curY - g_targetY) < 0.5f) g_curY = g_targetY;
        SetWindowPos(g_hwnd, HWND_TOPMOST,
                     g_baseX, (int)g_curY, kWidth, kHeight,
                     SWP_NOACTIVATE | SWP_NOZORDER | SWP_NOSENDCHANGING);
        paint();
        return;
    }

    if (g_targetY >= 0.0f) {
        if (pt.y > kLeaveZoneY || pt.x < g_baseX || pt.x > g_baseX + kWidth)
            g_targetY = -(float)kHeight;
    } else {
        if (inZone) g_targetY = 0.0f;
    }

    float prevY = g_curY;
    g_curY += (g_targetY - g_curY) * kLerpSpeed;
    if (std::fabs(g_curY - g_targetY) < 0.5f) g_curY = g_targetY;

    if (std::fabs(g_curY - prevY) > 0.1f) {
        SetWindowPos(g_hwnd, HWND_TOPMOST,
                     g_baseX, (int)g_curY, kWidth, kHeight,
                     SWP_NOACTIVATE | SWP_NOZORDER | SWP_NOSENDCHANGING);
    }
    if (g_curY > -(float)kHeight + 1.0f) paint();
}

// ═════════════════════════════════════════════════════════════
// Window proc
// ═════════════════════════════════════════════════════════════
static LRESULT CALLBACK wndProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
    switch (msg) {
        case WM_TIMER:  animTick(); return 0;
        case WM_DESTROY: KillTimer(hwnd, kTimerId); PostQuitMessage(0); return 0;
    }
    return DefWindowProcA(hwnd, msg, wp, lp);
}

// ═════════════════════════════════════════════════════════════
// Init
// ═════════════════════════════════════════════════════════════
bool init() {
    HRESULT hr = D2D1CreateFactory(D2D1_FACTORY_TYPE_SINGLE_THREADED, &g_d2dFactory);
    if (FAILED(hr)) return false;
    hr = DWriteCreateFactory(DWRITE_FACTORY_TYPE_SHARED,
        __uuidof(IDWriteFactory), (IUnknown**)&g_dwFactory);
    if (FAILED(hr)) return false;
    D2D1_RENDER_TARGET_PROPERTIES rtProps = D2D1::RenderTargetProperties(
        D2D1_RENDER_TARGET_TYPE_DEFAULT,
        D2D1::PixelFormat(DXGI_FORMAT_B8G8R8A8_UNORM, D2D1_ALPHA_MODE_PREMULTIPLIED),
        0, 0, D2D1_RENDER_TARGET_USAGE_NONE, D2D1_FEATURE_LEVEL_DEFAULT);
    hr = g_d2dFactory->CreateDCRenderTarget(&rtProps, &g_dcRt);
    if (FAILED(hr)) return false;

    g_hInstance = GetModuleHandleA(nullptr);
    WNDCLASSEXA wc{};
    wc.cbSize=sizeof(WNDCLASSEXA); wc.lpfnWndProc=wndProc;
    wc.hInstance=g_hInstance; wc.lpszClassName=kClassName;
    wc.hCursor=LoadCursorA(nullptr,IDC_ARROW);
    wc.hIcon=(HICON)LoadImageA(g_hInstance,MAKEINTRESOURCEA(100),
                IMAGE_ICON,GetSystemMetrics(SM_CXICON),GetSystemMetrics(SM_CYICON),LR_DEFAULTCOLOR);
    wc.hIconSm=(HICON)LoadImageA(g_hInstance,MAKEINTRESOURCEA(100),
                IMAGE_ICON,GetSystemMetrics(SM_CXSMICON),GetSystemMetrics(SM_CYSMICON),LR_DEFAULTCOLOR);
    if (!RegisterClassExA(&wc) && GetLastError()!=ERROR_CLASS_ALREADY_EXISTS) return false;

    recalcPosition();
    g_hwnd = CreateWindowExA(
        WS_EX_TOPMOST|WS_EX_TOOLWINDOW|WS_EX_LAYERED,
        kClassName,"DAEMONITOR",WS_POPUP|WS_VISIBLE,
        g_baseX,-kHeight,kWidth,kHeight,
        nullptr,nullptr,g_hInstance,nullptr);
    if (!g_hwnd) return false;
    ShowWindow(g_hwnd,SW_SHOWNOACTIVATE);
    paint();
    return true;
}

// ═════════════════════════════════════════════════════════════
// Public API
// ═════════════════════════════════════════════════════════════
void startPeekTimer() {
    if (g_hwnd) SetTimer(g_hwnd, kTimerId, kTimerMs, nullptr);
}
void setSlotIconOrder(const SlotIconDef order[5]) {
    for (int i=0;i<5;++i) g_slotOrder[i]=(SlotIcon)order[i];
}
void setPollMs(int ms) { (void)ms; }
void setOnLeft(bool left) {
    if (g_onLeft==left) return;
    g_onLeft=left;
    recalcPosition();
}
void setAutoHide(bool on) {
    g_autoHide=on;
    if (!on) g_targetY=0.0f;
}
void setChartMode(bool on) {
    g_chartMode=on;
    if (!on) {
        // Reset chart buffer when switching back to slot mode
        g_chartIdx=0; g_chartCount=0;
    }
}
void setChartMetric(int idx) {
    g_chartMetric = idx;
    g_chartIdx=0; g_chartCount=0;  // reset buffer on metric change
    g_chartMode = (idx >= 0 && idx <= 4);
}

int  getPollMs()    { return kDefaultPollMs; }
bool getOnLeft()    { return g_onLeft; }
bool getAutoHide()  { return g_autoHide; }
bool getChartMode() { return g_chartMode; }
int  getChartMetric(){ return g_chartMetric; }

void close() {
    if (g_hwnd) {
        KillTimer(g_hwnd,kTimerId);
        DestroyWindow(g_hwnd);
        g_hwnd=nullptr;
    }
    freeIcons();
    freeFont();
    if (g_dcRt)      { g_dcRt->Release();      g_dcRt=nullptr; }
    if (g_dwFactory) { g_dwFactory->Release(); g_dwFactory=nullptr; }
    if (g_d2dFactory){ g_d2dFactory->Release();g_d2dFactory=nullptr; }
}

} // namespace display
