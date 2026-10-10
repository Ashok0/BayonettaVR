#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include "MenuPanel9.h"
#include "WorldHud.h"
#include <windows.h>
#include <wrl/client.h>
#include <cstdio>
#include <cstdlib>
#include <cstring>
using namespace bvr;
using Microsoft::WRL::ComPtr;
static void Require(bool value,const char* message) {
    if(!value){std::printf("menu panel failed: %s\n",message);std::exit(1);}
}
static void Check(HRESULT hr,const char* message){if(FAILED(hr)){std::printf("HRESULT=%08lX ",hr);Require(false,message);}}
static void Rectangle(IDirect3DDevice9* d,float x,float y,float size,DWORD color) {
    struct V{float x,y,z,w;DWORD color;};
    const V v[]={{x,y,0,1,color},{x+size,y,0,1,color},{x,y+size,0,1,color},{x+size,y+size,0,1,color}};
    Check(d->DrawPrimitiveUP(D3DPT_TRIANGLESTRIP,2,v,sizeof(V)),"raw XYZRHW UI draw");
}
static void FlatUi(IDirect3DDevice9* d) {
    d->SetVertexShader(nullptr);d->SetPixelShader(nullptr);d->SetTexture(0,nullptr);
    d->SetFVF(D3DFVF_XYZRHW|D3DFVF_DIFFUSE);
    d->SetTextureStageState(0,D3DTSS_COLOROP,D3DTOP_SELECTARG1);
    d->SetTextureStageState(0,D3DTSS_COLORARG1,D3DTA_DIFFUSE);
    d->SetTextureStageState(0,D3DTSS_ALPHAOP,D3DTOP_SELECTARG1);
    d->SetTextureStageState(0,D3DTSS_ALPHAARG1,D3DTA_DIFFUSE);
    d->SetTextureStageState(1,D3DTSS_COLOROP,D3DTOP_DISABLE);
    d->SetRenderState(D3DRS_ZENABLE,FALSE);d->SetRenderState(D3DRS_CULLMODE,D3DCULL_NONE);
    d->SetRenderState(D3DRS_ALPHATESTENABLE,FALSE);d->SetRenderState(D3DRS_ALPHABLENDENABLE,FALSE);
}
int main() {
    HWND window=CreateWindowExW(0,L"STATIC",L"Menu panel GPU test",WS_OVERLAPPEDWINDOW,0,0,128,128,nullptr,nullptr,GetModuleHandleW(nullptr),nullptr);
    Require(window!=nullptr,"hidden test window");
    ComPtr<IDirect3D9Ex> d3d;Check(Direct3DCreate9Ex(D3D_SDK_VERSION,&d3d),"D3D9Ex");
    D3DPRESENT_PARAMETERS pp{};pp.Windowed=TRUE;pp.SwapEffect=D3DSWAPEFFECT_DISCARD;
    pp.hDeviceWindow=window;pp.BackBufferWidth=pp.BackBufferHeight=128;pp.BackBufferFormat=D3DFMT_X8R8G8B8;
    pp.PresentationInterval=D3DPRESENT_INTERVAL_IMMEDIATE;
    ComPtr<IDirect3DDevice9Ex> d;
    Check(d3d->CreateDeviceEx(D3DADAPTER_DEFAULT,D3DDEVTYPE_HAL,window,D3DCREATE_HARDWARE_VERTEXPROCESSING,&pp,nullptr,&d),"device");
    ComPtr<IDirect3DSurface9> back,read;
    Check(d->GetBackBuffer(0,0,D3DBACKBUFFER_TYPE_MONO,&back),"backbuffer");
    Check(d->CreateOffscreenPlainSurface(128,128,D3DFMT_X8R8G8B8,D3DPOOL_SYSTEMMEM,&read,nullptr),"readback");
    MenuPanel9 panel;
    unsigned cases=0;
    for(int cycle=0;cycle<2;++cycle) {
        panel.BeginFrame();
        Check(d->BeginScene(),"begin capture scene");
        Require(panel.Begin(d.Get(),0),"begin ID capture");
        FlatUi(d.Get());
        Rectangle(d.Get(),54,54,20,0xff00ff00); // Opaque menu icon.
        d->SetRenderState(D3DRS_ALPHABLENDENABLE,TRUE);
        d->SetRenderState(D3DRS_SRCBLEND,D3DBLEND_SRCALPHA);
        d->SetRenderState(D3DRS_DESTBLEND,D3DBLEND_INVSRCALPHA);
        d->SetRenderState(D3DRS_SEPARATEALPHABLENDENABLE,TRUE);
        d->SetRenderState(D3DRS_SRCBLENDALPHA,D3DBLEND_ONE);
        d->SetRenderState(D3DRS_DESTBLENDALPHA,D3DBLEND_INVSRCALPHA);
        Rectangle(d.Get(),10,54,20,0x80ff0000); // Half-transparent UI edge.
        panel.End(d.Get(),0);
        Require(panel.Begin(d.Get(),1),"begin font capture");
        FlatUi(d.Get());Rectangle(d.Get(),54,10,20,0xffffffff);
        panel.End(d.Get(),1);
        Check(d->EndScene(),"end capture scene");
        for(int turn=-1;turn<=1;++turn) for(int eye=0;eye<2;++eye) {
            Check(d->Clear(0,nullptr,D3DCLEAR_TARGET,0xff0000ff,1,0),"clear background");
            Mat4 head=Identity(); const float angle=turn*0.25f;
            head.m[0][0]=head.m[2][2]=std::cos(angle);
            head.m[0][2]=std::sin(angle);head.m[2][0]=-std::sin(angle);
            Mat4 eyePose=Identity();eyePose.m[0][3]=eye ? 0.032f : -0.032f;
            const Mat4 clip=Mul(PerspectiveFovLH(1.57079632679f,1,0.1f,100),
                Mul(InvertRigid(Mul(head,eyePose)),HudPanelFromClip(1,2,2,0)));
            const float saved[16]={1,2,3,4,5,6,7,8,9,10,11,12,13,14,15,16};
            d->SetVertexShaderConstantF(0,saved,4);
            d->SetRenderState(D3DRS_SCISSORTESTENABLE,TRUE);
            const RECT scissor={1,2,100,110};d->SetScissorRect(&scissor);
            Check(d->BeginScene(),"begin eye");
            Check(panel.Draw(d.Get(),0,clip),"ID replay");
            Check(panel.Draw(d.Get(),1,clip),"font replay");
            Check(d->EndScene(),"end eye");
            float restored[16]{};d->GetVertexShaderConstantF(0,restored,4);
            Require(std::memcmp(saved,restored,sizeof(saved))==0,"shader constants restored");
            DWORD enabled=0;d->GetRenderState(D3DRS_SCISSORTESTENABLE,&enabled);
            RECT restoredScissor{};d->GetScissorRect(&restoredScissor);
            Require(enabled && std::memcmp(&scissor,&restoredScissor,sizeof(scissor))==0,"scissor restored");
            Check(d->GetRenderTargetData(back.Get(),read.Get()),"GPU readback");
            D3DLOCKED_RECT locked{};Check(read->LockRect(&locked,nullptr,D3DLOCK_READONLY),"map pixels");
            unsigned green=0,white=0,purple=0,background=0;double xsum=0;
            for(int y=0;y<128;++y) for(int x=0;x<128;++x) {
                const auto* p=static_cast<const unsigned char*>(locked.pBits)+y*locked.Pitch+x*4;
                if(p[1]>230 && p[0]<15 && p[2]<15){++green;xsum+=x+0.5;}
                if(p[0]>230 && p[1]>230 && p[2]>230) ++white;
                if(p[0]>115 && p[0]<140 && p[2]>115 && p[2]<140 && p[1]<5) ++purple;
                if(p[0]==255 && p[1]==0 && p[2]==0) ++background;
            }
            read->UnlockRect();
            const float expected=(0.5f+0.5f*clip.m[0][3]/clip.m[3][3])*128;
            if(green<40 || white<30 || purple<30 || background<14500 || std::fabs(xsum/green-expected)>2) {
                std::printf("cycle=%d eye=%d turn=%d green=%u white=%u purple=%u background=%u center=%.2f expected=%.2f\n",cycle,eye,turn,green,white,purple,background,green?xsum/green:0,expected);
                return 2;
            }
            ++cases;
        }
        panel.BeginFrame();Require(!panel.Ready(0)&&!panel.Ready(1),"no stale menus on next frame");
        panel.Reset();
    }
    DestroyWindow(window);
    std::printf("menu panel: %u GPU readback cases passed (both eyes, head turns, opaque UI, alpha, font, state restore, recreation)\n",cases);
}
