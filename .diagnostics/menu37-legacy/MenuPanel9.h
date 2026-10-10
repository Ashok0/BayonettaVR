#pragma once
#include "Matrix.h"
#include <d3d9.h>

namespace bvr {
// Capture the game's complete Id / IdFont passes, including fixed-function
// vertices, opaque backgrounds and clipping. Reuse the pixels for both eyes.
class MenuPanel9 {
public: template<class Call> HRESULT CaptureDraw(IDirect3DDevice9*, int, Call&& call) { return call(); }
    ~MenuPanel9() { Reset(); }
    void Reset();
    void BeginFrame();
    bool Begin(IDirect3DDevice9* device, int layer);
    void End(IDirect3DDevice9* device, int layer);
    HRESULT Draw(IDirect3DDevice9* device, int layer, const Mat4& clip);
    bool Ready(int layer) const { return ready_[layer]; }
    IDirect3DSurface9* SourceTarget() const { return oldTarget_; }
    IDirect3DSurface9* CaptureTarget(int layer) const { return surfaces_[layer]; }
    float Aspect() const { return height_ ? float(width_)/height_ : 16.0f/9; }
private:
    bool Ensure(IDirect3DDevice9* device, UINT width, UINT height);
    IDirect3DTexture9* textures_[2]{};
    IDirect3DSurface9* surfaces_[2]{};
    IDirect3DSurface9* depth_ = nullptr;
    IDirect3DSurface9* oldTarget_ = nullptr;
    IDirect3DSurface9* oldDepth_ = nullptr;
    D3DVIEWPORT9 oldViewport_{};
    IDirect3DVertexShader9* vs_ = nullptr;
    IDirect3DPixelShader9* ps_ = nullptr;
    IDirect3DVertexDeclaration9* declaration_ = nullptr;
    IDirect3DStateBlock9* state_ = nullptr;
    UINT width_ = 0, height_ = 0;
    bool ready_[2]{};
};
}
