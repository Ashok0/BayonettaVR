#pragma once
#include "Matrix.h"
#include <d3d11.h>

namespace bvr {
// Only for the mod's private D3D11 copy context; deliberately does not preserve
// an application's draw state. Never call with Bayonetta's D3D9 device.
class ReferenceGridRenderer {
public:
    ~ReferenceGridRenderer() { Reset(); }
    ReferenceGridRenderer() = default;
    ReferenceGridRenderer(const ReferenceGridRenderer&) = delete;
    ReferenceGridRenderer& operator=(const ReferenceGridRenderer&) = delete;
    void Reset();
    HRESULT Draw(ID3D11Device* device, ID3D11DeviceContext* context,
                 ID3D11Texture2D* target, const Mat4& clip);
private:
    HRESULT Init(ID3D11Device* device);
    ID3D11VertexShader* vertexShader_ = nullptr;
    ID3D11PixelShader* pixelShader_ = nullptr;
    ID3D11InputLayout* layout_ = nullptr;
    ID3D11Buffer* vertices_ = nullptr;
};
}
