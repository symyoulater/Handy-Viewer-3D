// Handy Viewer 3D - simple file logger.
// Writes to %LOCALAPPDATA%\HandyViewer3D\layer.log so problems can be diagnosed
// without a debugger attached to the game.
#pragma once

#include <string>

namespace hv {

// Folder where config, log and user shapes live (created on first use).
const std::wstring& DataDir();

// Folder the layer DLL was loaded from (holds installed default shapes).
const std::wstring& ModuleDir();

void Log(const char* fmt, ...);

std::string Narrow(const std::wstring& w);
std::wstring Widen(const std::string& s);

} // namespace hv
