#pragma once
#include "Matrix.h"
#include <d3d9.h>
namespace bvr {
// Decode/draw the engine's movie once, then project the same pixels into both
// eyes. Movie rendering uses mutable decoder/vertex state and is not replayable.
class MoviePanel9 {
public:
    ~MoviePanel9(){Reset();}
    void Reset();
    void BeginFrame(){ready_=false;draws_=0;}
    bool Begin(IDirect3DDevice9* device);
    void End(IDirect3DDevice9* device);
    // Called after the engine finishes its draw lists, within its D3D scene.
    // Preserve the complete cinematic,
    // including scene geometry, movie, Id overlays, fades and subtitles.
    bool CaptureFrame(IDirect3DDevice9* device,IDirect3DSurface9* source);
    HRESULT PresentFrame(IDirect3DDevice9* device,IDirect3DSurface9* target,const Mat4& clip);
    template<class Call> HRESULT CaptureDraw(IDirect3DDevice9* device,Call&& call){
        IDirect3DSurface9* target=nullptr;
        const bool recording=SUCCEEDED(device->GetRenderTarget(0,&target)) && target==surface_;
        if(target)target->Release();
        const HRESULT hr=call();
        if(recording){if(SUCCEEDED(hr))++draws_;else failed_=true;}
        return hr;
    }
    HRESULT Draw(IDirect3DDevice9* device,const Mat4& clip,bool clearBackground=false);
    bool Ready()const{return ready_;}
    float Aspect()const{return height_?float(width_)/height_:16.f/9;}
    IDirect3DSurface9* SourceTarget()const{return oldTarget_;}
    IDirect3DSurface9* CaptureTarget()const{return surface_;}
private:
    bool Ensure(IDirect3DDevice9* device,UINT width,UINT height);
    IDirect3DTexture9* texture_=nullptr;
    IDirect3DSurface9* surface_=nullptr;
    IDirect3DSurface9* oldTarget_=nullptr;
    IDirect3DSurface9* oldDepth_=nullptr;
    IDirect3DVertexShader9* vs_=nullptr;
    IDirect3DPixelShader9* ps_=nullptr;
    IDirect3DVertexDeclaration9* declaration_=nullptr;
    IDirect3DStateBlock9* state_=nullptr;
    D3DVIEWPORT9 oldViewport_{};
    RECT oldScissor_{};
    UINT width_=0,height_=0;
    unsigned draws_=0;
    bool ready_=false,failed_=false;
};
}
