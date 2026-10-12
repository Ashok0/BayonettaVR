#pragma once
#include "FloorCalibration.h"
#include <windows.h>
#include <cassert>
#include <cstdio>
#include <cstdint>

// Execute the game's actual x86 camera builders, relocating only their
// references to runtime exports and the original +1/-1/half constants.
class GameCameraFixture {
public:
    GameCameraFixture() {
        code_=static_cast<unsigned char*>(VirtualAlloc(nullptr,4096,MEM_COMMIT|MEM_RESERVE,PAGE_EXECUTE_READWRITE));assert(code_);
        d3dx_=LoadLibraryW(L"d3dx9_43.dll");crt_=LoadLibraryW(L"msvcr100.dll");assert(d3dx_&&crt_);
        Load("look_at",0,607);Load("projection",0x400,176);
        auto normalize=GetProcAddress(d3dx_,"D3DXVec3Normalize");assert(normalize);
        for(size_t at:{0x42u,0xC0u,0x13Eu})Call(at,normalize);
        Constant(0x168+4,1);Constant(0x18E+4,-1);
        Constant(0x400+0x7+4,.5f);Constant(0x400+0x91+4,-1);
        Call(0x400+0x16,GetProcAddress(crt_,"__libm_sse2_cosf"));
        Call(0x400+0x27,GetProcAddress(crt_,"__libm_sse2_sinf"));
        Call(0x400+0x72,GetProcAddress(d3dx_,"D3DXMatrixScaling"));
        FlushInstructionCache(GetCurrentProcess(),code_,4096);
    }
    ~GameCameraFixture(){VirtualFree(code_,0,MEM_RELEASE);FreeLibrary(d3dx_);FreeLibrary(crt_);}
    bvr::Mat4 View(const float* eye,const float* target,const float* up) const {
        using Fn=void(__cdecl*)(bvr::Mat4*,const float*,const float*,const float*);
        bvr::Mat4 result{};reinterpret_cast<Fn>(code_)(&result,eye,target,up);return result;
    }
    bvr::Mat4 Projection(float fov,float aspect,float nearZ,float farZ) const {
        using Fn=void(__cdecl*)(bvr::Mat4*,float,float,float,float);
        bvr::Mat4 result{};reinterpret_cast<Fn>(code_+0x400)(&result,fov,aspect,nearZ,farZ);return result;
    }
private:
    void Load(const char* name,size_t offset,size_t length) {
        char path[256]{};sprintf_s(path,"tests/fixtures/calibration/%s.bin",name);
        FILE* f=nullptr;fopen_s(&f,path,"rb");assert(f);
        assert(fread(code_+offset,1,length,f)==length);fclose(f);
    }
    void Call(size_t offset,FARPROC function) {
        assert(function&&code_[offset]==0xE8);
        const auto delta=reinterpret_cast<uintptr_t>(function)-reinterpret_cast<uintptr_t>(code_+offset+5);
        const auto relative=static_cast<int32_t>(delta);memcpy(code_+offset+1,&relative,4);
    }
    void Constant(size_t operand,float value) {
        auto* address=code_+0x800+4*constants_++;memcpy(address,&value,4);
        const auto pointer=reinterpret_cast<uintptr_t>(address);memcpy(code_+operand,&pointer,4);
    }
    unsigned char* code_=nullptr;
    unsigned constants_=0;
    HMODULE d3dx_=nullptr,crt_=nullptr;
};
