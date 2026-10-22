// Handy Viewer 3D - global hotkeys polled every frame (work while the sim has focus).
#pragma once

namespace hv {

enum class Action {
    None,
    ToggleAll,       // Ctrl+Shift+F1
    SelectNext,      // Ctrl+Shift+F2
    PlaceInFront,    // Ctrl+Shift+F3
    NextWheel,       // Ctrl+Shift+F4
    CentreSteering,  // Ctrl+Shift+F5
    InvertSteering,  // Ctrl+Shift+F6
    MoveLeft, MoveRight, MoveUp, MoveDown, MoveAway, MoveCloser, // Ctrl+Shift+Numpad 4 6 8 2 9 3
    TiltUp, TiltDown,      // Ctrl+Shift+Numpad 7 1
    TurnLeft, TurnRight,   // Ctrl+Shift+Numpad / *
    RollLeft, RollRight,   // Ctrl+Shift+Page Up / Page Down
    Bigger, Smaller,       // Ctrl+Shift+Numpad + -
    Count
};

// Returns the actions triggered this frame (with key repeat while held).
// 'fine' is true when Alt is also held, for small steps.
int PollHotkeys(Action* out, int maxOut, bool& fine);

} // namespace hv
