#include "config.h"
#include "log.h"

#define WIN32_LEAN_AND_MEAN
#include <windows.h>

#include <nlohmann/json.hpp>

#include <filesystem>
#include <fstream>
#include <sstream>

using nlohmann::json;

namespace hv {

namespace {

std::wstring ConfigPath() { return DataDir() + L"\\config.json"; }

json PoseToJson(const Pose& p) {
    return json{{"position_m", {p.position[0], p.position[1], p.position[2]}},
                {"yaw_deg", p.yaw},
                {"pitch_deg", p.pitch},
                {"roll_deg", p.roll}};
}

void PoseFromJson(const json& j, Pose& p) {
    if (!j.is_object()) return;
    if (j.contains("position_m") && j["position_m"].is_array() && j["position_m"].size() == 3)
        for (int i = 0; i < 3; ++i) p.position[i] = j["position_m"][i].get<float>();
    p.yaw = j.value("yaw_deg", p.yaw);
    p.pitch = j.value("pitch_deg", p.pitch);
    p.roll = j.value("roll_deg", p.roll);
}

bool ReadFile(const std::wstring& path, std::string& out) {
    std::ifstream f(std::filesystem::path(path), std::ios::binary);
    if (!f) return false;
    std::stringstream ss;
    ss << f.rdbuf();
    out = ss.str();
    return true;
}

} // namespace

Config DefaultConfig() {
    Config c;

    WheelProfile gt;
    gt.id = "300gt";
    gt.name = "Conspit 300GT";
    gt.shapeFile = "conspit-300gt.json";
    gt.pose.position = {0.0f, 0.95f, -0.55f};
    gt.pose.pitch = -15.0f;
    c.wheels.push_back(gt);

    WheelProfile apex;
    apex.id = "310apex";
    apex.name = "Conspit 310 APEX";
    apex.shapeFile = "conspit-310-apex.json";
    apex.rotates = false; // round rim: looks the same at any angle
    apex.pose = gt.pose;
    c.wheels.push_back(apex);

    RectWindow deck;
    deck.name = "Stream Deck";
    deck.pose.position = {0.35f, 0.85f, -0.5f};
    deck.pose.pitch = -40.0f;
    c.windows.push_back(deck);

    return c;
}

bool SaveConfig(const Config& c) {
    json j;
    j["enabled"] = c.enabled;
    j["key_colour_rgb"] = {c.keyColour[0], c.keyColour[1], c.keyColour[2]};
    j["place_distance_m"] = c.placeDistanceM;
    j["place_aim_down_deg"] = c.placeAimDownDeg;
    j["wheel_margin_mm"] = c.wheelMarginMm;
    j["wheel_placed"] = c.wheelPlaced;
    j["nudge_mm"] = c.nudgeMm;
    j["nudge_deg"] = c.nudgeDeg;
    j["steering"] = {{"range_deg", c.steering.rangeDeg},
                     {"invert", c.steering.invert},
                     {"centre_offset_deg", c.steering.centreOffsetDeg},
                     {"device_match", c.steering.deviceMatch}};
    j["active_wheel"] = c.activeWheel;
    json wheels = json::array();
    for (const auto& w : c.wheels) {
        wheels.push_back({{"id", w.id},
                          {"name", w.name},
                          {"shape_file", w.shapeFile},
                          {"scale", w.scale},
                          {"rotates", w.rotates},
                          {"hub_offset_mm", {w.hubOffsetMm[0], w.hubOffsetMm[1]}},
                          {"pose", PoseToJson(w.pose)}});
    }
    j["wheels"] = wheels;
    json wins = json::array();
    for (const auto& w : c.windows) {
        wins.push_back({{"name", w.name},
                        {"width_mm", w.widthMm},
                        {"height_mm", w.heightMm},
                        {"enabled", w.enabled},
                        {"pose", PoseToJson(w.pose)}});
    }
    j["windows"] = wins;

    // Write to a temp file then swap, so a crash never leaves a half-written config.
    std::wstring path = ConfigPath();
    std::wstring tmp = path + L".tmp";
    {
        std::ofstream f(std::filesystem::path(tmp), std::ios::binary | std::ios::trunc);
        if (!f) {
            Log("Could not write config");
            return false;
        }
        f << j.dump(2);
    }
    if (!MoveFileExW(tmp.c_str(), path.c_str(), MOVEFILE_REPLACE_EXISTING)) {
        Log("Could not replace config (error %lu)", GetLastError());
        return false;
    }
    return true;
}

bool LoadConfig(Config& c) {
    std::string text;
    if (!ReadFile(ConfigPath(), text)) {
        c = DefaultConfig();
        SaveConfig(c);
        Log("No config found - created default config.json");
        return true;
    }
    try {
        json j = json::parse(text);
        Config d = DefaultConfig();
        c = d;
        c.enabled = j.value("enabled", d.enabled);
        if (j.contains("key_colour_rgb") && j["key_colour_rgb"].size() == 3)
            for (int i = 0; i < 3; ++i) c.keyColour[i] = j["key_colour_rgb"][i].get<int>();
        c.placeDistanceM = j.value("place_distance_m", d.placeDistanceM);
        c.placeAimDownDeg = j.value("place_aim_down_deg", d.placeAimDownDeg);
        c.wheelMarginMm = j.value("wheel_margin_mm", d.wheelMarginMm);
        c.wheelPlaced = j.value("wheel_placed", d.wheelPlaced);
        c.nudgeMm = j.value("nudge_mm", d.nudgeMm);
        c.nudgeDeg = j.value("nudge_deg", d.nudgeDeg);
        if (j.contains("steering")) {
            const auto& s = j["steering"];
            c.steering.rangeDeg = s.value("range_deg", d.steering.rangeDeg);
            c.steering.invert = s.value("invert", d.steering.invert);
            c.steering.centreOffsetDeg = s.value("centre_offset_deg", d.steering.centreOffsetDeg);
            c.steering.deviceMatch = s.value("device_match", d.steering.deviceMatch);
        }
        c.activeWheel = j.value("active_wheel", d.activeWheel);
        if (j.contains("wheels") && j["wheels"].is_array()) {
            c.wheels.clear();
            for (const auto& w : j["wheels"]) {
                WheelProfile p;
                p.id = w.value("id", std::string("wheel"));
                p.name = w.value("name", p.id);
                p.shapeFile = w.value("shape_file", std::string());
                p.scale = w.value("scale", 1.0f);
                p.rotates = w.value("rotates", true);
                if (w.contains("hub_offset_mm") && w["hub_offset_mm"].size() == 2) {
                    p.hubOffsetMm[0] = w["hub_offset_mm"][0].get<float>();
                    p.hubOffsetMm[1] = w["hub_offset_mm"][1].get<float>();
                }
                if (w.contains("pose")) PoseFromJson(w["pose"], p.pose);
                c.wheels.push_back(p);
            }
        }
        if (j.contains("windows") && j["windows"].is_array()) {
            c.windows.clear();
            for (const auto& w : j["windows"]) {
                RectWindow r;
                r.name = w.value("name", std::string("Window"));
                r.widthMm = w.value("width_mm", r.widthMm);
                r.heightMm = w.value("height_mm", r.heightMm);
                r.enabled = w.value("enabled", true);
                if (w.contains("pose")) PoseFromJson(w["pose"], r.pose);
                c.windows.push_back(r);
            }
        }
        return true;
    } catch (const std::exception& e) {
        Log("config.json could not be read (%s) - keeping previous settings", e.what());
        return false;
    }
}

long long ConfigFileStamp() {
    WIN32_FILE_ATTRIBUTE_DATA a{};
    if (!GetFileAttributesExW(ConfigPath().c_str(), GetFileExInfoStandard, &a)) return 0;
    return (static_cast<long long>(a.ftLastWriteTime.dwHighDateTime) << 32) | a.ftLastWriteTime.dwLowDateTime;
}

Shape LoadShape(const std::string& file) {
    Shape s;
    // User shapes first, then the defaults installed next to the DLL.
    std::wstring candidates[] = {DataDir() + L"\\shapes\\" + Widen(file),
                                 ModuleDir() + L"\\shapes\\" + Widen(file)};
    for (const auto& path : candidates) {
        std::string text;
        if (!ReadFile(path, text)) continue;
        try {
            json j = json::parse(text);
            for (const auto& p : j["outline_mm"]) s.points.emplace_back(p[0].get<float>(), p[1].get<float>());
            Log("Loaded shape %s (%zu points)", Narrow(path).c_str(), s.points.size());
            return s;
        } catch (const std::exception& e) {
            Log("Shape %s could not be read: %s", Narrow(path).c_str(), e.what());
        }
    }
    Log("Shape %s not found", file.c_str());
    return s;
}

} // namespace hv
