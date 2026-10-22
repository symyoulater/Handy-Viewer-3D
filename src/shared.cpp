#include "shared.h"
#include "log.h"

#define WIN32_LEAN_AND_MEAN
#include <windows.h>

namespace hv {

namespace {
HANDLE g_mapping = nullptr;
LinkBlock* g_block = nullptr;
} // namespace

LinkBlock* OpenLink() {
    if (g_block) return g_block;
    g_mapping = CreateFileMappingW(INVALID_HANDLE_VALUE, nullptr, PAGE_READWRITE, 0, sizeof(LinkBlock),
                                   L"Local\\ThinPlaceVR.Link");
    if (!g_mapping) {
        Log("Live link to the app unavailable (error %lu)", GetLastError());
        return nullptr;
    }
    bool existed = GetLastError() == ERROR_ALREADY_EXISTS;
    g_block = static_cast<LinkBlock*>(MapViewOfFile(g_mapping, FILE_MAP_ALL_ACCESS, 0, 0, sizeof(LinkBlock)));
    if (!g_block) {
        CloseHandle(g_mapping);
        g_mapping = nullptr;
        return nullptr;
    }
    if (!existed || g_block->magic != kLinkMagic) {
        ZeroMemory(g_block, sizeof(LinkBlock));
        g_block->magic = kLinkMagic;
        g_block->version = kLinkVersion;
    }
    return g_block;
}

void CloseLink() {
    if (g_block) {
        g_block->heartbeatMs = 0;
        UnmapViewOfFile(g_block);
    }
    if (g_mapping) CloseHandle(g_mapping);
    g_block = nullptr;
    g_mapping = nullptr;
}

} // namespace hv
