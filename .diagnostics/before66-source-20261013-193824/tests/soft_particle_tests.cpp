#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include "SoftParticleDepth.h"
#include "Matrix.h"
#include <wrl/client.h>
#include <fstream>
#include <vector>
#include <cstdio>
#include <cstdlib>
using namespace bvr;
using Microsoft::WRL::ComPtr;
static void Require(bool yes,const char* label){if(!yes){printf("soft depth FAILED: %s\n",label);exit(1);}}
static void Check(HRESULT hr){if(FAILED(hr)){printf("soft depth HRESULT %08lX\n",hr);exit(2);}}
static std::vector<DWORD> Load(const char* path){std::ifstream f(path,std::ios::binary|std::ios::ate);Require(f.good(),path);size_t n=size_t(f.tellg());f.seekg(0);std::vector<DWORD> b(n/4);f.read(reinterpret_cast<char*>(b.data()),n);return b;}
int main(){
    HWND window=CreateWindowExW(0,L"STATIC",L"Soft depth regression",WS_OVERLAPPEDWINDOW,0,0,96,96,nullptr,nullptr,GetModuleHandleW(nullptr),nullptr);
    ComPtr<IDirect3D9Ex> api;Check(Direct3DCreate9Ex(D3D_SDK_VERSION,&api));
    D3DPRESENT_PARAMETERS pp{};pp.Windowed=TRUE;pp.SwapEffect=D3DSWAPEFFECT_DISCARD;pp.hDeviceWindow=window;pp.BackBufferWidth=pp.BackBufferHeight=96;pp.BackBufferFormat=D3DFMT_X8R8G8B8;
    ComPtr<IDirect3DDevice9Ex> d;Check(api->CreateDeviceEx(0,D3DDEVTYPE_HAL,window,D3DCREATE_HARDWARE_VERTEXPROCESSING,&pp,nullptr,&d));
    auto psBytes=Load("tests/fixtures/soft_particle/original.ps.bin"),vsBytes=Load("tests/fixtures/soft_particle/original.vs.bin");
    Require(IsSoftParticleShader(ShaderCrc32(psBytes.data(),psBytes.size()*4),psBytes.size()*4),"exact pixel shader");
    Require(!IsSoftParticleShader(0xe166cf17,556)&&!IsSoftParticleShader(0xe166cf16,552),"reject near matches");
    ComPtr<IDirect3DPixelShader9> ps;ComPtr<IDirect3DVertexShader9> vs;
    Check(d->CreatePixelShader(psBytes.data(),&ps));Check(d->CreateVertexShader(vsBytes.data(),&vs));
    SoftParticleDepth fix;Require(fix.Register(ps.Get())==0xe166cf17,"registration");
    Check(d->SetPixelShader(ps.Get()));Check(d->SetVertexShader(vs.Get()));
    ComPtr<IDirect3DStateBlock9> block;Check(d->CreateStateBlock(D3DSBT_PIXELSTATE,&block));
    Check(d->SetPixelShader(nullptr));Require(!fix.Matches(d.Get(),true),"unrelated shader");Check(block->Apply());Require(fix.Matches(d.Get(),true),"state block binding");
    const D3DVERTEXELEMENT9 elements[]={{0,0,D3DDECLTYPE_FLOAT3,0,D3DDECLUSAGE_POSITION,0},{0,12,D3DDECLTYPE_FLOAT2,0,D3DDECLUSAGE_TEXCOORD,0},D3DDECL_END()};
    ComPtr<IDirect3DVertexDeclaration9> decl;Check(d->CreateVertexDeclaration(elements,&decl));Check(d->SetVertexDeclaration(decl.Get()));
    ComPtr<IDirect3DSurface9> target,read;Check(d->CreateRenderTarget(96,96,D3DFMT_A8R8G8B8,D3DMULTISAMPLE_NONE,0,FALSE,&target,nullptr));Check(d->CreateOffscreenPlainSurface(96,96,D3DFMT_A8R8G8B8,D3DPOOL_SYSTEMMEM,&read,nullptr));Check(d->SetRenderTarget(0,target.Get()));
    ComPtr<IDirect3DTexture9> color,depth;
    Check(d->CreateTexture(1,1,1,D3DUSAGE_DYNAMIC,D3DFMT_A8R8G8B8,D3DPOOL_DEFAULT,&color,nullptr));
    D3DLOCKED_RECT lock{};Check(color->LockRect(0,&lock,nullptr,D3DLOCK_DISCARD));*static_cast<DWORD*>(lock.pBits)=0xffff0000;Check(color->UnlockRect(0));
    const Mat4 game=PerspectiveFovLH(.7f,1.f,.1f,6553.f);
    Check(d->CreateTexture(64,64,1,D3DUSAGE_DYNAMIC,D3DFMT_R32F,D3DPOOL_DEFAULT,&depth,nullptr));
    Check(depth->LockRect(0,&lock,nullptr,D3DLOCK_DISCARD));
    for(int y=0;y<64;++y)for(int x=0;x<64;++x){const float z=((x/8+y/8)&1)?100.f:.5f;reinterpret_cast<float*>(static_cast<char*>(lock.pBits)+y*lock.Pitch)[x]=1.f-game.m[2][2]-game.m[2][3]/z;}
    Check(depth->UnlockRect(0));Check(d->SetTexture(0,color.Get()));Check(d->SetTexture(1,depth.Get()));
    for(int i=0;i<2;++i){Check(d->SetSamplerState(i,D3DSAMP_MINFILTER,D3DTEXF_POINT));Check(d->SetSamplerState(i,D3DSAMP_MAGFILTER,D3DTEXF_POINT));Check(d->SetSamplerState(i,D3DSAMP_ADDRESSU,D3DTADDRESS_CLAMP));Check(d->SetSamplerState(i,D3DSAMP_ADDRESSV,D3DTADDRESS_CLAMP));}
    Check(d->SetRenderState(D3DRS_CULLMODE,D3DCULL_NONE));Check(d->SetRenderState(D3DRS_ZENABLE,FALSE));Check(d->SetRenderState(D3DRS_ALPHABLENDENABLE,FALSE));Check(d->SetRenderState(D3DRS_ALPHATESTENABLE,FALSE));
    const float white[4]={1,1,1,1},saved[4]={3,5,7,11},tmp[4]={0,0,-game.m[2][2],game.m[2][3]};
    Check(d->SetPixelShaderConstantF(184,white,1));Check(d->SetPixelShaderConstantF(185,white,1));Check(d->SetPixelShaderConstantF(223,saved,1));Check(d->SetVertexShaderConstantF(6,tmp,1));
    const float quad[][5]={{-2,2,3,0,0},{2,2,3,1,0},{-2,-2,3,0,1},{2,-2,3,1,1}};
    auto pixels=[&](){std::vector<DWORD> out(96*96);Check(d->GetRenderTargetData(target.Get(),read.Get()));Check(read->LockRect(&lock,nullptr,D3DLOCK_READONLY));for(int y=0;y<96;++y)memcpy(out.data()+96*y,static_cast<char*>(lock.pBits)+y*lock.Pitch,96*4);Check(read->UnlockRect());return out;};
    unsigned cases=0,wrongBefore=0,checked=0;
    for(float yaw:{-.5f,0.f,.5f})for(float pitch:{-.3f,0.f,.3f})for(int eye=0;eye<2;++eye)for(UINT offset:{0u,11u}){
        D3DVIEWPORT9 viewport{offset,offset,64,64,0,1};Check(d->SetViewport(&viewport));
        Mat4 ry=Identity(),rx=Identity();ry.m[0][0]=ry.m[2][2]=cosf(yaw);ry.m[0][2]=sinf(yaw);ry.m[2][0]=-sinf(yaw);rx.m[1][1]=rx.m[2][2]=cosf(pitch);rx.m[1][2]=-sinf(pitch);rx.m[2][1]=sinf(pitch);
        Mat4 view=Mul(rx,ry);view.m[0][3]=eye?.032f:-.032f;
        Mat4 projection=WithSceneDepth(PerspectiveRawLH(eye?-.9f:-1.1f,eye?1.1f:.9f,-1,1,.1f,100000),game);
        Mat4 clip=Mul(projection,view);Check(d->SetVertexShaderConstantF(24,&clip.m[0][0],4));
        auto draw=[&](bool enabled){Check(d->Clear(0,nullptr,D3DCLEAR_TARGET,0xff0000ff,1,0));Check(d->BeginScene());{SoftParticleDepthScope guard(d.Get(),fix,enabled);Require(guard.Active()==enabled,"scope applied");Check(d->DrawPrimitiveUP(D3DPT_TRIANGLESTRIP,2,quad,sizeof(quad[0])));}Check(d->EndScene());float after[4]{};Check(d->GetPixelShaderConstantF(223,after,1));Require(memcmp(saved,after,sizeof(saved))==0,"constant restored");ComPtr<IDirect3DPixelShader9> current;Check(d->GetPixelShader(&current));Require(current.Get()==ps.Get(),"binding restored");return pixels();};
        const auto original=draw(false),fixed=draw(true);
        for(int y=0;y<64;++y)for(int x=0;x<64;++x){const int i=(y+offset)*96+x+offset;if((fixed[i]&0xffffff)!=0xff0000)continue;const unsigned expected=((x/8+y/8)&1)?255:0;const unsigned actual=fixed[i]>>24;if(actual!=expected){printf("yaw %.2f pitch %.2f eye %d offset %u pixel %d,%d alpha %u expected %u\n",yaw,pitch,eye,offset,x,y,actual,expected);Require(false,"depth mask aligned to rendered pixel");}if((original[i]>>24)!=expected)++wrongBefore;++checked;}
        Require(draw(false)==original,"toggle restores original pixels");++cases;
    }
    Require(wrongBefore>1000&&checked>10000,"regression reproduces substantial original distortion");
    auto after=psBytes;UINT size=UINT(after.size()*4);Check(ps->GetFunction(after.data(),&size));Require(after==psBytes,"game shader bytecode unchanged");
    fix.ReleaseReplacement();{SoftParticleDepthScope guard(d.Get(),fix,true);Require(guard.Active(),"recreate after reset");}fix.Reset();Require(!fix.Matches(d.Get(),true),"reset classification");
    DestroyWindow(window);printf("soft depth: %u GPU cases, %u verified pixels; %u original misplaced mask pixels corrected; toggle/state blocks/reset/metadata passed\n",cases,checked,wrongBefore);
}
