#pragma once
#include <windows.h>
#include <d3d11.h>
#include <dxgi.h>

namespace OpenRipper {
namespace D3D11 {

bool InitializeHooks();
void ShutdownHooks();
void Log(const char* fmt, ...);

} // namespace D3D11
} // namespace OpenRipper
