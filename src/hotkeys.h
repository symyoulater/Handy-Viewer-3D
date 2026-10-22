// Handy Viewer 3D - keyboard control with an "edit mode".
//
// Ctrl+Alt+H switches edit mode on and off. While edit mode is on, only the adjustment keys
// are caught (they don't reach the game); every other key works as normal. Edit mode ends
// with Esc, Ctrl+Alt+H, or after 30 seconds without an adjustment key.
// Outside edit mode only Ctrl+Alt+H and Ctrl+Alt+V are caught; everything else goes to the game.
#pragma once

namespace hv {

enum class Action {
    None,
    EditModeOn,
    EditModeOff,
    ToggleAll,       // Ctrl+Alt+V (any time)
    SelectNext,      // Tab
    PlaceInFront,    // P
    NextWheel,       // W
    CentreSteering,  // X
    InvertSteering,  // I
    MoveLeft, MoveRight, MoveUp, MoveDown, // arrow keys
    MoveAway, MoveCloser,                 // R / F
    TiltUp, TiltDown,                     // T / G
    TurnLeft, TurnRight,                  // Q / E
    RollLeft, RollRight,                  // Z / C
    Bigger, Smaller,                      // = / -
};

void StartHotkeys();
void StopHotkeys();

bool InEditMode();

// Actions queued since the last call. 'fine' is set if Shift was held for any of them.
int PollHotkeys(Action* out, int maxOut, bool& fine);

} // namespace hv
