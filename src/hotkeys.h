// Thin Place VR - keyboard control with an "edit mode".
//
// Ctrl+Alt+H switches edit mode on and off. While edit mode is on, only the adjustment keys
// are caught (they don't reach the game); every other key works as normal. Edit mode ends
// with Esc, Ctrl+Alt+H, or after 30 seconds without an adjustment key.
// Outside edit mode only Ctrl+Alt+H, Ctrl+Alt+V and Ctrl+Alt+K are caught; everything else goes to the game.
#pragma once

namespace hv {

// Numbers are fixed: the desktop app sends them over the live link (app/Link.cs).
enum class Action : int {
    None = 0,
    EditModeOn = 1,
    EditModeOff = 2,
    ToggleAll = 3,       // Ctrl+Alt+V (any time)
    SelectNext = 4,      // Tab
    PlaceInFront = 5,    // P
    NextWheel = 6,       // W
    CentreSteering = 7,  // X
    InvertSteering = 8,  // I
    MoveLeft = 9, MoveRight = 10, MoveUp = 11, MoveDown = 12, // arrow keys
    MoveAway = 13, MoveCloser = 14,                           // R / F
    TiltUp = 15, TiltDown = 16,                               // T / G
    TurnLeft = 17, TurnRight = 18,                            // Q / E
    RollLeft = 19, RollRight = 20,                            // Z / C
    Bigger = 21, Smaller = 22,                                // = / -
    Select = 23,         // app only: select the target window and outline it
    ToggleBoard = 24,    // Ctrl+Alt+K (any time): help board in the headset
};

void StartHotkeys();
void StopHotkeys();

bool InEditMode();

// Actions queued since the last call. 'fine' is set if Shift was held for any of them.
int PollHotkeys(Action* out, int maxOut, bool& fine);

} // namespace hv
