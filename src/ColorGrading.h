#pragma once
#include "DepthOfField.h"
namespace bvr {
// Exact filter00/filter04 pixel shaders from the Steam shader.dat.
inline bool IsColorGradingShader(uint32_t crc,size_t size) {
    return (crc==0x6e722de9u&&size==760)||(crc==0x373a4bb7u&&size==500);
}
// Both shaders apply g_MatrialColor.rgb (c184) and g_Saido.rgb (c185)
// after the tone-table lookup. filter00 also adds a haze using c186.rgb.
// Neutralize only these RGB controls. All .w controls, the tone table,
// scene copy, original shader object and render/blend state remain intact.
class ColorGradingBypass {
public:
    ColorGradingBypass(IDirect3DDevice9* device,bool enabled):device_(device) {
        if(!enabled||FAILED(device_->GetPixelShaderConstantF(184,saved_,3)))return;
        const float neutral[12]={1,1,1,saved_[3],0,0,0,saved_[7],0,0,0,saved_[11]};
        active_=SUCCEEDED(device_->SetPixelShaderConstantF(184,neutral,3));
    }
    ~ColorGradingBypass(){if(active_)device_->SetPixelShaderConstantF(184,saved_,3);}
    bool Active()const{return active_;}
    ColorGradingBypass(const ColorGradingBypass&)=delete;
    ColorGradingBypass& operator=(const ColorGradingBypass&)=delete;
private:
    IDirect3DDevice9* device_;
    float saved_[12]{};
    bool active_=false;
};
}
