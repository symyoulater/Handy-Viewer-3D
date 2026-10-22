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
    {Action::ToggleAll, '1', false},
    {Action::SelectNext, '2', false},
    {Action::PlaceInFront, '3', false},
    {Action::NextWheel, '4', false},
    {Action::CentreSteering, '5', false},
    {Action::InvertSteering, '6', false},
    {Action::MoveLeft, 'A', true},
    {Action::MoveRight, 'D', true},
    {Action::MoveUp, 'W', true},
    {Action::MoveDown, 'S', true},
    {Action::MoveAway, 'R', true},
    {Action::MoveCloser, 'F', true},
    {Action::TiltUp, 'T', true},
    {Action::TiltDown, 'G', true},
    {Action::TurnLeft, 'Q', true},
    {Action::TurnRight, 'E', true},
    {Action::RollLeft, 'Z', true},
    {Action::RollRight, 'C', true},
    {Action::Bigger, VK_OEM_PLUS, true},
    {Action::Smaller, VK_OEM_MINUS, true},
};

constexpr int kCount = sizeof(kBindings) / sizeof(kBindings[0]);
bool g_down[kCount] = {};
DWORD g_nextRepeat[kCount] = {};

bool Held(int vk) { return (GetAsyncKeyState(vk) & 0x8000) != 0; }

} // namespace

int PollHotkeys(Action* out, int maxOut, bool& fine) {
    int n = 0;
    bool mods = Held(VK_CONTROL) && Held(VK_MENU);
    fine = Held(VK_SHIFT);
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
