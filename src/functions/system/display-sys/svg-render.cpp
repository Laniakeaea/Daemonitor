// svg-render.cpp — SVG path parser → ID2D1PathGeometry
// Zero-dependency C-style implementation (no std::string to avoid GCC16 link bug).
// Codex §6.4: functions/system/display-sys/

#include "functions/system/display-sys/svg-render.h"
#define WIN32_LEAN_AND_MEAN
#include <Windows.h>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <cctype>

namespace svg {

// ── Scan for substring (C strings) ────────────────────
static const char* strnstr(const char* hay, const char* ndl, size_t hn) {
    size_t nn = strlen(ndl);
    if (nn == 0) return hay;
    for (size_t i = 0; i + nn <= hn; ++i)
        if (memcmp(hay + i, ndl, nn) == 0) return hay + i;
    return nullptr;
}

// Extract attribute value after name=" → before closing "
// On success, *valOut points into the source buffer and *len = value length.
static bool extractAttr(const char* buf, size_t n, const char* attrName,
                        const char** valOut, size_t* len) {
    char key[64];
    int kl = _snprintf_s(key, sizeof(key), _TRUNCATE, "%s=\"", attrName);
    if (kl <= 0) return false;
    const char* pos = strnstr(buf, key, n);
    if (!pos) return false;
    pos += kl;
    const char* end = (const char*)memchr(pos, '"', n - (pos - buf));
    if (!end) return false;
    *valOut = pos;
    *len    = end - pos;
    return true;
}

// Whitespace/comma skip
static void skipSep(const char* d, size_t& p, size_t n) {
    while (p < n) {
        char ch = d[p];
        if (ch == ' ' || ch == '\t' || ch == '\n' || ch == '\r' || ch == ',') { ++p; continue; }
        break;
    }
}

static bool readNum(const char* d, size_t& p, size_t n, float& v) {
    skipSep(d, p, n);
    if (p >= n) return false;
    char* end = nullptr;
    v = strtof(d + p, &end);
    if (end == d + p) return false;
    p = (size_t)(end - d);
    return true;
}

// ── D2D Geometry builder (struct) ──────────────────────
struct PathBuilder {
    ID2D1PathGeometry* geom = nullptr;
    ID2D1GeometrySink* sink = nullptr;
    bool closed = false;
    float cx = 0, cy = 0;

    bool begin(ID2D1Factory* f) {
        if (FAILED(f->CreatePathGeometry(&geom))) return false;
        if (FAILED(geom->Open(&sink))) { geom->Release(); geom = nullptr; return false; }
        sink->SetSegmentFlags(D2D1_PATH_SEGMENT_FORCE_ROUND_LINE_JOIN);
        return true;
    }
    void startFigure(float x, float y) {
        if (closed) { sink->SetSegmentFlags(D2D1_PATH_SEGMENT_FORCE_ROUND_LINE_JOIN); closed = false; }
        sink->BeginFigure({x, y}, D2D1_FIGURE_BEGIN_FILLED);
        cx = x; cy = y;
    }
    void addLine(float x, float y)  { sink->AddLine({x, y}); cx = x; cy = y; }
    void addBez(float x1,float y1,float x2,float y2,float x3,float y3) {
        D2D1_BEZIER_SEGMENT seg{};
        seg.point1={x1,y1}; seg.point2={x2,y2}; seg.point3={x3,y3};
        sink->AddBezier(seg); cx=x3; cy=y3;
    }
    void closeFigure() { sink->EndFigure(D2D1_FIGURE_END_CLOSED); closed=true; }
    ID2D1PathGeometry* finish() {
        if (!geom) return nullptr;
        if (!closed) sink->EndFigure(D2D1_FIGURE_END_OPEN);
        sink->Close(); sink->Release(); sink=nullptr;
        return geom;
    }
};

// ── Parse path d-string from C buffer → geometry sink ─
static void parseD(PathBuilder& pb, const char* d, size_t n) {
    size_t pos = 0;
    char cmd = 0;
    while (pos < n) {
        skipSep(d, pos, n);
        if (pos >= n) break;
        if (isalpha((unsigned char)d[pos])) {
            cmd = d[pos++];
            if (cmd == 'Z' || cmd == 'z') pb.closeFigure();
            continue;
        }
        if (cmd == 0) { ++pos; continue; }
        float x, y, x1, y1, x2, y2, x3, y3;
        switch (cmd) {
            case 'M': case 'm':
                if (!readNum(d,pos,n,x)||!readNum(d,pos,n,y)) break;
                if (cmd=='m') { x+=pb.cx; y+=pb.cy; }
                pb.startFigure(x, y);
                for (;;) {
                    size_t probe=pos; skipSep(d,probe,n);
                    if (probe>=n||isalpha((unsigned char)d[probe])) break;
                    float lx,ly;
                    if (!readNum(d,pos,n,lx)||!readNum(d,pos,n,ly)) break;
                    if (cmd=='m') { lx+=pb.cx; ly+=pb.cy; }
                    pb.addLine(lx,ly);
                }
                break;
            case 'L': case 'l':
                if (!readNum(d,pos,n,x)||!readNum(d,pos,n,y)) break;
                if (cmd=='l') { x+=pb.cx; y+=pb.cy; }
                pb.addLine(x,y);
                break;
            case 'H': case 'h':
                if (!readNum(d,pos,n,x)) break;
                if (cmd=='h') x+=pb.cx;
                pb.addLine(x, pb.cy);
                break;
            case 'V': case 'v':
                if (!readNum(d,pos,n,y)) break;
                if (cmd=='v') y+=pb.cy;
                pb.addLine(pb.cx, y);
                break;
            case 'C': case 'c':
                if (!readNum(d,pos,n,x1)||!readNum(d,pos,n,y1)||
                    !readNum(d,pos,n,x2)||!readNum(d,pos,n,y2)||
                    !readNum(d,pos,n,x3)||!readNum(d,pos,n,y3)) break;
                if (cmd=='c') {
                    x1+=pb.cx; y1+=pb.cy; x2+=pb.cx; y2+=pb.cy; x3+=pb.cx; y3+=pb.cy;
                }
                pb.addBez(x1,y1,x2,y2,x3,y3);
                break;
            default: ++pos; break;
        }
    }
}

// ═════════════════════════════════════════════════════════════
// Public: parse SVG string (C-string, null-terminated) → geometry
// ═════════════════════════════════════════════════════════════
ID2D1PathGeometry* loadPath(const char* content, ID2D1Factory* factory) {
    if (!content) return nullptr;
    size_t n = strlen(content);
    if (n == 0) return nullptr;

    const char* pathTag = strnstr(content, "<path", n);
    if (!pathTag) return nullptr;
    const char* tagEnd  = (const char*)memchr(pathTag, '>', n - (pathTag - content));
    if (!tagEnd) return nullptr;
    size_t tagLen = tagEnd - pathTag + 1;

    const char* dVal = nullptr;
    size_t dLen = 0;
    // Extract d="..." from the <path> tag (use a temp buffer)
    char* tagBuf = (char*)malloc(tagLen + 1);
    if (!tagBuf) return nullptr;
    memcpy(tagBuf, pathTag, tagLen);
    tagBuf[tagLen] = 0;
    bool ok = extractAttr(tagBuf, tagLen, "d", &dVal, &dLen);
    if (!ok) { free(tagBuf); return nullptr; }

    PathBuilder pb;
    if (!pb.begin(factory)) { free(tagBuf); return nullptr; }
    parseD(pb, dVal, dLen);
    free(tagBuf);
    return pb.finish();
}

} // namespace svg
