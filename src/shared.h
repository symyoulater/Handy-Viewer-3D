// Thin Place VR - live link between the VR layer (inside the game) and the desktop app.
//
// A small block of shared memory named "Local\ThinPlaceVR.Link". The layer writes live
// status into it every frame; the app writes commands (place, nudge, select...) which the
// layer carries out on its next frame. The app's copy of this layout is in app/Link.cs -
// keep the two identical.
#pragma once

#include <cstddef>
#include <cstdint>

namespace hv {

#pragma pack(push, 4)
struct LinkBlock {
    uint32_t magic;          // 'TPVR'
    uint32_t version;        // layout version (1)
    uint64_t heartbeatMs;    // GetTickCount64 at last layer update; stale = no game running

    // Layer -> app
    float steeringDeg;       // as shown on the wheel window (after centre / flip)
    int32_t wheelFound;      // 1 if the wheel base is being read
    int32_t windowsEnabled;  // 1 if windows are showing
    int32_t selected;        // 0 = wheel, 1.. = other windows
    int32_t editMode;        // 1 while keyboard edit mode is on
    char game[128];          // game's own name for itself (UTF-8)
    char runtime[64];        // OpenXR runtime name
    char wheelBase[96];      // wheel base product name

    // App -> layer
    uint32_t cmdSeq;         // app increments after filling the fields below
    int32_t cmdAction;       // hv::Action value
    int32_t cmdTarget;       // window to act on (0 = wheel, 1.. = others), -1 = current
    int32_t cmdFine;         // 1 = small steps

    // Layer -> app: scroll a panel (edit-mode [ and ] keys)
    uint32_t panelSeq;       // layer increments after filling the fields below
    int32_t panelWindow;     // window index in config (0-based list of windows)
    int32_t panelCmd;        // 1 = scroll up, 2 = scroll down
    uint8_t reserved[160];   // room to grow without changing the size again
};
#pragma pack(pop)

// Must match the offsets in app/Link.cs.
static_assert(offsetof(LinkBlock, heartbeatMs) == 8, "link layout");
static_assert(offsetof(LinkBlock, steeringDeg) == 16, "link layout");
static_assert(offsetof(LinkBlock, game) == 36, "link layout");
static_assert(offsetof(LinkBlock, runtime) == 164, "link layout");
static_assert(offsetof(LinkBlock, wheelBase) == 228, "link layout");
static_assert(offsetof(LinkBlock, cmdSeq) == 324, "link layout");
static_assert(offsetof(LinkBlock, cmdFine) == 336, "link layout");
static_assert(offsetof(LinkBlock, panelSeq) == 340, "link layout");
static_assert(offsetof(LinkBlock, panelCmd) == 348, "link layout");
static_assert(sizeof(LinkBlock) == 512, "link layout");

constexpr uint32_t kLinkMagic = 0x52565054; // "TPVR"
constexpr uint32_t kLinkVersion = 2;

// Opens (or creates) the block. Returns nullptr if shared memory is unavailable.
LinkBlock* OpenLink();
void CloseLink();

} // namespace hv
