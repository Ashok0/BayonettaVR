#pragma once
#include <d3d9.h>

namespace bvr {
void HookDirect3D9Ex(IDirect3D9Ex* d3d);
void ShutdownD3D();
}
