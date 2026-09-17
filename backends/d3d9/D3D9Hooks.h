#pragma once
#include <windows.h>
#include <d3d9.h>

namespace OpenRipper {
namespace D3D9 {

bool InitializeHooks();
void ShutdownHooks();

} // namespace D3D9
} // namespace OpenRipper
