#include "steering.h"
#include "log.h"

#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <setupapi.h>
extern "C" {
#include <hidsdi.h>
#include <hidpi.h>
}

#include <algorithm>
#include <atomic>
#include <mutex>
#include <cwctype>
#include <thread>
#include <vector>

namespace hv {

namespace {

std::atomic<bool> g_run{false};
std::atomic<bool> g_found{false};
std::atomic<float> g_value{0.0f};
std::thread g_thread;
std::string g_match;
std::mutex g_nameMutex;
std::string g_deviceName;

struct OpenDevice {
    HANDLE handle = INVALID_HANDLE_VALUE;
    PHIDP_PREPARSED_DATA preparsed = nullptr;
    HIDP_CAPS caps{};
    HIDP_VALUE_CAPS xCaps{};
    std::wstring name;

    void Close() {
        if (preparsed) HidD_FreePreparsedData(preparsed);
        if (handle != INVALID_HANDLE_VALUE) CloseHandle(handle);
        preparsed = nullptr;
        handle = INVALID_HANDLE_VALUE;
    }
};

bool ContainsNoCase(const std::wstring& hay, const std::wstring& needle) {
    if (needle.empty()) return true;
    std::wstring a = hay, b = needle;
    std::transform(a.begin(), a.end(), a.begin(), ::towlower);
    std::transform(b.begin(), b.end(), b.begin(), ::towlower);
    return a.find(b) != std::wstring::npos;
}

// Find a game controller whose product name matches and that has an X (steering) axis.
// Wheel bases recognised automatically (lower case, matched anywhere in the USB product name).
const wchar_t* const kKnownBases[] = {
    L"simucube", L"fanatec", L"moza", L"gudsen", L"simagic", L"thrustmaster", L"logitech g", L"g29", L"g920",
    L"g923", L"g pro", L"driving force", L"asetek", L"forte", L"invicta", L"la prima", L"vrs", L"directforce",
    L"cammus", L"conspit", L"turtle beach", L"velocityone", L"pxn", L"accuforce", L"osw", L"leo bodnar",
    L"sim-r", L"simsteering", L"t300", L"t248", L"t-gt", L"t818", L"ts-pc", L"ts-xw", L"tx racing", L"t150", L"tmx",
};
// Things that report an X axis but aren't the steering.
const wchar_t* const kNotBases[] = {
    L"pedal", L"handbrake", L"hand brake", L"shifter", L"sequential", L"button box", L"buttonbox", L"joystick",
    L"throttle", L"hotas", L"gamepad", L"controller", L"xbox", L"dualsense", L"dualshock",
};

bool IsKnownBase(const std::wstring& name) {
    for (auto k : kNotBases) if (ContainsNoCase(name, k)) return false;
    for (auto k : kKnownBases) if (ContainsNoCase(name, k)) return true;
    return false;
}

bool FindDevice(OpenDevice& dev) {
    GUID hidGuid;
    HidD_GetHidGuid(&hidGuid);
    HDEVINFO set = SetupDiGetClassDevsW(&hidGuid, nullptr, nullptr, DIGCF_PRESENT | DIGCF_DEVICEINTERFACE);
    if (set == INVALID_HANDLE_VALUE) return false;

    std::wstring match = Widen(g_match);
    bool automatic = match.empty() || (ContainsNoCase(match, L"auto") && match.size() <= 9);
    std::wstring seen; // every controller with a steering-type axis, for the log
    bool ok = false;
    SP_DEVICE_INTERFACE_DATA iface{sizeof(iface)};
    for (DWORD i = 0; !ok && SetupDiEnumDeviceInterfaces(set, nullptr, &hidGuid, i, &iface); ++i) {
        DWORD size = 0;
        SetupDiGetDeviceInterfaceDetailW(set, &iface, nullptr, 0, &size, nullptr);
        std::vector<BYTE> buf(size);
        auto* detail = reinterpret_cast<SP_DEVICE_INTERFACE_DETAIL_DATA_W*>(buf.data());
        detail->cbSize = sizeof(SP_DEVICE_INTERFACE_DETAIL_DATA_W);
        if (!SetupDiGetDeviceInterfaceDetailW(set, &iface, detail, size, nullptr, nullptr)) continue;

        HANDLE h = CreateFileW(detail->DevicePath, GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr,
                               OPEN_EXISTING, FILE_FLAG_OVERLAPPED, nullptr);
        if (h == INVALID_HANDLE_VALUE) continue;

        OpenDevice d;
        d.handle = h;
        wchar_t product[256] = {};
        HidD_GetProductString(h, product, sizeof(product));
        d.name = product;

        if (!HidD_GetPreparsedData(h, &d.preparsed) || HidP_GetCaps(d.preparsed, &d.caps) != HIDP_STATUS_SUCCESS) {
            d.Close();
            continue;
        }
        bool isController = d.caps.UsagePage == 0x01 &&
                            (d.caps.Usage == 0x04 || d.caps.Usage == 0x05 || d.caps.Usage == 0x08);
        if (!isController) {
            d.Close();
            continue;
        }

        bool hasX = false;
        USHORT n = d.caps.NumberInputValueCaps;
        std::vector<HIDP_VALUE_CAPS> vcaps(n);
        if (n && HidP_GetValueCaps(HidP_Input, vcaps.data(), &n, d.preparsed) == HIDP_STATUS_SUCCESS) {
            for (USHORT k = 0; k < n && !hasX; ++k) {
                const auto& v = vcaps[k];
                if (v.UsagePage == 0x01 &&
                    (v.IsRange ? (v.Range.UsageMin <= 0x30 && v.Range.UsageMax >= 0x30) : v.NotRange.Usage == 0x30)) {
                    d.xCaps = v;
                    hasX = true;
                }
            }
        }
        if (hasX) seen += (seen.empty() ? L"" : L", ") + d.name;

        bool wanted = hasX && (automatic ? IsKnownBase(d.name) : ContainsNoCase(d.name, match));
        if (wanted) {
            dev = d;
            ok = true;
        } else {
            d.Close();
        }
    }
    SetupDiDestroyDeviceInfoList(set);

    static std::wstring lastSeen;
    if (!ok && seen != lastSeen) {
        Log("Looking for %s; controllers connected: %s", automatic ? "a known wheel base" : ("\"" + g_match + "\"").c_str(),
            seen.empty() ? "none" : Narrow(seen).c_str());
        lastSeen = seen;
    }
    return ok;
}

float Normalise(const OpenDevice& d, ULONG raw) {
    LONG lo = d.xCaps.LogicalMin;
    LONG hi = d.xCaps.LogicalMax;
    LONG v = static_cast<LONG>(raw);
    USHORT bits = d.xCaps.BitSize;
    if (lo < 0 && bits > 0 && bits < 32) {
        // Signed axis: sign-extend the raw value.
        ULONG sign = 1UL << (bits - 1);
        if (raw & sign) v = static_cast<LONG>(raw | (~0UL << bits));
    }
    if (hi < lo) {
        // Some devices report the max as unsigned that overflowed LONG.
        hi = static_cast<LONG>((1ULL << bits) - 1);
    }
    double t = hi > lo ? (double(v) - lo) / (double(hi) - lo) : 0.5;
    t = std::clamp(t, 0.0, 1.0);
    return static_cast<float>(t * 2.0 - 1.0);
}

// Read a value from a raw input report into g_value. Returns true if the report held the axis.
bool TakeReport(const OpenDevice& dev, std::vector<BYTE>& report) {
    ULONG raw = 0;
    if (HidP_GetUsageValue(HidP_Input, 0x01, 0, 0x30, &raw, dev.preparsed, reinterpret_cast<PCHAR>(report.data()),
                           (ULONG)report.size()) != HIDP_STATUS_SUCCESS)
        return false;
    g_value = Normalise(dev, raw);
    return true;
}

// Ask the base for its current position. Bases only send reports when something changes,
// so this gives a position straight away, even if the wheel is sitting still.
// Some bases (the Simucube 2) don't answer and stall for seconds instead, so after one failed
// or slow answer we stop asking that base and rely on its reports alone.
bool g_askWorks = true;

void AskForPosition(const OpenDevice& dev) {
    if (!g_askWorks) return;
    std::vector<BYTE> report(dev.caps.InputReportByteLength);
    if (report.empty()) return;
    report[0] = dev.xCaps.ReportID;
    DWORD start = GetTickCount();
    bool ok = HidD_GetInputReport(dev.handle, report.data(), (ULONG)report.size()) && TakeReport(dev, report);
    DWORD took = GetTickCount() - start;
    if (!ok || took > 200) {
        g_askWorks = false;
        Log("Wheel base doesn't answer position requests (%s after %lu ms) - using its reports only",
            ok ? "answered" : "no answer", took);
    }
}

void ReaderThread() {
    OpenDevice dev;
    DWORD lastSearch = 0;
    DWORD lastAsk = 0;
    while (g_run) {
        if (dev.handle == INVALID_HANDLE_VALUE) {
            g_found = false;
            DWORD now = GetTickCount();
            if (lastSearch && now - lastSearch < 1000) {
                Sleep(200);
                continue;
            }
            lastSearch = now;
            if (FindDevice(dev)) {
                // The Simucube 2 is known not to answer (it stalls for 5 s), so don't ask it at all.
                {
                    std::wstring lower = dev.name;
                    for (auto& ch : lower) ch = towlower(ch);
                    g_askWorks = lower.find(L"simucube") == std::wstring::npos;
                }
                {
                    std::lock_guard<std::mutex> lock(g_nameMutex);
                    g_deviceName = Narrow(dev.name);
                }
                Log("Wheel base found: %s (axis %d..%d, %u bits)", Narrow(dev.name).c_str(),
                    (int)dev.xCaps.LogicalMin, (int)dev.xCaps.LogicalMax, (unsigned)dev.xCaps.BitSize);
                // Connected counts as found, whether or not the wheel has moved yet.
                AskForPosition(dev);
                lastAsk = GetTickCount();
                g_found = true;
            } else {
                continue; // FindDevice logs what it saw
            }
        }

        std::vector<BYTE> report(dev.caps.InputReportByteLength);
        OVERLAPPED ov{};
        ov.hEvent = CreateEventW(nullptr, TRUE, FALSE, nullptr);
        DWORD got = 0;
        BOOL done = ReadFile(dev.handle, report.data(), (DWORD)report.size(), &got, &ov);
        if (!done && GetLastError() == ERROR_IO_PENDING) {
            if (WaitForSingleObject(ov.hEvent, 250) == WAIT_OBJECT_0) {
                done = GetOverlappedResult(dev.handle, &ov, &got, FALSE);
            } else {
                CancelIo(dev.handle);
                GetOverlappedResult(dev.handle, &ov, &got, TRUE);
                CloseHandle(ov.hEvent);
                // No change from the wheel; check its position once a second anyway.
                if (GetTickCount() - lastAsk > 1000) {
                    AskForPosition(dev);
                    lastAsk = GetTickCount();
                }
                continue;
            }
        }
        DWORD readError = done ? 0 : GetLastError();
        CloseHandle(ov.hEvent);
        if (!done && readError == ERROR_OPERATION_ABORTED) continue; // our own cancel; not a disconnect
        if (!done) {
            Log("Lost contact with wheel base (error %lu) - will reconnect", readError);
            dev.Close();
            continue;
        }

        static int total = 0, withSteering = 0;
        static DWORD firstAt = 0;
        static bool summarised = false;
        if (!firstAt) firstAt = GetTickCount();
        total++;
        if (TakeReport(dev, report)) { g_found = true; withSteering++; }
        if (!summarised && GetTickCount() - firstAt > 3000) {
            summarised = true;
            Log("Wheel base: %d reports in the first 3 s, %d with steering (last report id %u)", total, withSteering,
                report.empty() ? 0u : (unsigned)report[0]);
        }
    }
    dev.Close();
}

} // namespace

void StartSteeringReader(const std::string& deviceMatch) {
    if (g_run) return;
    g_match = deviceMatch;
    g_run = true;
    g_thread = std::thread(ReaderThread);
}

void RestartSteeringReader(const std::string& deviceMatch) {
    if (deviceMatch == g_match && g_run) return;
    StopSteeringReader();
    {
        std::lock_guard<std::mutex> lock(g_nameMutex);
        g_deviceName.clear();
    }
    StartSteeringReader(deviceMatch);
}

void StopSteeringReader() {
    if (!g_run) return;
    g_run = false;
    if (g_thread.joinable()) g_thread.join();
}

std::string GetSteeringDeviceName() {
    std::lock_guard<std::mutex> lock(g_nameMutex);
    return g_found ? g_deviceName : std::string();
}

bool GetSteeringNormalised(float& out) {
    if (!g_found) return false;
    out = g_value;
    return true;
}

} // namespace hv
