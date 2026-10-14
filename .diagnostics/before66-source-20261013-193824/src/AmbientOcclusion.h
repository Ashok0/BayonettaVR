#pragma once
#include "DepthOfField.h"
namespace bvr {
// Exact filtershaderssao.pso from the installed Steam shader.dat.
inline bool IsAmbientOcclusionShader(uint32_t crc,size_t size) {
    return crc==0x46871f68 && size==8172;
}
// The original shader clamps its sample-count selector to c198.y. Zero makes
// its occlusion multiplier 1, preserving s0 RGBA. Skipping this draw would lose
// the scene copy. Change only this constant for this draw, then restore it.
class AmbientOcclusionBypass {
public:
    AmbientOcclusionBypass(IDirect3DDevice9* device,bool enabled):device_(device) {
        if(!enabled||FAILED(device_->GetPixelShaderConstantF(198,original_,1)))return;
        const float neutral[4]={original_[0],0,original_[2],original_[3]};
        active_=SUCCEEDED(device_->SetPixelShaderConstantF(198,neutral,1));
    }
    ~AmbientOcclusionBypass(){if(active_)device_->SetPixelShaderConstantF(198,original_,1);}
    bool Active()const{return active_;}
    AmbientOcclusionBypass(const AmbientOcclusionBypass&)=delete;
    AmbientOcclusionBypass& operator=(const AmbientOcclusionBypass&)=delete;
private:
    IDirect3DDevice9* device_;
    float original_[4]{};
    bool active_=false;
};
}
