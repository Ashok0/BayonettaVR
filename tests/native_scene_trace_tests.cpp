#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include "NativeSceneTrace9.h"
#include <wrl/client.h>
#include <filesystem>
#include <fstream>
#include <iterator>
using namespace bvr;
using Microsoft::WRL::ComPtr;
static void Require(bool ok,const char* reason){if(!ok){printf("native trace FAILED: %s\n",reason);exit(1);}}
static void Check(HRESULT hr){if(FAILED(hr)){printf("native trace HRESULT %08lX\n",hr);exit(2);}}
int main(){
    HWND window=CreateWindowExW(0,L"STATIC",L"Native trace checks",WS_OVERLAPPEDWINDOW,0,0,640,360,nullptr,nullptr,GetModuleHandleW(nullptr),nullptr);
    ComPtr<IDirect3D9Ex> api;Check(Direct3DCreate9Ex(D3D_SDK_VERSION,&api));
    D3DPRESENT_PARAMETERS pp{};pp.Windowed=TRUE;pp.SwapEffect=D3DSWAPEFFECT_DISCARD;pp.hDeviceWindow=window;
    pp.BackBufferWidth=640;pp.BackBufferHeight=360;pp.BackBufferFormat=D3DFMT_X8R8G8B8;
    ComPtr<IDirect3DDevice9Ex> d;Check(api->CreateDeviceEx(0,D3DDEVTYPE_HAL,window,D3DCREATE_HARDWARE_VERTEXPROCESSING,&pp,nullptr,&d));
    ComPtr<IDirect3DSurface9> target,read,depth;
    ComPtr<IDirect3DTexture9> sceneTexture;
    Check(d->CreateTexture(640,360,1,D3DUSAGE_RENDERTARGET,D3DFMT_A8R8G8B8,D3DPOOL_DEFAULT,&sceneTexture,nullptr));
    Check(sceneTexture->GetSurfaceLevel(0,&target));
    Check(d->CreateOffscreenPlainSurface(640,360,D3DFMT_A8R8G8B8,D3DPOOL_SYSTEMMEM,&read,nullptr));
    Check(d->CreateDepthStencilSurface(640,360,D3DFMT_D24S8,D3DMULTISAMPLE_NONE,0,FALSE,&depth,nullptr));
    Check(d->SetRenderTarget(0,target.Get()));Check(d->SetDepthStencilSurface(depth.Get()));
    Check(d->Clear(0,nullptr,D3DCLEAR_TARGET|D3DCLEAR_ZBUFFER|D3DCLEAR_STENCIL,0x00405060,1,0));
    const RECT scissor{11,17,210,140};Check(d->SetScissorRect(&scissor));Check(d->SetRenderState(D3DRS_SCISSORTESTENABLE,TRUE));
    Check(d->SetRenderState(D3DRS_ZENABLE,TRUE));Check(d->SetRenderState(D3DRS_CULLMODE,D3DCULL_CW));
    Check(d->SetFVF(D3DFVF_XYZRHW|D3DFVF_DIFFUSE));
    const char shader[]="float4 vs(float4 p:POSITION):POSITION{return p;}float4 ps():COLOR{return float4(1,0,0,1);}";
    ComPtr<ID3DBlob> blob;ComPtr<IDirect3DVertexShader9> originalVS;ComPtr<IDirect3DPixelShader9> originalPS;
    Check(D3DCompile(shader,sizeof(shader)-1,nullptr,nullptr,nullptr,"vs","vs_2_0",0,0,&blob,nullptr));
    Check(d->CreateVertexShader(static_cast<const DWORD*>(blob->GetBufferPointer()),&originalVS));blob.Reset();
    Check(D3DCompile(shader,sizeof(shader)-1,nullptr,nullptr,nullptr,"ps","ps_2_0",0,0,&blob,nullptr));
    Check(d->CreatePixelShader(static_cast<const DWORD*>(blob->GetBufferPointer()),&originalPS));
    Check(d->SetVertexShader(originalVS.Get()));Check(d->SetPixelShader(originalPS.Get()));
    const float originalConstants[4]{9,8,7,6};Check(d->SetPixelShaderConstantF(0,originalConstants,1));
    ComPtr<IDirect3DSurface9> secondary;
    Check(d->CreateRenderTarget(640,360,D3DFMT_A8R8G8B8,D3DMULTISAMPLE_NONE,0,FALSE,&secondary,nullptr));
    Check(d->SetRenderTarget(1,secondary.Get()));
    Check(d->ColorFill(secondary.Get(),nullptr,0xff405060));
    ComPtr<IDirect3DVertexBuffer9> originalStream;
    Check(d->CreateVertexBuffer(1024,0,0,D3DPOOL_DEFAULT,&originalStream,nullptr));
    Check(d->SetStreamSource(0,originalStream.Get(),64,24));
    const auto path=std::filesystem::path(L"build/test")/(L"native_trace_"+std::to_wstring(GetCurrentProcessId())+L"_"+std::to_wstring(GetTickCount64()));
    NativeSceneTrace9 trace;Require(!trace.Active(),"disabled without F12");
    Require(trace.Begin(d.Get(),path.c_str(),nullptr),"begin explicit trace");
    std::vector<ComPtr<IDirect3DTexture9>> textures(70);
    Check(d->BeginScene());
    for(unsigned i=0;i<textures.size();++i){
        Check(d->CreateTexture(256,144,1,D3DUSAGE_DYNAMIC,D3DFMT_A8R8G8B8,D3DPOOL_DEFAULT,&textures[i],nullptr));
        D3DLOCKED_RECT locked{};Check(textures[i]->LockRect(0,&locked,nullptr,D3DLOCK_DISCARD));
        for(int y=0;y<144;++y)for(int x=0;x<256;++x)
            reinterpret_cast<DWORD*>(static_cast<char*>(locked.pBits)+y*locked.Pitch)[x]=(i==63?0x00113300:0xff113300)+i;
        Check(textures[i]->UnlockRect(0));Check(d->SetTexture(0,textures[i].Get()));
        trace.Record(d.Get(),2,24,Identity(),i==0);
        ComPtr<IDirect3DSurface9> current,ds;ComPtr<IDirect3DBaseTexture9> texture;
        Check(d->GetRenderTarget(0,&current));Check(d->GetDepthStencilSurface(&ds));Check(d->GetTexture(0,&texture));
        Require(current.Get()==target.Get() && ds.Get()==depth.Get() && texture.Get()==textures[i].Get(),"target/depth/texture restored");
        ComPtr<IDirect3DSurface9> restoredSecondary;Check(d->GetRenderTarget(1,&restoredSecondary));
        Require(restoredSecondary.Get()==secondary.Get(),"secondary render target restored");
        ComPtr<IDirect3DVertexBuffer9> restoredStream;UINT streamOffset=0,streamStride=0;
        Check(d->GetStreamSource(0,&restoredStream,&streamOffset,&streamStride));
        Require(restoredStream.Get()==originalStream.Get() && streamOffset==64 && streamStride==24,"vertex stream and nonzero offset restored");
        ComPtr<IDirect3DVertexShader9> restoredVS;ComPtr<IDirect3DPixelShader9> restoredPS;
        Check(d->GetVertexShader(&restoredVS));Check(d->GetPixelShader(&restoredPS));
        Require(restoredVS.Get()==originalVS.Get() && restoredPS.Get()==originalPS.Get(),"original shader objects restored");
        float restoredConstants[4]{};Check(d->GetPixelShaderConstantF(0,restoredConstants,1));
        Require(!memcmp(restoredConstants,originalConstants,sizeof(originalConstants)),"preview mode constant restored");
        D3DVIEWPORT9 vp{};RECT rect{};DWORD enabled=0,cull=0,fvf=0;
        Check(d->GetViewport(&vp));Check(d->GetScissorRect(&rect));Check(d->GetRenderState(D3DRS_SCISSORTESTENABLE,&enabled));
        Check(d->GetRenderState(D3DRS_CULLMODE,&cull));Check(d->GetFVF(&fvf));
        Require(vp.Width==640 && vp.Height==360 && !memcmp(&rect,&scissor,sizeof(rect)) && enabled && cull==D3DCULL_CW && fvf==(D3DFVF_XYZRHW|D3DFVF_DIFFUSE),"viewport/scissor/cull/vertex layout restored");
    }
    // Late draws must keep their checkpoints, then stop at the hard draw cap.
    for(unsigned i=70;i<2100;++i)trace.Record(d.Get(),2,24,Identity(),false);
    Require(trace.Finish(d.Get())==2112,"64 textures plus 2048 scene checkpoints within engine-owned scene");Check(d->EndScene());
    Require(!trace.Active() && !trace.Busy(),"trace disarmed after one frame");
    Check(d->GetRenderTargetData(target.Get(),read.Get()));D3DLOCKED_RECT locked{};Check(read->LockRect(&locked,nullptr,D3DLOCK_READONLY));
    for(int y=0;y<360;++y)for(int x=0;x<640;++x)Require(reinterpret_cast<DWORD*>(static_cast<char*>(locked.pBits)+y*locked.Pitch)[x]==0x00405060,"scene image and zero alpha unchanged");
    Check(read->UnlockRect());
    Check(d->GetRenderTargetData(secondary.Get(),read.Get()));Check(read->LockRect(&locked,nullptr,D3DLOCK_READONLY));
    for(int y=0;y<360;++y)for(int x=0;x<640;++x)Require(reinterpret_cast<DWORD*>(static_cast<char*>(locked.pBits)+y*locked.Pitch)[x]==0xff405060,"secondary scene image unchanged");
    Check(read->UnlockRect());
    std::ifstream metadata(path/L"draws.txt");const std::string text((std::istreambuf_iterator<char>(metadata)),{});
    Require(text.find("end draws=2048 savedPreviews=2112")!=std::string::npos,"metadata draw cap and completion");
    Require(std::filesystem::exists(path/L"draw0001_texture0.bmp"),"orthographic card preview included");
    Require(std::filesystem::exists(path/L"draw2048_scene.bmp"),"late scene draw captured");
    Require(!std::filesystem::exists(path/L"draw2049_scene.bmp"),"scene draw cap enforced");
    Require(!std::filesystem::exists(path/L"draw0065_texture0.bmp"),"texture preview cap enforced");
    unsigned previews=0;
    for(const auto& entry:std::filesystem::directory_iterator(path))if(entry.path().extension()==L".bmp"){
        std::ifstream bmp(entry.path(),std::ios::binary);BITMAPFILEHEADER file{};BITMAPINFOHEADER info{};DWORD pixel=0;
        bmp.read(reinterpret_cast<char*>(&file),sizeof(file));bmp.read(reinterpret_cast<char*>(&info),sizeof(info));bmp.read(reinterpret_cast<char*>(&pixel),4);
        Require(info.biWidth==256 && info.biHeight==-144 && file.bfSize==256*144*4+54,"small preview dimensions");
        const bool scene=entry.path().filename().wstring().find(L"_scene")!=std::wstring::npos;
        const bool transparent=entry.path().filename()==L"draw0064_texture0.bmp";
        Require(scene?pixel==0xff405060:transparent?pixel==0xff000000:(pixel&0xffff00)==0x113300,"scene RGB ignores alpha; texture RGB includes alpha");++previews;
    }
    Require(previews==2112,"preview file count");trace.Finish(nullptr);
    // The real room's color target is NOT texture-backed. Preserve both
    // checkpoints at use inside BeginScene, before the same surface changes.
    ComPtr<IDirect3DSurface9> standalone;
    Check(d->CreateRenderTarget(640,360,D3DFMT_A8R8G8B8,D3DMULTISAMPLE_NONE,0,FALSE,&standalone,nullptr));
    Check(d->SetRenderTarget(0,standalone.Get()));Check(d->ColorFill(standalone.Get(),nullptr,0x00123456));
    ComPtr<IDirect3DCubeTexture9> cube;
    Check(d->CreateCubeTexture(64,1,0,D3DFMT_A8R8G8B8,D3DPOOL_DEFAULT,&cube,nullptr));
    Check(d->SetTexture(5,cube.Get()));Check(d->SetTexture(12,textures[0].Get()));
    float vc[64]{},pc[96]{};for(int i=0;i<64;++i)vc[i]=float(i);for(int i=0;i<96;++i)pc[i]=float(i+100);
    Check(d->SetVertexShaderConstantF(0,vc,16));Check(d->SetPixelShaderConstantF(40,pc,24));
    const auto standalonePath=path/L"standalone";Require(trace.Begin(d.Get(),standalonePath.c_str(),nullptr),"standalone trace starts");
    Check(d->BeginScene());trace.Record(d.Get(),64,8,Identity(),false);
    Check(d->ColorFill(standalone.Get(),nullptr,0xff654321));trace.Record(d.Get(),64,8,Identity(),false);
    Require(trace.Finish(d.Get())==2,"standalone color checkpoints work inside native scene");Check(d->EndScene());
    for(unsigned i=1;i<=2;++i) {
        const auto name=i==1?L"draw0001_scene.bmp":L"draw0002_scene.bmp";
        std::ifstream f(standalonePath/name,std::ios::binary);Require(f.good(),"standalone color image saved");
        f.seekg(54);DWORD pixel=0;
        for(unsigned p=0;p<256*144;++p){f.read(reinterpret_cast<char*>(&pixel),4);Require((pixel&0xffffff)==(i==1?0x123456u:0x654321u),"snapshot contains color at that draw, regardless of alpha");}
    }
    ComPtr<IDirect3DSurface9> current;Check(d->GetRenderTarget(0,&current));Require(current.Get()==standalone.Get(),"standalone target still bound");
    Check(d->GetRenderTarget(1,&current));Require(current.Get()==secondary.Get(),"standalone capture preserves MRT binding");
    Check(d->GetRenderTargetData(secondary.Get(),read.Get()));Check(read->LockRect(&locked,nullptr,D3DLOCK_READONLY));
    for(int y=0;y<360;++y)for(int x=0;x<640;++x)Require(reinterpret_cast<DWORD*>(static_cast<char*>(locked.pBits)+y*locked.Pitch)[x]==0xff405060,"standalone capture preserves secondary pixels");
    Check(read->UnlockRect());
    float afterVC[64]{},afterPC[96]{};Check(d->GetVertexShaderConstantF(0,afterVC,16));Check(d->GetPixelShaderConstantF(40,afterPC,24));
    Require(!memcmp(vc,afterVC,sizeof(vc))&&!memcmp(pc,afterPC,sizeof(pc)),"additional captured constants unchanged");
    std::ifstream extraMetadata(standalonePath/L"draws.txt");const std::string extra((std::istreambuf_iterator<char>(extraMetadata)),{});
    Require(extra.find("cube5=")!=std::string::npos&&extra.find("texture12=")!=std::string::npos,"cube and higher sampler slots recorded");
    Require(extra.find("vc12 48 49 50 51")!=std::string::npos&&extra.find("pc40 100 101 102 103")!=std::string::npos,"view and material constants recorded");
    DestroyWindow(window);
    printf("native scene trace: 2048 draw cap, 64 texture and 2048 scene checkpoints, texture alpha visibility, scene RGB, both MRT images/bindings and scene/depth/texture/viewport/scissor/layout/constants preserved; explicit arm/disarm passed\n");
}
