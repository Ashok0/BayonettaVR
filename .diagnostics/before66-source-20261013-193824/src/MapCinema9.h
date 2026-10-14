#pragma once
#include "DepthOfField.h"
#include <d3dcompiler.h>
#include <algorithm>
#include <vector>

namespace bvr {
// Recognize the actual map asset, not every large texture or every cutscene.
// The fingerprint is from .52 draw0193, 2026-10-11 11:54:49. Sampling runs
// only at the draw-list boundary, never recursively inside an engine draw.
class MapCinema9 {
    template<class T> static void Release(T*& p){if(p)p->Release();p=nullptr;}
    struct Result { unsigned version; uint32_t hash; };
    inline static const GUID key_{0xa9605de3,0x3b64,0x4ce8,{0x89,0x28,0x46,0xa2,0xbd,0x76,0x45,0x31}};
public:
    static constexpr uint32_t MapFingerprint=0x7a6d8996;
    explicit MapCinema9(uint32_t expected=MapFingerprint):expected_(expected){}
    ~MapCinema9(){Reset();}
    bool Busy()const{return busy_;}
    bool Active()const{return active_;}
    unsigned Samples()const{return samples_;}
    uint32_t LastHash()const{return lastHash_;}
    void BeginFrame(){ClearPending();seen_=false;}
    void EndFrame(){active_=seen_;ClearPending();}
    void Reset(){ClearPending();Release(ps_);Release(state_);Release(preview_);Release(read_);active_=seen_=busy_=false;}
    void Observe(IDirect3DDevice9* d,UINT primitives){
        if(busy_ || primitives!=2)return;
        IDirect3DBaseTexture9* base=nullptr;
        if(FAILED(d->GetTexture(0,&base)) || !base)return;
        if(base->GetType()==D3DRTYPE_TEXTURE){
            auto* texture=static_cast<IDirect3DTexture9*>(base);D3DSURFACE_DESC desc{};
            if(SUCCEEDED(texture->GetLevelDesc(0,&desc)) && desc.Width==4096 && desc.Height==2048 &&
               desc.Format==D3DFMT_DXT1 && !(desc.Usage&(D3DUSAGE_RENDERTARGET|D3DUSAGE_DEPTHSTENCIL))){
                Result result{};DWORD bytes=sizeof(result);
                if(SUCCEEDED(texture->GetPrivateData(key_,&result,&bytes)) && bytes==sizeof(result) && result.version==1)
                    seen_=seen_ || result.hash==expected_;
                else if(pending_.size()<4 && std::find(pending_.begin(),pending_.end(),texture)==pending_.end()){
                    texture->AddRef();pending_.push_back(texture);
                }
            }
        }
        base->Release();
    }
    bool Resolve(IDirect3DDevice9* d){
        busy_=true;
        for(auto* texture:pending_){
            uint32_t hash=0;
            if(SUCCEEDED(Fingerprint(d,texture,hash))){
                ++samples_;
                lastHash_=hash;
                const Result result{1,hash};
                texture->SetPrivateData(key_,&result,sizeof(result),0);
                seen_=seen_ || result.hash==expected_;
            }
        }
        ClearPending();busy_=false;return seen_;
    }
    // Also exercised directly by the GPU regression, including the captured
    // reference pixels and all render-target/state restoration.
    HRESULT Fingerprint(IDirect3DDevice9* d,IDirect3DTexture9* texture,uint32_t& hash){
        HRESULT hr=Ensure(d);if(FAILED(hr))return hr;
        IDirect3DSurface9* targets[4]{};IDirect3DSurface9* depth=nullptr;
        D3DCAPS9 caps{};D3DVIEWPORT9 vp{};RECT scissor{};
        hr=d->GetDeviceCaps(&caps);const DWORD count=std::min<DWORD>(caps.NumSimultaneousRTs,4);
        for(DWORD i=0;SUCCEEDED(hr) && i<count;++i){
            const HRESULT get=d->GetRenderTarget(i,&targets[i]);
            if(FAILED(get) && !(i && get==D3DERR_NOTFOUND))hr=get;
        }
        if(SUCCEEDED(hr))hr=d->GetViewport(&vp);
        if(SUCCEEDED(hr))hr=d->GetScissorRect(&scissor);
        if(SUCCEEDED(hr))hr=state_->Capture();
        if(SUCCEEDED(hr)){
            d->GetDepthStencilSurface(&depth);
            hr=d->SetDepthStencilSurface(nullptr);
            for(DWORD i=1;SUCCEEDED(hr) && i<count;++i)hr=d->SetRenderTarget(i,nullptr);
            if(SUCCEEDED(hr))hr=d->SetRenderTarget(0,preview_);
            const D3DVIEWPORT9 previewViewport{0,0,256,144,0,1};
            if(SUCCEEDED(hr))hr=d->SetViewport(&previewViewport);
            d->SetVertexShader(nullptr);d->SetFVF(D3DFVF_XYZRHW|D3DFVF_TEX1);d->SetPixelShader(ps_);
            d->SetTexture(0,texture);d->SetStreamSourceFreq(0,1);
            d->SetSamplerState(0,D3DSAMP_MINFILTER,D3DTEXF_LINEAR);d->SetSamplerState(0,D3DSAMP_MAGFILTER,D3DTEXF_LINEAR);
            d->SetSamplerState(0,D3DSAMP_MIPFILTER,D3DTEXF_NONE);d->SetSamplerState(0,D3DSAMP_MAXMIPLEVEL,0);
            d->SetSamplerState(0,D3DSAMP_MIPMAPLODBIAS,0);d->SetSamplerState(0,D3DSAMP_ADDRESSU,D3DTADDRESS_CLAMP);
            d->SetSamplerState(0,D3DSAMP_ADDRESSV,D3DTADDRESS_CLAMP);d->SetSamplerState(0,D3DSAMP_SRGBTEXTURE,FALSE);
            d->SetRenderState(D3DRS_ZENABLE,FALSE);d->SetRenderState(D3DRS_ZWRITEENABLE,FALSE);
            d->SetRenderState(D3DRS_STENCILENABLE,FALSE);d->SetRenderState(D3DRS_SCISSORTESTENABLE,FALSE);
            d->SetRenderState(D3DRS_ALPHATESTENABLE,FALSE);d->SetRenderState(D3DRS_ALPHABLENDENABLE,FALSE);
            d->SetRenderState(D3DRS_COLORWRITEENABLE,15);d->SetRenderState(D3DRS_CULLMODE,D3DCULL_NONE);
            d->SetRenderState(D3DRS_FILLMODE,D3DFILL_SOLID);d->SetRenderState(D3DRS_SRGBWRITEENABLE,FALSE);
            d->SetRenderState(D3DRS_CLIPPLANEENABLE,0);d->SetRenderState(D3DRS_FOGENABLE,FALSE);
            const float quad[][6]={{-.5f,-.5f,0,1,0,0},{255.5f,-.5f,0,1,1,0},{-.5f,143.5f,0,1,0,1},{255.5f,143.5f,0,1,1,1}};
            if(SUCCEEDED(hr))hr=d->DrawPrimitiveUP(D3DPT_TRIANGLESTRIP,2,quad,sizeof(quad[0]));
            d->SetDepthStencilSurface(nullptr);d->SetRenderTarget(0,targets[0]);
            for(DWORD i=1;i<count;++i)d->SetRenderTarget(i,targets[i]);
            d->SetDepthStencilSurface(depth);state_->Apply();d->SetViewport(&vp);d->SetScissorRect(&scissor);
        }
        for(auto& target:targets)Release(target);Release(depth);
        if(SUCCEEDED(hr))hr=d->GetRenderTargetData(preview_,read_);
        D3DLOCKED_RECT locked{};
        if(SUCCEEDED(hr))hr=read_->LockRect(&locked,nullptr,D3DLOCK_READONLY);
        if(SUCCEEDED(hr)){
            std::vector<unsigned char> pixels(256*144*4);
            for(int y=0;y<144;++y)memcpy(pixels.data()+y*256*4,static_cast<const char*>(locked.pBits)+y*locked.Pitch,256*4);
            hash=ShaderCrc32(pixels.data(),pixels.size());read_->UnlockRect();
        }
        return hr;
    }
private:
    void ClearPending(){for(auto*& texture:pending_)Release(texture);pending_.clear();}
    HRESULT Ensure(IDirect3DDevice9* d){
        if(ps_ && state_ && preview_ && read_)return S_OK;
        Release(ps_);Release(state_);Release(preview_);Release(read_);
        const char shader[]="sampler2D s:register(s0);float4 ps(float2 uv:TEXCOORD0):COLOR{return float4(tex2D(s,uv).rgb,1);}";
        ID3DBlob* blob=nullptr;
        HRESULT hr=D3DCompile(shader,sizeof(shader)-1,nullptr,nullptr,nullptr,"ps","ps_2_0",0,0,&blob,nullptr);
        if(SUCCEEDED(hr))hr=d->CreatePixelShader(static_cast<const DWORD*>(blob->GetBufferPointer()),&ps_);Release(blob);
        if(SUCCEEDED(hr))hr=d->CreateStateBlock(D3DSBT_ALL,&state_);
        if(SUCCEEDED(hr))hr=d->CreateRenderTarget(256,144,D3DFMT_A8R8G8B8,D3DMULTISAMPLE_NONE,0,FALSE,&preview_,nullptr);
        if(SUCCEEDED(hr))hr=d->CreateOffscreenPlainSurface(256,144,D3DFMT_A8R8G8B8,D3DPOOL_SYSTEMMEM,&read_,nullptr);
        return hr;
    }
    bool active_=false,seen_=false,busy_=false;
    uint32_t expected_;unsigned samples_=0;uint32_t lastHash_=0;
    std::vector<IDirect3DTexture9*> pending_;
    IDirect3DPixelShader9* ps_=nullptr;IDirect3DStateBlock9* state_=nullptr;
    IDirect3DSurface9 *preview_=nullptr,*read_=nullptr;
};
}
