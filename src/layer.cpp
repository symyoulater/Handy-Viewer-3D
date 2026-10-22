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

#define HV_VERSION "0.4.0"
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
    int selected = 0; // 0 = wheel, 1.. = rect windows
    DWORD highlightUntil = 0;
    bool loggedFirstFrame = false;
    int framesSinceStart = 0;
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

    const float pxPerMm = std::min(3.0f, 1024.0f / (2.0f * std::max(w.halfWmm, w.halfHmm)));
    int texW = std::max(16, (int)std::ceil(2.0f * w.halfWmm * pxPerMm));
    int texH = std::max(16, (int)std::ceil(2.0f * w.halfHmm * pxPerMm));
    if (texW != w.texW || texH != w.texH) {
        DestroyWindowResources(w);
        w.texW = texW;
        w.texH = texH;
    }

    w.pixels.assign((size_t)texW * texH * 4, 0);
    const uint8_t kr = (uint8_t)g.cfg.keyColour[0], kg = (uint8_t)g.cfg.keyColour[1], kb = (uint8_t)g.cfg.keyColour[2];
    for (int row = 0; row < texH; ++row) {
        float y = w.halfHmm - (row + 0.5f) * (2.0f * w.halfHmm / texH);
        for (int col = 0; col < texW; ++col) {
            float x = -w.halfWmm + (col + 0.5f) * (2.0f * w.halfWmm / texW);
            uint8_t r = 0, gg = 0, b = 0, a = 0;
            bool insideShape = PointInPolygon(w.shape.points, x, y);
            float edge = insideShape ? 0.0f : DistanceToEdges(w.shape.points, x, y);
            bool inside = insideShape || edge <= margin;
            if (w.highlighted && ((margin > 0 && !insideShape && std::fabs(edge - margin) < 2.5f) ||
                                  (margin <= 0 && DistanceToEdges(w.shape.points, x, y) < 2.5f))) {
                r = 255; gg = 220; b = 0; a = 255; // selection outline, at the window's outer edge
            } else if (inside) {
                r = kr; gg = kg; b = kb; a = 255;
            }
            uint8_t* px = &w.pixels[((size_t)row * texW + col) * 4];
            if (g.bgra) { px[0] = b; px[1] = gg; px[2] = r; }
            else { px[0] = r; px[1] = gg; px[2] = b; }
            px[3] = a;
        }
    }
    w.needsUpload = true;
}

// (Re)build the windows from the current config.
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
    for (auto& r : g.rects) DestroyWindowResources(r);
    g.rects.clear();
    for (auto& rw : g.cfg.windows) {
        Window w;
        w.label = rw.name;
        float hx = rw.widthMm * 0.5f, hy = rw.heightMm * 0.5f;
        w.shape.points = {{-hx, -hy}, {hx, -hy}, {hx, hy}, {-hx, hy}};
        g.rects.push_back(std::move(w));
    }
    if (g.selected > (int)g.rects.size()) g.selected = 0;
    g.wheel.highlighted = (g.selected == 0) && GetTickCount() < g.highlightUntil;
    if (g.wheel.shape.valid()) Rasterise(g.wheel);
    for (size_t i = 0; i < g.rects.size(); ++i) {
        g.rects[i].highlighted = (g.selected == (int)i + 1) && GetTickCount() < g.highlightUntil;
        Rasterise(g.rects[i]);
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
        if (g.rects[i].highlighted != want) { g.rects[i].highlighted = want; Rasterise(g.rects[i]); }
    }
}

void ClearHighlightIfExpired() {
    if (!g.highlightUntil || GetTickCount() < g.highlightUntil) return;
    g.highlightUntil = 0;
    if (g.wheel.highlighted) { g.wheel.highlighted = false; if (g.wheel.shape.valid()) Rasterise(g.wheel); }
    for (auto& r : g.rects)
        if (r.highlighted) { r.highlighted = false; Rasterise(r); }
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

// One adjustment, from a hotkey or from the desktop app.
void DoAction(Action action, bool fine, XrTime displayTime) {
    float step = (fine ? 1.0f : g.cfg.nudgeMm) / 1000.0f;
    float deg = fine ? 0.25f : g.cfg.nudgeDeg;
    {
        Pose* p = SelectedPose();
        switch (action) {
        case Action::EditModeOn:
            Log("Edit mode on - adjusting %s", SelectedName());
            Highlight();
            break;
        case Action::EditModeOff:
            Log("Edit mode off");
            g.highlightUntil = GetTickCount(); // outline clears on the next frame
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
                RectWindow& rw = g.cfg.windows[g.selected - 1];
                float d = (action == Action::Bigger ? 1.0f : -1.0f) * (fine ? 1.0f : g.cfg.nudgeMm);
                rw.widthMm = std::max(20.0f, rw.widthMm + d);
                rw.heightMm = std::max(20.0f, rw.heightMm + d);
                float hx = rw.widthMm * 0.5f, hy = rw.heightMm * 0.5f;
                Window& w = g.rects[g.selected - 1];
                w.shape.points = {{-hx, -hy}, {hx, -hy}, {hx, hy}, {-hx, hy}};
                Rasterise(w);
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
    DestroyWindowResources(g.wheel);
    for (auto& r : g.rects) DestroyWindowResources(r);
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
                Window& w = g.rects[i];
                if (w.needsUpload && !Upload(w)) continue;
                quads[nq++] = MakeQuad(w, ToXrPose(g.cfg.windows[i].pose, 0.0f));
            }
        }

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
