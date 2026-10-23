// Thin Place VR - settings stored in Documents\Thin Place VR\config.json
#pragma once

#include <array>
#include <string>
#include <utility>
#include <vector>

namespace hv {

struct Pose {
    std::array<float, 3> position{0.0f, 1.0f, -0.6f}; // metres, stage space
    float yaw = 0.0f;   // degrees
    float pitch = 0.0f; // degrees
    float roll = 0.0f;  // degrees
};

struct SteeringSettings {
    float rangeDeg = 900.0f;       // must match the steering range in Simucube Tuner
    bool invert = false;           // flip if the window turns the wrong way
    float centreOffsetDeg = 0.0f;  // set by the "centre steering" hotkey
    std::string deviceMatch = "Simucube"; // part of the base's USB product name
};

struct WheelProfile {
    std::string id;
    std::string name;
    std::string shapeFile;           // file in the shapes folder
    float scale = 1.0f;              // size adjustment (1.0 = real size)
    bool userAdded = false;          // added from a photo in the app (can be removed there)
    bool rotates = true;             // follow the steering angle
    std::array<float, 2> hubOffsetMm{0.0f, 0.0f}; // where the hub sits within the shape
    Pose pose;
};

struct RectWindow {
    std::string name;
    float widthMm = 130.0f;
    float heightMm = 95.0f;
    bool enabled = true;
    bool onlyWhileAdjusting = false; // e.g. a keyboard window: shown only in edit mode / while the app adjusts
    Pose pose;
};

struct Config {
    bool enabled = true;
    std::array<int, 3> keyColour{255, 0, 255}; // must match Virtual Desktop's chroma key colour
    float placeDistanceM = 0.6f;
    float placeAimDownDeg = 10.0f;  // P aims this far below where the head points (eyes look lower)
    float wheelMarginMm = 25.0f;    // extra space around the rim for hands
    bool wheelPlaced = false; // false until the wheel window has been placed once
    float nudgeMm = 5.0f;
    float nudgeDeg = 1.0f;
    SteeringSettings steering;
    std::string activeWheel = "300gt";
    std::vector<WheelProfile> wheels;
    std::vector<RectWindow> windows;
};

// A shape outline in millimetres, centred on its rotation point.
struct Shape {
    std::vector<std::pair<float, float>> points;
    bool valid() const { return points.size() >= 3; }
};

Config DefaultConfig();
bool LoadConfig(Config& out);      // creates the file with defaults if missing
bool SaveConfig(const Config& cfg);
long long ConfigFileStamp();       // last-write time, for live reloading

Shape LoadShape(const std::string& file);

} // namespace hv
