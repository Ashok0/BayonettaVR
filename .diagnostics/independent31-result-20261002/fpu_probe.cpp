#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <d3d9.h>
#include <float.h>
#include <process.h>
#include <cstdio>
unsigned __stdcall Worker(void*) { unsigned cw; _controlfp_s(&cw,0,0); printf("worker FP control=0x%08x precision=0x%x\n",cw,cw&_MCW_PC); return 0; }
int main() {
    unsigned cw; _controlfp_s(&cw,0,0); printf("initial FP control=0x%08x precision=0x%x\n",cw,cw&_MCW_PC);
    wchar_t path[MAX_PATH]; GetSystemDirectoryW(path,MAX_PATH); wcscat_s(path,L"\\d3d9.dll");
    auto dll=LoadLibraryW(path); auto create=reinterpret_cast<HRESULT(WINAPI*)(UINT,IDirect3D9Ex**)>(GetProcAddress(dll,"Direct3DCreate9Ex"));
    IDirect3D9Ex* d3d=nullptr; if(FAILED(create(D3D_SDK_VERSION,&d3d)))return 1;
    HWND window=CreateWindowExW(0,L"STATIC",L"FPU diagnostic",WS_OVERLAPPED,0,0,64,64,nullptr,nullptr,GetModuleHandle(nullptr),nullptr);
    D3DPRESENT_PARAMETERS p{}; p.Windowed=TRUE;p.SwapEffect=D3DSWAPEFFECT_DISCARD;p.hDeviceWindow=window;p.BackBufferWidth=p.BackBufferHeight=64;p.BackBufferFormat=D3DFMT_X8R8G8B8;
    IDirect3DDevice9Ex* device=nullptr; auto hr=d3d->CreateDeviceEx(0,D3DDEVTYPE_HAL,window,D3DCREATE_HARDWARE_VERTEXPROCESSING,&p,nullptr,&device);
    _controlfp_s(&cw,0,0); printf("after D3D9 hr=0x%08lx FP control=0x%08x precision=0x%x\n",hr,cw,cw&_MCW_PC);
    HANDLE thread=reinterpret_cast<HANDLE>(_beginthreadex(nullptr,0,Worker,nullptr,0,nullptr));WaitForSingleObject(thread,INFINITE);CloseHandle(thread);
    if(device)device->Release();d3d->Release();DestroyWindow(window);return FAILED(hr)?2:0;
}
