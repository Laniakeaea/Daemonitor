// display.h — Direct2D + DirectWrite overlay (top screen edge)
// Auto-hide with smooth Lerp animation. Supports left/right anchor.
// Chart mode: 1 slot + line chart (KEYMERA pattern).
// Codex §6.4: functions/system/display-sys/

#pragma once
#include <Windows.h>

namespace display {

enum class SlotIconDef : int { CPU = 0, GPU = 1, NET = 2, RAM = 3, SSD = 4 };

bool init();
void startPeekTimer();
void close();

void setSlotIconOrder(const SlotIconDef order[5]);
void updateMetrics();

// ── Settings (exposed for tray / registry) ────────────
void setPollMs(int ms);
void setOnLeft(bool left);
void setAutoHide(bool on);
void setChartMode(bool on);           // true = chart, false = 5-slot
void setChartMetric(int idx);         // 0..4 or -1 for off

int  getPollMs();
bool getOnLeft();
bool getAutoHide();
bool getChartMode();
int  getChartMetric();

static constexpr int kWidth  = 435;
static constexpr int kHeight = 60;
static constexpr int kDefaultPollMs = 1000;

} // namespace display
