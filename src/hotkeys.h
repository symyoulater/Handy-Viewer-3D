// Handy Viewer 3D - global hotkeys polled every frame (work while the sim has focus).
// All use Ctrl+Alt plus main-block keys, so they work on compact keyboards with no
// numpad or F-keys (e.g. Logitech K400) and can be put on Stream Deck buttons.
#pragma once

namespace hv {

enum class Action {
    None,
    ToggleAll,       // Ctrl+Alt+1
    SelectNext,      // Ctrl+Alt+2
    PlaceInFront,    // Ctrl+Alt+3
    NextWheel,       // Ctrl+Alt+4
    CentreSteering,  // Ctrl+Alt+5
    InvertSteering,  // Ctrl+Alt+6
    MoveLeft, MoveRight, MoveUp, MoveDown, MoveAway, MoveCloser, // A D W S R F
    TiltUp, TiltDown,      // T G
    TurnLeft, TurnRight,   // Q E
    RollLeft, RollRight,   // Z C
    Bigger, Smaller,       // = -
    Count
};

// Returns the actions triggered this frame (with key repeat while held).
// 'fine' is true when Shift is also held, for small steps.
int PollHotkeys(Action* out, int maxOut, bool& fine);

} // namespace hv
