// Handy Viewer 3D - small vector / quaternion helpers for OpenXR poses.
#pragma once

#include <openxr/openxr.h>
#include <cmath>

namespace hv {

constexpr float kPi = 3.14159265358979f;
inline float Deg2Rad(float d) { return d * kPi / 180.0f; }
inline float Rad2Deg(float r) { return r * 180.0f / kPi; }

inline XrQuaternionf QuatMul(const XrQuaternionf& a, const XrQuaternionf& b) {
    return {
        a.w * b.x + a.x * b.w + a.y * b.z - a.z * b.y,
        a.w * b.y - a.x * b.z + a.y * b.w + a.z * b.x,
        a.w * b.z + a.x * b.y - a.y * b.x + a.z * b.w,
        a.w * b.w - a.x * b.x - a.y * b.y - a.z * b.z,
    };
}

inline XrQuaternionf QuatAxisAngle(float ax, float ay, float az, float rad) {
    float s = std::sin(rad * 0.5f);
    return {ax * s, ay * s, az * s, std::cos(rad * 0.5f)};
}

// OpenXR convention: +Y up, -Z forward. Yaw about Y, pitch about X, roll about Z.
inline XrQuaternionf QuatFromYawPitchRoll(float yawDeg, float pitchDeg, float rollDeg) {
    XrQuaternionf qy = QuatAxisAngle(0, 1, 0, Deg2Rad(yawDeg));
    XrQuaternionf qx = QuatAxisAngle(1, 0, 0, Deg2Rad(pitchDeg));
    XrQuaternionf qz = QuatAxisAngle(0, 0, 1, Deg2Rad(rollDeg));
    return QuatMul(QuatMul(qy, qx), qz);
}

inline XrVector3f QuatRotate(const XrQuaternionf& q, const XrVector3f& v) {
    // v' = q * v * q^-1, expanded.
    float tx = 2.0f * (q.y * v.z - q.z * v.y);
    float ty = 2.0f * (q.z * v.x - q.x * v.z);
    float tz = 2.0f * (q.x * v.y - q.y * v.x);
    return {
        v.x + q.w * tx + (q.y * tz - q.z * ty),
        v.y + q.w * ty + (q.z * tx - q.x * tz),
        v.z + q.w * tz + (q.x * ty - q.y * tx),
    };
}

// Extract yaw and pitch (degrees) of a quaternion's forward (-Z) direction.
inline void ForwardYawPitch(const XrQuaternionf& q, float& yawDeg, float& pitchDeg) {
    XrVector3f f = QuatRotate(q, {0, 0, -1});
    yawDeg = Rad2Deg(std::atan2(-f.x, -f.z));
    pitchDeg = Rad2Deg(std::asin(std::fmax(-1.0f, std::fmin(1.0f, f.y))));
}

} // namespace hv
