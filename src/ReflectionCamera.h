#pragma once
#include "DepthOfField.h"
#include "Matrix.h"

namespace bvr {
inline bool IsReflectionCameraShader(uint32_t crc,size_t size) {
    // Captured Steam shaders: g_ViewProjection=c8..11, world-space eye
    // position read ONLY from c12.w/c13.w/c14.w (viewInverseMatrix).
    const struct {uint32_t crc;size_t size;} known[]={
        {0x3C21215Eu,1716},{0xC860EAECu,876},{0xFFD8347Eu,912},
        {0x90241E02u,1916},{0x5DD39D91u,1880},{0xAD68C3FEu,1716},
        {0xEF05A9C9u,1772},{0xBDAFACB9u,1880},{0x648FD477u,1692},
        {0x41D36437u,1980},{0x6955BB24u,1956}
    };
    for(const auto& shader:known)if(shader.crc==crc&&shader.size==size)return true;
    return false;
}
inline bool ReflectionCameraPosition(const Mat4& viewProjection,float* position) {
    // The optical center maps to clip x=y=w=0, including asymmetric VR
    // frusta and reflected native cameras. Solve those three planes in
    // double precision; no near/far subtraction or new pose sample needed.
    for(const auto& row:viewProjection.m)for(float v:row)if(!std::isfinite(v))return false;
    double a[3][4]{};
    const int rows[]={0,1,3};
    for(int r=0;r<3;++r) {
        for(int c=0;c<3;++c)a[r][c]=viewProjection.m[rows[r]][c];
        a[r][3]=-double(viewProjection.m[rows[r]][3]);
    }
    for(int c=0;c<3;++c) {
        int pivot=c;
        for(int r=c+1;r<3;++r)if(std::fabs(a[r][c])>std::fabs(a[pivot][c]))pivot=r;
        if(std::fabs(a[pivot][c])<1e-9)return false;
        for(int k=c;k<4;++k){const double t=a[c][k];a[c][k]=a[pivot][k];a[pivot][k]=t;}
        const double scale=a[c][c];for(int k=c;k<4;++k)a[c][k]/=scale;
        for(int r=0;r<3;++r)if(r!=c) {
            const double scaleRow=a[r][c];for(int k=c;k<4;++k)a[r][k]-=scaleRow*a[c][k];
        }
    }
    float result[3]{};
    for(int r=0;r<3;++r){result[r]=float(a[r][3]);if(!std::isfinite(result[r]))return false;}
    memcpy(position,result,sizeof(result));return true;
}
using ReflectionSetConstants=HRESULT(STDMETHODCALLTYPE*)(IDirect3DDevice9*,UINT,const float*,UINT);
class ReflectionCameraScope {
public:
    ReflectionCameraScope(IDirect3DDevice9* device,ReflectionSetConstants set,
                          const Mat4& adjusted,bool enabled):device_(device),set_(set) {
        float position[3]{};
        if(!enabled||!ReflectionCameraPosition(adjusted,position)||
           FAILED(device_->GetVertexShaderConstantF(12,saved_,3)))return;
        float changed[12];memcpy(changed,saved_,sizeof(changed));
        for(int r=0;r<3;++r)changed[r*4+3]=position[r];
        active_=SUCCEEDED(set_(device_,12,changed,3));
        if(!active_)set_(device_,12,saved_,3);
    }
    ~ReflectionCameraScope(){if(active_)set_(device_,12,saved_,3);}
    bool Active()const{return active_;}
    ReflectionCameraScope(const ReflectionCameraScope&)=delete;
    ReflectionCameraScope& operator=(const ReflectionCameraScope&)=delete;
private:
    IDirect3DDevice9* device_;
    ReflectionSetConstants set_;
    float saved_[12]{};
    bool active_=false;
};
}
