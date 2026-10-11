#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include "MapCinema9.h"
#include <wrl/client.h>
#include <fstream>
#include <cstdio>
using namespace bvr;
using Microsoft::WRL::ComPtr;
static void Require(bool ok,const char* why){if(!ok){printf("map cinema FAILED: %s\n",why);exit(1);}}
static void Check(HRESULT hr){if(FAILED(hr)){printf("map cinema HRESULT %08lX\n",hr);exit(2);}}
int main(){
    HWND window=CreateWindowExW(0,L"STATIC",L"Map cinema checks",WS_OVERLAPPEDWINDOW,0,0,640,360,nullptr,nullptr,GetModuleHandleW(nullptr),nullptr);
    ComPtr<IDirect3D9Ex> api;Check(Direct3DCreate9Ex(D3D_SDK_VERSION,&api));
    D3DPRESENT_PARAMETERS pp{};pp.Windowed=TRUE;pp.SwapEffect=D3DSWAPEFFECT_DISCARD;pp.hDeviceWindow=window;
    pp.BackBufferWidth=640;pp.BackBufferHeight=360;pp.BackBufferFormat=D3DFMT_X8R8G8B8;
    ComPtr<IDirect3DDevice9Ex> d;Check(api->CreateDeviceEx(0,D3DDEVTYPE_HAL,window,D3DCREATE_HARDWARE_VERTEXPROCESSING,&pp,nullptr,&d));
    ComPtr<IDirect3DSurface9> target,secondary,read;
    Check(d->CreateRenderTarget(640,360,D3DFMT_A8R8G8B8,D3DMULTISAMPLE_NONE,0,FALSE,&target,nullptr));
    Check(d->CreateRenderTarget(640,360,D3DFMT_A8R8G8B8,D3DMULTISAMPLE_NONE,0,FALSE,&secondary,nullptr));
    Check(d->CreateOffscreenPlainSurface(640,360,D3DFMT_A8R8G8B8,D3DPOOL_SYSTEMMEM,&read,nullptr));
    Check(d->SetRenderTarget(0,target.Get()));Check(d->SetRenderTarget(1,secondary.Get()));
    Check(d->Clear(0,nullptr,D3DCLEAR_TARGET,0xff405060,1,0));
    const D3DVIEWPORT9 viewport{17,23,600,300,0.1f,0.9f};const RECT scissor{22,28,310,200};
    Check(d->SetViewport(&viewport));Check(d->SetScissorRect(&scissor));Check(d->SetRenderState(D3DRS_SCISSORTESTENABLE,TRUE));
    Check(d->SetRenderState(D3DRS_CULLMODE,D3DCULL_CW));
    ComPtr<IDirect3DTexture9> reference;
    Check(d->CreateTexture(256,144,1,D3DUSAGE_DYNAMIC,D3DFMT_A8R8G8B8,D3DPOOL_DEFAULT,&reference,nullptr));
    std::ifstream bmp("tests/fixtures/map/preview.bmp",std::ios::binary);Require(bool(bmp),"captured map fixture");
    BITMAPFILEHEADER file{};BITMAPINFOHEADER info{};bmp.read(reinterpret_cast<char*>(&file),sizeof(file));bmp.read(reinterpret_cast<char*>(&info),sizeof(info));
    Require(file.bfType==0x4d42 && info.biWidth==256 && info.biHeight==-144,"reference dimensions");bmp.seekg(file.bfOffBits);
    D3DLOCKED_RECT locked{};Check(reference->LockRect(0,&locked,nullptr,D3DLOCK_DISCARD));
    for(int y=0;y<144;++y)bmp.read(static_cast<char*>(locked.pBits)+y*locked.Pitch,256*4);
    Require(bool(bmp),"complete reference pixels");Check(reference->UnlockRect(0));
    Check(d->SetTexture(0,reference.Get()));Check(d->BeginScene());
    MapCinema9 classifier;uint32_t hash=0;
    Check(classifier.Fingerprint(d.Get(),reference.Get(),hash));Require(hash==MapCinema9::MapFingerprint,"captured map pixels reproduce production fingerprint");
    classifier.BeginFrame();classifier.Observe(d.Get(),2);Require(!classifier.Resolve(d.Get()),"thumbnail dimensions cannot trigger scene recognition");classifier.EndFrame();
    // Same dimensions/format as the map, entirely different pixels. The
    // production matcher must reject it; an injected test hash exercises the
    // same positive path without needing to redistribute the full game asset.
    ComPtr<IDirect3DTexture9> candidate;
    Check(d->CreateTexture(4096,2048,1,D3DUSAGE_DYNAMIC,D3DFMT_DXT1,D3DPOOL_DEFAULT,&candidate,nullptr));
    Check(candidate->LockRect(0,&locked,nullptr,D3DLOCK_DISCARD));
    for(int y=0;y<512;++y)for(int x=0;x<1024;++x){
        auto* block=reinterpret_cast<DWORD*>(static_cast<char*>(locked.pBits)+y*locked.Pitch+x*8);block[0]=0xf800f800;block[1]=0;
    }
    Check(candidate->UnlockRect(0));Check(d->SetTexture(0,candidate.Get()));
    classifier.BeginFrame();classifier.Observe(d.Get(),2);classifier.Observe(d.Get(),2);
    Require(!classifier.Resolve(d.Get()),"unrelated full-size DXT1 asset rejected");classifier.EndFrame();
    Require(classifier.Samples()==1 && !classifier.Active(),"duplicate candidate sampled once");
    std::vector<DWORD> red(256*144,0xffff0000);MapCinema9 positive(ShaderCrc32(red.data(),red.size()*4));
    positive.BeginFrame();positive.Observe(d.Get(),2);Require(positive.Resolve(d.Get()),"matching pixels activate on entering frame");positive.EndFrame();
    Require(positive.Active() && positive.Samples()==0,"resource identity caches actual fingerprint across frames");
    for(int i=0;i<100;++i){positive.BeginFrame();positive.Observe(d.Get(),2);Require(positive.Resolve(d.Get()),"map remains recognized");positive.EndFrame();}
    Require(positive.Active() && positive.Samples()==0,"no repeated readback during sequence");
    positive.BeginFrame();Require(!positive.Resolve(d.Get()),"absence detected in ending frame");positive.EndFrame();Require(!positive.Active(),"native stereo resumes after absence");
    positive.BeginFrame();positive.Observe(d.Get(),3);Require(!positive.Resolve(d.Get()),"unrelated geometry rejected");positive.EndFrame();
    Check(d->EndScene());
    ComPtr<IDirect3DSurface9> restored0,restored1;Check(d->GetRenderTarget(0,&restored0));Check(d->GetRenderTarget(1,&restored1));
    Require(restored0.Get()==target.Get() && restored1.Get()==secondary.Get(),"both target bindings preserved");
    D3DVIEWPORT9 restoredVp{};RECT restoredScissor{};DWORD cull=0,scissorEnabled=0;
    Check(d->GetViewport(&restoredVp));Check(d->GetScissorRect(&restoredScissor));Check(d->GetRenderState(D3DRS_CULLMODE,&cull));Check(d->GetRenderState(D3DRS_SCISSORTESTENABLE,&scissorEnabled));
    Require(!memcmp(&restoredVp,&viewport,sizeof(viewport)) && !memcmp(&restoredScissor,&scissor,sizeof(scissor)) && cull==D3DCULL_CW && scissorEnabled,"viewport, scissor, cull restored");
    for(auto* surface:{target.Get(),secondary.Get()}){
        Check(d->GetRenderTargetData(surface,read.Get()));Check(read->LockRect(&locked,nullptr,D3DLOCK_READONLY));
        for(int y=0;y<360;++y)for(int x=0;x<640;++x)Require(reinterpret_cast<DWORD*>(static_cast<char*>(locked.pBits)+y*locked.Pitch)[x]==0xff405060,"scene images unchanged");
        Check(read->UnlockRect());
    }
    positive.Reset();classifier.Reset();Require(!positive.Active() && !classifier.Active(),"reset clears recognition");DestroyWindow(window);
    printf("map cinema: captured fingerprint, same-size nonmatch, entry/100 cached frames/exit, geometry exclusion, MRT pixels/bindings and state preservation passed\n");
}
