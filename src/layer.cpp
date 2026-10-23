// Thin Place VR - OpenXR API layer.
//
// Loads into any OpenXR game and adds flat "windows" on top of the game image,
// filled with Virtual Desktop's chroma-key colour. Virtual Desktop swaps that colour
// for the Quest 3's passthrough cameras, so the real wheel, hands and Stream Deck
// show through. The wheel window turns with the Simucube's steering angle.

#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <d3d11.h>
#include <d3d12.h>

#define XR_USE_PLATFORM_WIN32
#define XR_USE_GRAPHICS_API_D3D11
#define XR_USE_GRAPHICS_API_D3D12
#include <openxr/openxr.h>
#include <openxr/openxr_platform.h>
#include <openxr/openxr_loader_negotiation.h>

#include <algorithm>
#include <initializer_list>
#include <cmath>
#include <cstring>
#include <string>
#include <vector>

#include "config.h"
#include "hotkeys.h"
#include "log.h"
#include "mathutil.h"
#include "steering.h"
#include "shared.h"

#include <nlohmann/json.hpp>
#include <filesystem>
#include <fstream>
#include <sstream>

#define HV_VERSION "0.10.0"
#define HV_LAYER_NAME "XR_APILAYER_SYMYOULATER_thin_place_vr"

namespace hv {

namespace {

// ---------------------------------------------------------------------------
// Runtime function pointers (the next layer / the runtime)
// ---------------------------------------------------------------------------
struct Next {
    PFN_xrGetInstanceProcAddr GetInstanceProcAddr = nullptr;
    PFN_xrDestroyInstance DestroyInstance = nullptr;
    PFN_xrCreateSession CreateSession = nullptr;
    PFN_xrDestroySession DestroySession = nullptr;
    PFN_xrEndFrame EndFrame = nullptr;
    PFN_xrEnumerateReferenceSpaces EnumerateReferenceSpaces = nullptr;
    PFN_xrCreateReferenceSpace CreateReferenceSpace = nullptr;
    PFN_xrDestroySpace DestroySpace = nullptr;
    PFN_xrLocateSpace LocateSpace = nullptr;
    PFN_xrEnumerateSwapchainFormats EnumerateSwapchainFormats = nullptr;
    PFN_xrCreateSwapchain CreateSwapchain = nullptr;
    PFN_xrDestroySwapchain DestroySwapchain = nullptr;
    PFN_xrEnumerateSwapchainImages EnumerateSwapchainImages = nullptr;
    PFN_xrAcquireSwapchainImage AcquireSwapchainImage = nullptr;
    PFN_xrWaitSwapchainImage WaitSwapchainImage = nullptr;
    PFN_xrReleaseSwapchainImage ReleaseSwapchainImage = nullptr;
} next;

enum class Gfx { None, D3D11, D3D12 };

// One flat window in the room.
struct Window {
    std::string label;
    Shape shape;                 // outline in mm, centred on the rotation point
    float halfWmm = 0, halfHmm = 0;
    int texW = 0, texH = 0;
    XrSwapchain swapchain = XR_NULL_HANDLE;
    std::vector<ID3D11Texture2D*> d3d11Images;
    std::vector<ID3D12Resource*> d3d12Images;
    std::vector<uint8_t> pixels; // RGBA or BGRA depending on swapchain format
    float marginMm = 0.0f;       // extra see-through space around the outline
    bool highlighted = false;
    bool needsUpload = true;
    bool failed = false;
    // Panels: a picture shared by the desktop app instead of a passthrough cut-out.
    bool isPanel = false;
    std::string sourceId;
    HANDLE srcMap = nullptr;
    const uint8_t* srcView = nullptr;
    uint32_t srcSeq = 0;
    DWORD srcNextTry = 0;
    bool showingPlaceholder = false;
};

struct State {
    XrInstance instance = XR_NULL_HANDLE;
    XrSession session = XR_NULL_HANDLE;
    Gfx gfx = Gfx::None;
    ID3D11Device* d3d11Device = nullptr;
    ID3D12Device* d3d12Device = nullptr;
    ID3D12CommandQueue* d3d12Queue = nullptr;
    ID3D12CommandAllocator* d3d12Alloc = nullptr;
    ID3D12GraphicsCommandList* d3d12List = nullptr;
    ID3D12Fence* d3d12Fence = nullptr;
    UINT64 d3d12FenceValue = 0;
    HANDLE d3d12Event = nullptr;
    ID3D12Resource* d3d12Upload = nullptr;
    UINT64 d3d12UploadSize = 0;

    XrSpace baseSpace = XR_NULL_HANDLE;
    XrSpace viewSpace = XR_NULL_HANDLE;
    int64_t format = 0;
    bool bgra = false;

    Config cfg;
    long long cfgStamp = 0;
    DWORD lastStampCheck = 0;
    DWORD saveDue = 0;
    bool structureDirty = true; // windows need rebuilding from config

    Window wheel;
    std::vector<Window> rects;

    // Help board: a card of controls floating in the headset (Ctrl+Alt+K, and while in edit mode).
    Window board;
    bool boardManual = false;   // switched on with Ctrl+Alt+K
    bool boardHiddenInEdit = false; // switched off with Ctrl+Alt+K during this edit
    bool boardShowing = false;
    Pose boardPose;
    std::wstring boardKey;      // what the board currently shows; redrawn when it changes
    DWORD boardDrawnAt = 0;
    bool boardBring = false;    // B: move the panel back in front of you
    int selected = 0; // 0 = wheel, 1.. = rect windows
    DWORD highlightUntil = 0;
    bool loggedFirstFrame = false;
    int framesSinceStart = 0;
    DWORD lastAdjustTick = 0; // last time a window was adjusted from a hotkey or the app
    bool loggedError = false;

