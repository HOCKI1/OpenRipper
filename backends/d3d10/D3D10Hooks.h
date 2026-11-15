#pragma once
#include <windows.h>
#include <d3d10.h>
#include <dxgi.h>

namespace OpenRipper {
namespace D3D10 {

bool InitializeHooks();
void ShutdownHooks();
void Log(const char* fmt, ...);

} // namespace D3D10
} // namespace OpenRipper
