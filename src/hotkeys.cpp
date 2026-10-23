#include "hotkeys.h"
#include "log.h"

#define WIN32_LEAN_AND_MEAN
#include <windows.h>

#include <atomic>
#include <mutex>
#include <thread>
#include <vector>

namespace hv {

namespace {

std::atomic<bool> g_edit{false};
std::atomic<DWORD> g_threadId{0};
std::thread g_thread;
std::mutex g_mutex;
std::vector<Action> g_queue;
bool g_fine = false;
bool g_shift = false; // tracked here because swallowed keys don't update GetAsyncKeyState
DWORD g_lastEditKey = 0;

void Push(Action a, bool fine) {
    std::lock_guard<std::mutex> lock(g_mutex);
    if (g_queue.size() < 64) g_queue.push_back(a);
    if (fine) g_fine = true;
}

bool Held(int vk) { return (GetAsyncKeyState(vk) & 0x8000) != 0; }

Action EditAction(DWORD vk) {
    switch (vk) {
    case VK_TAB: return Action::SelectNext;
    case 'P': return Action::PlaceInFront;
    case 'O': return Action::ToggleWindow;
    case 'S': return Action::CycleShape;
    case 'B': return Action::BringBoard;
    case 'A': return Action::ShiftAll;
    case VK_OEM_4: return Action::PanelUp;
    case VK_OEM_6: return Action::PanelDown;
    case 'W': return Action::NextWheel;
    case 'X': return Action::CentreSteering;
    case 'I': return Action::InvertSteering;
    case VK_LEFT: return Action::MoveLeft;
    case VK_RIGHT: return Action::MoveRight;
    case VK_UP: return Action::MoveUp;
    case VK_DOWN: return Action::MoveDown;
    case 'R': return Action::MoveAway;
    case 'F': return Action::MoveCloser;
    case 'T': return Action::TiltUp;
    case 'G': return Action::TiltDown;
    case 'Q': return Action::TurnLeft;
    case 'E': return Action::TurnRight;
    case 'Z': return Action::RollLeft;
    case 'C': return Action::RollRight;
    case VK_OEM_PLUS: case VK_ADD: return Action::Bigger;
    case VK_OEM_MINUS: case VK_SUBTRACT: return Action::Smaller;
    default: return Action::None;
    }
}

LRESULT CALLBACK KeyboardProc(int code, WPARAM wParam, LPARAM lParam) {
    if (code != HC_ACTION) return CallNextHookEx(nullptr, code, wParam, lParam);
    const auto* k = reinterpret_cast<const KBDLLHOOKSTRUCT*>(lParam);
    bool down = wParam == WM_KEYDOWN || wParam == WM_SYSKEYDOWN;
    DWORD vk = k->vkCode;

    if (vk == VK_SHIFT || vk == VK_LSHIFT || vk == VK_RSHIFT) g_shift = down;

    if (g_edit) {
        // Edit mode: only Thin Place VR's own keys are caught; everything else still works normally.
        if (down) {
            bool ctrlAlt = Held(VK_CONTROL) && Held(VK_MENU);
            if (vk == VK_ESCAPE || (ctrlAlt && vk == 'H')) {
                g_edit = false;
                Push(Action::EditModeOff, false);
                return 1;
            }
            if (ctrlAlt && vk == 'V') {
                Push(Action::ToggleAll, false);
                return 1;
            }
            if (ctrlAlt && vk == 'K') {
                Push(Action::ToggleBoard, false);
                return 1;
            }
            Action a = ctrlAlt ? Action::None : EditAction(vk);
            if (a != Action::None) {
                g_lastEditKey = GetTickCount();
                Push(a, g_shift);
                return 1;
            }
        }
        // Key releases are never caught: swallowing a release leaves Windows thinking the key
        // (e.g. Ctrl or Alt) is still held, which makes the whole keyboard misbehave.
        return CallNextHookEx(nullptr, code, wParam, lParam);
    }

    // Normal driving: only our two shortcuts are caught.
    if (down && Held(VK_CONTROL) && Held(VK_MENU) && (vk == 'H' || vk == 'V' || vk == 'K')) {
        if (vk == 'H') {
            g_edit = true;
            g_lastEditKey = GetTickCount();
            Push(Action::EditModeOn, false);
        } else if (vk == 'K') {
            Push(Action::ToggleBoard, false);
        } else {
            Push(Action::ToggleAll, false);
        }
        return 1;
    }
    return CallNextHookEx(nullptr, code, wParam, lParam);
}

void HookThread() {
    g_threadId = GetCurrentThreadId();
    HMODULE mod = nullptr;
    GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                       reinterpret_cast<LPCWSTR>(&KeyboardProc), &mod);
    HHOOK hook = SetWindowsHookExW(WH_KEYBOARD_LL, KeyboardProc, mod, 0);
    if (!hook) {
        Log("Keyboard hook could not be installed (error %lu) - hotkeys won't work", GetLastError());
        return;
    }
    Log("Keyboard ready: Ctrl+Alt+H for edit mode, Ctrl+Alt+V to show/hide windows");
    MSG msg;
    // A timer wakes the loop so edit mode can time out if forgotten.
    SetTimer(nullptr, 0, 1000, nullptr);
    while (GetMessageW(&msg, nullptr, 0, 0) > 0) {
        if (g_edit && GetTickCount() - g_lastEditKey > 30000) {
            g_edit = false;
            Push(Action::EditModeOff, false);
        }
        DispatchMessageW(&msg);
    }
    UnhookWindowsHookEx(hook);
}

} // namespace

void StartHotkeys() {
    if (g_thread.joinable()) return;
    g_thread = std::thread(HookThread);
}

void StopHotkeys() {
    if (!g_thread.joinable()) return;
    for (int i = 0; i < 50 && !g_threadId; ++i) Sleep(10);
    if (g_threadId) PostThreadMessageW(g_threadId, WM_QUIT, 0, 0);
    g_thread.join();
    g_threadId = 0;
    g_edit = false;
}

bool InEditMode() { return g_edit; }

int PollHotkeys(Action* out, int maxOut, bool& fine) {
    std::lock_guard<std::mutex> lock(g_mutex);
    int n = 0;
    for (Action a : g_queue) {
        if (n >= maxOut) break;
        out[n++] = a;
    }
    g_queue.erase(g_queue.begin(), g_queue.begin() + n);
    fine = g_fine;
    if (g_queue.empty()) g_fine = false;
    return n;
}

} // namespace hv
