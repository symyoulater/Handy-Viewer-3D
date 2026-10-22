// Thin Place VR - Check
// Shows which OpenXR runtime is active, which API layers Windows has registered, and whether
// Thin Place VR loads when a program starts OpenXR. Writes the report to
// C:\ProgramData\Thin Place VR\check.txt and shows it on screen.

#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>

#include <openxr/openxr.h>

#include <string>
#include <vector>

namespace {

std::wstring g_report;

void Line(const std::wstring& s) { g_report += s + L"\r\n"; }

std::wstring Widen(const char* s) {
    int n = MultiByteToWideChar(CP_UTF8, 0, s, -1, nullptr, 0);
    std::wstring w(n > 0 ? n - 1 : 0, L'\0');
    if (n > 1) MultiByteToWideChar(CP_UTF8, 0, s, -1, w.data(), n);
    return w;
}

std::wstring ResultName(XrResult r) {
    switch (r) {
    case XR_SUCCESS: return L"success";
    case XR_ERROR_RUNTIME_UNAVAILABLE: return L"runtime unavailable (is Virtual Desktop / the headset connected?)";
    case XR_ERROR_RUNTIME_FAILURE: return L"runtime failure";
    case XR_ERROR_INSTANCE_LOST: return L"instance lost";
    case XR_ERROR_API_LAYER_NOT_PRESENT: return L"API layer not present";
    case XR_ERROR_INITIALIZATION_FAILED: return L"initialisation failed";
    default: return L"error " + std::to_wstring((int)r);
    }
}

void ListRegistry(HKEY root, const wchar_t* rootName, const wchar_t* path) {
    HKEY key;
    if (RegOpenKeyExW(root, path, 0, KEY_READ | KEY_WOW64_64KEY, &key) != ERROR_SUCCESS) {
        Line(std::wstring(L"  ") + rootName + L"\\" + path + L": (none)");
        return;
    }
    Line(std::wstring(L"  ") + rootName + L"\\" + path + L":");
    for (DWORD i = 0;; ++i) {
        wchar_t name[1024];
        DWORD nameLen = 1024, type = 0, data = 0, dataLen = sizeof(data);
        LONG r = RegEnumValueW(key, i, name, &nameLen, nullptr, &type, reinterpret_cast<BYTE*>(&data), &dataLen);
        if (r == ERROR_NO_MORE_ITEMS) break;
        if (r != ERROR_SUCCESS && r != ERROR_MORE_DATA) break;
        bool exists = GetFileAttributesW(name) != INVALID_FILE_ATTRIBUTES;
        std::wstring state = type == REG_DWORD ? (data == 0 ? L"enabled" : L"DISABLED") : L"?";
        Line(std::wstring(L"    [") + state + L"] " + name + (exists ? L"" : L"   <-- FILE MISSING"));
    }
    RegCloseKey(key);
}

std::wstring ReadString(HKEY root, const wchar_t* path, const wchar_t* value) {
    wchar_t buf[2048] = {};
    DWORD len = sizeof(buf);
    if (RegGetValueW(root, path, value, RRF_RT_REG_SZ | RRF_SUBKEY_WOW6464KEY, nullptr, buf, &len) == ERROR_SUCCESS)
        return buf;
    return L"(not set)";
}

} // namespace

int WINAPI wWinMain(HINSTANCE, HINSTANCE, PWSTR, int) {
    Line(L"Thin Place VR - Check");
    Line(L"");
    Line(L"Active OpenXR runtime:");
    Line(L"  " + ReadString(HKEY_LOCAL_MACHINE, L"SOFTWARE\\Khronos\\OpenXR\\1", L"ActiveRuntime"));
    Line(L"");
    Line(L"Registered implicit API layers (load into every OpenXR game):");
    ListRegistry(HKEY_LOCAL_MACHINE, L"HKLM", L"SOFTWARE\\Khronos\\OpenXR\\1\\ApiLayers\\Implicit");
    ListRegistry(HKEY_CURRENT_USER, L"HKCU", L"SOFTWARE\\Khronos\\OpenXR\\1\\ApiLayers\\Implicit");
    Line(L"");

    uint32_t count = 0;
    XrResult r = xrEnumerateApiLayerProperties(0, &count, nullptr);
    std::vector<XrApiLayerProperties> layers(count, {XR_TYPE_API_LAYER_PROPERTIES});
    if (XR_SUCCEEDED(r) && count) r = xrEnumerateApiLayerProperties(count, &count, layers.data());
    Line(L"Layers the OpenXR loader accepts: " + std::to_wstring(count));
    bool found = false;
    for (uint32_t i = 0; i < count; ++i) {
        std::wstring n = Widen(layers[i].layerName);
        if (n == L"XR_APILAYER_SYMYOULATER_thin_place_vr") found = true;
        Line(L"  " + n);
    }
    Line(found ? L"  -> Thin Place VR is accepted by the loader." : L"  -> Thin Place VR is NOT accepted by the loader.");
    Line(L"");

    XrInstanceCreateInfo ci{XR_TYPE_INSTANCE_CREATE_INFO};
    strcpy_s(ci.applicationInfo.applicationName, "Thin Place VR Check");
    ci.applicationInfo.apiVersion = XR_MAKE_VERSION(1, 0, 0);
    XrInstance instance = XR_NULL_HANDLE;
    r = xrCreateInstance(&ci, &instance);
    Line(L"Starting OpenXR: " + ResultName(r));
    if (XR_SUCCEEDED(r)) {
        XrInstanceProperties props{XR_TYPE_INSTANCE_PROPERTIES};
        xrGetInstanceProperties(instance, &props);
        Line(L"  Runtime in use: " + Widen(props.runtimeName));
        xrDestroyInstance(instance);
    }
    Line(L"");
    Line(L"If Thin Place VR loaded, C:\\ProgramData\\Thin Place VR\\layer.log now names this check program.");

    wchar_t pd[MAX_PATH] = {};
    GetEnvironmentVariableW(L"ProgramData", pd, MAX_PATH);
    std::wstring dir = std::wstring(pd) + L"\\Thin Place VR";
    CreateDirectoryW(dir.c_str(), nullptr);
    HANDLE f = CreateFileW((dir + L"\\check.txt").c_str(), GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS, 0, nullptr);
    if (f != INVALID_HANDLE_VALUE) {
        int n = WideCharToMultiByte(CP_UTF8, 0, g_report.c_str(), (int)g_report.size(), nullptr, 0, nullptr, nullptr);
        std::string utf8(n, '\0');
        WideCharToMultiByte(CP_UTF8, 0, g_report.c_str(), (int)g_report.size(), utf8.data(), n, nullptr, nullptr);
        DWORD written = 0;
        WriteFile(f, utf8.data(), (DWORD)utf8.size(), &written, nullptr);
        CloseHandle(f);
    }
    MessageBoxW(nullptr, g_report.c_str(), L"Thin Place VR - Check", MB_OK | MB_ICONINFORMATION);
    return 0;
}
