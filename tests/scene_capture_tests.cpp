#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include "SceneCapture9.h"
#include <d3dcompiler.h>
#include <wrl/client.h>
#include <fstream>
#include <iterator>
#include <cstdlib>
using namespace bvr;
using Microsoft::WRL::ComPtr;
static void Check(HRESULT hr){if(FAILED(hr)){printf("scene capture HRESULT=%08lX\n",hr);exit(1);}}
static void Require(bool yes,const char* label){if(!yes){printf("scene capture failed: %s\n",label);exit(2);}}
int main(){
    Require(LoadLibraryW(L"d3dx9_43.dll")!=nullptr,"D3DX available");
    HWND window=CreateWindowExW(0,L"STATIC",L"Scene capture regression",WS_OVERLAPPEDWINDOW,0,0,32,32,nullptr,nullptr,GetModuleHandleW(nullptr),nullptr);
    ComPtr<IDirect3D9Ex> api;Check(Direct3DCreate9Ex(D3D_SDK_VERSION,&api));
    D3DPRESENT_PARAMETERS pp{};pp.Windowed=TRUE;pp.SwapEffect=D3DSWAPEFFECT_DISCARD;pp.hDeviceWindow=window;
    pp.BackBufferWidth=pp.BackBufferHeight=32;pp.BackBufferFormat=D3DFMT_X8R8G8B8;
    ComPtr<IDirect3DDevice9Ex> d;Check(api->CreateDeviceEx(0,D3DDEVTYPE_HAL,window,D3DCREATE_HARDWARE_VERTEXPROCESSING,&pp,nullptr,&d));
    ComPtr<IDirect3DSurface9> back,read;Check(d->GetRenderTarget(0,&back));
    Check(d->CreateOffscreenPlainSurface(32,32,D3DFMT_X8R8G8B8,D3DPOOL_SYSTEMMEM,&read,nullptr));
    ComPtr<IDirect3DTexture9> input;Check(d->CreateTexture(32,32,1,D3DUSAGE_RENDERTARGET,D3DFMT_A8R8G8B8,D3DPOOL_DEFAULT,&input,nullptr));
    ComPtr<IDirect3DSurface9> surface;Check(input->GetSurfaceLevel(0,&surface));Check(d->ColorFill(surface.Get(),nullptr,0xff204080));
    const char source[]="sampler2D t:register(s0); float4 main(float2 uv:TEXCOORD):COLOR{return tex2D(t,uv);}";
    ComPtr<ID3DBlob> code;Check(D3DCompile(source,sizeof(source)-1,nullptr,nullptr,nullptr,"main","ps_3_0",0,0,&code,nullptr));
    ComPtr<IDirect3DPixelShader9> ps;Check(d->CreatePixelShader(static_cast<const DWORD*>(code->GetBufferPointer()),&ps));
    Check(d->SetPixelShader(ps.Get()));Check(d->SetTexture(0,input.Get()));Check(d->SetVertexShader(nullptr));
    Check(d->SetFVF(D3DFVF_XYZRHW|D3DFVF_TEX1));Check(d->SetRenderState(D3DRS_CULLMODE,D3DCULL_NONE));
    Check(d->SetRenderState(D3DRS_ZENABLE,FALSE));Check(d->SetRenderState(D3DRS_ALPHABLENDENABLE,FALSE));
    const float constant[4]={11,22,33,44};Check(d->SetVertexShaderConstantF(28,constant,1));
    const float quad[][6]={{-.5f,-.5f,0,1,0,0},{31.5f,-.5f,0,1,1,0},{-.5f,31.5f,0,1,0,1},{31.5f,31.5f,0,1,1,1}};
    SceneCapture9 capture;wchar_t directory[160]{};swprintf_s(directory,L"build\\test\\scene_capture_%llu",GetTickCount64());
    Require(capture.Begin(directory,nullptr),"begin");
    Check(d->Clear(0,nullptr,D3DCLEAR_TARGET,0xff000000,1,0));
    Check(d->BeginScene());
    {
        SceneCaptureDraw draw(capture,d.Get(),2);
        ComPtr<IDirect3DPixelShader9> livePs;Check(d->GetPixelShader(&livePs));Require(livePs.Get()==ps.Get(),"capture preserves shader identity");
        ComPtr<IDirect3DSurface9> liveTarget;Check(d->GetRenderTarget(0,&liveTarget));Require(liveTarget.Get()==back.Get(),"capture preserves render target");
        ComPtr<IDirect3DBaseTexture9> liveTexture;Check(d->GetTexture(0,&liveTexture));Require(liveTexture.Get()==input.Get(),"capture preserves sampler");
        float liveConstant[4]{};Check(d->GetVertexShaderConstantF(28,liveConstant,1));Require(memcmp(liveConstant,constant,sizeof(constant))==0,"capture preserves camera constants");
        Check(d->DrawPrimitiveUP(D3DPT_TRIANGLESTRIP,2,quad,sizeof(quad[0])));
    }
    capture.TargetChange(d.Get(),0,surface.Get());
    Check(d->EndScene());Require(capture.Images()==4,"input, before, after and completed target images");
    Check(d->GetRenderTargetData(back.Get(),read.Get()));D3DLOCKED_RECT map{};Check(read->LockRect(&map,nullptr,D3DLOCK_READONLY));
    for(int y=0;y<32;++y)for(int x=0;x<32;++x)Require((reinterpret_cast<DWORD*>(static_cast<char*>(map.pBits)+y*map.Pitch)[x]&0xffffff)==0x204080,"captured draw output unchanged");
    read->UnlockRect();capture.Finish();Require(!capture.Active(),"capture finishes");
    std::ifstream trace(std::wstring(directory)+L"\\draws.txt");
    const std::string text((std::istreambuf_iterator<char>(trace)),std::istreambuf_iterator<char>());
    Require(text.find("png=00000000")!=std::string::npos&&text.find("png=8")==std::string::npos,"PNG saves succeeded inside scene");
    Require(text.find("end draws=1 images=4")!=std::string::npos,"bounded one-frame trace completed");
    DestroyWindow(window);puts("scene capture: in-scene PNG readbacks, shader/target/sampler/constants preservation, output pixels and completion passed");
}