    // Live link to the desktop app
    LinkBlock* link = nullptr;
    uint32_t lastCmdSeq = 0;
    std::string gameName, gameExe, runtimeName;
    bool gameRecorded = false;
} g;

// ---------------------------------------------------------------------------
// Helpers
// ---------------------------------------------------------------------------
WheelProfile* ActiveWheel() {
    for (auto& w : g.cfg.wheels)
        if (w.id == g.cfg.activeWheel) return &w;
    if (!g.cfg.wheels.empty()) {
        g.cfg.activeWheel = g.cfg.wheels[0].id;
        return &g.cfg.wheels[0];
    }
    return nullptr;
}

Pose* SelectedPose() {
    if (g.selected == 0) {
        WheelProfile* w = ActiveWheel();
        return w ? &w->pose : nullptr;
    }
    size_t i = static_cast<size_t>(g.selected - 1);
    return i < g.cfg.windows.size() ? &g.cfg.windows[i].pose : nullptr;
}

const char* SelectedName() {
    if (g.selected == 0) {
        WheelProfile* w = ActiveWheel();
        return w ? w->name.c_str() : "wheel";
    }
    size_t i = static_cast<size_t>(g.selected - 1);
    return i < g.cfg.windows.size() ? g.cfg.windows[i].name.c_str() : "window";
}

void DestroyWindowResources(Window& w) {
    if (w.swapchain != XR_NULL_HANDLE && next.DestroySwapchain) next.DestroySwapchain(w.swapchain);
    w.swapchain = XR_NULL_HANDLE;
    w.d3d11Images.clear(); // owned by the runtime
    w.d3d12Images.clear();
    w.needsUpload = true;
}

bool PointInPolygon(const std::vector<std::pair<float, float>>& p, float x, float y) {
    bool inside = false;
    for (size_t i = 0, j = p.size() - 1; i < p.size(); j = i++) {
        float xi = p[i].first, yi = p[i].second, xj = p[j].first, yj = p[j].second;
        if (((yi > y) != (yj > y)) && (x < (xj - xi) * (y - yi) / (yj - yi + 1e-12f) + xi)) inside = !inside;
    }
    return inside;
}

float DistanceToEdges(const std::vector<std::pair<float, float>>& p, float x, float y) {
    float best = 1e9f;
    for (size_t i = 0, j = p.size() - 1; i < p.size(); j = i++) {
        float ax = p[j].first, ay = p[j].second, bx = p[i].first, by = p[i].second;
        float dx = bx - ax, dy = by - ay;
        float len2 = dx * dx + dy * dy;
        float t = len2 > 0 ? ((x - ax) * dx + (y - ay) * dy) / len2 : 0;
        t = std::clamp(t, 0.0f, 1.0f);
        float cx = ax + t * dx - x, cy = ay + t * dy - y;
        best = std::min(best, cx * cx + cy * cy);
    }
    return std::sqrt(best);
}

// Draw the window's shape into its pixel buffer: key colour inside, clear outside,
// and a yellow outline while it is selected for adjusting.
void Rasterise(Window& w) {
    const float border = 3.0f; // mm, room for the highlight outline
    float maxX = 0, maxY = 0;
    for (auto& p : w.shape.points) {
        maxX = std::max(maxX, std::fabs(p.first));
        maxY = std::max(maxY, std::fabs(p.second));
    }
    const float margin = std::max(0.0f, w.marginMm);
    w.halfWmm = maxX + margin + border;
    w.halfHmm = maxY + margin + border;

    const float pxPerMm = std::min(2.0f, 1024.0f / (2.0f * std::max(w.halfWmm, w.halfHmm)));
    int texW = std::max(16, (int)std::ceil(2.0f * w.halfWmm * pxPerMm));
    int texH = std::max(16, (int)std::ceil(2.0f * w.halfHmm * pxPerMm));
    if (texW != w.texW || texH != w.texH) {
        DestroyWindowResources(w);
        w.texW = texW;
        w.texH = texH;
    }
    const float mmPerPxX = 2.0f * w.halfWmm / texW, mmPerPxY = 2.0f * w.halfHmm / texH;
    const size_t n = (size_t)texW * texH;

    // 1. Fill the shape a row at a time: find where each row crosses the outline (fast scanline fill).
    std::vector<uint8_t> inside(n, 0);
    std::vector<float> xs;
    const auto& pts = w.shape.points;
    for (int row = 0; row < texH; ++row) {
        float y = w.halfHmm - (row + 0.5f) * mmPerPxY;
        xs.clear();
        for (size_t i = 0, j = pts.size() - 1; i < pts.size(); j = i++) {
            float yi = pts[i].second, yj = pts[j].second;
            if ((yi > y) != (yj > y))
                xs.push_back(pts[i].first + (y - yi) * (pts[j].first - pts[i].first) / (yj - yi));
        }
        std::sort(xs.begin(), xs.end());
        for (size_t k = 0; k + 1 < xs.size(); k += 2) {
            int c0 = std::max(0, (int)std::ceil((xs[k] + w.halfWmm) / mmPerPxX - 0.5f));
            int c1 = std::min(texW - 1, (int)std::floor((xs[k + 1] + w.halfWmm) / mmPerPxX - 0.5f));
            for (int c = c0; c <= c1; ++c) inside[(size_t)row * texW + c] = 1;
        }
    }

    // 2. Distance from every outside pixel to the shape, in two sweeps (a distance transform),
    //    for the hand margin and the selection outline.
    std::vector<float> dist(n, 0.0f);
    if (margin > 0 || w.highlighted) {
        const int kFar = 1 << 14;
        std::vector<int16_t> dx(n), dy(n);
        for (size_t i = 0; i < n; ++i) { dx[i] = inside[i] ? 0 : kFar; dy[i] = inside[i] ? 0 : kFar; }
        auto len2 = [](int a, int b) { return a * a + b * b; };
        auto relax = [&](int x, int y, int ox, int oy) {
            int nx = x + ox, ny = y + oy;
            if (nx < 0 || ny < 0 || nx >= texW || ny >= texH) return;
            size_t i = (size_t)y * texW + x, j = (size_t)ny * texW + nx;
            int vx = dx[j] == kFar ? kFar : dx[j] - ox;
            int vy = dy[j] == kFar ? kFar : dy[j] - oy;
            if (vx == kFar || vy == kFar) return;
            if (len2(vx, vy) < len2(dx[i] == kFar ? kFar : dx[i], dy[i] == kFar ? kFar : dy[i])) { dx[i] = (int16_t)vx; dy[i] = (int16_t)vy; }
        };
        for (int y = 0; y < texH; ++y) {
            for (int x = 0; x < texW; ++x) { relax(x, y, -1, 0); relax(x, y, -1, -1); relax(x, y, 0, -1); relax(x, y, 1, -1); }
            for (int x = texW - 1; x >= 0; --x) relax(x, y, 1, 0);
        }
        for (int y = texH - 1; y >= 0; --y) {
            for (int x = texW - 1; x >= 0; --x) { relax(x, y, 1, 0); relax(x, y, 1, 1); relax(x, y, 0, 1); relax(x, y, -1, 1); }
            for (int x = 0; x < texW; ++x) relax(x, y, -1, 0);
        }
        for (size_t i = 0; i < n; ++i) {
            if (inside[i]) continue;
            if (dx[i] == kFar || dy[i] == kFar) { dist[i] = 1e9f; continue; }
            float ex = dx[i] * mmPerPxX, ey = dy[i] * mmPerPxY;
            dist[i] = std::sqrt(ex * ex + ey * ey);
        }
    }

    // 3. Colour the pixels.
    w.pixels.assign(n * 4, 0);
    const uint8_t kr = (uint8_t)g.cfg.keyColour[0], kg = (uint8_t)g.cfg.keyColour[1], kb = (uint8_t)g.cfg.keyColour[2];
    for (size_t i = 0; i < n; ++i) {
        uint8_t r = 0, gg = 0, b = 0, a = 0;
        bool in = inside[i] || dist[i] <= margin;
        if (w.highlighted && !inside[i] && std::fabs(dist[i] - margin) < 2.5f) {
            r = 255; gg = 220; b = 0; a = 255; // selection outline, at the window's outer edge
        } else if (in) {
            r = kr; gg = kg; b = kb; a = 255;
        }
        uint8_t* px = &w.pixels[i * 4];
        if (g.bgra) { px[0] = b; px[1] = gg; px[2] = r; }
        else { px[0] = r; px[1] = gg; px[2] = b; }
        px[3] = a;
    }
    w.needsUpload = true;
}

// The outline of a non-wheel window, in mm around its centre, from its shape setting.
Shape WindowShape(const RectWindow& rw) {
    Shape sh;
    const float hx = rw.widthMm * 0.5f, hy = rw.heightMm * 0.5f;
    const float pi = 3.14159265f;
    if (rw.shape == "ellipse") {
        for (int i = 0; i < 72; ++i) sh.points.emplace_back(hx * std::cos(i * 2 * pi / 72), hy * std::sin(i * 2 * pi / 72));
    } else if (rw.shape == "rounded") {
        float r = std::min(hx, hy) * 0.35f;
        const float cx[4] = {hx - r, -hx + r, -hx + r, hx - r}, cy[4] = {hy - r, hy - r, -hy + r, -hy + r};
        for (int c = 0; c < 4; ++c)
            for (int i = 0; i <= 8; ++i) {
                float a = (c * 90 + i * 90.0f / 8) * pi / 180;
                sh.points.emplace_back(cx[c] + r * std::cos(a), cy[c] + r * std::sin(a));
            }
    } else if (rw.shape == "custom" && !rw.shapeFile.empty()) {
        // A traced outline, stretched to the window's width and height.
        Shape src = LoadShape(rw.shapeFile);
        if (src.valid()) {
            float minX = 1e9f, maxX = -1e9f, minY = 1e9f, maxY = -1e9f;
            for (auto& p : src.points) {
                minX = std::min(minX, p.first); maxX = std::max(maxX, p.first);
                minY = std::min(minY, p.second); maxY = std::max(maxY, p.second);
            }
            float sx = rw.widthMm / std::max(1.0f, maxX - minX), sy = rw.heightMm / std::max(1.0f, maxY - minY);
            float mx = (minX + maxX) / 2, my = (minY + maxY) / 2;
            for (auto& p : src.points) sh.points.emplace_back((p.first - mx) * sx, (p.second - my) * sy);
        }
    }
    if (!sh.valid()) sh.points = {{-hx, -hy}, {hx, -hy}, {hx, hy}, {-hx, hy}};
    return sh;
}

// (Re)build the windows from the current config.
// ---------------------------------------------------------------------------
// Panels: pictures shared by the desktop app (a desktop window, web page, PDF or camera).
// The app writes frames into shared memory "Local\ThinPlaceVR.Src.<id>":
//   uint32 magic 'TPVS', uint32 seq (odd while writing), uint32 width, uint32 height, 16 bytes spare,
//   then width*height BGRA pixels.
// ---------------------------------------------------------------------------
constexpr uint32_t kSrcMagic = 0x53565054; // "TPVS"
constexpr size_t kSrcHeader = 32;
constexpr uint32_t kSrcMaxW = 1920, kSrcMaxH = 1200;
constexpr size_t kSrcBytes = kSrcHeader + (size_t)kSrcMaxW * kSrcMaxH * 4;

void CloseSource(Window& w) {
    if (w.srcView) UnmapViewOfFile(w.srcView);
    if (w.srcMap) CloseHandle(w.srcMap);
    w.srcView = nullptr;
    w.srcMap = nullptr;
    w.srcSeq = 0;
}

// A dark card with a line of text, shown until the app sends a picture.
void RenderPlaceholder(Window& w, const std::wstring& line1, const std::wstring& line2) {
    const int W = 512, H = std::max(64, (int)(512.0f * w.halfHmm / std::max(1.0f, w.halfWmm)));
    BITMAPINFO bi{};
    bi.bmiHeader.biSize = sizeof(bi.bmiHeader);
    bi.bmiHeader.biWidth = W;
    bi.bmiHeader.biHeight = -H;
    bi.bmiHeader.biPlanes = 1;
    bi.bmiHeader.biBitCount = 32;
    void* bits = nullptr;
    HDC dc = CreateCompatibleDC(nullptr);
    HBITMAP bmp = CreateDIBSection(dc, &bi, DIB_RGB_COLORS, &bits, nullptr, 0);
    if (!bmp || !bits) { if (bmp) DeleteObject(bmp); DeleteDC(dc); return; }
    HGDIOBJ old = SelectObject(dc, bmp);
    RECT all{0, 0, W, H};
    HBRUSH bg = CreateSolidBrush(w.highlighted ? RGB(36, 32, 48) : RGB(21, 23, 28));
    FillRect(dc, &all, bg);
    DeleteObject(bg);
    SetBkMode(dc, TRANSPARENT);
    HFONT f1 = CreateFontW(-26, 0, 0, 0, FW_SEMIBOLD, 0, 0, 0, DEFAULT_CHARSET, 0, 0, CLEARTYPE_QUALITY, 0, L"Segoe UI");
    HFONT f2 = CreateFontW(-19, 0, 0, 0, FW_NORMAL, 0, 0, 0, DEFAULT_CHARSET, 0, 0, CLEARTYPE_QUALITY, 0, L"Segoe UI");
    SelectObject(dc, f1);
    SetTextColor(dc, RGB(236, 238, 242));
    RECT r{24, H / 2 - 40, W - 24, H / 2};
    DrawTextW(dc, line1.c_str(), -1, &r, DT_CENTER | DT_SINGLELINE | DT_END_ELLIPSIS | DT_NOPREFIX);
    SelectObject(dc, f2);
    SetTextColor(dc, RGB(169, 175, 186));
    r = {24, H / 2 + 4, W - 24, H / 2 + 60};
    DrawTextW(dc, line2.c_str(), -1, &r, DT_CENTER | DT_WORDBREAK | DT_NOPREFIX);
    GdiFlush();
    if (W != w.texW || H != w.texH) {
        DestroyWindowResources(w);
        w.texW = W;
        w.texH = H;
    }
    w.pixels.assign((size_t)W * H * 4, 0);
    const uint8_t* src = static_cast<const uint8_t*>(bits);
    for (size_t i = 0; i < (size_t)W * H; ++i) {
        uint8_t* d = &w.pixels[i * 4];
        const uint8_t* sp = src + i * 4;
        if (g.bgra) { d[0] = sp[0]; d[1] = sp[1]; d[2] = sp[2]; }
        else { d[0] = sp[2]; d[1] = sp[1]; d[2] = sp[0]; }
        d[3] = 235;
    }
    SelectObject(dc, old);
    DeleteObject(f1);
    DeleteObject(f2);
    DeleteObject(bmp);
    DeleteDC(dc);
    w.needsUpload = true;
    w.showingPlaceholder = true;
}

// Picks up a new frame from the app, if there is one. Cheap when nothing has changed.
void UpdatePanel(Window& w, const RectWindow& rw) {
    w.halfWmm = rw.widthMm * 0.5f;
    w.halfHmm = rw.heightMm * 0.5f;
    if (!w.srcView) {
        DWORD now = GetTickCount();
        if (now >= w.srcNextTry) {
            w.srcNextTry = now + 2000;
            std::wstring name = L"Local\\ThinPlaceVR.Src." + Widen(w.sourceId);
            w.srcMap = OpenFileMappingW(FILE_MAP_READ, FALSE, name.c_str());
            if (w.srcMap) {
                w.srcView = static_cast<const uint8_t*>(MapViewOfFile(w.srcMap, FILE_MAP_READ, 0, 0, kSrcBytes));
                if (!w.srcView) { CloseHandle(w.srcMap); w.srcMap = nullptr; }
                else Log("Panel %s connected to the app", rw.name.c_str());
            }
        }
        if (!w.srcView) {
            if (!w.showingPlaceholder || w.texW == 0)
                RenderPlaceholder(w, Widen(rw.name), L"Waiting for the Thin Place VR app - open it on the PC to show this panel");
            return;
        }
    }
    if (w.texW == 0) RenderPlaceholder(w, Widen(rw.name), L"Starting...");
    const volatile uint32_t* hdr = reinterpret_cast<const volatile uint32_t*>(w.srcView);
    if (hdr[0] != kSrcMagic) return;
    uint32_t seq = hdr[1];
    if (seq == w.srcSeq || (seq & 1)) return;
    uint32_t iw = hdr[2], ih = hdr[3];
    if (iw == 0 || ih == 0 || iw > kSrcMaxW || ih > kSrcMaxH) return;
    if ((int)iw != w.texW || (int)ih != w.texH) {
        DestroyWindowResources(w);
        w.texW = (int)iw;
        w.texH = (int)ih;
    }
    w.pixels.resize((size_t)iw * ih * 4);
    const uint8_t* src = w.srcView + kSrcHeader;
    if (g.bgra) {
        memcpy(w.pixels.data(), src, w.pixels.size());
    } else {
        for (size_t i = 0, n = (size_t)iw * ih; i < n; ++i) {
            w.pixels[i * 4] = src[i * 4 + 2];
            w.pixels[i * 4 + 1] = src[i * 4 + 1];
            w.pixels[i * 4 + 2] = src[i * 4];
        }
    }
    MemoryBarrier();
    if (hdr[1] != seq) return; // the app wrote a new frame while we copied: take the next one
    for (size_t i = 0, n = (size_t)iw * ih; i < n; ++i) w.pixels[i * 4 + 3] = 255;
    if (w.highlighted) {
        // Selection outline: a lilac frame round the edge.
        const int t = std::max(3, (int)iw / 160);
        const uint8_t c[3] = {183, 148, 246};
        for (uint32_t y = 0; y < ih; ++y)
            for (uint32_t x = 0; x < iw; ++x)
                if ((int)x < t || (int)y < t || x >= iw - t || y >= ih - t) {
                    uint8_t* d = &w.pixels[((size_t)y * iw + x) * 4];
                    if (g.bgra) { d[0] = c[2]; d[1] = c[1]; d[2] = c[0]; }
                    else { d[0] = c[0]; d[1] = c[1]; d[2] = c[2]; }
                }
    }
    w.srcSeq = seq;
    w.showingPlaceholder = false;
    w.needsUpload = true;
}

// Redraw a window after its size, shape or selection changed.
void RefreshRect(size_t i) {
    if (i >= g.rects.size() || i >= g.cfg.windows.size()) return;
    Window& w = g.rects[i];
    if (w.isPanel) {
        w.srcSeq = 0;              // copy the current frame again (with or without the outline)
        w.showingPlaceholder = false;
        return;
    }
    w.shape = WindowShape(g.cfg.windows[i]);
    Rasterise(w);
}

void RebuildWindows() {
    WheelProfile* wp = ActiveWheel();
    DestroyWindowResources(g.wheel);
    g.wheel = Window{};
    if (wp) {
        g.wheel.label = wp->name;
        g.wheel.shape = LoadShape(wp->shapeFile);
        float sc = std::clamp(wp->scale, 0.5f, 2.0f);
        for (auto& p : g.wheel.shape.points) {
            p.first = (p.first - wp->hubOffsetMm[0]) * sc;
            p.second = (p.second - wp->hubOffsetMm[1]) * sc;
        }
        g.wheel.marginMm = g.cfg.wheelMarginMm;
    }
    for (auto& r : g.rects) { DestroyWindowResources(r); CloseSource(r); }
    g.rects.clear();
    for (auto& rw : g.cfg.windows) {
        Window w;
        w.label = rw.name;
        if (!rw.sourceId.empty()) {
            w.isPanel = true;
            w.sourceId = rw.sourceId;
            w.halfWmm = rw.widthMm * 0.5f;
            w.halfHmm = rw.heightMm * 0.5f;
        } else {
            w.shape = WindowShape(rw);
        }
        g.rects.push_back(std::move(w));
    }
    if (g.selected > (int)g.rects.size()) g.selected = 0;
    g.wheel.highlighted = (g.selected == 0) && GetTickCount() < g.highlightUntil;
    if (g.wheel.shape.valid()) Rasterise(g.wheel);
    for (size_t i = 0; i < g.rects.size(); ++i) {
        g.rects[i].highlighted = (g.selected == (int)i + 1) && GetTickCount() < g.highlightUntil;
        if (!g.rects[i].isPanel) Rasterise(g.rects[i]);
    }
    g.structureDirty = false;
}

// ---------------------------------------------------------------------------
// GPU upload
// ---------------------------------------------------------------------------
bool CreateSwapchain(Window& w) {
    XrSwapchainCreateInfo ci{XR_TYPE_SWAPCHAIN_CREATE_INFO};
    ci.usageFlags = XR_SWAPCHAIN_USAGE_SAMPLED_BIT | XR_SWAPCHAIN_USAGE_COLOR_ATTACHMENT_BIT |
                    XR_SWAPCHAIN_USAGE_TRANSFER_DST_BIT;
    ci.format = g.format;
    ci.sampleCount = 1;
    ci.width = (uint32_t)w.texW;
    ci.height = (uint32_t)w.texH;
    ci.faceCount = 1;
    ci.arraySize = 1;
    ci.mipCount = 1;
    XrResult r = next.CreateSwapchain(g.session, &ci, &w.swapchain);
    if (XR_FAILED(r)) {
        Log("Could not create window texture for %s (%d)", w.label.c_str(), (int)r);
        w.failed = true;
        return false;
    }
    uint32_t count = 0;
    next.EnumerateSwapchainImages(w.swapchain, 0, &count, nullptr);
    if (g.gfx == Gfx::D3D11) {
        std::vector<XrSwapchainImageD3D11KHR> imgs(count, {XR_TYPE_SWAPCHAIN_IMAGE_D3D11_KHR});
        next.EnumerateSwapchainImages(w.swapchain, count, &count,
                                      reinterpret_cast<XrSwapchainImageBaseHeader*>(imgs.data()));
        for (auto& i : imgs) w.d3d11Images.push_back(i.texture);
    } else {
        std::vector<XrSwapchainImageD3D12KHR> imgs(count, {XR_TYPE_SWAPCHAIN_IMAGE_D3D12_KHR});
        next.EnumerateSwapchainImages(w.swapchain, count, &count,
                                      reinterpret_cast<XrSwapchainImageBaseHeader*>(imgs.data()));
        for (auto& i : imgs) w.d3d12Images.push_back(i.texture);
    }
    Log("Window %s: %dx%d texture, %u images", w.label.c_str(), w.texW, w.texH, count);
    return true;
}

bool EnsureD3D12Objects() {
    if (g.d3d12List) return true;
    if (FAILED(g.d3d12Device->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT, __uuidof(ID3D12CommandAllocator),
                                                     reinterpret_cast<void**>(&g.d3d12Alloc))))
        return false;
    if (FAILED(g.d3d12Device->CreateCommandList(0, D3D12_COMMAND_LIST_TYPE_DIRECT, g.d3d12Alloc, nullptr,
                                                __uuidof(ID3D12GraphicsCommandList),
                                                reinterpret_cast<void**>(&g.d3d12List))))
        return false;
    g.d3d12List->Close();
    if (FAILED(g.d3d12Device->CreateFence(0, D3D12_FENCE_FLAG_NONE, __uuidof(ID3D12Fence),
                                          reinterpret_cast<void**>(&g.d3d12Fence))))
        return false;
    g.d3d12Event = CreateEventW(nullptr, FALSE, FALSE, nullptr);
    return true;
}

bool UploadD3D12(Window& w, ID3D12Resource* image) {
    if (!EnsureD3D12Objects()) return false;
    D3D12_RESOURCE_DESC desc = image->GetDesc();
    D3D12_PLACED_SUBRESOURCE_FOOTPRINT fp{};
    UINT rows = 0;
    UINT64 rowSize = 0, total = 0;
    g.d3d12Device->GetCopyableFootprints(&desc, 0, 1, 0, &fp, &rows, &rowSize, &total);

    if (!g.d3d12Upload || g.d3d12UploadSize < total) {
        if (g.d3d12Upload) g.d3d12Upload->Release();
        g.d3d12Upload = nullptr;
        D3D12_HEAP_PROPERTIES hp{D3D12_HEAP_TYPE_UPLOAD};
        D3D12_RESOURCE_DESC bd{};
        bd.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
        bd.Width = total;
        bd.Height = 1;
        bd.DepthOrArraySize = 1;
        bd.MipLevels = 1;
        bd.SampleDesc.Count = 1;
        bd.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
        if (FAILED(g.d3d12Device->CreateCommittedResource(&hp, D3D12_HEAP_FLAG_NONE, &bd,
                                                          D3D12_RESOURCE_STATE_GENERIC_READ, nullptr,
                                                          __uuidof(ID3D12Resource),
                                                          reinterpret_cast<void**>(&g.d3d12Upload))))
            return false;
        g.d3d12UploadSize = total;
    }

    uint8_t* mapped = nullptr;
    D3D12_RANGE none{0, 0};
    if (FAILED(g.d3d12Upload->Map(0, &none, reinterpret_cast<void**>(&mapped)))) return false;
    for (int row = 0; row < w.texH && row < (int)rows; ++row)
        memcpy(mapped + fp.Offset + (size_t)row * fp.Footprint.RowPitch, &w.pixels[(size_t)row * w.texW * 4],
               (size_t)w.texW * 4);
    g.d3d12Upload->Unmap(0, nullptr);

    g.d3d12Alloc->Reset();
    g.d3d12List->Reset(g.d3d12Alloc, nullptr);

    D3D12_RESOURCE_BARRIER b{};
    b.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
    b.Transition.pResource = image;
    b.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
    b.Transition.StateBefore = D3D12_RESOURCE_STATE_RENDER_TARGET;
    b.Transition.StateAfter = D3D12_RESOURCE_STATE_COPY_DEST;
    g.d3d12List->ResourceBarrier(1, &b);

    D3D12_TEXTURE_COPY_LOCATION dst{};
    dst.pResource = image;
    dst.Type = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
    dst.SubresourceIndex = 0;
    D3D12_TEXTURE_COPY_LOCATION src{};
    src.pResource = g.d3d12Upload;
    src.Type = D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT;
    src.PlacedFootprint = fp;
    g.d3d12List->CopyTextureRegion(&dst, 0, 0, 0, &src, nullptr);

    b.Transition.StateBefore = D3D12_RESOURCE_STATE_COPY_DEST;
    b.Transition.StateAfter = D3D12_RESOURCE_STATE_RENDER_TARGET;
    g.d3d12List->ResourceBarrier(1, &b);
    g.d3d12List->Close();

    ID3D12CommandList* lists[] = {g.d3d12List};
    g.d3d12Queue->ExecuteCommandLists(1, lists);
    ++g.d3d12FenceValue;
    g.d3d12Queue->Signal(g.d3d12Fence, g.d3d12FenceValue);
    if (g.d3d12Fence->GetCompletedValue() < g.d3d12FenceValue) {
        g.d3d12Fence->SetEventOnCompletion(g.d3d12FenceValue, g.d3d12Event);
        WaitForSingleObject(g.d3d12Event, 1000);
    }
    return true;
}

// Push the pixel buffer into every image of the swapchain so whichever one the runtime
// shows is up to date. Only happens when a window changes, never every frame.
bool Upload(Window& w) {
    if (w.failed) return false;
    if (w.swapchain == XR_NULL_HANDLE && !CreateSwapchain(w)) return false;
    size_t images = g.gfx == Gfx::D3D11 ? w.d3d11Images.size() : w.d3d12Images.size();
    if (w.isPanel) images = std::min<size_t>(images, 1); // changes often: the newest image is the one shown
    for (size_t n = 0; n < images; ++n) {
        uint32_t index = 0;
        XrSwapchainImageAcquireInfo ai{XR_TYPE_SWAPCHAIN_IMAGE_ACQUIRE_INFO};
        if (XR_FAILED(next.AcquireSwapchainImage(w.swapchain, &ai, &index))) return false;
        XrSwapchainImageWaitInfo wi{XR_TYPE_SWAPCHAIN_IMAGE_WAIT_INFO};
        wi.timeout = XR_INFINITE_DURATION;
        if (XR_FAILED(next.WaitSwapchainImage(w.swapchain, &wi))) return false;

        if (g.gfx == Gfx::D3D11) {
            ID3D11DeviceContext* ctx = nullptr;
            g.d3d11Device->GetImmediateContext(&ctx);
            ctx->UpdateSubresource(w.d3d11Images[index], 0, nullptr, w.pixels.data(), (UINT)w.texW * 4, 0);
            ctx->Release();
        } else {
            UploadD3D12(w, w.d3d12Images[index]);
        }

        XrSwapchainImageReleaseInfo ri{XR_TYPE_SWAPCHAIN_IMAGE_RELEASE_INFO};
        next.ReleaseSwapchainImage(w.swapchain, &ri);
    }
    w.needsUpload = false;
    return true;
}

// ---------------------------------------------------------------------------
// Placement and hotkeys
// ---------------------------------------------------------------------------
void MarkChanged() { g.saveDue = GetTickCount() + 1500; }

void Highlight() {
    // In edit mode the outline stays until edit mode ends; otherwise it shows for 4 seconds.
    g.highlightUntil = InEditMode() ? 0xFFFFFFFF : GetTickCount() + 4000;
    bool wantWheel = g.selected == 0;
    if (g.wheel.highlighted != wantWheel) { g.wheel.highlighted = wantWheel; if (g.wheel.shape.valid()) Rasterise(g.wheel); }
    for (size_t i = 0; i < g.rects.size(); ++i) {
        bool want = g.selected == (int)i + 1;
        if (g.rects[i].highlighted != want) { g.rects[i].highlighted = want; RefreshRect(i); }
    }
}

void ClearHighlightIfExpired() {
    if (!g.highlightUntil || GetTickCount() < g.highlightUntil) return;
    g.highlightUntil = 0;
    if (g.wheel.highlighted) { g.wheel.highlighted = false; if (g.wheel.shape.valid()) Rasterise(g.wheel); }
    for (size_t i = 0; i < g.rects.size(); ++i)
        if (g.rects[i].highlighted) { g.rects[i].highlighted = false; RefreshRect(i); }
}

void MoveLocal(Pose& p, float dx, float dy, float dz) {
    XrQuaternionf q = QuatFromYawPitchRoll(p.yaw, p.pitch, p.roll);
    XrVector3f d = QuatRotate(q, {dx, dy, dz});
    p.position[0] += d.x;
    p.position[1] += d.y;
    p.position[2] += d.z;
}

bool PlaceSelectedInFront(XrTime displayTime) {
    Pose* p = SelectedPose();
    if (!p) return false;
    XrSpaceLocation loc{XR_TYPE_SPACE_LOCATION};
    if (XR_FAILED(next.LocateSpace(g.viewSpace, g.baseSpace, displayTime, &loc)) ||
        !(loc.locationFlags & XR_SPACE_LOCATION_POSITION_VALID_BIT) ||
        !(loc.locationFlags & XR_SPACE_LOCATION_ORIENTATION_VALID_BIT))
        return false;
    // People look down at a wheel mostly with their eyes, so aim a little below where the head points.
    XrQuaternionf aim = QuatMul(loc.pose.orientation, QuatAxisAngle(1, 0, 0, Deg2Rad(-g.cfg.placeAimDownDeg)));
    XrVector3f fwd = QuatRotate(aim, {0, 0, -g.cfg.placeDistanceM});
    p->position = {loc.pose.position.x + fwd.x, loc.pose.position.y + fwd.y, loc.pose.position.z + fwd.z};
    ForwardYawPitch(aim, p->yaw, p->pitch);
    p->roll = 0;
    if (g.selected == 0) g.cfg.wheelPlaced = true;
    Log("Placed %s in front of you", SelectedName());
    Highlight();
    MarkChanged();
    return true;
}

// True while someone is adjusting windows: keyboard edit mode, or a hotkey / app button in the
// last 20 seconds. "Only while adjusting" windows (like a keyboard window) show during this.
bool Adjusting() {
    return InEditMode() || (g.lastAdjustTick && GetTickCount() - g.lastAdjustTick < 20000);
}

// One adjustment, from a hotkey or from the desktop app.
void DoAction(Action action, bool fine, XrTime displayTime) {
    g.lastAdjustTick = GetTickCount();
    float step = (fine ? 1.0f : g.cfg.nudgeMm) / 1000.0f;
    float deg = fine ? 0.25f : g.cfg.nudgeDeg;
    {
        Pose* p = SelectedPose();
        switch (action) {
        case Action::EditModeOn:
            g.boardHiddenInEdit = false;
            Log("Edit mode on - adjusting %s", SelectedName());
            Highlight();
            break;
        case Action::EditModeOff:
            Log("Edit mode off");
            g.highlightUntil = GetTickCount(); // outline clears on the next frame
            break;
        case Action::ToggleBoard:
            if (g.boardShowing) {
                g.boardManual = false;
                g.boardHiddenInEdit = InEditMode(); // keep it hidden for the rest of this edit
            } else {
                g.boardManual = true;
                g.boardHiddenInEdit = false;
            }
            Log("Help board %s", g.boardShowing ? "off" : "on");
            break;
        case Action::ToggleAll:
            g.cfg.enabled = !g.cfg.enabled;
            Log("Windows %s", g.cfg.enabled ? "on" : "off");
            MarkChanged();
            break;
        case Action::SelectNext:
            g.selected = (g.selected + 1) % (int)(g.rects.size() + 1);
            Log("Selected: %s", SelectedName());
            Highlight();
            break;
        case Action::NextWheel: {
            if (g.cfg.wheels.empty()) break;
            size_t idx = 0;
            for (size_t k = 0; k < g.cfg.wheels.size(); ++k)
                if (g.cfg.wheels[k].id == g.cfg.activeWheel) idx = k;
            g.cfg.activeWheel = g.cfg.wheels[(idx + 1) % g.cfg.wheels.size()].id;
            Log("Wheel: %s", ActiveWheel()->name.c_str());
            g.selected = 0;
            g.highlightUntil = GetTickCount() + 4000;
            RebuildWindows();
            MarkChanged();
            break;
        }
        case Action::CentreSteering: {
            float v = 0;
            if (GetSteeringNormalised(v)) {
                float a = v * g.cfg.steering.rangeDeg * 0.5f;
                g.cfg.steering.centreOffsetDeg = g.cfg.steering.invert ? -a : a;
                Log("Steering centred (offset %.2f deg)", g.cfg.steering.centreOffsetDeg);
                MarkChanged();
            } else {
                Log("Can't centre steering - wheel base not found");
            }
            break;
        }
        case Action::InvertSteering:
            g.cfg.steering.invert = !g.cfg.steering.invert;
            g.cfg.steering.centreOffsetDeg = -g.cfg.steering.centreOffsetDeg;
            Log("Steering direction %s", g.cfg.steering.invert ? "inverted" : "normal");
            MarkChanged();
            break;
        case Action::PlaceInFront:
            PlaceSelectedInFront(displayTime);
            break;
        case Action::PanelUp:
        case Action::PanelDown: {
            if (g.selected == 0 || g.cfg.windows[g.selected - 1].sourceId.empty()) {
                Log("[ and ] scroll panels - select a panel with Tab first");
                break;
            }
            if (g.link) {
                g.link->panelWindow = g.selected - 1;
                g.link->panelCmd = action == Action::PanelUp ? 1 : 2;
                MemoryBarrier();
                g.link->panelSeq = g.link->panelSeq + 1;
            }
            break;
        }
        case Action::BringBoard:
            g.boardBring = true;
            if (!g.boardShowing) { g.boardManual = true; g.boardHiddenInEdit = false; }
            break;
        case Action::ToggleWindow: {
            if (g.selected == 0) { Log("The wheel window can't be switched off here - choose a rim in the app"); break; }
            RectWindow& rw = g.cfg.windows[g.selected - 1];
            rw.enabled = !rw.enabled;
            Log("%s %s", rw.name.c_str(), rw.enabled ? "on" : "off");
            Highlight();
            MarkChanged();
            break;
        }
        case Action::CycleShape: {
            if (g.selected == 0) { Log("The wheel window's shape comes from the rim - press W for the next rim"); break; }
            RectWindow& rw = g.cfg.windows[g.selected - 1];
            if (!rw.sourceId.empty()) { Log("%s is a panel - panels are always rectangles", rw.name.c_str()); break; }
            const char* order[] = {"rect", "rounded", "ellipse", "custom"};
            int n = rw.shapeFile.empty() ? 3 : 4, cur = 0;
            for (int k = 0; k < n; ++k) if (rw.shape == order[k]) cur = k;
            rw.shape = order[(cur + 1) % n];
            RefreshRect(g.selected - 1);
            Log("%s shape: %s", rw.name.c_str(), rw.shape.c_str());
            Highlight();
            MarkChanged();
            break;
        }
        default:
            if (!p) break;
            switch (action) {
            case Action::MoveLeft: MoveLocal(*p, -step, 0, 0); break;
            case Action::MoveRight: MoveLocal(*p, step, 0, 0); break;
            case Action::MoveUp: MoveLocal(*p, 0, step, 0); break;
            case Action::MoveDown: MoveLocal(*p, 0, -step, 0); break;
            case Action::MoveAway: MoveLocal(*p, 0, 0, -step); break;
            case Action::MoveCloser: MoveLocal(*p, 0, 0, step); break;
            case Action::TiltUp: p->pitch += deg; break;
            case Action::TiltDown: p->pitch -= deg; break;
            case Action::TurnLeft: p->yaw += deg; break;
            case Action::TurnRight: p->yaw -= deg; break;
            case Action::RollLeft: p->roll += deg; break;
            case Action::RollRight: p->roll -= deg; break;
            case Action::Bigger:
            case Action::Smaller: {
                if (g.selected == 0) {
                    WheelProfile* wp = ActiveWheel();
                    if (!wp) break;
                    float step = (fine ? 0.005f : 0.02f) * (action == Action::Bigger ? 1.0f : -1.0f);
                    wp->scale = std::clamp(wp->scale + step, 0.5f, 2.0f);
                    Log("Wheel window size %.0f%%", wp->scale * 100.0f);
                    RebuildWindows();
                    break;
                }
                // Resize keeping the window's proportions.
                RectWindow& rw = g.cfg.windows[g.selected - 1];
                float d = (action == Action::Bigger ? 1.0f : -1.0f) * (fine ? 1.0f : g.cfg.nudgeMm);
                float f = std::max(20.0f, rw.widthMm + d) / rw.widthMm;
                if (rw.heightMm * f < 20.0f) f = 20.0f / rw.heightMm;
                rw.widthMm *= f;
                rw.heightMm *= f;
                RefreshRect(g.selected - 1);
                break;
            }
            default: break;
            }
            Highlight();
            MarkChanged();
            break;
        }
    }
}

void HandleHotkeys(XrTime displayTime) {
    Action actions[8];
    bool fine = false;
    int n = PollHotkeys(actions, 8, fine);
    for (int i = 0; i < n; ++i) DoAction(actions[i], fine, displayTime);
}

// ---- help board ---------------------------------------------------------------------------

XrPosef ToXrPose(const Pose& p, float spinDeg);
XrCompositionLayerQuad MakeQuad(const Window& w, const XrPosef& pose);

// Everything the setup panel shows, as text. The panel is redrawn only when this changes.
struct PanelRow {
    std::wstring name, info;
    bool selected = false, dim = false;
};

std::wstring Deg(float d) {
    wchar_t b[32];
    swprintf(b, 32, L"%.0f\x00B0", std::fabs(d) < 0.5f ? 0.0f : d);
    return b;
}

std::vector<PanelRow> PanelRows() {
    std::vector<PanelRow> rows;
    WheelProfile* wp = ActiveWheel();
    PanelRow wr;
    wr.name = L"Wheel";
    if (wp) {
        wchar_t b[96];
        swprintf(b, 96, L"%ls \x00B7 %.0f%%", Widen(wp->name).c_str(), std::clamp(wp->scale, 0.5f, 2.0f) * 100.0f);
        wr.info = b;
    } else {
        wr.info = L"no rim chosen";
        wr.dim = true;
    }
    wr.selected = g.selected == 0;
    rows.push_back(wr);
    for (size_t i = 0; i < g.cfg.windows.size(); ++i) {
        const RectWindow& rw = g.cfg.windows[i];
        PanelRow r;
        r.name = Widen(rw.name);
        if (!rw.enabled) {
            r.info = L"off";
            r.dim = true;
        } else if (!rw.sourceId.empty()) {
            const wchar_t* kind = rw.source == "window" ? L"desktop window" : rw.source == "web" ? L"web page"
                                : rw.source == "pdf" ? L"PDF" : rw.source == "camera" ? L"camera"
                                : rw.source == "data" ? L"game data" : L"panel";
            r.info = std::wstring(kind) + (rw.sourceTarget.empty() ? L"" : L" \x00B7 " + Widen(rw.sourceTarget));
        } else {
            wchar_t b[96];
            swprintf(b, 96, L"%.0f \x00D7 %.0f mm \x00B7 %ls%ls", rw.widthMm, rw.heightMm, Widen(rw.shape).c_str(),
                     rw.onlyWhileAdjusting ? L" \x00B7 editing only" : L"");
            r.info = b;
        }
        r.selected = g.selected == (int)i + 1;
        rows.push_back(r);
    }
    return rows;
}

// One line about the selected window's position: distance from your head and its angles.
std::wstring SelectedDetail(const XrSpaceLocation* head) {
    Pose* p = SelectedPose();
    if (!p) return L"";
    std::wstring t;
    if (head && (head->locationFlags & XR_SPACE_LOCATION_POSITION_VALID_BIT)) {
        float dx = p->position[0] - head->pose.position.x, dy = p->position[1] - head->pose.position.y,
              dz = p->position[2] - head->pose.position.z;
        wchar_t b[48];
        swprintf(b, 48, L"%.0f cm away   ", std::sqrt(dx * dx + dy * dy + dz * dz) * 100.0f);
        t += b;
    }
    t += L"tilt " + Deg(p->pitch) + L"   turn " + Deg(p->yaw) + L"   roll " + Deg(p->roll);
    return t;
}

std::wstring SteeringLine() {
    float v = 0;
    std::string base = GetSteeringDeviceName();
    if (!GetSteeringNormalised(v)) return base.empty() ? L"Wheel base: not found" : L"Wheel base: " + Widen(base) + L" (turn the wheel)";
    float a = v * g.cfg.steering.rangeDeg * 0.5f;
    if (g.cfg.steering.invert) a = -a;
    a -= g.cfg.steering.centreOffsetDeg;
    return L"Wheel base: " + Widen(base) + L"   " + Deg(std::round(a / 2.0f) * 2.0f);
}

// Draws the setup panel with Windows' own text drawing, then copies it into the window's pixels.
void RenderBoard(Window& w, const XrSpaceLocation* head) {
    const int W = 640, H = 812;
    const float mmW = 220.0f, mmH = 279.0f;
    BITMAPINFO bi{};
    bi.bmiHeader.biSize = sizeof(bi.bmiHeader);
    bi.bmiHeader.biWidth = W;
    bi.bmiHeader.biHeight = -H; // top-down
    bi.bmiHeader.biPlanes = 1;
    bi.bmiHeader.biBitCount = 32;
    void* bits = nullptr;
    HDC dc = CreateCompatibleDC(nullptr);
    HBITMAP bmp = CreateDIBSection(dc, &bi, DIB_RGB_COLORS, &bits, nullptr, 0);
    if (!bmp || !bits) { if (bmp) DeleteObject(bmp); DeleteDC(dc); return; }
    HGDIOBJ old = SelectObject(dc, bmp);

    const COLORREF text = RGB(236, 238, 242), text2 = RGB(169, 175, 186), text3 = RGB(120, 127, 139),
                   accent = RGB(183, 148, 246), amber = RGB(240, 200, 90), lineCol = RGB(48, 52, 60);
    RECT all{0, 0, W, H};
    HBRUSH bg = CreateSolidBrush(RGB(21, 23, 28));
    FillRect(dc, &all, bg);
    DeleteObject(bg);
    SetBkMode(dc, TRANSPARENT);
    auto font = [&](int px, int weight, const wchar_t* face) {
        return CreateFontW(-px, 0, 0, 0, weight, 0, 0, 0, DEFAULT_CHARSET, 0, 0, CLEARTYPE_QUALITY, 0, face);
    };
    HFONT title = font(30, FW_SEMIBOLD, L"Segoe UI"), head2 = font(17, FW_SEMIBOLD, L"Segoe UI"),
          body = font(21, FW_NORMAL, L"Segoe UI"), bodyB = font(21, FW_SEMIBOLD, L"Segoe UI"),
          keyf = font(19, FW_SEMIBOLD, L"Consolas"), smallFont = font(17, FW_NORMAL, L"Segoe UI");
    HPEN pen = CreatePen(PS_SOLID, 1, lineCol);
    SelectObject(dc, pen);
    auto rule = [&](int y) { MoveToEx(dc, 28, y, nullptr); LineTo(dc, W - 28, y); };
    auto put = [&](HFONT f, COLORREF c, int x0, int y0, int x1, int y1, const std::wstring& str, UINT fmt) {
        SelectObject(dc, f);
        SetTextColor(dc, c);
        RECT r{x0, y0, x1, y1};
        DrawTextW(dc, str.c_str(), -1, &r, fmt | DT_NOPREFIX);
    };
    const UINT one = DT_LEFT | DT_SINGLELINE | DT_END_ELLIPSIS;

    // Header
    int y = 20;
    put(title, text, 28, y, W - 28, y + 40, L"Thin Place VR", one);
    if (InEditMode()) {
        RECT pill{W - 168, y + 4, W - 28, y + 36};
        HBRUSH pb = CreateSolidBrush(RGB(58, 46, 84));
        HGDIOBJ ob = SelectObject(dc, pb);
        HGDIOBJ op = SelectObject(dc, GetStockObject(NULL_PEN));
        RoundRect(dc, pill.left, pill.top, pill.right, pill.bottom, 16, 16);
        SelectObject(dc, op);
        SelectObject(dc, ob);
        DeleteObject(pb);
        put(head2, accent, pill.left, pill.top + 6, pill.right, pill.bottom, L"EDITING", DT_CENTER | DT_SINGLELINE);
    } else {
        put(head2, text3, W - 220, y + 10, W - 28, y + 36, L"Ctrl+Alt+H to edit", DT_RIGHT | DT_SINGLELINE);
    }
    y += 52;
    if (!g.cfg.enabled) {
        put(body, amber, 28, y, W - 28, y + 28, L"Windows are hidden (Ctrl+Alt+V)", one);
        y += 34;
    }

    // Windows
    put(head2, text3, 28, y, W - 28, y + 22,
        g.cfg.perGameLayouts && !g.gameName.empty() ? L"WINDOWS FOR " + Widen(g.gameName) + L"  \x00B7  Tab selects"
                                                    : std::wstring(L"WINDOWS  \x00B7  Tab selects"), one);
    y += 28;
    for (const PanelRow& r : PanelRows()) {
        if (y > 330) { put(smallFont, text3, 28, y, W - 28, y + 22, L"\x2026 more in the app", one); y += 26; break; }
        if (r.selected) {
            RECT hl{20, y - 3, W - 20, y + 31};
            HBRUSH hb = CreateSolidBrush(RGB(36, 32, 48));
            FillRect(dc, &hl, hb);
            DeleteObject(hb);
            put(bodyB, accent, 28, y, 48, y + 30, L"\x25B6", one);
        }
        put(r.selected ? bodyB : body, r.selected ? text : (r.dim ? text3 : text), 52, y, 250, y + 30, r.name, one);
        put(body, r.dim ? text3 : text2, 256, y, W - 28, y + 30, r.info, one);
        y += 36;
    }
    y += 4;
    put(smallFont, text2, 52, y, W - 28, y + 24, SelectedDetail(head), one);
    y += 34;
    rule(y);
    y += 12;

    // Steering
    put(body, text2, 28, y, W - 28, y + 28, SteeringLine(), one);
    y += 38;
    rule(y);
    y += 14;

    // Keys
    put(head2, text3, 28, y, W - 28, y + 22, InEditMode() ? L"KEYS" : L"KEYS  \x00B7  in edit mode", one);
    y += 30;
    const wchar_t* keys[][2] = {
        {L"P", L"place where you look"},  {L"Tab", L"next window"},
        {L"\x2190\x2191\x2192\x2193", L"move"},       {L"R  F", L"away  closer"},
        {L"T  G", L"tilt"},               {L"Q  E", L"turn"},
        {L"Z  C", L"roll"},               {L"=  \x2212", L"bigger  smaller"},
        {L"O", L"window on / off"},       {L"S", L"change shape"},
        {L"W", L"next rim"},              {L"X", L"centre steering"},
        {L"I", L"flip steering"},         {L"B", L"bring this panel"},
        {L"[  ]", L"scroll a panel"},
    };
    const int colW = (W - 56) / 2;
    for (size_t i = 0; i < sizeof(keys) / sizeof(keys[0]); ++i) {
        int x = 28 + (int)(i % 2) * colW;
        int yy = y + (int)(i / 2) * 32;
        put(keyf, text, x, yy + 1, x + 78, yy + 30, keys[i][0], one);
        put(body, text2, x + 84, yy, x + colW - 8, yy + 30, keys[i][1], one);
    }
    y += 8 * 32 + 6;
    rule(y);
    y += 12;
    put(smallFont, text3, 28, y, W - 28, y + 24, L"Shift = fine steps   \x00B7   Esc = finish   \x00B7   Ctrl+Alt+K = hide", one);

    GdiFlush();
    // Copy out: GDI leaves alpha at 0, so make the panel solid with slightly rounded corners.
    w.texW = W;
    w.texH = H;
    w.halfWmm = mmW / 2;
    w.halfHmm = mmH / 2;
    w.pixels.assign((size_t)W * H * 4, 0);
    const uint8_t* src = static_cast<const uint8_t*>(bits);
    const int rad = 18;
    for (int py = 0; py < H; ++py)
        for (int px = 0; px < W; ++px) {
            int cx = px < rad ? rad - px : px >= W - rad ? px - (W - rad - 1) : 0;
            int cy = py < rad ? rad - py : py >= H - rad ? py - (H - rad - 1) : 0;
            if (cx * cx + cy * cy > rad * rad) continue; // outside a rounded corner: clear
            const uint8_t* sp = src + ((size_t)py * W + px) * 4; // B G R x
            uint8_t* d = &w.pixels[((size_t)py * W + px) * 4];
            if (g.bgra) { d[0] = sp[0]; d[1] = sp[1]; d[2] = sp[2]; }
            else { d[0] = sp[2]; d[1] = sp[1]; d[2] = sp[0]; }
            d[3] = 242;
        }
    w.needsUpload = true;

    SelectObject(dc, old);
    DeleteObject(pen);
    for (HFONT f : {title, head2, body, bodyB, keyf, smallFont}) DeleteObject(f);
    DeleteObject(bmp);
    DeleteDC(dc);
}

// A summary of what the panel shows, to tell when it needs redrawing.
std::wstring PanelKey(const XrSpaceLocation* head) {
    std::wstring k = InEditMode() ? L"E" : L"-";
    k += g.cfg.enabled ? L"1" : L"0";
    for (const PanelRow& r : PanelRows()) k += r.name + L"|" + r.info + (r.selected ? L"*" : L"") + L"\n";
    k += SelectedDetail(head) + L"\n" + SteeringLine();
    return k;
}

// Shows the board in front of you when it's switched on or edit mode starts; it then stays put.
bool BoardFrame(XrTime displayTime, XrCompositionLayerQuad& out) {
    bool want = g.boardManual || (InEditMode() && !g.boardHiddenInEdit);
    if (!want) {
        g.boardShowing = false;
        return false;
    }
    XrSpaceLocation loc{XR_TYPE_SPACE_LOCATION};
    bool haveHead = XR_SUCCEEDED(next.LocateSpace(g.viewSpace, g.baseSpace, displayTime, &loc)) &&
                    (loc.locationFlags & XR_SPACE_LOCATION_POSITION_VALID_BIT);
    if (!g.boardShowing || g.boardBring) {
        if (!haveHead) return false;
        // Half a metre ahead, a little to the left and below eye level, facing you.
        float yaw = 0, pitch = 0;
        ForwardYawPitch(loc.pose.orientation, yaw, pitch);
        XrQuaternionf flat = QuatFromYawPitchRoll(yaw, 0, 0);
        XrVector3f off = QuatRotate(flat, {-0.22f, -0.12f, -0.5f});
        g.boardPose.position = {loc.pose.position.x + off.x, loc.pose.position.y + off.y, loc.pose.position.z + off.z};
        g.boardPose.yaw = yaw + 18.0f;
        g.boardPose.pitch = -12.0f;
        g.boardPose.roll = 0;
        g.boardShowing = true;
        g.boardBring = false;
        g.boardKey.clear();
    }
    // Redraw when anything shown changes, at most ten times a second.
    DWORD now = GetTickCount();
    if (g.board.texW == 0 || now - g.boardDrawnAt >= 100) {
        std::wstring key = PanelKey(haveHead ? &loc : nullptr);
        if (key != g.boardKey || g.board.texW == 0) {
            RenderBoard(g.board, haveHead ? &loc : nullptr);
            g.boardKey = key;
            g.boardDrawnAt = now;
        }
    }
    if (g.board.needsUpload && !Upload(g.board)) return false;
    out = MakeQuad(g.board, ToXrPose(g.boardPose, 0.0f));
    return true;
}

// Measures how long Thin Place VR's own work takes each frame and logs it every 30 seconds,
// so any frame-rate cost can be told apart from the game's.
void RecordFrameCost(long long ticks) {
    static LARGE_INTEGER freq = [] { LARGE_INTEGER f; QueryPerformanceFrequency(&f); return f; }();
    static double total = 0, worst = 0;
    static long frames = 0;
    static DWORD since = GetTickCount();
    double ms = 1000.0 * (double)ticks / (double)freq.QuadPart;
    total += ms;
    worst = std::max(worst, ms);
    ++frames;
    DWORD now = GetTickCount();
    if (now - since >= 30000) {
        Log("Frame cost: %.3f ms average, %.2f ms worst, over %ld frames (%.0f fps)", total / frames, worst, frames,
            frames * 1000.0 / (now - since));
        total = worst = 0;
        frames = 0;
        since = now;
    }
}

// Steering angle as the wheel window shows it (after range, flip and centre).
bool SteeringDeg(float& out) {
    float v = 0.0f;
    if (!GetSteeringNormalised(v)) return false;
    float angle = v * g.cfg.steering.rangeDeg * 0.5f;
    if (g.cfg.steering.invert) angle = -angle;
    out = angle - g.cfg.steering.centreOffsetDeg;
    return true;
}

void CopyText(char* dst, size_t size, const std::string& src) {
    size_t n = std::min(size - 1, src.size());
    memcpy(dst, src.data(), n);
    dst[n] = 0;
}

// Publish live status to the app and carry out any command it sent.
void ProcessLink(XrTime displayTime) {
    LinkBlock* l = g.link;
    if (!l) return;
    float deg = 0.0f;
    bool found = SteeringDeg(deg);
    l->steeringDeg = found ? deg : 0.0f;
    l->wheelFound = found ? 1 : 0;
    l->windowsEnabled = g.cfg.enabled ? 1 : 0;
    l->selected = g.selected;
    l->editMode = InEditMode() ? 1 : 0;
    CopyText(l->game, sizeof(l->game), g.gameName);
    CopyText(l->runtime, sizeof(l->runtime), g.runtimeName);
    CopyText(l->wheelBase, sizeof(l->wheelBase), GetSteeringDeviceName());
    l->heartbeatMs = GetTickCount64();

    uint32_t seq = l->cmdSeq;
    if (seq == g.lastCmdSeq) return;
    g.lastCmdSeq = seq;
    MemoryBarrier();
    int target = l->cmdTarget;
    Action action = static_cast<Action>(l->cmdAction);
    bool fine = l->cmdFine != 0;
    if (target >= 0 && target <= (int)g.rects.size()) g.selected = target;
    if (action == Action::Select) {
        g.lastAdjustTick = GetTickCount();
        Log("App selected %s", SelectedName());
        Highlight();
        return;
    }
    Log("App command %d on %s", (int)action, SelectedName());
    DoAction(action, fine, displayTime);
}

// Remember which games have used Thin Place VR, for the app's Home page.
void RecordGame() {
    try {
        std::wstring path = LogDir() + L"\\games.json";
        nlohmann::json j = nlohmann::json::object();
        {
            std::ifstream f{std::filesystem::path(path)};
            if (f) {
                std::stringstream ss;
                ss << f.rdbuf();
                j = nlohmann::json::parse(ss.str(), nullptr, false);
                if (!j.is_object()) j = nlohmann::json::object();
            }
        }
        SYSTEMTIME t;
        GetLocalTime(&t);
        char when[32];
        snprintf(when, sizeof(when), "%04d-%02d-%02dT%02d:%02d:%02d", t.wYear, t.wMonth, t.wDay, t.wHour, t.wMinute, t.wSecond);
        j[g.gameExe.empty() ? g.gameName : g.gameExe] = {{"name", g.gameName}, {"last_used", when}, {"working", true}};
        std::ofstream f{std::filesystem::path(path), std::ios::trunc};
        f << j.dump(2);
    } catch (...) {
    }
}

void CheckConfig() {
    DWORD now = GetTickCount();
    if (g.saveDue && (LONG)(now - g.saveDue) >= 0) {
        g.saveDue = 0;
        if (SaveConfig(g.cfg)) {
            g.cfgStamp = ConfigFileStamp();
            Log("Settings saved");
        }
    }
    if (now - g.lastStampCheck < 1000) return;
    g.lastStampCheck = now;
    long long stamp = ConfigFileStamp();
    if (stamp && stamp != g.cfgStamp && !g.saveDue) {
        g.cfgStamp = stamp;
        Config c;
        if (LoadConfig(c)) {
            if (c.steering.deviceMatch != g.cfg.steering.deviceMatch) {
                Log("Wheel base choice changed to \"%s\"", c.steering.deviceMatch.c_str());
                RestartSteeringReader(c.steering.deviceMatch);
            }
            g.cfg = c;
            g.structureDirty = true;
            Log("config.json changed - reloaded");
        }
    }
}

XrPosef ToXrPose(const Pose& p, float spinDeg) {
    XrPosef out;
    XrQuaternionf q = QuatFromYawPitchRoll(p.yaw, p.pitch, p.roll);
    if (spinDeg != 0.0f) q = QuatMul(q, QuatAxisAngle(0, 0, 1, Deg2Rad(spinDeg)));
    out.orientation = q;
    out.position = {p.position[0], p.position[1], p.position[2]};
    return out;
}

XrCompositionLayerQuad MakeQuad(const Window& w, const XrPosef& pose) {
    XrCompositionLayerQuad q{XR_TYPE_COMPOSITION_LAYER_QUAD};
    q.layerFlags = XR_COMPOSITION_LAYER_BLEND_TEXTURE_SOURCE_ALPHA_BIT;
    q.space = g.baseSpace;
    q.eyeVisibility = XR_EYE_VISIBILITY_BOTH;
    q.subImage.swapchain = w.swapchain;
    q.subImage.imageRect = {{0, 0}, {w.texW, w.texH}};
    q.subImage.imageArrayIndex = 0;
    q.pose = pose;
    q.size = {2.0f * w.halfWmm / 1000.0f, 2.0f * w.halfHmm / 1000.0f};
    return q;
}

// ---------------------------------------------------------------------------
// Session setup / teardown
// ---------------------------------------------------------------------------
void SetupSession(XrSession session, const XrSessionCreateInfo* info) {
    g.session = session;
    g.gfx = Gfx::None;
    for (auto* h = reinterpret_cast<const XrBaseInStructure*>(info->next); h; h = h->next) {
        if (h->type == XR_TYPE_GRAPHICS_BINDING_D3D11_KHR) {
            g.gfx = Gfx::D3D11;
            g.d3d11Device = reinterpret_cast<const XrGraphicsBindingD3D11KHR*>(h)->device;
        } else if (h->type == XR_TYPE_GRAPHICS_BINDING_D3D12_KHR) {
            auto* b = reinterpret_cast<const XrGraphicsBindingD3D12KHR*>(h);
            g.gfx = Gfx::D3D12;
            g.d3d12Device = b->device;
            g.d3d12Queue = b->queue;
        }
    }
    if (g.gfx == Gfx::None) {
        Log("Game uses a graphics API other than Direct3D 11/12 - windows disabled for this session");
        return;
    }
    Log("Session started (%s)", g.gfx == Gfx::D3D11 ? "Direct3D 11" : "Direct3D 12");

    // Stage space keeps windows fixed to the room across restarts; fall back to local.
    uint32_t n = 0;
    next.EnumerateReferenceSpaces(session, 0, &n, nullptr);
    std::vector<XrReferenceSpaceType> types(n);
    next.EnumerateReferenceSpaces(session, n, &n, types.data());
    bool hasStage = std::find(types.begin(), types.end(), XR_REFERENCE_SPACE_TYPE_STAGE) != types.end();
    XrReferenceSpaceCreateInfo rs{XR_TYPE_REFERENCE_SPACE_CREATE_INFO};
    rs.poseInReferenceSpace.orientation.w = 1.0f;
    rs.referenceSpaceType = hasStage ? XR_REFERENCE_SPACE_TYPE_STAGE : XR_REFERENCE_SPACE_TYPE_LOCAL;
    next.CreateReferenceSpace(session, &rs, &g.baseSpace);
    rs.referenceSpaceType = XR_REFERENCE_SPACE_TYPE_VIEW;
    next.CreateReferenceSpace(session, &rs, &g.viewSpace);
    Log("Using %s space", hasStage ? "stage (room)" : "local");

    // Choose a plain 8-bit colour format. Magenta and yellow are identical in sRGB and linear.
    next.EnumerateSwapchainFormats(session, 0, &n, nullptr);
    std::vector<int64_t> formats(n);
    next.EnumerateSwapchainFormats(session, n, &n, formats.data());
    const int64_t rgba[] = {DXGI_FORMAT_R8G8B8A8_UNORM_SRGB, DXGI_FORMAT_R8G8B8A8_UNORM};
    const int64_t bgra[] = {DXGI_FORMAT_B8G8R8A8_UNORM_SRGB, DXGI_FORMAT_B8G8R8A8_UNORM};
    g.format = 0;
    for (int64_t f : formats) {
        if (std::find(std::begin(rgba), std::end(rgba), f) != std::end(rgba)) { g.format = f; g.bgra = false; break; }
        if (std::find(std::begin(bgra), std::end(bgra), f) != std::end(bgra)) { g.format = f; g.bgra = true; break; }
    }
    if (!g.format) {
        Log("No suitable texture format offered - windows disabled");
        g.gfx = Gfx::None;
        return;
    }

    LoadConfig(g.cfg);
    g.cfgStamp = ConfigFileStamp();
    g.structureDirty = true;
    StartSteeringReader(g.cfg.steering.deviceMatch);
    StartHotkeys();
    g.link = OpenLink();
    if (g.link) g.lastCmdSeq = g.link->cmdSeq; // ignore anything sent before this session
}

void TeardownSession() {
    DestroyWindowResources(g.board);
    g.board = Window{};
    g.boardShowing = false;
    DestroyWindowResources(g.wheel);
    for (auto& r : g.rects) { DestroyWindowResources(r); CloseSource(r); }
    g.rects.clear();
    g.wheel = Window{};
    if (g.baseSpace) next.DestroySpace(g.baseSpace);
    if (g.viewSpace) next.DestroySpace(g.viewSpace);
    g.baseSpace = g.viewSpace = XR_NULL_HANDLE;
    if (g.d3d12Upload) g.d3d12Upload->Release();
    if (g.d3d12List) g.d3d12List->Release();
    if (g.d3d12Alloc) g.d3d12Alloc->Release();
    if (g.d3d12Fence) g.d3d12Fence->Release();
    if (g.d3d12Event) CloseHandle(g.d3d12Event);
    g.d3d12Upload = nullptr; g.d3d12List = nullptr; g.d3d12Alloc = nullptr; g.d3d12Fence = nullptr;
    g.d3d12Event = nullptr; g.d3d12UploadSize = 0;
    if (g.saveDue) { SaveConfig(g.cfg); g.saveDue = 0; }
    g.session = XR_NULL_HANDLE;
    g.gfx = Gfx::None;
}

// ---------------------------------------------------------------------------
// Hooks
// ---------------------------------------------------------------------------
XrResult XRAPI_CALL Hook_xrCreateSession(XrInstance instance, const XrSessionCreateInfo* info, XrSession* session) {
    XrResult r = next.CreateSession(instance, info, session);
    if (XR_SUCCEEDED(r) && info) {
        try {
            SetupSession(*session, info);
        } catch (...) {
            Log("Error setting up session - windows disabled");
            g.gfx = Gfx::None;
        }
    }
    return r;
}

XrResult XRAPI_CALL Hook_xrDestroySession(XrSession session) {
    if (session == g.session) {
        try { TeardownSession(); } catch (...) {}
    }
    return next.DestroySession(session);
}

XrResult XRAPI_CALL Hook_xrEndFrame(XrSession session, const XrFrameEndInfo* info) {
    if (session != g.session || g.gfx == Gfx::None || !info) return next.EndFrame(session, info);

    LARGE_INTEGER t0, t1;
    QueryPerformanceCounter(&t0);
    try {
        if (!g.loggedFirstFrame) {
            Log("First frame - adding windows");
            g.loggedFirstFrame = true;
        }
        // First ever run: put the wheel window in front of you after a few seconds,
        // so there is something visible straight away to confirm it's working.
        if (!g.cfg.wheelPlaced && ++g.framesSinceStart > 300) {
            g.selected = 0;
            if (PlaceSelectedInFront(info->displayTime)) Log("First run - wheel window placed automatically");
            else g.framesSinceStart = 0;
        }
        HandleHotkeys(info->displayTime);
        ProcessLink(info->displayTime);
        if (!g.gameRecorded) { RecordGame(); g.gameRecorded = true; }
        CheckConfig();
        if (g.structureDirty) RebuildWindows();
        ClearHighlightIfExpired();

        std::vector<const XrCompositionLayerBaseHeader*> layers(info->layers, info->layers + info->layerCount);
        XrCompositionLayerQuad quads[16];
        int nq = 0;

        if (g.cfg.enabled) {
            WheelProfile* wp = ActiveWheel();
            if (wp && g.wheel.shape.valid()) {
                if (!g.wheel.needsUpload || Upload(g.wheel)) {
                    float spin = 0.0f;
                    float angle = 0.0f;
                    if (wp->rotates && SteeringDeg(angle)) spin = -angle; // turning right = clockwise
                    quads[nq++] = MakeQuad(g.wheel, ToXrPose(wp->pose, spin));
                }
            }
            for (size_t i = 0; i < g.rects.size() && i < g.cfg.windows.size() && nq < 16; ++i) {
                if (!g.cfg.windows[i].enabled) continue;
                if (g.cfg.windows[i].onlyWhileAdjusting && !Adjusting()) continue;
                Window& w = g.rects[i];
                if (w.isPanel) UpdatePanel(w, g.cfg.windows[i]);
                if (w.texW == 0) continue;
                if (w.needsUpload && !Upload(w)) continue;
                quads[nq++] = MakeQuad(w, ToXrPose(g.cfg.windows[i].pose, 0.0f));
            }
        }

        if (nq < 16 && BoardFrame(info->displayTime, quads[nq])) {
            g.board.label = "Setup panel";
            ++nq;
        }

        QueryPerformanceCounter(&t1);
        RecordFrameCost(t1.QuadPart - t0.QuadPart);
        if (nq == 0) return next.EndFrame(session, info);
        for (int i = 0; i < nq; ++i) layers.push_back(reinterpret_cast<const XrCompositionLayerBaseHeader*>(&quads[i]));
        XrFrameEndInfo mod = *info;
        mod.layerCount = (uint32_t)layers.size();
        mod.layers = layers.data();
        XrResult r = next.EndFrame(session, &mod);
        if (XR_FAILED(r) && r != XR_ERROR_SESSION_LOST) {
            // Never break the game: switch the windows off for this session and resend the frame.
            Log("Runtime rejected the frame with windows (%d) - windows off for this session", (int)r);
            g.gfx = Gfx::None;
            return next.EndFrame(session, info);
        }
        return r;
    } catch (const std::exception& e) {
        if (!g.loggedError) Log("Error while drawing windows: %s", e.what());
        g.loggedError = true;
        return next.EndFrame(session, info);
    }
}

XrResult XRAPI_CALL Hook_xrDestroyInstance(XrInstance instance) {
    if (g.session) { try { TeardownSession(); } catch (...) {} }
    StopHotkeys();
    CloseLink();
    g.link = nullptr;
    StopSteeringReader();
    XrResult r = next.DestroyInstance(instance);
    g.instance = XR_NULL_HANDLE;
    Log("Game closed its VR session");
    return r;
}

XrResult XRAPI_CALL Hook_xrGetInstanceProcAddr(XrInstance instance, const char* name, PFN_xrVoidFunction* fn) {
    if (!next.GetInstanceProcAddr) return XR_ERROR_HANDLE_INVALID;
    XrResult r = next.GetInstanceProcAddr(instance, name, fn);
    if (XR_FAILED(r) || !name) return r;
    if (!strcmp(name, "xrGetInstanceProcAddr")) *fn = reinterpret_cast<PFN_xrVoidFunction>(Hook_xrGetInstanceProcAddr);
    else if (!strcmp(name, "xrDestroyInstance")) *fn = reinterpret_cast<PFN_xrVoidFunction>(Hook_xrDestroyInstance);
    else if (!strcmp(name, "xrCreateSession")) *fn = reinterpret_cast<PFN_xrVoidFunction>(Hook_xrCreateSession);
    else if (!strcmp(name, "xrDestroySession")) *fn = reinterpret_cast<PFN_xrVoidFunction>(Hook_xrDestroySession);
    else if (!strcmp(name, "xrEndFrame")) *fn = reinterpret_cast<PFN_xrVoidFunction>(Hook_xrEndFrame);
    return r;
}

template <typename T>
void Resolve(XrInstance instance, const char* name, T& out) {
    PFN_xrVoidFunction f = nullptr;
    next.GetInstanceProcAddr(instance, name, &f);
    out = reinterpret_cast<T>(f);
}

XrResult XRAPI_CALL Hook_xrCreateApiLayerInstance(const XrInstanceCreateInfo* info, const XrApiLayerCreateInfo* layerInfo,
                                                  XrInstance* instance) {
    if (!layerInfo || !layerInfo->nextInfo) return XR_ERROR_INITIALIZATION_FAILED;
    next.GetInstanceProcAddr = layerInfo->nextInfo->nextGetInstanceProcAddr;

    XrApiLayerCreateInfo chain = *layerInfo;
    chain.nextInfo = layerInfo->nextInfo->next;
    XrResult r = layerInfo->nextInfo->nextCreateApiLayerInstance(info, &chain, instance);
    if (XR_FAILED(r)) return r;

    g.instance = *instance;
    g.gameName = info->applicationInfo.applicationName;
    {
        wchar_t exe[MAX_PATH] = {};
        GetModuleFileNameW(nullptr, exe, MAX_PATH);
        std::wstring e(exe);
        auto slash = e.find_last_of(L"\\/");
        g.gameExe = Narrow(slash == std::wstring::npos ? e : e.substr(slash + 1));
        std::string key = g.gameExe;
        for (auto& ch : key) ch = (char)tolower((unsigned char)ch);
        SetLayoutGame(key, g.gameName.empty() ? g.gameExe : g.gameName);
    }
    Resolve(*instance, "xrDestroyInstance", next.DestroyInstance);
    Resolve(*instance, "xrCreateSession", next.CreateSession);
    Resolve(*instance, "xrDestroySession", next.DestroySession);
    Resolve(*instance, "xrEndFrame", next.EndFrame);
    Resolve(*instance, "xrEnumerateReferenceSpaces", next.EnumerateReferenceSpaces);
    Resolve(*instance, "xrCreateReferenceSpace", next.CreateReferenceSpace);
    Resolve(*instance, "xrDestroySpace", next.DestroySpace);
    Resolve(*instance, "xrLocateSpace", next.LocateSpace);
    Resolve(*instance, "xrEnumerateSwapchainFormats", next.EnumerateSwapchainFormats);
    Resolve(*instance, "xrCreateSwapchain", next.CreateSwapchain);
    Resolve(*instance, "xrDestroySwapchain", next.DestroySwapchain);
    Resolve(*instance, "xrEnumerateSwapchainImages", next.EnumerateSwapchainImages);
    Resolve(*instance, "xrAcquireSwapchainImage", next.AcquireSwapchainImage);
    Resolve(*instance, "xrWaitSwapchainImage", next.WaitSwapchainImage);
    Resolve(*instance, "xrReleaseSwapchainImage", next.ReleaseSwapchainImage);

    XrInstanceProperties props{XR_TYPE_INSTANCE_PROPERTIES};
    PFN_xrGetInstanceProperties getProps = nullptr;
    Resolve(*instance, "xrGetInstanceProperties", getProps);
    if (getProps) getProps(*instance, &props);
    g.runtimeName = props.runtimeName;
    Log("Thin Place VR %s loaded into \"%s\" (engine \"%s\") - runtime \"%s\"", HV_VERSION,
        info->applicationInfo.applicationName, info->applicationInfo.engineName, props.runtimeName);
    return r;
}

} // namespace
} // namespace hv

