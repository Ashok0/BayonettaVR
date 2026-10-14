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
static MenuPanel9* capturingPanel=nullptr;
static int captureLayer=0;
static void Rectangle(IDirect3DDevice9* d,float x,float y,float size,DWORD color) {
    struct V{float x,y,z,w;DWORD color;};
    const V v[]={{x,y,0,1,color},{x+size,y,0,1,color},{x,y+size,0,1,color},{x+size,y+size,0,1,color}};
    const auto draw=[&]{return d->DrawPrimitiveUP(D3DPT_TRIANGLESTRIP,2,v,sizeof(V));};
    Check(capturingPanel ? capturingPanel->CaptureDraw(d,captureLayer,draw) : draw(),"raw XYZRHW UI draw");
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
    pp.EnableAutoDepthStencil=TRUE;pp.AutoDepthStencilFormat=D3DFMT_D24S8;
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
        capturingPanel=&panel;captureLayer=0;
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
        capturingPanel=nullptr;
        Require(panel.Begin(d.Get(),1),"begin font capture");
        capturingPanel=&panel;captureLayer=1;
        FlatUi(d.Get());Rectangle(d.Get(),54,10,20,0xffffffff);
        panel.End(d.Get(),1);
        capturingPanel=nullptr;
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
    // Inventory background uses the early list, options use Id, text uses
    // IdFont. Check their common physical plane against independent eye rays.
    unsigned inventoryPixels=0,inventoryCases=0;
    for(int cycle=0;cycle<2;++cycle) {
        panel.BeginFrame();
        Check(d->BeginScene(),"inventory capture");
        for(int layer:{2,0,1}) {
            d->SetRenderState(D3DRS_SCISSORTESTENABLE,FALSE);
            Require(panel.Begin(d.Get(),layer),"inventory layer capture");
            capturingPanel=&panel;captureLayer=layer;FlatUi(d.Get());
            if(layer==2) {
                const RECT bounds={16,16,112,112};d->SetScissorRect(&bounds);
                d->SetRenderState(D3DRS_SCISSORTESTENABLE,TRUE);
                Rectangle(d.Get(),-64,-64,256,0xffffff00);
            } else Rectangle(d.Get(),54,layer==0?54.f:10.f,20,layer==0?0xff00ff00:0xffffffff);
            panel.End(d.Get(),layer);capturingPanel=nullptr;
        }
        Check(d->EndScene(),"inventory captured");
        for(int turn=-1;turn<=1;++turn)for(int eye=0;eye<2;++eye)for(float lean:{-.2f,.2f}) {
            const float angle=turn*.25f;
            Mat4 head=Identity();head.m[0][0]=head.m[2][2]=std::cos(angle);
            head.m[0][2]=std::sin(angle);head.m[2][0]=-std::sin(angle);head.m[0][3]=lean;
            Mat4 offset=Identity();offset.m[0][3]=eye?.032f:-.032f;
            const auto eyePose=Mul(head,offset);
            const auto clip=Mul(PerspectiveFovLH(1.57079632679f,1,.1f,100),
                Mul(InvertRigid(eyePose),HudPanelFromClip(1,2,2,0)));
            d->SetRenderState(D3DRS_SCISSORTESTENABLE,FALSE);
            Check(d->Clear(0,nullptr,D3DCLEAR_TARGET,0xff0000ff,1,0),"inventory scene");
            Check(d->BeginScene(),"inventory eye");
            for(int layer:{2,0,1})Check(panel.Draw(d.Get(),layer,clip),"inventory layer replay");
            Check(d->EndScene(),"inventory eye complete");
            Check(d->GetRenderTargetData(back.Get(),read.Get()),"inventory readback");
            D3DLOCKED_RECT locked{};Check(read->LockRect(&locked,nullptr,D3DLOCK_READONLY),"inventory pixels");
            for(int y=0;y<128;++y)for(int x=0;x<128;++x) {
                const float dx=2.f*(x+.5f)/128-1,dy=1-2.f*(y+.5f)/128;
                const float rx=eyePose.m[0][0]*dx+eyePose.m[0][2];
                const float rz=eyePose.m[2][0]*dx+eyePose.m[2][2];
                const float t=(2-eyePose.m[2][3])/rz;
                const float u=(eyePose.m[0][3]+t*rx+1)*64,v=(1-t*dy)*64;
                bool edge=false;
                for(float b:{10.f,16.f,30.f,54.f,74.f,112.f})if(std::fabs(u-b)<3||std::fabs(v-b)<3)edge=true;
                if(edge)continue; // Raster/filter boundary; test solid interiors.
                DWORD expected=0x000000ff;
                if(u>16&&u<112&&v>16&&v<112)expected=0x00ffff00;
                if(u>54&&u<74&&v>54&&v<74)expected=0x0000ff00;
                if(u>54&&u<74&&v>10&&v<30)expected=0x00ffffff;
                const DWORD actual=*reinterpret_cast<const DWORD*>(static_cast<const char*>(locked.pBits)+y*locked.Pitch+x*4)&0x00ffffff;
                Require(actual==expected,"early parchment and later options/font share bounded panel under eye/turn/lean");
                ++inventoryPixels;
            }
            Check(read->UnlockRect(),"inventory unmap");++inventoryCases;
        }
        panel.BeginFrame();for(int layer=0;layer<MenuPanel9::LayerCount;++layer)Require(!panel.Ready(layer),"all three layers cleared after closing menu");
        panel.Reset();
    }
    printf("Inventory panel: %u eye/turn/lean/recreation cases, %u ray-checked pixels; early background clipping and all three layers aligned.\n",inventoryCases,inventoryPixels);
    // Compare capture/composite with direct D3D9 rendering over the same scene.
    // Unselected labels can multiply destination RGB; the previous transparent
    // black capture erased that contribution and produced black rectangles.
    struct BlendCase { DWORD src,dst,op,mask,color; const char* label; };
    const BlendCase modes[]={
        {D3DBLEND_DESTCOLOR,D3DBLEND_INVSRCALPHA,D3DBLENDOP_ADD,7,0xff808080,"unselected multiply label"},
        {D3DBLEND_SRCALPHA,D3DBLEND_INVSRCALPHA,D3DBLENDOP_ADD,7,0x80ffffff,"highlighted label"},
        {D3DBLEND_ZERO,D3DBLEND_SRCCOLOR,D3DBLENDOP_ADD,7,0xff4080c0,"colored transmission"},
        {D3DBLEND_SRCALPHA,D3DBLEND_ONE,D3DBLENDOP_ADD,7,0x80c08040,"additive highlight"},
        {D3DBLEND_SRCALPHA,D3DBLEND_ONE,D3DBLENDOP_REVSUBTRACT,7,0x40808080,"subtractive shade"},
        {D3DBLEND_ONE,D3DBLEND_INVSRCALPHA,D3DBLENDOP_ADD,7,0x80402010,"premultiplied label"},
        {D3DBLEND_DESTCOLOR,D3DBLEND_INVSRCALPHA,D3DBLENDOP_ADD,7,0x80cccccc,"bright destination-color label"},
        {D3DBLEND_ONE,D3DBLEND_ZERO,D3DBLENDOP_ADD,7,0x00808080,"RGB-only opaque label"}
    };
    auto sample=[&](int x,int y) {
        Check(d->GetRenderTargetData(back.Get(),read.Get()),"blend GPU readback");
        D3DLOCKED_RECT locked{};Check(read->LockRect(&locked,nullptr,D3DLOCK_READONLY),"blend map");
        DWORD pixel=0;std::memcpy(&pixel,static_cast<const unsigned char*>(locked.pBits)+y*locked.Pitch+x*4,4);
        read->UnlockRect();return pixel;
    };
    auto drawLabel=[&](const BlendCase& mode,bool nested) {
        FlatUi(d.Get());
        d->SetRenderState(D3DRS_SCISSORTESTENABLE,FALSE);
        d->SetRenderState(D3DRS_STENCILENABLE,FALSE);
        d->SetRenderState(D3DRS_SEPARATEALPHABLENDENABLE,FALSE);
        if(nested) {
            d->SetRenderState(D3DRS_COLORWRITEENABLE,7);
            d->SetRenderState(D3DRS_ALPHABLENDENABLE,TRUE);
            d->SetRenderState(D3DRS_SRCBLEND,D3DBLEND_SRCALPHA);
            d->SetRenderState(D3DRS_DESTBLEND,D3DBLEND_INVSRCALPHA);
            d->SetRenderState(D3DRS_BLENDOP,D3DBLENDOP_ADD);
            Rectangle(d.Get(),44,44,40,0x80402010); // Translucent option panel.
            d->SetRenderState(D3DRS_COLORWRITEENABLE,0);
            d->SetRenderState(D3DRS_STENCILENABLE,TRUE);
            d->SetRenderState(D3DRS_STENCILFUNC,D3DCMP_ALWAYS);
            d->SetRenderState(D3DRS_STENCILREF,1);
            d->SetRenderState(D3DRS_STENCILMASK,255);
            d->SetRenderState(D3DRS_STENCILWRITEMASK,255);
            d->SetRenderState(D3DRS_STENCILFAIL,D3DSTENCILOP_KEEP);
            d->SetRenderState(D3DRS_STENCILZFAIL,D3DSTENCILOP_KEEP);
            d->SetRenderState(D3DRS_STENCILPASS,D3DSTENCILOP_REPLACE);
            Rectangle(d.Get(),54,54,20,0xffffffff);
            d->SetRenderState(D3DRS_STENCILFUNC,D3DCMP_EQUAL);
            d->SetRenderState(D3DRS_STENCILPASS,D3DSTENCILOP_KEEP);
            const RECT nestedClip={58,54,74,74};
            d->SetScissorRect(&nestedClip);
            d->SetRenderState(D3DRS_SCISSORTESTENABLE,TRUE);
        }
        d->SetRenderState(D3DRS_COLORWRITEENABLE,mode.mask);
        d->SetRenderState(D3DRS_ALPHABLENDENABLE,TRUE);
        d->SetRenderState(D3DRS_SRCBLEND,mode.src);
        d->SetRenderState(D3DRS_DESTBLEND,mode.dst);
        d->SetRenderState(D3DRS_BLENDOP,mode.op);
        Rectangle(d.Get(),nested ? 44.0f : 54.0f,nested ? 44.0f : 54.0f,nested ? 40.0f : 20.0f,mode.color);
    };
    unsigned blendCases=0;
    for(const auto& mode:modes) for(int nested=0;nested<2;++nested) {
        const DWORD background=0xffb08050;
        d->SetRenderState(D3DRS_SCISSORTESTENABLE,FALSE);
        Check(d->Clear(0,nullptr,D3DCLEAR_TARGET|D3DCLEAR_STENCIL,background,1,0),"direct background");
        Check(d->BeginScene(),"direct begin");drawLabel(mode,nested!=0);Check(d->EndScene(),"direct end");
        const DWORD expected=sample(64,64);
        const DWORD expectedEdge=sample(48,48);
        const DWORD expectedClipped=sample(55,64);
        panel.BeginFrame();
        Check(d->BeginScene(),"basis begin");Require(panel.Begin(d.Get(),0),"basis target");
        capturingPanel=&panel;captureLayer=0;drawLabel(mode,nested!=0);capturingPanel=nullptr;
        DWORD restoredSrc=0,restoredMask=0;
        d->GetRenderState(D3DRS_SRCBLEND,&restoredSrc);d->GetRenderState(D3DRS_COLORWRITEENABLE,&restoredMask);
        Require(restoredSrc==mode.src && restoredMask==mode.mask,"game blend and RGB-only write mask preserved");
        panel.End(d.Get(),0);Check(d->EndScene(),"basis end");
        for(int eye=0;eye<2;++eye) {
            d->SetRenderState(D3DRS_SCISSORTESTENABLE,FALSE);
            Check(d->Clear(0,nullptr,D3DCLEAR_TARGET,background,1,0),"replay background");
            Mat4 clip=Identity();clip.m[0][3]=eye ? 0.125f : -0.125f;
            Check(d->BeginScene(),"blend eye begin");Check(panel.Draw(d.Get(),0,clip),"blend eye replay");Check(d->EndScene(),"blend eye end");
            const DWORD actual=sample(eye ? 72 : 56,64);
            const DWORD actualEdge=sample(eye ? 56 : 40,48);
            const DWORD actualClipped=sample(eye ? 63 : 47,64);
            for(int c=0;c<3;++c) if(std::abs(int((actual>>(8*c))&255)-int((expected>>(8*c))&255))>2) {
                std::printf("blend mismatch: %s eye=%d expected=%08lX actual=%08lX\n",mode.label,eye,expected,actual);return 3;
            }
            for(int c=0;c<3;++c) Require(std::abs(int((actualEdge>>(8*c))&255)-int((expectedEdge>>(8*c))&255))<=2,
                "stencil clip and overlapping option background match direct rendering");
            for(int c=0;c<3;++c) Require(std::abs(int((actualClipped>>(8*c))&255)-int((expectedClipped>>(8*c))&255))<=2,
                "scissor rectangle survives capture target switches");
            Require((sample(4,4)&0xffffff)==(background&0xffffff),"outside menu remains scene colored");
            ++blendCases;
        }
    }
    DestroyWindow(window);
    std::printf("menu panel: %u GPU readback cases passed (both eyes, head turns, opaque UI, alpha, font, state restore, recreation)\n",cases);
    std::printf("menu blending: %u direct-versus-world-panel GPU cases passed (unselected/highlighted labels, multiply, additive, subtract, RGB-only writes, overlapping panels, stencil)\n",blendCases);
}
