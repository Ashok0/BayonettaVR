#include "CameraCulling.h"
#include <windows.h>
#include <cassert>
#include <cstdio>
#include <cstring>
#include <limits>
#include <random>

namespace bvr { void Log(const char*,...) {} }
namespace {
bool enabled=true;
bool Enabled(){return enabled;}
using Check=int(__thiscall*)(void*,const float*,float);
using RawCheck=unsigned char(__thiscall*)(void*,const float*,float);
void Load(unsigned char* destination,const char* name,size_t length) {
    char path[256]{};sprintf_s(path,"tests/fixtures/culling/%s.bin",name);
    FILE* file=nullptr;fopen_s(&file,path,"rb");assert(file);
    assert(fread(destination,1,length,file)==length);fclose(file);
}
void Init(bvr::SceneFrustum& f) {
    f={};f.aspect=1.7777778f;
    f.cosY=std::cos(0.6f);f.sinY=std::sin(0.6f);
    const float halfX=std::atan(f.aspect*std::tan(0.6f));
    f.cosX=std::cos(halfX);f.sinX=std::sin(halfX);
    f.nearZ=1;f.farZ=100;f.direction[2]=1;
    f.normals[0][0]= f.cosX;f.normals[0][2]=-f.sinX;
    f.normals[1][0]=-f.cosX;f.normals[1][2]=-f.sinX;
    f.normals[2][1]= f.cosY;f.normals[2][2]=-f.sinY;
    f.normals[3][1]=-f.cosY;f.normals[3][2]=-f.sinY;
}
}
int main() {
    // Run the actual Steam x86 routines at their relative image offsets, so
    // calling convention, trampoline replay and relative call are all tested.
    auto* image=static_cast<unsigned char*>(VirtualAlloc(nullptr,0x59B8000,
        MEM_RESERVE|MEM_COMMIT,PAGE_EXECUTE_READWRITE));assert(image);
    auto* dos=reinterpret_cast<IMAGE_DOS_HEADER*>(image);
    dos->e_magic=IMAGE_DOS_SIGNATURE;dos->e_lfanew=0x100;
    auto* nt=reinterpret_cast<IMAGE_NT_HEADERS*>(image+0x100);
    nt->Signature=IMAGE_NT_SIGNATURE;nt->FileHeader.Machine=IMAGE_FILE_MACHINE_I386;
    nt->FileHeader.TimeDateStamp=0x591AD330;nt->OptionalHeader.SizeOfImage=0x59B8000;
    Load(image+0x11AF40,"check",183);Load(image+0x6AA720,"wrapper",31);
    Load(image+0x11B000,"shadow",216);
    const unsigned char getter[]={0x8B,0x41,0x08,0xC3};
    memcpy(image+0x6AA2C0,getter,sizeof(getter));
    FlushInstructionCache(GetCurrentProcess(),image,0x59B8000);
    auto check=reinterpret_cast<Check>(image+0x6AA720);
    auto original=reinterpret_cast<RawCheck>(image+0x11AF40);
    auto shadow=reinterpret_cast<RawCheck>(image+0x11B000);
    auto* main=image+0xB2E0D0;
    auto& f=*reinterpret_cast<bvr::SceneFrustum*>(main+0x10);Init(f);
    auto* alternate=main+0x1000;
    auto& other=*reinterpret_cast<bvr::SceneFrustum*>(alternate+0x10);Init(other);
    auto** active=reinterpret_cast<unsigned char**>(image+0x5729058);
    *active=main-0x5F0;
    float front[]={0,0,20},back[]={0,0,-20},side[]={20,0,0},distant[]={0,0,-1000};
    assert(check(main,front,1)==1 && check(main,back,1)==0 && check(main,side,1)==0);
    assert(shadow(&f,back,1)==0);
    // Unsupported binaries and signature mismatches cannot be patched.
    nt->FileHeader.TimeDateStamp=0;assert(!bvr::InstallCameraCullingHook(image,Enabled));
    nt->FileHeader.TimeDateStamp=0x591AD330;
    image[0x6AA720]^=1;assert(!bvr::InstallCameraCullingHook(image,Enabled));image[0x6AA720]^=1;
    image[0x6AA2C0]^=1;assert(!bvr::InstallCameraCullingHook(image,Enabled));image[0x6AA2C0]^=1;
    assert(bvr::InstallCameraCullingHook(image,Enabled));
    assert(bvr::InstallCameraCullingHook(image,Enabled));
    assert(check(main,front,1)==1 && check(main,back,1)==1 && check(main,side,1)==1);
    assert(check(main,distant,1)==0 && shadow(&f,back,1)==0);
    assert(check(alternate,back,1)==0); // Unrelated cameras remain unchanged.
    *active=alternate-0x5F0;assert(check(alternate,back,1)==1);
    *active=nullptr;assert(check(alternate,back,1)==0 && check(main,back,1)==1);
    enabled=false;assert(check(main,back,1)==0);enabled=true;
    // Orbit through every yaw/pitch: no disappearance outside the old cone.
    for(int yaw=0;yaw<360;yaw+=3)for(int pitch=-90;pitch<=90;pitch+=3) {
        const float a=yaw*0.01745329252f,b=pitch*0.01745329252f;
        float point[]={80*std::cos(b)*std::sin(a),80*std::sin(b),80*std::cos(b)*std::cos(a)};
        assert(check(main,point,0)==1);
    }
    // The enclosing range includes the original far corners, with radius.
    std::mt19937 rng(54);std::uniform_real_distribution<float> pos(-250,250),rad(0,20);
    for(int i=0;i<100000;++i) {
        float point[]={pos(rng),pos(rng),pos(rng)};const float radius=rad(rng);
        const auto before=original(&f,point,radius);
        const int after=check(main,point,radius);
        if(before)assert(after==1 && bvr::SurroundVisible(f,point,radius));
        enabled=false;assert(check(main,point,radius)==before);enabled=true;
    }
    float corner[]={(100*f.sinX+5)/f.cosX,(100*f.sinY+5)/f.cosY,100};
    assert(check(main,corner,5)==1);
    f.position[0]=10;float shifted[]={10,0,-20};assert(check(main,shifted,0)==1);
    f.farZ=std::numeric_limits<float>::quiet_NaN();assert(!bvr::SurroundVisible(f,back,1));
    Init(f);f.cosX=0;assert(!bvr::SurroundVisible(f,back,1));Init(f);
    assert(!bvr::SurroundVisible(f,back,-1));assert(!bvr::SurroundVisible(f,nullptr,1));
    bvr::RemoveCameraCullingHook();bvr::RemoveCameraCullingHook();
    assert(check(main,back,1)==0 && check(main,front,1)==1);
    assert(bvr::InstallCameraCullingHook(image,Enabled));assert(check(main,back,1)==1);
    bvr::RemoveCameraCullingHook();VirtualFree(image,0,MEM_RELEASE);
    puts("Camera culling: real x86 hook/restore, 360-degree visibility, scope, fallback and distance checks passed.");
}
