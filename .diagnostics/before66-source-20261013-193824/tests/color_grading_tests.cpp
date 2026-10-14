#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include "ColorGrading.h"
#include <wrl/client.h>
#include <fstream>
#include <vector>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <cmath>
using namespace bvr;
using Microsoft::WRL::ComPtr;
static void Require(bool ok,const char* message){if(!ok){printf("Color grading failed: %s\n",message);exit(2);}}
static void Check(HRESULT hr){if(FAILED(hr)){printf("Color grading HRESULT %08lX\n",hr);exit(1);}}
static int Byte(float x){return int(std::floor((x<0?0:x>1?1:x)*255.f+.5f));}
int main() {
    HWND window=CreateWindowExW(0,L"STATIC",L"Color grading regression",WS_OVERLAPPEDWINDOW,0,0,32,32,nullptr,nullptr,GetModuleHandleW(nullptr),nullptr);
    ComPtr<IDirect3D9Ex> api;Check(Direct3DCreate9Ex(D3D_SDK_VERSION,&api));
    D3DPRESENT_PARAMETERS pp{};pp.Windowed=TRUE;pp.SwapEffect=D3DSWAPEFFECT_DISCARD;pp.hDeviceWindow=window;
    pp.BackBufferWidth=pp.BackBufferHeight=32;pp.BackBufferFormat=D3DFMT_X8R8G8B8;
    ComPtr<IDirect3DDevice9Ex> d;Check(api->CreateDeviceEx(0,D3DDEVTYPE_HAL,window,D3DCREATE_HARDWARE_VERTEXPROCESSING,&pp,nullptr,&d));
    ComPtr<IDirect3DSurface9> target,readback;
    Check(d->CreateRenderTarget(32,32,D3DFMT_A8R8G8B8,D3DMULTISAMPLE_NONE,0,FALSE,&target,nullptr));
    Check(d->CreateOffscreenPlainSurface(32,32,D3DFMT_A8R8G8B8,D3DPOOL_SYSTEMMEM,&readback,nullptr));
    Check(d->SetRenderTarget(0,target.Get()));
    ComPtr<IDirect3DTexture9> scene,tone;
    Check(d->CreateTexture(32,32,1,D3DUSAGE_DYNAMIC,D3DFMT_A8R8G8B8,D3DPOOL_DEFAULT,&scene,nullptr));
    Check(d->CreateTexture(256,1,1,D3DUSAGE_DYNAMIC,D3DFMT_A8R8G8B8,D3DPOOL_DEFAULT,&tone,nullptr));
    Check(d->SetTexture(0,scene.Get()));Check(d->SetTexture(1,tone.Get()));
    for(int i=0;i<2;++i) {
        Check(d->SetSamplerState(i,D3DSAMP_MINFILTER,D3DTEXF_POINT));Check(d->SetSamplerState(i,D3DSAMP_MAGFILTER,D3DTEXF_POINT));
        Check(d->SetSamplerState(i,D3DSAMP_ADDRESSU,D3DTADDRESS_CLAMP));Check(d->SetSamplerState(i,D3DSAMP_ADDRESSV,D3DTADDRESS_CLAMP));
        Check(d->SetSamplerState(i,D3DSAMP_SRGBTEXTURE,FALSE));
    }
    Check(d->SetVertexShader(nullptr));Check(d->SetFVF(D3DFVF_XYZRHW|D3DFVF_TEX1));
    Check(d->SetRenderState(D3DRS_CULLMODE,D3DCULL_NONE));Check(d->SetRenderState(D3DRS_ZENABLE,FALSE));
    Check(d->SetRenderState(D3DRS_ALPHABLENDENABLE,FALSE));Check(d->SetRenderState(D3DRS_ALPHATESTENABLE,FALSE));
    Check(d->SetRenderState(D3DRS_COLORWRITEENABLE,15));Check(d->SetRenderState(D3DRS_SRGBWRITEENABLE,FALSE));
    const float quad[][6]={{-.5f,-.5f,0,1,0,0},{31.5f,-.5f,0,1,1,0},{-.5f,31.5f,0,1,0,1},{31.5f,31.5f,0,1,1,1}};
    auto pixels=[&](){std::vector<DWORD> out(1024);Check(d->GetRenderTargetData(target.Get(),readback.Get()));D3DLOCKED_RECT lock{};
        Check(readback->LockRect(&lock,nullptr,D3DLOCK_READONLY));for(int y=0;y<32;++y)memcpy(out.data()+32*y,static_cast<char*>(lock.pBits)+y*lock.Pitch,128);
        Check(readback->UnlockRect());return out;};
    unsigned cases=0,changed=0,verified=0;
    for(int shaderIndex=0;shaderIndex<2;++shaderIndex) {
        const char* file=shaderIndex?"tests/fixtures/color_grading/filter04.pso":"tests/fixtures/color_grading/filter00.pso";
        std::ifstream f(file,std::ios::binary|std::ios::ate);Require(f.good(),"native shader fixture");
        const size_t size=size_t(f.tellg());f.seekg(0);std::vector<DWORD> original(size/4);f.read(reinterpret_cast<char*>(original.data()),size);
        const uint32_t crc=ShaderCrc32(original.data(),size);
        Require(IsColorGradingShader(crc,size),"exact shader fingerprint");
        Require(!IsColorGradingShader(crc^1,size)&&!IsColorGradingShader(crc,size+4),"near matches rejected");
        ComPtr<IDirect3DPixelShader9> ps;Check(d->CreatePixelShader(original.data(),&ps));
        PixelShaderDrawFilter classifier(IsColorGradingShader);Require(classifier.Register(ps.Get())==crc,"registration");
        Check(d->SetPixelShader(ps.Get()));ComPtr<IDirect3DStateBlock9> state;Check(d->CreateStateBlock(D3DSBT_PIXELSTATE,&state));
        Check(d->SetPixelShader(nullptr));Require(!classifier.ShouldSkip(d.Get(),true),"unrelated shader unchanged");
        Check(state->Apply());Require(classifier.ShouldSkip(d.Get(),true),"state-block binding recognized");
        Require(!classifier.ShouldSkip(d.Get(),false),"setting off");
        for(int curve=0;curve<2;++curve)for(int grade=0;grade<3;++grade)for(int eye=0;eye<2;++eye) {
            DWORD input[1024]{};int lut[256]{};D3DLOCKED_RECT lock{};
            Check(scene->LockRect(0,&lock,nullptr,D3DLOCK_DISCARD));
            for(int y=0;y<32;++y)for(int x=0;x<32;++x) {
                const DWORD value=(DWORD(25+y*7)<<24)|(DWORD(12+x*7)<<16)|(DWORD(15+y*7)<<8)|DWORD(20+(x+y)*3+eye*10);
                input[y*32+x]=value;reinterpret_cast<DWORD*>(static_cast<char*>(lock.pBits)+y*lock.Pitch)[x]=value;
            }
            Check(scene->UnlockRect(0));Check(tone->LockRect(0,&lock,nullptr,D3DLOCK_DISCARD));
            for(int i=0;i<256;++i){lut[i]=curve?(i*i+127)/255:i;reinterpret_cast<DWORD*>(lock.pBits)[i]=0xff000000|DWORD(lut[i]*0x010101);}
            Check(tone->UnlockRect(0));
            const float params[12]={1.f,.8f-grade*.1f,.5f+grade*.1f,.13f+grade*.1f,
                -.4f-grade*.2f,-.4f-grade*.2f,-.4f-grade*.2f,.7f,.07f,.04f,.02f,.3f};
            Check(d->SetPixelShaderConstantF(184,params,3));
            auto draw=[&](bool enabled) {
                Check(d->BeginScene());{
                    ColorGradingBypass bypass(d.Get(),classifier.ShouldSkip(d.Get(),enabled));
                    Require(bypass.Active()==enabled,"bypass state");
                    Check(d->DrawPrimitiveUP(D3DPT_TRIANGLESTRIP,2,quad,sizeof(quad[0])));
                }Check(d->EndScene());
                float restored[12]{};Check(d->GetPixelShaderConstantF(184,restored,3));Require(!memcmp(restored,params,sizeof(params)),"all constants restored exactly");
                ComPtr<IDirect3DPixelShader9> current;Check(d->GetPixelShader(&current));Require(current.Get()==ps.Get(),"original shader binding preserved");
            };
            draw(false);const auto before=pixels();draw(true);const auto after=pixels();
            for(unsigned i=0;i<1024;++i) {
                const float rgb[]={float((input[i]>>16)&255)/255.f,float((input[i]>>8)&255)/255.f,float(input[i]&255)/255.f};
                const float lum=rgb[0]*.298911989f+rgb[1]*.586610973f+rgb[2]*.114478f;
                int index=int(lum*256.f);if(index>255)index=255;
                for(int c=0;c<3;++c) {
                    const float expected=shaderIndex?rgb[c]*(lut[index]/255.f)/lum:lut[(input[i]>>(16-8*c))&255]/255.f;
                    Require(std::abs(int((after[i]>>(16-8*c))&255)-Byte(expected))<=2,"neutral RGB matches independent tone-only result");
                }
                Require((before[i]>>24)==(after[i]>>24),"native alpha exactly preserved");
                if((before[i]&0xffffff)!=(after[i]&0xffffff))++changed;
                ++verified;
            }
            draw(false);Require(pixels()==before,"toggle off restores original graded pixels");++cases;
        }
        UINT bytes=UINT(size);std::vector<DWORD> after(original.size());Check(ps->GetFunction(after.data(),&bytes));Require(after==original,"game shader bytecode unchanged");
        classifier.Reset();Require(!classifier.ShouldSkip(d.Get(),true),"classifier reset");
    }
    Require(changed>10000,"native baseline actually tinted/desaturated pixels");
    DestroyWindow(window);
    printf("Color grading: %u GPU cases, %u pixels match tone-only RGB and native alpha; %u graded pixels changed. Toggle, state blocks, reset, exact matching, constants and bytecode preservation passed.\n",cases,verified,changed);
}
