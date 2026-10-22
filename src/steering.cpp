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
bool FindDevice(OpenDevice& dev) {
    GUID hidGuid;
    HidD_GetHidGuid(&hidGuid);
    HDEVINFO set = SetupDiGetClassDevsW(&hidGuid, nullptr, nullptr, DIGCF_PRESENT | DIGCF_DEVICEINTERFACE);
    if (set == INVALID_HANDLE_VALUE) return false;

    std::wstring match = Widen(g_match);
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
        if (!isController || !ContainsNoCase(d.name, match)) {
            d.Close();
            continue;
        }

        USHORT n = d.caps.NumberInputValueCaps;
        std::vector<HIDP_VALUE_CAPS> vcaps(n);
        if (n && HidP_GetValueCaps(HidP_Input, vcaps.data(), &n, d.preparsed) == HIDP_STATUS_SUCCESS) {
            for (USHORT k = 0; k < n; ++k) {
                const auto& v = vcaps[k];
                bool hasX = v.UsagePage == 0x01 &&
                            (v.IsRange ? (v.Range.UsageMin <= 0x30 && v.Range.UsageMax >= 0x30) : v.NotRange.Usage == 0x30);
                if (hasX) {
                    d.xCaps = v;
                    dev = d;
                    ok = true;
                    break;
                }
            }
        }
        if (!ok) d.Close();
    }
    SetupDiDestroyDeviceInfoList(set);
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

void ReaderThread() {
    OpenDevice dev;
    DWORD lastSearch = 0;
    while (g_run) {
        if (dev.handle == INVALID_HANDLE_VALUE) {
            g_found = false;
            DWORD now = GetTickCount();
            if (lastSearch && now - lastSearch < 3000) {
                Sleep(200);
                continue;
            }
            lastSearch = now;
            if (FindDevice(dev)) {
                Log("Wheel base found: %s (axis %d..%d, %u bits)", Narrow(dev.name).c_str(),
                    (int)dev.xCaps.LogicalMin, (int)dev.xCaps.LogicalMax, (unsigned)dev.xCaps.BitSize);
            } else {
                static bool warned = false;
                if (!warned) Log("No wheel base matching \"%s\" yet - will keep looking", g_match.c_str());
                warned = true;
                continue;
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
                continue; // no change from the wheel; try again
            }
        }
        CloseHandle(ov.hEvent);
        if (!done) {
            Log("Lost contact with wheel base - will reconnect");
            dev.Close();
            continue;
        }

        ULONG raw = 0;
        if (HidP_GetUsageValue(HidP_Input, 0x01, 0, 0x30, &raw, dev.preparsed, reinterpret_cast<PCHAR>(report.data()),
                               (ULONG)report.size()) == HIDP_STATUS_SUCCESS) {
            g_value = Normalise(dev, raw);
            g_found = true;
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

void StopSteeringReader() {
    if (!g_run) return;
    g_run = false;
    if (g_thread.joinable()) g_thread.join();
}

bool GetSteeringNormalised(float& out) {
    if (!g_found) return false;
    out = g_value;
    return true;
}

} // namespace hv
