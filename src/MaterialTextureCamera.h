#pragma once
#include "DepthOfField.h"
#include "Matrix.h"
namespace bvr {
inline bool IsMaterialTextureVertex(uint32_t crc,size_t size) {
    return (crc==0xCB1E6A15u&&size==288)||(crc==0x9FFEA812u&&size==396);
}
inline bool IsMaterialTexturePixel(uint32_t crc,size_t size) {
    return (crc==0xE54B73A3u&&size==308)||(crc==0xEDBEB70Bu&&size==300)||
           (crc==0xEF0C3C98u&&size==464);
}
class MaterialTextureCamera {
public:
    uint32_t Register(IDirect3DPixelShader9* shader){return shaders_.Register(shader);}
    void Reset(){shaders_.Reset();}
    bool UsesNativeCamera(IDirect3DDevice9* d,const Mat4& matrix,int base,bool vertexMatch,bool enabled)const {
        if(!enabled||!vertexMatch||base!=24)return false;
        // The wall's animated 128x128 material is painted with its own
        // camera at Z=10, near=.1/far=1000. This is texture generation,
        // not a scene viewed by either eye. XY may rotate per particle.
        const float expected[2][4]={{0,0,-1.00010002f,9.90099049f},{0,0,-1,10}};
        for(int r=0;r<4;++r)for(int c=0;c<4;++c)if(!std::isfinite(matrix.m[r][c]))return false;
        for(int r=0;r<2;++r)for(int c=0;c<4;++c)
            if(std::fabs(matrix.m[r+2][c]-expected[r][c])>.0001f)return false;
        D3DVIEWPORT9 vp{};
        if(FAILED(d->GetViewport(&vp))||vp.X||vp.Y||vp.Width!=128||vp.Height!=128)return false;
        IDirect3DSurface9* target=nullptr;D3DSURFACE_DESC desc{};
        const bool targetMatch=SUCCEEDED(d->GetRenderTarget(0,&target))&&target&&
            SUCCEEDED(target->GetDesc(&desc))&&desc.Width==128&&desc.Height==128&&
            desc.Format==D3DFMT_A8R8G8B8;
        if(target)target->Release();
        return targetMatch&&shaders_.ShouldSkip(d,true);
    }
private:
    PixelShaderDrawFilter shaders_{IsMaterialTexturePixel};
};
}
