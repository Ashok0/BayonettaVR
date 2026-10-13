#pragma once
#include "DepthOfField.h"
#include <cmath>

namespace bvr {
inline bool IsPracticeReflectionShader(uint32_t crc,size_t size) {
    return crc==0xdd5f8df1 && size==476;
}

// The maroon practice arena's flat reflection card: exact shader, red tint,
// zero distortion, shared 4x4 mask and a full-size reflection render texture.
// Other uses of the shimmer shader (water/heat/combat effects) remain enabled.
class PracticeReflectionFilter {
public:
    uint32_t Register(IDirect3DPixelShader9* shader){return shader_.Register(shader);}
    void Reset(){shader_.Reset();}
    bool ShouldSkip(IDirect3DDevice9* d,UINT primitives,bool enabled)const {
        if(primitives!=2 || !shader_.ShouldSkip(d,enabled))return false;
        float constants[8]{};
        const float expected[8]{.5f,.2f,.2f,1,0,0,-.5f,-.5f};
        if(FAILED(d->GetPixelShaderConstantF(184,constants,2)))return false;
        for(unsigned i=0;i<8;++i)
            if(!std::isfinite(constants[i]) || std::fabs(constants[i]-expected[i])>0.00001f)return false;
        const D3DRENDERSTATETYPE states[]{D3DRS_ZENABLE,D3DRS_ZWRITEENABLE,
            D3DRS_ALPHABLENDENABLE,D3DRS_SRCBLEND,D3DRS_DESTBLEND};
        const DWORD values[]{D3DZB_TRUE,FALSE,TRUE,D3DBLEND_SRCALPHA,D3DBLEND_INVSRCALPHA};
        for(unsigned i=0;i<5;++i){DWORD value=0;
            if(FAILED(d->GetRenderState(states[i],&value)) || value!=values[i])return false;}
        IDirect3DBaseTexture9* textures[3]{};
        bool valid=true;
        for(unsigned i=0;i<3;++i)
            if(FAILED(d->GetTexture(i,&textures[i])) || !textures[i] || textures[i]->GetType()!=D3DRTYPE_TEXTURE)valid=false;
        if(valid){
            D3DSURFACE_DESC mask{},reflection{};D3DVIEWPORT9 viewport{};
            valid=textures[0]==textures[2] &&
                SUCCEEDED(static_cast<IDirect3DTexture9*>(textures[0])->GetLevelDesc(0,&mask)) &&
                SUCCEEDED(static_cast<IDirect3DTexture9*>(textures[1])->GetLevelDesc(0,&reflection)) &&
                SUCCEEDED(d->GetViewport(&viewport)) &&
                mask.Width==4 && mask.Height==4 && mask.Format==D3DFMT_DXT5 && mask.Usage==0 &&
                reflection.Format==D3DFMT_A8R8G8B8 && (reflection.Usage&D3DUSAGE_RENDERTARGET)!=0 &&
                reflection.Width==viewport.Width && reflection.Height==viewport.Height;
        }
        for(auto* texture:textures)if(texture)texture->Release();
        return valid;
    }
private:
    PixelShaderDrawFilter shader_{IsPracticeReflectionShader};
};
}
