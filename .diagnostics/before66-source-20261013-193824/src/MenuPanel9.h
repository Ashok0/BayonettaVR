#pragma once
#include "Matrix.h"
#include <d3d9.h>
#include <vector>

namespace bvr {
// Capture each draw over black and white, retaining RGB emission and background
// transmission separately. Alpha alone cannot represent destination-color UI.
class MenuPanel9 {
public:
    ~MenuPanel9() { Reset(); }
    void Reset();
    void BeginFrame();
    bool Begin(IDirect3DDevice9* device, int layer);
    void End(IDirect3DDevice9* device, int layer);
    HRESULT Draw(IDirect3DDevice9* device, int layer, const Mat4& clip);
    using CopyFn=HRESULT(STDMETHODCALLTYPE*)(IDirect3DDevice9*,IDirect3DSurface9*,const RECT*,IDirect3DSurface9*,const RECT*,D3DTEXTUREFILTERTYPE);
    HRESULT CaptureCopy(IDirect3DDevice9* device,int layer,IDirect3DSurface9* source,const RECT* sourceRect,
        IDirect3DSurface9* destination,const RECT* destinationRect,D3DTEXTUREFILTERTYPE filter,CopyFn copy);
    template<class Call> HRESULT CaptureDraw(IDirect3DDevice9* device, int layer, Call&& call) {
        IDirect3DSurface9* target = nullptr;
        if (FAILED(device->GetRenderTarget(0, &target))) return call();
        const bool capturing = target == surfaces_[layer][0];
        if (target) target->Release();
        if (!capturing) return call(); // Intermediate game render targets are independent.
        const HRESULT first = call();
        if (FAILED(first)) { captureResult_ = first; return first; }
        if (!BeginWhite(device, layer)) { captureResult_ = E_FAIL; return E_FAIL; }
        const HRESULT second = call();
        const HRESULT restored = EndWhite(device, layer);
        if (FAILED(second) || FAILED(restored)) captureResult_ = FAILED(second) ? second : restored;
        return FAILED(second) ? second : restored;
    }
    bool Ready(int layer) const { return ready_[layer]; }
    IDirect3DSurface9* SourceTarget() const { return oldTarget_; }
    IDirect3DSurface9* CaptureTarget(int layer) const { return surfaces_[layer][0]; }
    IDirect3DTexture9* CaptureTexture(int layer, int basis) const { return textures_[layer][basis]; }
    float Aspect() const { return height_ ? float(width_)/height_ : 16.0f/9; }
private:
    bool Ensure(IDirect3DDevice9* device, UINT width, UINT height);
    bool BeginWhite(IDirect3DDevice9* device, int layer);
    HRESULT EndWhite(IDirect3DDevice9* device, int layer);
    struct FeedbackTexture {
        IDirect3DTexture9* game=nullptr;
        IDirect3DTexture9* white=nullptr;
        IDirect3DSurface9* surface=nullptr;
        bool ready=false;
    };
    std::vector<FeedbackTexture> feedback_;
    IDirect3DBaseTexture9* savedTextures_[16]{};
    IDirect3DTexture9* textures_[2][2]{};
    IDirect3DSurface9* surfaces_[2][2]{};
    IDirect3DSurface9* depth_[2]{};
    IDirect3DSurface9* drawDepth_ = nullptr;
    D3DVIEWPORT9 drawViewport_{};
    RECT drawScissor_{};
    HRESULT captureResult_ = S_OK;
    IDirect3DSurface9* oldTarget_ = nullptr;
    IDirect3DSurface9* oldDepth_ = nullptr;
    D3DVIEWPORT9 oldViewport_{};
    RECT oldScissor_{};
    IDirect3DVertexShader9* vs_ = nullptr;
    IDirect3DPixelShader9* ps_[4]{};
    IDirect3DVertexDeclaration9* declaration_ = nullptr;
    IDirect3DStateBlock9* state_ = nullptr;
    UINT width_ = 0, height_ = 0;
    bool ready_[2]{};
};
}
