#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include "MenuPanel9.h"
#include <windows.h>
#include <d3dcompiler.h>
#include <wrl/client.h>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <iterator>
#include <string>
#include <vector>
using namespace bvr;
using Microsoft::WRL::ComPtr;
static void Require(bool value,const char* message) {
    if(!value){std::printf("menu feedback failed: %s\n",message);std::exit(1);}
}
static void Check(HRESULT hr,const char* message) {
    if(FAILED(hr)){std::printf("HRESULT=%08lX ",hr);Require(false,message);}
}
struct ShaderBuffer : IUnknown {
    virtual void* STDMETHODCALLTYPE GetBufferPointer()=0;
    virtual DWORD STDMETHODCALLTYPE GetBufferSize()=0;
};
using AssembleFn=HRESULT(WINAPI*)(const char*,UINT,const void*,void*,DWORD,ShaderBuffer**,ShaderBuffer**);
static HRESULT STDMETHODCALLTYPE Copy(IDirect3DDevice9* d,IDirect3DSurface9* s,const RECT* sr,
    IDirect3DSurface9* t,const RECT* tr,D3DTEXTUREFILTERTYPE f){return d->StretchRect(s,sr,t,tr,f);}
int main() {
    HWND window=CreateWindowExW(0,L"STATIC",L"Menu feedback test",WS_OVERLAPPEDWINDOW,0,0,128,128,nullptr,nullptr,GetModuleHandleW(nullptr),nullptr);
    Require(window!=nullptr,"test window");
    ComPtr<IDirect3D9Ex> d3d;Check(Direct3DCreate9Ex(D3D_SDK_VERSION,&d3d),"D3D9Ex");
    D3DPRESENT_PARAMETERS pp{};pp.Windowed=TRUE;pp.SwapEffect=D3DSWAPEFFECT_DISCARD;pp.hDeviceWindow=window;
    pp.BackBufferWidth=pp.BackBufferHeight=128;pp.BackBufferFormat=D3DFMT_X8R8G8B8;
    ComPtr<IDirect3DDevice9Ex> d;
    Check(d3d->CreateDeviceEx(0,D3DDEVTYPE_HAL,window,D3DCREATE_HARDWARE_VERTEXPROCESSING,&pp,nullptr,&d),"device");
    ComPtr<IDirect3DSurface9> back,read,scratchSurface;
    ComPtr<IDirect3DTexture9> scratch;
    Check(d->GetBackBuffer(0,0,D3DBACKBUFFER_TYPE_MONO,&back),"backbuffer");
    Check(d->CreateTexture(128,128,1,D3DUSAGE_RENDERTARGET,D3DFMT_A8R8G8B8,D3DPOOL_DEFAULT,&scratch,nullptr),"game blur scratch");
    Check(scratch->GetSurfaceLevel(0,&scratchSurface),"scratch surface");
    Check(d->CreateOffscreenPlainSurface(128,128,D3DFMT_X8R8G8B8,D3DPOOL_SYSTEMMEM,&read,nullptr),"readback");
    HMODULE d3dx=LoadLibraryW(L"d3dx9_43.dll");Require(d3dx!=nullptr,"D3DX shader assembler");
    auto assemble=reinterpret_cast<AssembleFn>(GetProcAddress(d3dx,"D3DXAssembleShader"));Require(assemble!=nullptr,"assembler export");
    ComPtr<IDirect3DPixelShader9> ps[2];
    const char* paths[]={"tests/fixtures/menu/blur_h.asm","tests/fixtures/menu/blur_v.asm"};
    for(int i=0;i<2;++i) {
        std::ifstream input(paths[i],std::ios::binary);
        Require(input.good(),"captured shader fixture");
        std::string source((std::istreambuf_iterator<char>(input)),std::istreambuf_iterator<char>());
        while(!source.empty() && source.back()=='\0')source.pop_back();
        ComPtr<ShaderBuffer> code,error;
        const HRESULT hr=assemble(source.data(),UINT(source.size()),nullptr,nullptr,0,&code,&error);
        if(error)std::printf("%s\n",static_cast<const char*>(error->GetBufferPointer()));
        Check(hr,"assemble actual game blur shader");
        Check(d->CreatePixelShader(static_cast<const DWORD*>(code->GetBufferPointer()),&ps[i]),"game pixel shader");
    }
    const char vsSource[]="struct O{float4 p:POSITION;float4 screen:TEXCOORD2;float4 color:COLOR0;};"
        "O main(float4 p:POSITION){O o;o.p=p;o.screen=p;o.color=1;return o;}";
    ComPtr<ID3DBlob> vsCode;Check(D3DCompile(vsSource,sizeof(vsSource)-1,nullptr,nullptr,nullptr,"main","vs_3_0",0,0,&vsCode,nullptr),"test VS");
    ComPtr<IDirect3DVertexShader9> vs;Check(d->CreateVertexShader(static_cast<const DWORD*>(vsCode->GetBufferPointer()),&vs),"create VS");
    D3DVERTEXELEMENT9 elements[]={{0,0,D3DDECLTYPE_FLOAT4,D3DDECLMETHOD_DEFAULT,D3DDECLUSAGE_POSITION,0},D3DDECL_END()};
    ComPtr<IDirect3DVertexDeclaration9> decl;Check(d->CreateVertexDeclaration(elements,&decl),"declaration");
    MenuPanel9 panel;
    auto rectangle=[&](float x,float y,float w,float h,bool capture) {
        struct V{float x,y,z,rhw;DWORD color;};
        const V vertices[]={{x,y,0,1,0xffd08030},{x+w,y,0,1,0xffd08030},{x,y+h,0,1,0xffd08030},{x+w,y+h,0,1,0xffd08030}};
        auto draw=[&]{return d->DrawPrimitiveUP(D3DPT_TRIANGLESTRIP,2,vertices,sizeof(V));};
        Check(capture ? panel.CaptureDraw(d.Get(),0,draw) : draw(),"glyph draw");
    };
    auto glyph=[&](bool capture) {
        d->SetVertexShader(nullptr);d->SetPixelShader(nullptr);d->SetFVF(D3DFVF_XYZRHW|D3DFVF_DIFFUSE);
        d->SetTexture(0,nullptr);d->SetTextureStageState(0,D3DTSS_COLOROP,D3DTOP_SELECTARG1);d->SetTextureStageState(0,D3DTSS_COLORARG1,D3DTA_DIFFUSE);
        d->SetTextureStageState(0,D3DTSS_ALPHAOP,D3DTOP_SELECTARG1);d->SetTextureStageState(0,D3DTSS_ALPHAARG1,D3DTA_DIFFUSE);
        d->SetTextureStageState(1,D3DTSS_COLOROP,D3DTOP_DISABLE);
        d->SetRenderState(D3DRS_ZENABLE,FALSE);d->SetRenderState(D3DRS_STENCILENABLE,FALSE);d->SetRenderState(D3DRS_CULLMODE,D3DCULL_NONE);
        d->SetRenderState(D3DRS_ALPHABLENDENABLE,TRUE);d->SetRenderState(D3DRS_SRCBLEND,D3DBLEND_SRCALPHA);d->SetRenderState(D3DRS_DESTBLEND,D3DBLEND_INVSRCALPHA);
        d->SetRenderState(D3DRS_BLENDOP,D3DBLENDOP_ADD);d->SetRenderState(D3DRS_SEPARATEALPHABLENDENABLE,FALSE);d->SetRenderState(D3DRS_COLORWRITEENABLE,7);
        rectangle(44,44,8,40,capture);rectangle(44,44,32,8,capture);rectangle(44,60,24,8,capture);
    };
    auto blur=[&](bool capture,bool routeCopies) {
        for(int pass=0;pass<2;++pass) {
            Check(capture && routeCopies ? panel.CaptureCopy(d.Get(),0,back.Get(),nullptr,scratchSurface.Get(),nullptr,D3DTEXF_POINT,&Copy)
                : Copy(d.Get(),back.Get(),nullptr,scratchSurface.Get(),nullptr,D3DTEXF_POINT),"framebuffer snapshot");
            d->SetVertexShader(vs.Get());d->SetVertexDeclaration(decl.Get());d->SetPixelShader(ps[pass].Get());
            d->SetTexture(14,scratch.Get());
            d->SetSamplerState(14,D3DSAMP_MINFILTER,D3DTEXF_LINEAR);d->SetSamplerState(14,D3DSAMP_MAGFILTER,D3DTEXF_LINEAR);
            d->SetSamplerState(14,D3DSAMP_ADDRESSU,D3DTADDRESS_CLAMP);d->SetSamplerState(14,D3DSAMP_ADDRESSV,D3DTADDRESS_CLAMP);
            const float rate[]={0,0,1,1};d->SetPixelShaderConstantF(184,rate,1);
            const float quad[][4]={{-.75f,.75f,0,1},{.75f,.75f,0,1},{-.75f,-.75f,0,1},{.75f,-.75f,0,1}};
            auto draw=[&]{return d->DrawPrimitiveUP(D3DPT_TRIANGLESTRIP,2,quad,sizeof(quad[0]));};
            Check(capture ? panel.CaptureDraw(d.Get(),0,draw) : draw(),"actual game blur pass");
            ComPtr<IDirect3DBaseTexture9> restored;d->GetTexture(14,&restored);
            Require(restored.Get()==scratch.Get(),"sampler 14 restored after white capture");
        }
    };
    auto pixels=[&] {
        Check(d->GetRenderTargetData(back.Get(),read.Get()),"read pixels");
        D3DLOCKED_RECT lock{};Check(read->LockRect(&lock,nullptr,D3DLOCK_READONLY),"map pixels");
        std::vector<DWORD> result(128*128);
        for(int y=0;y<128;++y)std::memcpy(result.data()+y*128,static_cast<const char*>(lock.pBits)+y*lock.Pitch,128*4);
        read->UnlockRect();return result;
    };
    const DWORD background=0xff204060;
    Check(d->Clear(0,nullptr,D3DCLEAR_TARGET,background,1,0),"direct background");
    Check(d->BeginScene(),"direct begin");glyph(false);blur(false,false);Check(d->EndScene(),"direct end");
    const auto reference=pixels();
    unsigned checked=0;
    for(int cycle=0;cycle<2;++cycle)for(int routed=0;routed<2;++routed)for(int frame=0;frame<2;++frame) {
        panel.BeginFrame();
        Check(d->Clear(0,nullptr,D3DCLEAR_TARGET,background,1,0),"capture background");
        Check(d->BeginScene(),"capture begin");Require(panel.Begin(d.Get(),0),"panel capture");
        glyph(true);blur(true,routed!=0);panel.End(d.Get(),0);Check(d->EndScene(),"capture end");
        for(int eye=0;eye<2;++eye) {
            Check(d->Clear(0,nullptr,D3DCLEAR_TARGET,background,1,0),"eye background");
            Mat4 clip=Identity();const int offset=eye ? 8 : -8;clip.m[0][3]=float(offset)/64;
            Check(d->BeginScene(),"eye begin");Check(panel.Draw(d.Get(),0,clip),"panel replay");Check(d->EndScene(),"eye end");
            const auto actual=pixels();
            const int samples[][2]={{48,55},{48,76},{72,48},{60,64},{85,80},{30,32}};
            int maxError=0;
            for(const auto& point:samples)for(int channel=0;channel<3;++channel) {
                const int expected=(reference[point[1]*128+point[0]]>>(channel*8))&255;
                const int value=(actual[point[1]*128+point[0]+offset]>>(channel*8))&255;
                maxError=std::max(maxError,std::abs(expected-value));
            }
            if(routed) {if(maxError>3)std::printf("cycle=%d eye=%d error=%d\n",cycle,eye,maxError);Require(maxError<=3,"blurred glyph and background match native rendering");++checked;}
            else Require(maxError>40,"old unredirected copy path reproduces missing glyph");
        }
        if(frame==1)panel.Reset();
    }
    DestroyWindow(window);
    std::printf("menu feedback: %u stereo/recreation cases passed with captured game blur shaders; old copy path reproduces missing labels\n",checked);
}
