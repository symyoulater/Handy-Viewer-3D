// Handy Viewer 3D - reads the steering axis straight from the wheel base over USB HID.
// Works whatever the game is doing and needs no window focus.
#pragma once

#include <string>

namespace hv {

void StartSteeringReader(const std::string& deviceMatch);
void StopSteeringReader();

// Steering position from -1 (full left) to +1 (full right). Returns false if no base found yet.
bool GetSteeringNormalised(float& out);

} // namespace hv