BOOL WINAPI DllMain(HINSTANCE, DWORD reason, LPVOID) {
    if (reason == DLL_PROCESS_ATTACH) {
        wchar_t exe[MAX_PATH] = {};
        GetModuleFileNameW(nullptr, exe, MAX_PATH);
        hv::Log("Thin Place VR " HV_VERSION " loaded into %s", hv::Narrow(exe).c_str());
    }
    return TRUE;
}

extern "C" __declspec(dllexport) XrResult XRAPI_CALL xrNegotiateLoaderApiLayerInterface(
    const XrNegotiateLoaderInfo* loaderInfo, const char* layerName, XrNegotiateApiLayerRequest* request) {
    if (loaderInfo)
        hv::Log("OpenXR loader asking for layer \"%s\" (interface %u-%u, API %u.%u-%u.%u)", layerName ? layerName : "?",
                loaderInfo->minInterfaceVersion, loaderInfo->maxInterfaceVersion,
                (unsigned)XR_VERSION_MAJOR(loaderInfo->minApiVersion), (unsigned)XR_VERSION_MINOR(loaderInfo->minApiVersion),
                (unsigned)XR_VERSION_MAJOR(loaderInfo->maxApiVersion), (unsigned)XR_VERSION_MINOR(loaderInfo->maxApiVersion));
    if (!loaderInfo || !request || loaderInfo->structType != XR_LOADER_INTERFACE_STRUCT_LOADER_INFO ||
        request->structType != XR_LOADER_INTERFACE_STRUCT_API_LAYER_REQUEST)
        return XR_ERROR_INITIALIZATION_FAILED;
    if (loaderInfo->minInterfaceVersion > XR_CURRENT_LOADER_API_LAYER_VERSION ||
        loaderInfo->maxInterfaceVersion < XR_CURRENT_LOADER_API_LAYER_VERSION)
        return XR_ERROR_INITIALIZATION_FAILED;

    request->layerInterfaceVersion = XR_CURRENT_LOADER_API_LAYER_VERSION;
    request->layerApiVersion =
        XR_CURRENT_API_VERSION < loaderInfo->maxApiVersion ? XR_CURRENT_API_VERSION : loaderInfo->maxApiVersion;
    request->getInstanceProcAddr = hv::Hook_xrGetInstanceProcAddr;
    request->createApiLayerInstance = hv::Hook_xrCreateApiLayerInstance;
    return XR_SUCCESS;
}
