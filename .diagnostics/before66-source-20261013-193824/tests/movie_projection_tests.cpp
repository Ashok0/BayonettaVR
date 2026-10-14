#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include "WorldHud.h"
#include "ShaderAnalysis.h"
#include "MoviePanel9.h"
#include "MovieDrawQueue.h"
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
    MovieDrawQueue queue{};MovieDrawQueue::Bucket buckets[2][5]{};
    queue.buckets=1;queue.groups=5;
    for(int bank=0;bank<2;++bank)for(int group=0;group<5;++group)queue.banks[bank][group]=&buckets[bank][group];
    Require(!queue.Pending(),"empty queue must retain native stereo");
    buckets[0][4].head=&queue;
    Require(!queue.Pending(),"write-bank command must not trigger cinema early");
    queue.writeBank=1;Require(queue.Pending(),"last render group activates cinema");
    buckets[0][4].head=nullptr;Require(!queue.Pending(),"last movie frame returns to native stereo");
    buckets[1][0].head=&queue;Require(!queue.Pending(),"opposite write bank ignored");
    queue.writeBank=0;Require(queue.Pending(),"bank flip selects next movie frame");
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
    MoviePanel9 panel;
    Check(d->SetVertexShaderConstantF(24,&original.m[0][0],4));
    Require(panel.Begin(d.Get()),"capture allocation");Check(d->BeginScene());
    Check(panel.CaptureDraw(d.Get(),[&]{return d->DrawPrimitiveUP(D3DPT_TRIANGLESTRIP,2,quad,sizeof(quad[0]));}));
    Check(d->EndScene());panel.End(d.Get());Require(panel.Ready(),"captured movie ready");
    // Model the observed non-replayable decoder: its inputs cease to contain
    // the decoded frame after the first movie render. Neither eye may read them.
    Check(textures[0]->LockRect(0,&locked,nullptr,D3DLOCK_DISCARD));
    for(int y=0;y<32;++y)memset(static_cast<char*>(locked.pBits)+y*locked.Pitch,0,32*4);
    Check(textures[0]->UnlockRect(0));
    auto replay=[&](const Mat4& clip){
        Check(d->Clear(0,nullptr,D3DCLEAR_TARGET,0,1,0));Check(d->BeginScene());Check(panel.Draw(d.Get(),clip));Check(d->EndScene());
        Check(d->GetRenderTargetData(target.Get(),read.Get()));std::vector<DWORD> pixels(128*128);
        Check(read->LockRect(&locked,nullptr,D3DLOCK_READONLY));for(int y=0;y<128;++y)memcpy(pixels.data()+y*128,static_cast<char*>(locked.pBits)+y*locked.Pitch,128*4);Check(read->UnlockRect());
        ComPtr<IDirect3DVertexShader9> restoredVS;ComPtr<IDirect3DPixelShader9> restoredPS;
        Check(d->GetVertexShader(&restoredVS));Check(d->GetPixelShader(&restoredPS));
        Require(restoredVS.Get()==vs.Get()&&restoredPS.Get()==ps.Get(),"game shaders restored");
        ComPtr<IDirect3DBaseTexture9> restoredTexture;Check(d->GetTexture(0,&restoredTexture));Require(restoredTexture.Get()==textures[0].Get(),"decoder binding restored");
        return pixels;
    };
    for(float yaw:{-.35f,0.f,.35f})for(float pitch:{-.2f,0.f,.2f})for(float lean:{0.f,.3f})for(int eye=0;eye<2;++eye){
        Mat4 eyeOffset=Identity();eyeOffset.m[0][3]=eye?.032f:-.032f;
        const Mat4 eyePose=Mul(Pose(yaw,pitch,lean),eyeOffset);
        const Mat4 projection=PerspectiveRawLH(eye?-.85f:-1.3f,eye?1.3f:.85f,-1.15f,.95f,.1f,100000);
        const Mat4 movie=Mul(projection,Mul(InvertRigid(eyePose),HudPanelFromClip(16.f/9,4,3,0)));
        const auto pixels=replay(movie);
        for(int y=2;y<126;y+=2)for(int x=2;x<126;x+=2){
            // Independently intersect each eye's ray with the fixed z=3m screen.
            const float ray[3]={(2.f*x/128-1-projection.m[0][2])/projection.m[0][0],(1-2.f*y/128-projection.m[1][2])/projection.m[1][1],1};float world[3]{};
            for(int r=0;r<3;++r)for(int c=0;c<3;++c)world[r]+=eyePose.m[r][c]*ray[c];
            const float t=(3-eyePose.m[2][3])/world[2];
            const float u=.5f+(eyePose.m[0][3]+t*world[0])/4, vv=.5f-(eyePose.m[1][3]+t*world[1])/2.25f;
            if(u<.02f||u>.98f||vv<.02f||vv>.98f)continue;
            if(std::fabs(u*8-std::round(u*8))<.09f || std::fabs(vv*8-std::round(vv*8))<.09f)continue;
            const float luma=float(64+((int(u*8)+int(vv*8))&1)*128)/255;
            const int expected=int(std::round(255*(1.164f*(luma-.0625f)+1.596f*(128.f/255-.5f))));
            const int red=int((pixels[y*128+x]>>16)&255),old=int((legacy[y*128+x]>>16)&255);
            if(std::abs(red-expected)>2){printf("pose %.2f %.2f %.2f eye %d xy %d %d red %d expected %d\n",yaw,pitch,lean,eye,x,y,red,expected);Require(false,"same movie pixel on the same world ray");}
            if(std::abs(old-expected)>2)++oldWrong;++samples;
        }
        Mat4 after{};Check(d->GetVertexShaderConstantF(24,&after.m[0][0],4));Require(memcmp(&after,&original,sizeof(original))==0,"matrix restored");++cases;
    }
    Require(samples>10000&&oldWrong>3000,"original path reproduces binocular mismatch");
    unsigned composedSamples=0,overlaySamples=0,rearCases=0;
    auto readPixels=[&](){
        Check(d->GetRenderTargetData(target.Get(),read.Get()));std::vector<DWORD> pixels(128*128);
        Check(read->LockRect(&locked,nullptr,D3DLOCK_READONLY));
        for(int y=0;y<128;++y)memcpy(pixels.data()+y*128,static_cast<char*>(locked.pBits)+y*locked.Pitch,128*4);
        Check(read->UnlockRect());return pixels;
    };
    // Three animation positions, composed by the engine BEFORE projection.
    // Opaque movie/map + moving green overlay + white subtitle. Each eye must
    // see the entire composition even after the original target is overwritten.
    for(int animation=0;animation<3;++animation) {
        Check(d->BeginScene());
        const D3DVIEWPORT9 full{0,0,128,128,0,1};Check(d->SetViewport(&full));
        Check(d->SetRenderState(D3DRS_SCISSORTESTENABLE,FALSE));
        Check(d->Clear(0,nullptr,D3DCLEAR_TARGET,0xff304050,1,0));
        const D3DRECT marker{20+animation*25,45,38+animation*25,65},caption{25,105,103,114};
        Check(d->Clear(1,&marker,D3DCLEAR_TARGET,0xff20ee40,1,0));
        Check(d->Clear(1,&caption,D3DCLEAR_TARGET,0xffeeeeee,1,0));
        const auto composition=readPixels();
        Require(panel.CaptureFrame(d.Get(),target.Get()),"complete cinematic captured");
        Check(d->EndScene());
        for(float yaw:{-.35f,0.f,.35f,3.14159265f})for(int eye=0;eye<2;++eye){
            Mat4 eyeOffset=Identity();eyeOffset.m[0][3]=eye?.032f:-.032f;
            const Mat4 eyePose=Mul(Pose(yaw,0,.15f),eyeOffset);
            const Mat4 projection=PerspectiveRawLH(eye?-.85f:-1.3f,eye?1.3f:.85f,-1.15f,.95f,.1f,100000);
            const Mat4 movie=Mul(projection,Mul(InvertRigid(eyePose),HudPanelFromClip(16.f/9,4,3,0)));
            Check(d->SetViewport(&full));Check(d->SetRenderState(D3DRS_SCISSORTESTENABLE,FALSE));
            Check(d->Clear(0,nullptr,D3DCLEAR_TARGET,0xffff00ff,1,0));
            const D3DVIEWPORT9 partial{7,9,80,70,0,1};const RECT scissor{10,12,40,45};
            Check(d->SetViewport(&partial));Check(d->SetScissorRect(&scissor));Check(d->SetRenderState(D3DRS_SCISSORTESTENABLE,TRUE));
            Check(d->BeginScene());Check(panel.PresentFrame(d.Get(),target.Get(),movie));Check(d->EndScene());
            D3DVIEWPORT9 restored{};RECT restoredScissor{};DWORD scissorEnabled=0;
            Check(d->GetViewport(&restored));Check(d->GetScissorRect(&restoredScissor));Check(d->GetRenderState(D3DRS_SCISSORTESTENABLE,&scissorEnabled));
            Require(!memcmp(&restored,&partial,sizeof(partial))&&!memcmp(&scissor,&restoredScissor,sizeof(scissor))&&scissorEnabled,"cinema restores viewport and scissor");
            const auto pixels=readPixels();
            if(yaw>3){for(auto pixel:pixels)Require((pixel&0xffffff)==0,"looking behind shows no giant scene card");++rearCases;}
            for(int y=2;y<126;y+=2)for(int x=2;x<126;x+=2){
                const float ray[3]={(2.f*x/128-1-projection.m[0][2])/projection.m[0][0],(1-2.f*y/128-projection.m[1][2])/projection.m[1][1],1};float world[3]{};
                for(int r=0;r<3;++r)for(int c=0;c<3;++c)world[r]+=eyePose.m[r][c]*ray[c];
                const float t=(3-eyePose.m[2][3])/world[2];
                const float u=.5f+(eyePose.m[0][3]+t*world[0])/4,vv=.5f-(eyePose.m[1][3]+t*world[1])/2.25f;
                DWORD expected=0;
                if(t>0&&u>0&&u<1&&vv>0&&vv<1){
                    const int sx=int(u*128),sy=int(vv*128);
                    if(sx<2||sx>125||sy<2||sy>125)continue;
                    expected=composition[sy*128+sx]&0xffffff;
                    bool edge=false;for(int dy=-2;dy<=2;++dy)for(int dx=-2;dx<=2;++dx)
                        if((composition[(sy+dy)*128+sx+dx]&0xffffff)!=expected)edge=true;
                    if(edge)continue;
                } else if(t>0 && u>-.02f && u<1.02f && vv>-.02f && vv<1.02f)continue;
                Require((pixels[y*128+x]&0xffffff)==expected,"video, moving overlay and subtitle share one world screen");
                if(expected==0x20ee40)++overlaySamples;++composedSamples;
            }
        }
    }
    Require(overlaySamples>30 && rearCases==6,"moving overlays and both rear eye views covered");
    const D3DVIEWPORT9 full{0,0,128,128,0,1};Check(d->SetViewport(&full));Check(d->SetRenderState(D3DRS_SCISSORTESTENABLE,FALSE));
    printf("cinematic composition: %u ray-matched pixels, %u moving-overlay pixels, %u black rear views; queue transitions and state restoration passed\n",composedSamples,overlaySamples,rearCases);
    panel.BeginFrame();Require(!panel.Ready(),"new frame invalidates previous video");
    Require(panel.Begin(d.Get()),"empty movie pass begin");panel.End(d.Get());
    Require(!panel.Ready()&&panel.Draw(d.Get(),Identity())==S_FALSE,"empty movie pass cannot obscure CRIWARE or gameplay");
    panel.Reset();Require(panel.Begin(d.Get()),"recreate after device resource reset");panel.End(d.Get());Require(!panel.Ready(),"reset cannot replay stale movie");
    DestroyWindow(window);printf("movies: %u GPU eye/turn/lean cases, %u ray-matched video pixels; %u old-path mismatches; consumed decoder inputs, shaders/bindings/matrix restoration, empty pass and reset passed\n",cases,samples,oldWrong);
}
