// tray.h — System tray icon with right-click menu (KEYMERA pattern)
// Atomic flags bridge WndProc → main loop.
// Codex §6.4: functions/system/tray-sys/

#pragma once

namespace tray {

enum class Action { None, Exit, SetPollMs, ToggleLeft, ToggleAutoHide, SetChartMetric, ToggleStartup };

bool init();
void close();

void syncState(bool onLeft, bool autoHide, int chartMetric, bool startup);

Action consumeAction();
int   getActionValue();   // payload

} // namespace tray
