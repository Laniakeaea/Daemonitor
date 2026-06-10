// svg-render.h — SVG path parser → ID2D1PathGeometry
// Now supports embedded string data (no file I/O needed).
// Codex §6.4: functions/system/display-sys/

#pragma once
#include <d2d1.h>

namespace svg {

// Parse SVG path data from a C string → ID2D1PathGeometry
// The string must contain a valid SVG <path d="..."/> tag or just the d="..." attribute value.
ID2D1PathGeometry* loadPath(const char* svgContent, ID2D1Factory* factory);

} // namespace svg
