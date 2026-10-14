#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include "MaterialTextureCamera.h"
#include <wrl/client.h>
#include <fstream>
#include <vector>
#include <cstdio>
#include <cstdlib>
using namespace bvr;
using Microsoft::WRL::ComPtr;
static void Require(bool ok,const char* why){if(!ok){printf("material texture FAILED: %s\n",why);exit(1);}}
static void Check(HRESULT hr){if(FAILED(hr)){printf("material texture HRESULT %08lX\n",hr);exit(2);}}
static std::vector<DWORD> Load(const char* name) {
    std::ifstream f(std::string("tests/fixtures/material_texture/")+name,std::ios::binary|std::ios::ate);
    Require(f.good(),"native fixture exists");const auto size=size_t(f.tellg());f.seekg(0);
    std::vector<DWORD> result((size+3)/4);f.read(reinterpret_cast<char*>(result.data()),size);return result;
}
int main() {
    HWND window=CreateWindowExW(0,L"STATIC",L"Material texture regression",WS_OVERLAPPEDWINDOW,0,0,128,128,nullptr,nullptr,GetModuleHandleW(nullptr),nullptr);
    ComPtr<IDirect3D9Ex> api;Check(Direct3DCreate9Ex(D3D_SDK_VERSION,&api));
    D3DPRESENT_PARAMETERS pp{};pp.Windowed=TRUE;pp.SwapEffect=D3DSWAPEFFECT_DISCARD;pp.hDeviceWindow=window;
    pp.BackBufferWidth=pp.BackBufferHeight=256;pp.BackBufferFormat=D3DFMT_X8R8G8B8;
    ComPtr<IDirect3DDevice9Ex> d;Check(api->CreateDeviceEx(0,D3DDEVTYPE_HAL,window,D3DCREATE_HARDWARE_VERTEXPROCESSING,&pp,nullptr,&d));
    ComPtr<IDirect3DSurface9> target,read,large;
    Check(d->CreateRenderTarget(128,128,D3DFMT_A8R8G8B8,D3DMULTISAMPLE_NONE,0,FALSE,&target,nullptr));
    Check(d->CreateRenderTarget(256,256,D3DFMT_A8R8G8B8,D3DMULTISAMPLE_NONE,0,FALSE,&large,nullptr));
    Check(d->CreateOffscreenPlainSurface(128,128,D3DFMT_A8R8G8B8,D3DPOOL_SYSTEMMEM,&read,nullptr));Check(d->SetRenderTarget(0,target.Get()));
    MaterialTextureCamera filter;
    std::vector<ComPtr<IDirect3DPixelShader9>> shaders;
    for(const char* name:{"E54B73A3.pso","EDBEB70B.pso","EF0C3C98.pso"}) {
        auto bytes=Load(name);auto crc=ShaderCrc32(bytes.data(),bytes.size()*4);
        Require(IsMaterialTexturePixel(crc,bytes.size()*4)&&!IsMaterialTexturePixel(crc,bytes.size()*4+4),"exact pixel signatures");
        ComPtr<IDirect3DPixelShader9> shader;Check(d->CreatePixelShader(bytes.data(),&shader));Require(filter.Register(shader.Get())==crc,"pixel registration");shaders.push_back(shader);
    }
    for(const char* name:{"CB1E6A15.vso","9FFEA812.vso"}) {
        auto bytes=Load(name);auto crc=ShaderCrc32(bytes.data(),bytes.size()*4);
        Require(IsMaterialTextureVertex(crc,bytes.size()*4)&&!IsMaterialTextureVertex(crc^1,bytes.size()*4),"exact vertex signatures");
    }
    const auto code=Load("CB1E6A15.vso");ComPtr<IDirect3DVertexShader9> vs;Check(d->CreateVertexShader(code.data(),&vs));Check(d->SetVertexShader(vs.Get()));
    const Mat4 original={{{20.0575142f,0,0,10.0287571f},{0,-20.0575142f,0,-10.0287571f},{0,0,-1.00010002f,9.90099049f},{0,0,-1,10}}};
    for(auto& shader:shaders){Check(d->SetPixelShader(shader.Get()));Require(filter.UsesNativeCamera(d.Get(),original,24,true,true),"each captured material pass recognized");}
    Check(d->SetPixelShader(shaders[0].Get()));
    Require(!filter.UsesNativeCamera(d.Get(),original,24,true,false)&&!filter.UsesNativeCamera(d.Get(),original,24,false,true)&&!filter.UsesNativeCamera(d.Get(),original,8,true,true),"inactive/other vertex/projection passes not excluded from stereo");
    for(int r=2;r<4;++r)for(int c=0;c<4;++c){auto changed=original;changed.m[r][c]+=.01f;Require(!filter.UsesNativeCamera(d.Get(),changed,24,true,true),"different scene camera keeps stereo");}
    auto invalid=original;invalid.m[0][0]=NAN;Require(!filter.UsesNativeCamera(d.Get(),invalid,24,true,true),"invalid matrix rejected");
    const D3DVIEWPORT9 materialViewport{0,0,128,128,0,1},other{0,0,64,64,0,1};Check(d->SetViewport(&other));Require(!filter.UsesNativeCamera(d.Get(),original,24,true,true),"different viewport keeps stereo");
    Check(d->SetRenderTarget(0,large.Get()));Check(d->SetViewport(&materialViewport));Require(!filter.UsesNativeCamera(d.Get(),original,24,true,true),"128 viewport on a scene target keeps stereo");Check(d->SetRenderTarget(0,target.Get()));
    ComPtr<IDirect3DStateBlock9> state;Check(d->CreateStateBlock(D3DSBT_ALL,&state));Check(state->Capture());Check(d->SetPixelShader(nullptr));
    Require(!filter.UsesNativeCamera(d.Get(),original,24,true,true),"other pixel shader keeps stereo");Check(state->Apply());Require(filter.UsesNativeCamera(d.Get(),original,24,true,true),"state-block binding recognized");
    ComPtr<IDirect3DTexture9> texture;Check(d->CreateTexture(64,64,1,D3DUSAGE_DYNAMIC,D3DFMT_A8R8G8B8,D3DPOOL_DEFAULT,&texture,nullptr));
    D3DLOCKED_RECT lock{};Check(texture->LockRect(0,&lock,nullptr,D3DLOCK_DISCARD));
    for(int y=0;y<64;++y)for(int x=0;x<64;++x)reinterpret_cast<DWORD*>(static_cast<char*>(lock.pBits)+y*lock.Pitch)[x]=0xff000040u|DWORD(x*4)<<16|DWORD(y*4)<<8;
    Check(texture->UnlockRect(0));Check(d->SetTexture(0,texture.Get()));
    Check(d->SetSamplerState(0,D3DSAMP_MINFILTER,D3DTEXF_POINT));Check(d->SetSamplerState(0,D3DSAMP_MAGFILTER,D3DTEXF_POINT));
    const float color[4]{1,1,1,1};Check(d->SetPixelShaderConstantF(184,color,1));Check(d->SetFVF(D3DFVF_XYZW|D3DFVF_TEX1));
    Check(d->SetRenderState(D3DRS_ZENABLE,FALSE));Check(d->SetRenderState(D3DRS_CULLMODE,D3DCULL_NONE));Check(d->SetRenderState(D3DRS_ALPHABLENDENABLE,FALSE));
    const float quad[][6]={{-1,-1,0,1,0,0},{0,-1,0,1,1,0},{-1,0,0,1,0,1},{0,0,0,1,1,1}};
    auto draw=[&](const Mat4& m) {
        Check(d->SetVertexShaderConstantF(24,&m.m[0][0],4));Check(d->Clear(0,nullptr,D3DCLEAR_TARGET,0xff000000,1,0));
        Check(d->BeginScene());Check(d->DrawPrimitiveUP(D3DPT_TRIANGLESTRIP,2,quad,sizeof(quad[0])));Check(d->EndScene());
        Check(d->GetRenderTargetData(target.Get(),read.Get()));Check(read->LockRect(&lock,nullptr,D3DLOCK_READONLY));
        std::vector<DWORD> pixels(128*128);for(int y=0;y<128;++y)memcpy(pixels.data()+128*y,static_cast<char*>(lock.pBits)+y*lock.Pitch,128*4);Check(read->UnlockRect());return pixels;
    };
    const auto expected=draw(original);
    unsigned colored=0;for(auto pixel:expected)colored+=(pixel&0x00ffffffu)!=0;
    Require(colored>15000,"native reference contains the painted material, not a blank target");
    Mat4 invGame{};Require(Invert(PerspectiveFovLH(.9f,16.f/9,.1f,10000),invGame),"game projection inversion");
    unsigned cases=0,different=0;
    for(float yaw:{-.5f,0.f,.5f})for(float eye:{-.032f,.032f})for(float lean:{0.f,.3f}) {
        Mat4 head=Identity();head.m[0][0]=head.m[2][2]=std::cos(yaw);head.m[0][2]=std::sin(yaw);head.m[2][0]=-std::sin(yaw);head.m[0][3]=eye+lean;
        const auto prefix=Mul(PerspectiveRawLH(-1.1f,.9f,-1,1,.1f,10000),Mul(InvertRigid(head),invGame));
        const auto old=draw(Mul(prefix,original));for(size_t i=0;i<old.size();++i)different+=old[i]!=expected[i];
        const bool local=filter.UsesNativeCamera(d.Get(),original,24,true,true);Require(local,"local texture recognized during native shader render");
        Require(draw(local?original:Mul(prefix,original))==expected,"material image invariant under both eyes, head turns and lean");++cases;
    }
    Require(different>10000,"old path reproduces head-dependent material corruption");
    std::vector<DWORD> saved(code.size());UINT count=UINT(saved.size()*4);Check(vs->GetFunction(saved.data(),&count));Require(saved==code,"vertex bytecode unchanged");
    filter.Reset();Require(!filter.UsesNativeCamera(d.Get(),original,24,true,true),"reset clears recognition");DestroyWindow(window);
    printf("Material texture: exact native shaders/camera/target, negative matches, state blocks/reset, %u GPU eye/turn/lean cases (%u pixels); old path differs at %u pixels.\n",cases,cases*16384,different);
}
