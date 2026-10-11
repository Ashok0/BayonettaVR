#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include "WorldHud.h"
#include "ShaderAnalysis.h"
#include <d3d9.h>
#include <wrl/client.h>
#include <fstream>
#include <vector>
#include <cstdio>
#include <cstdlib>
using namespace bvr;
using Microsoft::WRL::ComPtr;
static void Require(bool ok,const char* why){if(!ok){printf("movie FAILED: %s\n",why);exit(1);}}
static void Check(HRESULT hr){if(FAILED(hr)){printf("movie HRESULT %08lX\n",hr);exit(2);}}
static std::vector<DWORD> Load(const char* path){std::ifstream f(path,std::ios::binary|std::ios::ate);Require(f.good(),path);size_t n=size_t(f.tellg());f.seekg(0);std::vector<DWORD> b(n/4);f.read(reinterpret_cast<char*>(b.data()),n);return b;}
static Mat4 Pose(float yaw,float pitch,float x){Mat4 ry=Identity(),rx=Identity();ry.m[0][0]=ry.m[2][2]=cosf(yaw);ry.m[0][2]=sinf(yaw);ry.m[2][0]=-sinf(yaw);rx.m[1][1]=rx.m[2][2]=cosf(pitch);rx.m[1][2]=-sinf(pitch);rx.m[2][1]=sinf(pitch);auto p=Mul(ry,rx);p.m[0][3]=x;return p;}
int main(){
    HWND window=CreateWindowExW(0,L"STATIC",L"Movie projection regression",WS_OVERLAPPEDWINDOW,0,0,128,128,nullptr,nullptr,GetModuleHandleW(nullptr),nullptr);
    ComPtr<IDirect3D9Ex> api;Check(Direct3DCreate9Ex(D3D_SDK_VERSION,&api));
    D3DPRESENT_PARAMETERS pp{};pp.Windowed=TRUE;pp.SwapEffect=D3DSWAPEFFECT_DISCARD;pp.hDeviceWindow=window;pp.BackBufferWidth=pp.BackBufferHeight=128;pp.BackBufferFormat=D3DFMT_X8R8G8B8;
    ComPtr<IDirect3DDevice9Ex> d;Check(api->CreateDeviceEx(0,D3DDEVTYPE_HAL,window,D3DCREATE_HARDWARE_VERTEXPROCESSING,&pp,nullptr,&d));
    auto v=Load("tests/fixtures/movie/smp_video_yuv_rgb_conv_shader_vs.vso"),p=Load("tests/fixtures/movie/smp_video_yuv_rgb_conv_shader_vs.pso");
    ComPtr<IDirect3DVertexShader9> vs;ComPtr<IDirect3DPixelShader9> ps;
    Check(d->CreateVertexShader(v.data(),&vs));Check(d->CreatePixelShader(p.data(),&ps));Check(d->SetVertexShader(vs.Get()));Check(d->SetPixelShader(ps.Get()));
    const D3DVERTEXELEMENT9 elements[]={{0,0,D3DDECLTYPE_FLOAT3,0,D3DDECLUSAGE_POSITION,0},{0,12,D3DDECLTYPE_FLOAT2,0,D3DDECLUSAGE_TEXCOORD,0},D3DDECL_END()};
    ComPtr<IDirect3DVertexDeclaration9> declaration;Check(d->CreateVertexDeclaration(elements,&declaration));Check(d->SetVertexDeclaration(declaration.Get()));
    ComPtr<IDirect3DSurface9> target,read;Check(d->CreateRenderTarget(128,128,D3DFMT_A8R8G8B8,D3DMULTISAMPLE_NONE,0,FALSE,&target,nullptr));Check(d->CreateOffscreenPlainSurface(128,128,D3DFMT_A8R8G8B8,D3DPOOL_SYSTEMMEM,&read,nullptr));Check(d->SetRenderTarget(0,target.Get()));
    ComPtr<IDirect3DTexture9> textures[3];D3DLOCKED_RECT locked{};
    for(int i=0;i<3;++i){Check(d->CreateTexture(32,32,1,D3DUSAGE_DYNAMIC,D3DFMT_A8R8G8B8,D3DPOOL_DEFAULT,&textures[i],nullptr));Check(textures[i]->LockRect(0,&locked,nullptr,D3DLOCK_DISCARD));for(int y=0;y<32;++y)for(int x=0;x<32;++x)reinterpret_cast<DWORD*>(static_cast<char*>(locked.pBits)+y*locked.Pitch)[x]=DWORD(i?128:64+((x/4+y/4)&1)*128)<<24;Check(textures[i]->UnlockRect(0));Check(d->SetTexture(i,textures[i].Get()));Check(d->SetSamplerState(i,D3DSAMP_MINFILTER,D3DTEXF_POINT));Check(d->SetSamplerState(i,D3DSAMP_MAGFILTER,D3DTEXF_POINT));Check(d->SetSamplerState(i,D3DSAMP_ADDRESSU,D3DTADDRESS_CLAMP));Check(d->SetSamplerState(i,D3DSAMP_ADDRESSV,D3DTADDRESS_CLAMP));}
    d->SetRenderState(D3DRS_ZENABLE,FALSE);d->SetRenderState(D3DRS_CULLMODE,D3DCULL_NONE);d->SetRenderState(D3DRS_ALPHABLENDENABLE,FALSE);
    const float quad[][5]={{0,0,0,0,0},{1280,0,0,1,0},{0,720,0,0,1},{1280,720,0,1,1}};
    Mat4 original=Identity();original.m[0][0]=2.f/1280;original.m[0][3]=-1;original.m[1][1]=-2.f/720;original.m[1][3]=1;
    Require(LooksOrthographic(original),"movie orthographic classification");
    unsigned cases=0,samples=0,oldWrong=0;
    auto draw=[&](const Mat4& matrix){Check(d->SetVertexShaderConstantF(24,&matrix.m[0][0],4));Check(d->Clear(0,nullptr,D3DCLEAR_TARGET,0,1,0));Check(d->BeginScene());Check(d->DrawPrimitiveUP(D3DPT_TRIANGLESTRIP,2,quad,sizeof(quad[0])));Check(d->EndScene());Check(d->SetVertexShaderConstantF(24,&original.m[0][0],4));Check(d->GetRenderTargetData(target.Get(),read.Get()));std::vector<DWORD> pixels(128*128);Check(read->LockRect(&locked,nullptr,D3DLOCK_READONLY));for(int y=0;y<128;++y)memcpy(pixels.data()+y*128,static_cast<char*>(locked.pBits)+y*locked.Pitch,128*4);Check(read->UnlockRect());return pixels;};
    const auto legacy=draw(original);
    for(float yaw:{-.35f,0.f,.35f})for(float pitch:{-.2f,0.f,.2f})for(float lean:{0.f,.3f})for(int eye=0;eye<2;++eye){
        Mat4 eyeOffset=Identity();eyeOffset.m[0][3]=eye?.032f:-.032f;
        const Mat4 eyePose=Mul(Pose(yaw,pitch,lean),eyeOffset);
        const Mat4 projection=PerspectiveRawLH(eye?-.85f:-1.3f,eye?1.3f:.85f,-1.15f,.95f,.1f,100000);
        const Mat4 movie=Mul(projection,Mul(InvertRigid(eyePose),HudPanelFromClip(16.f/9,4,3,0)));
        const auto pixels=draw(Mul(movie,original));
        for(int y=2;y<126;y+=2)for(int x=2;x<126;x+=2){
            // Independently intersect each eye's ray with the fixed z=3m screen.
            const float ray[3]={(2.f*x/128-1-projection.m[0][2])/projection.m[0][0],(1-2.f*y/128-projection.m[1][2])/projection.m[1][1],1};float world[3]{};
            for(int r=0;r<3;++r)for(int c=0;c<3;++c)world[r]+=eyePose.m[r][c]*ray[c];
            const float t=(3-eyePose.m[2][3])/world[2];
            const float u=.5f+(eyePose.m[0][3]+t*world[0])/4, vv=.5f-(eyePose.m[1][3]+t*world[1])/2.25f;
            if(u<.02f||u>.98f||vv<.02f||vv>.98f)continue;
            if(std::fabs(u*8-std::round(u*8))<.02f || std::fabs(vv*8-std::round(vv*8))<.02f)continue;
            const float luma=float(64+((int(u*8)+int(vv*8))&1)*128)/255;
            const int expected=int(std::round(255*(1.164f*(luma-.0625f)+1.596f*(128.f/255-.5f))));
            const int red=int((pixels[y*128+x]>>16)&255),old=int((legacy[y*128+x]>>16)&255);
            if(std::abs(red-expected)>2){printf("pose %.2f %.2f %.2f eye %d xy %d %d red %d expected %d\n",yaw,pitch,lean,eye,x,y,red,expected);Require(false,"same movie pixel on the same world ray");}
            if(std::abs(old-expected)>2)++oldWrong;++samples;
        }
        Mat4 after{};Check(d->GetVertexShaderConstantF(24,&after.m[0][0],4));Require(memcmp(&after,&original,sizeof(original))==0,"matrix restored");++cases;
    }
    Require(samples>10000&&oldWrong>3000,"original path reproduces binocular mismatch");
    DestroyWindow(window);printf("movies: %u GPU eye/turn/lean cases, %u ray-matched video pixels; %u old-path mismatches; original matrix restored\n",cases,samples,oldWrong);
}
