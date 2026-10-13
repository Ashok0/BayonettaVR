#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include "PracticeReflection.h"
#include <wrl/client.h>
#include <fstream>
#include <vector>
#include <cstdio>
#include <cstdlib>
#include <cstring>
using namespace bvr;
using Microsoft::WRL::ComPtr;
static void Require(bool ok,const char* why){if(!ok){printf("practice reflection FAILED: %s\n",why);exit(1);}}
static void Check(HRESULT hr){if(FAILED(hr)){printf("practice reflection HRESULT %08lX\n",hr);exit(2);}}
int main(){
    HWND window=CreateWindowExW(0,L"STATIC",L"Reflection regression",WS_OVERLAPPEDWINDOW,0,0,64,64,nullptr,nullptr,GetModuleHandleW(nullptr),nullptr);
    ComPtr<IDirect3D9Ex> api;Check(Direct3DCreate9Ex(D3D_SDK_VERSION,&api));
    D3DPRESENT_PARAMETERS pp{};pp.Windowed=TRUE;pp.SwapEffect=D3DSWAPEFFECT_DISCARD;pp.hDeviceWindow=window;
    pp.BackBufferWidth=pp.BackBufferHeight=64;pp.BackBufferFormat=D3DFMT_X8R8G8B8;
    ComPtr<IDirect3DDevice9Ex> d;Check(api->CreateDeviceEx(0,D3DDEVTYPE_HAL,window,D3DCREATE_HARDWARE_VERTEXPROCESSING,&pp,nullptr,&d));
    ComPtr<IDirect3DSurface9> target,readback,depth;
    Check(d->CreateRenderTarget(64,64,D3DFMT_A8R8G8B8,D3DMULTISAMPLE_NONE,0,FALSE,&target,nullptr));
    Check(d->CreateOffscreenPlainSurface(64,64,D3DFMT_A8R8G8B8,D3DPOOL_SYSTEMMEM,&readback,nullptr));
    Check(d->CreateDepthStencilSurface(64,64,D3DFMT_D24S8,D3DMULTISAMPLE_NONE,0,FALSE,&depth,nullptr));
    Check(d->SetRenderTarget(0,target.Get()));Check(d->SetDepthStencilSurface(depth.Get()));
    ComPtr<IDirect3DTexture9> mask,upload,reflection,otherMask;
    Check(d->CreateTexture(4,4,1,0,D3DFMT_DXT5,D3DPOOL_DEFAULT,&mask,nullptr));
    Check(d->CreateTexture(4,4,1,0,D3DFMT_DXT5,D3DPOOL_DEFAULT,&otherMask,nullptr));
    Check(d->CreateTexture(4,4,1,0,D3DFMT_DXT5,D3DPOOL_SYSTEMMEM,&upload,nullptr));
    D3DLOCKED_RECT locked{};Check(upload->LockRect(0,&locked,nullptr,0));
    const unsigned char whiteBlock[16]{255,255,0,0,0,0,0,0,255,255,255,255,0,0,0,0};
    memcpy(locked.pBits,whiteBlock,16);Check(upload->UnlockRect(0));Check(d->UpdateTexture(upload.Get(),mask.Get()));
    Check(d->UpdateTexture(upload.Get(),otherMask.Get()));
    Check(d->CreateTexture(64,64,1,D3DUSAGE_RENDERTARGET,D3DFMT_A8R8G8B8,D3DPOOL_DEFAULT,&reflection,nullptr));
    ComPtr<IDirect3DSurface9> reflectedSurface;Check(reflection->GetSurfaceLevel(0,&reflectedSurface));
    Check(d->ColorFill(reflectedSurface.Get(),nullptr,0xff604020));
    Check(d->SetTexture(0,mask.Get()));Check(d->SetTexture(1,reflection.Get()));Check(d->SetTexture(2,mask.Get()));
    for(unsigned i=0;i<3;++i){
        Check(d->SetSamplerState(i,D3DSAMP_MINFILTER,D3DTEXF_POINT));Check(d->SetSamplerState(i,D3DSAMP_MAGFILTER,D3DTEXF_POINT));
        Check(d->SetSamplerState(i,D3DSAMP_ADDRESSU,D3DTADDRESS_CLAMP));Check(d->SetSamplerState(i,D3DSAMP_ADDRESSV,D3DTADDRESS_CLAMP));
    }
    std::ifstream f("tests/fixtures/practice_reflection/espshadershimmer.pso",std::ios::binary|std::ios::ate);
    Require(f.good(),"native fixture exists");const size_t size=size_t(f.tellg());f.seekg(0);
    std::vector<DWORD> code((size+3)/4);f.read(reinterpret_cast<char*>(code.data()),size);
    Require(IsPracticeReflectionShader(ShaderCrc32(code.data(),size),size),"exact original shader fingerprint");
    Require(!IsPracticeReflectionShader(0xdd5f8df0,size)&&!IsPracticeReflectionShader(0xdd5f8df1,size+4),"near fingerprints rejected");
    ComPtr<IDirect3DPixelShader9> ps;Check(d->CreatePixelShader(code.data(),&ps));Check(d->SetPixelShader(ps.Get()));
    PracticeReflectionFilter filter;Require(filter.Register(ps.Get())==0xdd5f8df1,"native registration");
    Check(d->SetVertexShader(nullptr));Check(d->SetFVF(D3DFVF_XYZRHW|D3DFVF_TEX2|D3DFVF_TEXCOORDSIZE4(1)));
    Check(d->SetRenderState(D3DRS_ZENABLE,TRUE));Check(d->SetRenderState(D3DRS_ZWRITEENABLE,FALSE));
    Check(d->SetRenderState(D3DRS_CULLMODE,D3DCULL_NONE));Check(d->SetRenderState(D3DRS_ALPHABLENDENABLE,TRUE));
    Check(d->SetRenderState(D3DRS_SRCBLEND,D3DBLEND_SRCALPHA));Check(d->SetRenderState(D3DRS_DESTBLEND,D3DBLEND_INVSRCALPHA));
    const float captured[8]{.499999911f,.199999928f,.199999928f,1,-0.f,0,-.5f,-.5f};
    Check(d->SetPixelShaderConstantF(184,captured,2));
    Require(filter.ShouldSkip(d.Get(),2,true),"captured arena card recognized");
    Require(!filter.ShouldSkip(d.Get(),2,false)&&!filter.ShouldSkip(d.Get(),3,true),"disabled VR and other geometry retained");
    // Test independent near matches; ordinary distorted/tinted reflections must remain.
    for(unsigned i=0;i<8;++i){float changed[8];memcpy(changed,captured,sizeof(changed));changed[i]+=.01f;
        Check(d->SetPixelShaderConstantF(184,changed,2));Require(!filter.ShouldSkip(d.Get(),2,true),"other reflection parameters retained");}
    Check(d->SetPixelShaderConstantF(184,captured,2));
    Check(d->SetTexture(2,otherMask.Get()));Require(!filter.ShouldSkip(d.Get(),2,true),"independent mask retained");Check(d->SetTexture(2,mask.Get()));
    Check(d->SetTexture(1,mask.Get()));Require(!filter.ShouldSkip(d.Get(),2,true),"non-render texture retained");Check(d->SetTexture(1,reflection.Get()));
    Check(d->SetRenderState(D3DRS_DESTBLEND,D3DBLEND_ONE));Require(!filter.ShouldSkip(d.Get(),2,true),"additive particles retained");Check(d->SetRenderState(D3DRS_DESTBLEND,D3DBLEND_INVSRCALPHA));
    ComPtr<IDirect3DStateBlock9> state;Check(d->CreateStateBlock(D3DSBT_ALL,&state));Check(state->Capture());
    Check(d->SetPixelShader(nullptr));Require(!filter.ShouldSkip(d.Get(),2,true),"unrelated shader retained");Check(state->Apply());
    Require(filter.ShouldSkip(d.Get(),2,true),"state-block binding recognized");
    const float quad[][10]={{-.5f,-.5f,.5f,1,0,0,-1,1,.5f,1},{63.5f,-.5f,.5f,1,1,0,1,1,.5f,1},
                          {-.5f,63.5f,.5f,1,0,1,-1,-1,.5f,1},{63.5f,63.5f,.5f,1,1,1,1,-1,.5f,1}};
    for(int enabled=0;enabled<2;++enabled){
        Check(d->Clear(0,nullptr,D3DCLEAR_TARGET|D3DCLEAR_ZBUFFER,0xff112233,1,0));Check(d->BeginScene());
        if(!filter.ShouldSkip(d.Get(),2,enabled!=0))Check(d->DrawPrimitiveUP(D3DPT_TRIANGLESTRIP,2,quad,sizeof(quad[0])));
        Check(d->EndScene());Check(d->GetRenderTargetData(target.Get(),readback.Get()));Check(readback->LockRect(&locked,nullptr,D3DLOCK_READONLY));
        for(int y=0;y<64;++y)for(int x=0;x<64;++x){const DWORD pixel=reinterpret_cast<DWORD*>(static_cast<char*>(locked.pBits)+y*locked.Pitch)[x];
            Require(enabled?pixel==0xff112233:pixel!=0xff112233,"suppression preserves scene pixels; original shader draws when disabled");}
        Check(readback->UnlockRect());
    }
    float restored[8]{};Check(d->GetPixelShaderConstantF(184,restored,2));Require(!memcmp(restored,captured,sizeof(captured)),"game constants untouched");
    std::vector<DWORD> preserved(code.size());UINT bytes=UINT(size);Check(ps->GetFunction(preserved.data(),&bytes));Require(preserved==code,"native bytecode unchanged");
    filter.Reset();Require(!filter.ShouldSkip(d.Get(),2,true),"reset removes stale identities");DestroyWindow(window);
    puts("practice reflection: exact native shader, captured signature, negative cases, state-block binding, unchanged bytecode/constants and 8192 GPU pixel checks passed");
}
