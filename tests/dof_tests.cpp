#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include "DepthOfField.h"
#include <d3dcompiler.h>
#include <wrl/client.h>
#include <vector>
#include <fstream>
#include <cstdio>
#include <cstdlib>
#include <cstring>
using namespace bvr;
using Microsoft::WRL::ComPtr;
static void Require(bool ok,const char* message){if(!ok){printf("DOF failed: %s\n",message);exit(1);}}
static void Check(HRESULT hr){if(FAILED(hr)){printf("DOF HRESULT=%08lX\n",hr);exit(2);}}
static HRESULT STDMETHODCALLTYPE Create(IDirect3DDevice9* d,const DWORD* code,IDirect3DPixelShader9** out){return d->CreatePixelShader(code,out);}
static std::vector<DWORD> Bytes(IDirect3DPixelShader9* shader){UINT n=0;Check(shader->GetFunction(nullptr,&n));std::vector<DWORD> v(n/4);Check(shader->GetFunction(v.data(),&n));return v;}
int main(){
    Require(ShaderCrc32("123456789",9)==0xcbf43926,"standard CRC32");
    HWND window=CreateWindowExW(0,L"STATIC",L"DOF regression",WS_OVERLAPPEDWINDOW,0,0,32,32,nullptr,nullptr,GetModuleHandleW(nullptr),nullptr);
    Require(window!=nullptr,"window");
    ComPtr<IDirect3D9Ex> d3d;Check(Direct3DCreate9Ex(D3D_SDK_VERSION,&d3d));
    D3DPRESENT_PARAMETERS pp{};pp.Windowed=TRUE;pp.SwapEffect=D3DSWAPEFFECT_DISCARD;pp.hDeviceWindow=window;
    pp.BackBufferWidth=pp.BackBufferHeight=32;pp.BackBufferFormat=D3DFMT_X8R8G8B8;
    pp.EnableAutoDepthStencil=TRUE;pp.AutoDepthStencilFormat=D3DFMT_D24S8;
    ComPtr<IDirect3DDevice9Ex> d;Check(d3d->CreateDeviceEx(0,D3DDEVTYPE_HAL,window,D3DCREATE_HARDWARE_VERTEXPROCESSING,&pp,nullptr,&d));
    ComPtr<IDirect3DSurface9> back,read;Check(d->GetBackBuffer(0,0,D3DBACKBUFFER_TYPE_MONO,&back));
    Check(d->CreateOffscreenPlainSurface(32,32,D3DFMT_X8R8G8B8,D3DPOOL_SYSTEMMEM,&read,nullptr));
    const char solid[]="float4 main():COLOR {return float4(1,0,0,1);}";
    ComPtr<ID3DBlob> compiled;Check(D3DCompile(solid,sizeof(solid)-1,nullptr,nullptr,nullptr,"main","ps_3_0",0,0,&compiled,nullptr));
    DepthOfFieldFilter filter;ComPtr<IDirect3DPixelShader9> red;
    Check(Create(d.Get(),static_cast<const DWORD*>(compiled->GetBufferPointer()),&red));
    Require(!filter.Register(red.Get()),"ordinary shader is not filtered");
    auto draw=[&](IDirect3DPixelShader9* shader,float depth){
        const float quad[][4]={{-.5f,-.5f,depth,1},{31.5f,-.5f,depth,1},{-.5f,31.5f,depth,1},{31.5f,31.5f,depth,1}};
        d->SetPixelShader(shader);d->SetVertexShader(nullptr);d->SetFVF(D3DFVF_XYZRHW);
        d->SetRenderState(D3DRS_CULLMODE,D3DCULL_NONE);d->SetRenderState(D3DRS_ALPHABLENDENABLE,FALSE);
        d->SetRenderState(D3DRS_ZENABLE,TRUE);d->SetRenderState(D3DRS_ZWRITEENABLE,TRUE);d->SetRenderState(D3DRS_ZFUNC,D3DCMP_LESS);
        if(!filter.ShouldSkip(d.Get(),true))Check(d->DrawPrimitiveUP(D3DPT_TRIANGLESTRIP,2,quad,sizeof(quad[0])));
    };
    auto pixels=[&](DWORD expected){
        Check(d->GetRenderTargetData(back.Get(),read.Get()));D3DLOCKED_RECT map{};Check(read->LockRect(&map,nullptr,D3DLOCK_READONLY));
        bool equal=true;for(int y=0;y<32;++y)for(int x=0;x<32;++x){const auto row=reinterpret_cast<const DWORD*>(static_cast<const char*>(map.pBits)+y*map.Pitch);if((row[x]&0xffffff)!=expected)equal=false;}
        read->UnlockRect();Require(equal,"all pixels preserve expected scene/depth");
    };
    const uint32_t hashes[]={0xb959e4d7,0x548c7346,0xfc2a113c,0xcb1d0471};
    for(const auto hash:hashes){
        char path[128]{};sprintf_s(path,"tests/fixtures/dof/%08X.bin",hash);
        std::ifstream f(path,std::ios::binary|std::ios::ate);Require(f.good(),"original shader fixture");const size_t n=size_t(f.tellg());f.seekg(0);
        std::vector<DWORD> original(n/4);f.read(reinterpret_cast<char*>(original.data()),n);
        Require(IsDepthOfFieldShader(ShaderCrc32(original.data(),n),n),"exact game shader hash and size");
        Require(!IsDepthOfFieldShader(hash,n+4)&&!IsDepthOfFieldShader(hash^1,n),"near match rejected");
        ComPtr<IDirect3DPixelShader9> filtered;
        Check(Create(d.Get(),original.data(),&filtered));
        Require(filter.Register(filtered.Get())==hash,"original shader identified");
        Require(Bytes(filtered.Get())==original,"shader bytecode and metadata remain unchanged");
        d->SetPixelShader(filtered.Get());
        Require(!filter.ShouldSkip(d.Get(),false),"setting off retains original draw");
        ComPtr<IDirect3DStateBlock9> state;
        Check(d->CreateStateBlock(D3DSBT_ALL,&state));
        d->SetPixelShader(red.Get());Require(!filter.ShouldSkip(d.Get(),true),"ordinary draw not skipped");
        Check(state->Apply());Require(filter.ShouldSkip(d.Get(),true),"state-block restored DOF is detected");
        for(int eye=0;eye<2;++eye){
            const DWORD background=eye?0x00204080:0x00804020;
            Check(d->Clear(0,nullptr,D3DCLEAR_TARGET|D3DCLEAR_ZBUFFER,background,1,0));
            Check(d->BeginScene());draw(filtered.Get(),.25f);Check(d->EndScene());pixels(background);
            Check(d->BeginScene());draw(red.Get(),.75f);Check(d->EndScene());pixels(0xff0000);
        }
    }
    d->SetPixelShader(nullptr);DestroyWindow(window);
    puts("DOF: four actual game shaders matched; 8 eye render cases preserve scene/depth; original shader identity/metadata, setting-off, unrelated shaders, near matches and state-block restoration passed");
}
