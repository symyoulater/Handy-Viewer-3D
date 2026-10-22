#include "hotkeys.h"

#define WIN32_LEAN_AND_MEAN
#include <windows.h>

namespace hv {

namespace {

struct Binding {
    Action action;
    int vk;
    bool repeats;
};

const Binding kBindings[] = {
    {Action::ToggleAll, VK_F1, false},
    {Action::SelectNext, VK_F2, false},
    {Action::PlaceInFront, VK_F3, false},
    {Action::NextWheel, VK_F4, false},
    {Action::CentreSteering, VK_F5, false},
    {Action::InvertSteering, VK_F6, false},
    {Action::MoveLeft, VK_NUMPAD4, true},
    {Action::MoveRight, VK_NUMPAD6, true},
    {Action::MoveUp, VK_NUMPAD8, true},
    {Action::MoveDown, VK_NUMPAD2, true},
    {Action::MoveAway, VK_NUMPAD9, true},
    {Action::MoveCloser, VK_NUMPAD3, true},
    {Action::TiltUp, VK_NUMPAD7, true},
    {Action::TiltDown, VK_NUMPAD1, true},
    {Action::TurnLeft, VK_DIVIDE, true},
    {Action::TurnRight, VK_MULTIPLY, true},
    {Action::RollLeft, VK_PRIOR, true},
    {Action::RollRight, VK_NEXT, true},
    {Action::Bigger, VK_ADD, true},
    {Action::Smaller, VK_SUBTRACT, true},
};

constexpr int kCount = sizeof(kBindings) / sizeof(kBindings[0]);
bool g_down[kCount] = {};
DWORD g_nextRepeat[kCount] = {};

bool Held(int vk) { return (GetAsyncKeyState(vk) & 0x8000) != 0; }

} // namespace

int PollHotkeys(Action* out, int maxOut, bool& fine) {
    int n = 0;
    bool mods = Held(VK_CONTROL) && Held(VK_SHIFT);
    fine = Held(VK_MENU);
    DWORD now = GetTickCount();
    for (int i = 0; i < kCount; ++i) {
        bool down = mods && Held(kBindings[i].vk);
        if (down && !g_down[i]) {
            if (n < maxOut) out[n++] = kBindings[i].action;
            g_nextRepeat[i] = now + 400;
        } else if (down && kBindings[i].repeats && (LONG)(now - g_nextRepeat[i]) >= 0) {
            if (n < maxOut) out[n++] = kBindings[i].action;
            g_nextRepeat[i] = now + 80;
        }
        g_down[i] = down;
    }
    return n;
}

} // namespace hv
