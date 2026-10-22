#include "log.h"

#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <shlobj.h>

#include <cstdarg>
#include <cstdio>
#include <mutex>

namespace hv {

namespace {
std::mutex g_logMutex;
bool g_logStarted = false;
} // namespace

std::string Narrow(const std::wstring& w) {
    if (w.empty()) return {};
    int n = WideCharToMultiByte(CP_UTF8, 0, w.c_str(), (int)w.size(), nullptr, 0, nullptr, nullptr);
    std::string s(n, '\0');
    WideCharToMultiByte(CP_UTF8, 0, w.c_str(), (int)w.size(), s.data(), n, nullptr, nullptr);
    return s;
}

std::wstring Widen(const std::string& s) {
    if (s.empty()) return {};
    int n = MultiByteToWideChar(CP_UTF8, 0, s.c_str(), (int)s.size(), nullptr, 0);
    std::wstring w(n, L'\0');
    MultiByteToWideChar(CP_UTF8, 0, s.c_str(), (int)s.size(), w.data(), n);
    return w;
}

const std::wstring& DataDir() {
    static std::wstring dir = [] {
        PWSTR base = nullptr;
        std::wstring d;
        if (SUCCEEDED(SHGetKnownFolderPath(FOLDERID_LocalAppData, 0, nullptr, &base))) {
            d = std::wstring(base) + L"\\HandyViewer3D";
            CoTaskMemFree(base);
        } else {
            d = L"C:\\HandyViewer3D";
        }
        CreateDirectoryW(d.c_str(), nullptr);
        CreateDirectoryW((d + L"\\shapes").c_str(), nullptr);
        return d;
    }();
    return dir;
}

const std::wstring& ModuleDir() {
    static std::wstring dir = [] {
        HMODULE mod = nullptr;
        GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                           reinterpret_cast<LPCWSTR>(&ModuleDir), &mod);
        wchar_t path[MAX_PATH] = {};
        GetModuleFileNameW(mod, path, MAX_PATH);
        std::wstring p(path);
        auto slash = p.find_last_of(L"\\/");
        return slash == std::wstring::npos ? std::wstring(L".") : p.substr(0, slash);
    }();
    return dir;
}

void Log(const char* fmt, ...) {
    char msg[2048];
    va_list args;
    va_start(args, fmt);
    vsnprintf(msg, sizeof(msg), fmt, args);
    va_end(args);

    SYSTEMTIME t;
    GetLocalTime(&t);

    std::lock_guard<std::mutex> lock(g_logMutex);
    std::wstring path = DataDir() + L"\\layer.log";
    // Start a fresh log for each game launch so it stays short and readable.
    FILE* f = _wfopen(path.c_str(), g_logStarted ? L"a" : L"w");
    g_logStarted = true;
    if (!f) return;
    fprintf(f, "%02d:%02d:%02d.%03d  %s\n", t.wHour, t.wMinute, t.wSecond, t.wMilliseconds, msg);
    fclose(f);
    OutputDebugStringA(msg);
    OutputDebugStringA("\n");
}

} // namespace hv
