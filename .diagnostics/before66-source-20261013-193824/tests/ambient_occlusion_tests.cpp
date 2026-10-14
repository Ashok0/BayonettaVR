#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include "AmbientOcclusion.h"
#include <wrl/client.h>
#include <fstream>
#include <vector>
#include <cstdio>
#include <cstdlib>
#include <cstring>
using namespace bvr;
using Microsoft::WRL::ComPtr;
static void Check(HRESULT hr){if(FAILED(hr)){printf("AO HRESULT=%08lX\n",hr);exit(1);}}
static void Require(bool yes,const char* label){if(!yes){printf("AO failed: %s\n",label);exit(2);}}
int main(){
    HWND window=CreateWindowExW(0,L"STATIC",L"AO regression",WS_OVERLAPPEDWINDOW,0,0,32,32,nullptr,nullptr,GetModuleHandleW(nullptr),nullptr);
    ComPtr<IDirect3D9Ex> api;Check(Direct3DCreate9Ex(D3D_SDK_VERSION,&api));
    D3DPRESENT_PARAMETERS pp{};pp.Windowed=TRUE;pp.SwapEffect=D3DSWAPEFFECT_DISCARD;pp.hDeviceWindow=window;
    pp.BackBufferWidth=pp.BackBufferHeight=32;pp.BackBufferFormat=D3DFMT_X8R8G8B8;
    ComPtr<IDirect3DDevice9Ex> d;Check(api->CreateDeviceEx(0,D3DDEVTYPE_HAL,window,D3DCREATE_HARDWARE_VERTEXPROCESSING,&pp,nullptr,&d));
    std::ifstream f("tests/fixtures/ambient_occlusion/46871F68.bin",std::ios::binary|std::ios::ate);Require(f.good(),"fixture");
    const size_t size=size_t(f.tellg());f.seekg(0);std::vector<DWORD> original(size/4);f.read(reinterpret_cast<char*>(original.data()),size);
    Require(IsAmbientOcclusionShader(ShaderCrc32(original.data(),size),size),"exact shader match");
    Require(!IsAmbientOcclusionShader(0x46871f68,size+4)&&!IsAmbientOcclusionShader(0x46871f69,size),"reject near matches");
    ComPtr<IDirect3DPixelShader9> ps;Check(d->CreatePixelShader(original.data(),&ps));
    PixelShaderDrawFilter classifier(IsAmbientOcclusionShader);Require(classifier.Register(ps.Get())==0x46871f68,"registered");
    Check(d->SetPixelShader(ps.Get()));ComPtr<IDirect3DStateBlock9> block;Check(d->CreateStateBlock(D3DSBT_PIXELSTATE,&block));
    Check(d->SetPixelShader(nullptr));Require(!classifier.ShouldSkip(d.Get(),true),"unrelated binding");
    Check(block->Apply());Require(classifier.ShouldSkip(d.Get(),true),"state-block binding detected");
    ComPtr<IDirect3DSurface9> target,read;
    Check(d->CreateRenderTarget(32,32,D3DFMT_A8R8G8B8,D3DMULTISAMPLE_NONE,0,FALSE,&target,nullptr));
    Check(d->CreateOffscreenPlainSurface(32,32,D3DFMT_A8R8G8B8,D3DPOOL_SYSTEMMEM,&read,nullptr));
    Check(d->SetRenderTarget(0,target.Get()));
    ComPtr<IDirect3DTexture9> color,normals,depth;
    auto texture=[&](D3DFORMAT fmt,IDirect3DTexture9** out){Check(d->CreateTexture(32,32,1,D3DUSAGE_DYNAMIC,fmt,D3DPOOL_DEFAULT,out,nullptr));};
    texture(D3DFMT_A8R8G8B8,&color);texture(D3DFMT_A8R8G8B8,&normals);texture(D3DFMT_R32F,&depth);
    DWORD expected[1024]{};D3DLOCKED_RECT locked{};
    Check(color->LockRect(0,&locked,nullptr,D3DLOCK_DISCARD));
    for(int y=0;y<32;++y)for(int x=0;x<32;++x){const DWORD pixel=(DWORD(40+y*5)<<24)|(DWORD(20+x*6)<<16)|(DWORD(30+y*5)<<8)|DWORD(40+(x+y)*2);expected[y*32+x]=pixel;reinterpret_cast<DWORD*>(static_cast<char*>(locked.pBits)+y*locked.Pitch)[x]=pixel;}
    color->UnlockRect(0);
    Check(normals->LockRect(0,&locked,nullptr,D3DLOCK_DISCARD));
    for(int y=0;y<32;++y)for(int x=0;x<32;++x)reinterpret_cast<DWORD*>(static_cast<char*>(locked.pBits)+y*locked.Pitch)[x]=0xff8080ff;
    normals->UnlockRect(0);
    Check(d->SetTexture(0,color.Get()));Check(d->SetTexture(1,normals.Get()));Check(d->SetTexture(2,depth.Get()));
    for(int i=0;i<3;++i){d->SetSamplerState(i,D3DSAMP_MINFILTER,D3DTEXF_POINT);d->SetSamplerState(i,D3DSAMP_MAGFILTER,D3DTEXF_POINT);d->SetSamplerState(i,D3DSAMP_SRGBTEXTURE,FALSE);}
    Check(d->SetVertexShader(nullptr));Check(d->SetFVF(D3DFVF_XYZRHW|D3DFVF_TEX1));
    d->SetRenderState(D3DRS_CULLMODE,D3DCULL_NONE);d->SetRenderState(D3DRS_ZENABLE,FALSE);d->SetRenderState(D3DRS_ALPHABLENDENABLE,FALSE);d->SetRenderState(D3DRS_ALPHATESTENABLE,FALSE);
    d->SetRenderState(D3DRS_COLORWRITEENABLE,15);d->SetRenderState(D3DRS_SRGBWRITEENABLE,FALSE);
    const float sizeParam[4]={32,32,0,0},camera[4]={1,1,1,1};d->SetPixelShaderConstantF(194,sizeParam,1);d->SetPixelShaderConstantF(202,camera,1);
    const float quad[][6]={{-.5f,-.5f,0,1,0,0},{31.5f,-.5f,0,1,1,0},{-.5f,31.5f,0,1,0,1},{31.5f,31.5f,0,1,1,1}};
    auto pixels=[&](){std::vector<DWORD> out(1024);Check(d->GetRenderTargetData(target.Get(),read.Get()));Check(read->LockRect(&locked,nullptr,D3DLOCK_READONLY));for(int y=0;y<32;++y)memcpy(out.data()+32*y,static_cast<char*>(locked.pBits)+y*locked.Pitch,128);read->UnlockRect();return out;};
    unsigned cases=0,darkened=0;
    for(float value:{.002f,.01f,.1f})for(float samples:{1.f,3.f,5.f})for(int eye=0;eye<2;++eye){
        Check(depth->LockRect(0,&locked,nullptr,D3DLOCK_DISCARD));
        for(int y=0;y<32;++y)for(int x=0;x<32;++x)reinterpret_cast<float*>(static_cast<char*>(locked.pBits)+y*locked.Pitch)[x]=value+(eye?float(x%2)*.00001f:0);
        depth->UnlockRect(0);
        const float param[4]={2,samples,-1,-.1f};Check(d->SetPixelShaderConstantF(198,param,1));
        auto draw=[&](bool bypass){Check(d->BeginScene());{AmbientOcclusionBypass guard(d.Get(),bypass);Require(guard.Active()==bypass,"override state");Check(d->DrawPrimitiveUP(D3DPT_TRIANGLESTRIP,2,quad,sizeof(quad[0])));}Check(d->EndScene());float restored[4]{};Check(d->GetPixelShaderConstantF(198,restored,1));Require(memcmp(restored,param,sizeof(param))==0,"original parameters restored");};
        draw(false);const auto baseline=pixels();
        draw(true);const auto neutral=pixels();
        for(size_t i=0;i<neutral.size();++i){Require(neutral[i]==expected[i],"bypass preserves every RGBA scene pixel");if(baseline[i]!=neutral[i])++darkened;}
        draw(false);Require(pixels()==baseline,"toggle off restores original shading");++cases;
    }
    Require(darkened>0,"baseline actually applies occlusion");
    UINT bytes=UINT(size);std::vector<DWORD> after(original.size());Check(ps->GetFunction(after.data(),&bytes));Require(after==original,"original bytecode unchanged");
    DestroyWindow(window);printf("AO: %u GPU cases preserve scene RGBA; original shading restored, metadata unchanged, state blocks and near matches passed; %u baseline pixels were shaded\n",cases,darkened);
}
