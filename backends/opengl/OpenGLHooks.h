#pragma once
#include <windows.h>

namespace OpenRipper {
namespace OpenGL {

void InitializeHooks(HMODULE hModule);
void ShutdownHooks();

} // namespace OpenGL
} // namespace OpenRipper
