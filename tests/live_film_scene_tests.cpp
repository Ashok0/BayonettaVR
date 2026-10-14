#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include "LiveFilmScene9.h"
#include <wrl/client.h>
#include <fstream>
#include <vector>
#include <cstdio>
#include <cstdlib>
using namespace bvr;
using Microsoft::WRL::ComPtr;
static void Require(bool ok,const char* why){if(!ok){printf("live film FAILED: %s\n",why);exit(1);}}
static void CheckAt(HRESULT hr,int line){if(FAILED(hr)){printf("live film HRESULT %08lX at line %d\n",hr,line);exit(2);}}
#define Check(hr) CheckAt((hr),__LINE__)
static std::vector<DWORD> Load(const char* name){
    std::ifstream f(std::string("tests/fixtures/live_film/")+name,std::ios::binary|std::ios::ate);
    Require(f.good(),"native fixture exists");const auto n=size_t(f.tellg());f.seekg(0);
    std::vector<DWORD> bytes((n+3)/4);f.read(reinterpret_cast<char*>(bytes.data()),n);return bytes;
}
int main(){
    HWND window=CreateWindowExW(0,L"STATIC",L"Live film regression",WS_OVERLAPPEDWINDOW,0,0,160,90,nullptr,nullptr,GetModuleHandleW(nullptr),nullptr);
    ComPtr<IDirect3D9Ex> api;Check(Direct3DCreate9Ex(D3D_SDK_VERSION,&api));
    D3DPRESENT_PARAMETERS pp{};pp.Windowed=TRUE;pp.SwapEffect=D3DSWAPEFFECT_DISCARD;pp.hDeviceWindow=window;
    pp.BackBufferWidth=160;pp.BackBufferHeight=90;pp.BackBufferFormat=D3DFMT_X8R8G8B8;
    ComPtr<IDirect3DDevice9Ex> d;Check(api->CreateDeviceEx(0,D3DDEVTYPE_HAL,window,D3DCREATE_HARDWARE_VERTEXPROCESSING,&pp,nullptr,&d));
    ComPtr<IDirect3DSurface9> target,other,depth,read;ComPtr<IDirect3DTexture9> texture;
    Check(d->CreateRenderTarget(160,90,D3DFMT_A8R8G8B8,D3DMULTISAMPLE_NONE,0,FALSE,&target,nullptr));
    Check(d->CreateRenderTarget(160,90,D3DFMT_A8R8G8B8,D3DMULTISAMPLE_NONE,0,FALSE,&other,nullptr));
    Check(d->CreateDepthStencilSurface(160,90,D3DFMT_D24S8,D3DMULTISAMPLE_NONE,0,FALSE,&depth,nullptr));
    Check(d->CreateOffscreenPlainSurface(160,90,D3DFMT_A8R8G8B8,D3DPOOL_SYSTEMMEM,&read,nullptr));
    Check(d->CreateTexture(160,90,1,D3DUSAGE_RENDERTARGET,D3DFMT_A8R8G8B8,D3DPOOL_DEFAULT,&texture,nullptr));
    Check(d->SetRenderTarget(0,target.Get()));Check(d->SetRenderTarget(1,other.Get()));Check(d->SetDepthStencilSurface(depth.Get()));
    Check(d->ColorFill(other.Get(),nullptr,0xffaabbcc));Check(d->SetTexture(0,texture.Get()));
    LiveFilmScene9 film;ComPtr<IDirect3DPixelShader9> ps[3];ComPtr<IDirect3DVertexShader9> vs[3];
    const char* pixels[]={"B8690130.pso","CA043AE5.pso","0103F1A9.pso"};
    const char* vertices[]={"CB1E6A15.vso","3379538A.vso","67B951BD.vso"};
    const FilmVertex roles[]={FilmVertex::Copy,FilmVertex::Card,FilmVertex::Backdrop};
    for(int i=0;i<3;++i){
        auto p=Load(pixels[i]);Check(d->CreatePixelShader(p.data(),&ps[i]));film.Register(ps[i].Get());
        auto v=Load(vertices[i]);Check(d->CreateVertexShader(v.data(),&vs[i]));const auto crc=ShaderCrc32(v.data(),v.size()*4);
        Require(LiveFilmVertex(crc,v.size()*4)==roles[i]&&LiveFilmVertex(crc,v.size()*4+4)==FilmVertex::None,"exact native vertex fingerprints");
    }
    const Mat4 copy={{{1,0,0,-1},{0,-1,0,1},{0,0,-1.52590219e-5f,.000152590219f},{0,0,0,1}}};
    const Mat4 card={{{4.27261114f,0,0,0},{0,7.59575367f,0,0},{0,0,-1.00001514f,6.10652351f},{0,0,-.99999994f,6.20642996f}}};
    auto bind=[&](int i){Check(d->SetVertexShader(vs[i].Get()));Check(d->SetPixelShader(ps[i].Get()));};
    auto pixelsFrom=[&](IDirect3DSurface9* surface){
        Check(d->GetRenderTargetData(surface,read.Get()));D3DLOCKED_RECT r{};Check(read->LockRect(&r,nullptr,D3DLOCK_READONLY));
        std::vector<DWORD> out(160*90);for(int y=0;y<90;++y)memcpy(out.data()+160*y,static_cast<char*>(r.pBits)+y*r.Pitch,160*4);
        Check(read->UnlockRect());return out;
    };
    unsigned cases=0;std::vector<DWORD> previous;
    for(int cycle=0;cycle<2;++cycle)for(int pose=0;pose<3;++pose)for(int eye=0;eye<2;++eye){
        film.BeginEye();bind(1);
        Require(!film.Handle(d.Get(),8,card,FilmVertex::Card,true),"never promote prior eye/frame or prerecorded content without live snapshot");
        const DWORD color=0x00103050+DWORD(cycle*0x300000+pose*0x10000+eye*0x400000);
        Check(d->ColorFill(target.Get(),nullptr,color));
        const RECT patch{24+eye*10+pose*7,15,65+eye*10+pose*7,60};
        Check(d->ColorFill(target.Get(),&patch,0xff90c020+DWORD(eye*0x40+pose)));
        const auto expected=pixelsFrom(target.Get());
        Require(previous.empty()||previous!=expected,"distinct eye/pose source images");previous=expected;
        bind(0);Check(d->SetRenderState(D3DRS_ALPHABLENDENABLE,FALSE));
        Check(d->BeginScene());
        auto wrong=copy;wrong.m[0][0]=2;
        Require(!film.Handle(d.Get(),24,copy,FilmVertex::Copy,false),"disabled snapshot");
        Require(!film.Handle(d.Get(),8,copy,FilmVertex::Copy,true),"other register excluded");
        Require(!film.Handle(d.Get(),24,wrong,FilmVertex::Copy,true),"ordinary fullscreen copy excluded");
        bind(1);Require(!film.Handle(d.Get(),8,card,FilmVertex::Card,true),"negative cases did not capture");bind(0);
        Require(!film.Handle(d.Get(),24,copy,FilmVertex::Copy,true),"snapshot retains original setup draw");
        Check(d->ColorFill(target.Get(),nullptr,0xff000000));
        Require(!film.Handle(d.Get(),24,copy,FilmVertex::Copy,true),"second thumbnail cannot overwrite live scene snapshot");
        bind(1);Check(d->SetRenderState(D3DRS_ALPHABLENDENABLE,TRUE));
        const RECT scissor{7,9,95,70};Check(d->SetScissorRect(&scissor));Check(d->SetRenderState(D3DRS_SCISSORTESTENABLE,TRUE));
        Check(d->SetVertexShaderConstantF(8,&card.m[0][0],4));
        Check(d->SetRenderTarget(1,nullptr));
        Check(d->SetRenderTarget(0,other.Get()));Require(!film.Handle(d.Get(),8,card,FilmVertex::Card,true),"other target cannot consume snapshot");
        Check(d->SetRenderTarget(0,target.Get()));Check(d->SetRenderTarget(1,other.Get()));
        Check(d->SetScissorRect(&scissor));
        Require(film.Handle(d.Get(),8,card,FilmVertex::Card,true)&&film.Promoted(),"live scene replaces native film card");
        bind(2);wrong=card;wrong.m[3][3]+=1;
        Require(!film.Handle(d.Get(),8,wrong,FilmVertex::Backdrop,true),"unrelated material/camera stays visible");
        Require(film.Handle(d.Get(),8,card,FilmVertex::Backdrop,true),"matching film backdrop omitted");
        Require(!film.Handle(d.Get(),8,card,FilmVertex::Backdrop,false),"disabled backdrop unchanged");
        ComPtr<IDirect3DSurface9> current;Check(d->GetRenderTarget(0,&current));Require(current.Get()==target.Get(),"target binding unchanged");
        Check(d->GetRenderTarget(1,&current));Require(current.Get()==other.Get(),"MRT unchanged");
        Check(d->GetDepthStencilSurface(&current));Require(current.Get()==depth.Get(),"depth binding unchanged");
        Mat4 restored{};Check(d->GetVertexShaderConstantF(8,&restored.m[0][0],4));Require(!memcmp(&restored,&card,sizeof(card)),"camera constants unchanged");
        RECT after{};DWORD enabled=0;Check(d->GetScissorRect(&after));Check(d->GetRenderState(D3DRS_SCISSORTESTENABLE,&enabled));
        Require(enabled&&!memcmp(&after,&scissor,sizeof(scissor)),"scissor state unchanged");
        Check(d->EndScene());Require(pixelsFrom(target.Get())==expected,"every pixel is current-eye scene, with no second projection or resampling");
        const auto secondary=pixelsFrom(other.Get());for(auto p:secondary)Require(p==0xffaabbcc,"MRT image unchanged");
        Check(d->SetRenderState(D3DRS_SCISSORTESTENABLE,FALSE));++cases;
        film.BeginEye();Require(!film.Handle(d.Get(),8,card,FilmVertex::Backdrop,true),"backdrop not hidden next eye/frame");
        if(eye==1&&pose==2){film.ReleaseResources();for(auto& p:ps)film.Register(p.Get());}
    }
    film.Reset();bind(0);Require(!film.Handle(d.Get(),24,copy,FilmVertex::Copy,true),"reset clears native shader recognition");
    DestroyWindow(window);printf("Live film: %u independent eye/pose/recreation images (%u pixels) preserved exactly; native signatures, sequence, camera/target/disabled exclusions, one snapshot per eye, backdrop scope and MRT/state preservation passed.\n",cases,cases*160*90);
}
