#pragma once
#include "DepthOfField.h"
#include "SoftParticleDepthBytecode.h"
namespace bvr {
inline bool IsSoftParticleShader(uint32_t crc,size_t size) {
    return crc==0xe166cf17 && size==552;
}
class SoftParticleDepth {
public:
    uint32_t Register(IDirect3DPixelShader9* shader){return originals_.Register(shader);}
    void Reset(){originals_.Reset();ReleaseReplacement();}
    void ReleaseReplacement(){if(replacement_)replacement_->Release();replacement_=nullptr;attempted_=false;}
    bool Matches(IDirect3DDevice9* device,bool enabled)const{return originals_.ShouldSkip(device,enabled);}
    IDirect3DPixelShader9* Replacement(IDirect3DDevice9* device) {
        // Do not alter game-owned objects or create shaders inside its creation
        // callback. One lazy allocation at the first affected draw, no readback.
        if(!attempted_){attempted_=true;device->CreatePixelShader(kSoftParticleDepthBytecode,&replacement_);}
        return replacement_;
    }
private:
    PixelShaderDrawFilter originals_{IsSoftParticleShader};
    IDirect3DPixelShader9* replacement_=nullptr;
    bool attempted_=false;
};
class SoftParticleDepthScope {
public:
    SoftParticleDepthScope(IDirect3DDevice9* device,SoftParticleDepth& fix,bool enabled):device_(device) {
        if(!fix.Matches(device,enabled))return;
        auto replacement=fix.Replacement(device);
        D3DVIEWPORT9 viewport{};
        if(!replacement || FAILED(device->GetViewport(&viewport)) || !viewport.Width || !viewport.Height ||
           FAILED(device->GetPixelShaderConstantF(223,saved_,1)) ||
           FAILED(device->GetPixelShader(&original_)))return;
        const float screen[4]={1.f/viewport.Width,1.f/viewport.Height,
            (.5f-float(viewport.X))/viewport.Width,(.5f-float(viewport.Y))/viewport.Height};
        if(SUCCEEDED(device->SetPixelShaderConstantF(223,screen,1))) {
            active_=SUCCEEDED(device->SetPixelShader(replacement));
            if(!active_)device->SetPixelShaderConstantF(223,saved_,1);
        }
    }
    ~SoftParticleDepthScope(){
        if(active_){device_->SetPixelShader(original_);device_->SetPixelShaderConstantF(223,saved_,1);}
        if(original_)original_->Release();
    }
    bool Active()const{return active_;}
    SoftParticleDepthScope(const SoftParticleDepthScope&)=delete;
    SoftParticleDepthScope& operator=(const SoftParticleDepthScope&)=delete;
private:
    IDirect3DDevice9* device_;
    IDirect3DPixelShader9* original_=nullptr;
    float saved_[4]{};
    bool active_=false;
};
}
