#include "ReferenceGridRenderer.h"
#include "ReferenceGrid.h"
#include <d3dcompiler.h>
#include <cstring>

namespace bvr {
namespace {
template<class T> void Release(T*& object) {
    if (object) object->Release();
    object = nullptr;
}
}

void ReferenceGridRenderer::Reset() {
    Release(vertices_); Release(layout_);
    Release(pixelShader_); Release(vertexShader_);
}

HRESULT ReferenceGridRenderer::Init(ID3D11Device* device) {
    if (vertices_) return S_OK;
    // No game shader, matrix upload/transposition, or texture sampling here.
    static const char source[] =
        "struct V { float4 p:SV_Position; float4 c:COLOR; };"
        "V vs(float4 p:POSITION,float4 c:COLOR) { V o; o.p=p; o.c=c; return o; }"
        "float4 ps(V v):SV_Target { return v.c; }";
    ID3DBlob* vs = nullptr;
    ID3DBlob* ps = nullptr;
    HRESULT hr = D3DCompile(source, sizeof(source)-1, nullptr, nullptr, nullptr,
                           "vs", "vs_4_0", 0, 0, &vs, nullptr);
    if (SUCCEEDED(hr)) hr = D3DCompile(source, sizeof(source)-1, nullptr, nullptr, nullptr,
                                     "ps", "ps_4_0", 0, 0, &ps, nullptr);
    if (SUCCEEDED(hr)) hr = device->CreateVertexShader(vs->GetBufferPointer(), vs->GetBufferSize(),
                                                      nullptr, &vertexShader_);
    if (SUCCEEDED(hr)) hr = device->CreatePixelShader(ps->GetBufferPointer(), ps->GetBufferSize(),
                                                     nullptr, &pixelShader_);
    const D3D11_INPUT_ELEMENT_DESC elements[] = {
        {"POSITION",0,DXGI_FORMAT_R32G32B32A32_FLOAT,0,0,D3D11_INPUT_PER_VERTEX_DATA,0},
        {"COLOR",0,DXGI_FORMAT_R32G32B32A32_FLOAT,0,16,D3D11_INPUT_PER_VERTEX_DATA,0},
    };
    if (SUCCEEDED(hr)) hr = device->CreateInputLayout(elements, 2, vs->GetBufferPointer(),
                                                     vs->GetBufferSize(), &layout_);
    D3D11_BUFFER_DESC desc{};
    desc.ByteWidth = sizeof(ReferenceVertices);
    desc.Usage = D3D11_USAGE_DYNAMIC;
    desc.BindFlags = D3D11_BIND_VERTEX_BUFFER;
    desc.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;
    if (SUCCEEDED(hr)) hr = device->CreateBuffer(&desc, nullptr, &vertices_);
    Release(vs); Release(ps);
    if (FAILED(hr)) Reset();
    return hr;
}

HRESULT ReferenceGridRenderer::Draw(ID3D11Device* device, ID3D11DeviceContext* context,
                                    ID3D11Texture2D* target, const Mat4& clip) {
    if (!device || !context || !target) return E_INVALIDARG;
    HRESULT hr = Init(device);
    if (FAILED(hr)) return hr;
    ID3D11RenderTargetView* rtv = nullptr;
    hr = device->CreateRenderTargetView(target, nullptr, &rtv);
    if (FAILED(hr)) return hr;
    const auto data = BuildReferenceGrid(clip);
    D3D11_MAPPED_SUBRESOURCE mapped{};
    hr = context->Map(vertices_, 0, D3D11_MAP_WRITE_DISCARD, 0, &mapped);
    if (SUCCEEDED(hr)) {
        memcpy(mapped.pData, data.data(), sizeof(data));
        context->Unmap(vertices_, 0);
        D3D11_TEXTURE2D_DESC desc{};
        target->GetDesc(&desc);
        const D3D11_VIEWPORT viewport{0,0,float(desc.Width),float(desc.Height),0,1};
        context->RSSetViewports(1, &viewport);
        context->RSSetState(nullptr);
        context->OMSetRenderTargets(1, &rtv, nullptr);
        context->OMSetBlendState(nullptr, nullptr, 0xffffffff);
        context->OMSetDepthStencilState(nullptr, 0);
        context->IASetInputLayout(layout_);
        context->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_LINELIST);
        const UINT stride = sizeof(ReferenceVertex), offset = 0;
        context->IASetVertexBuffers(0, 1, &vertices_, &stride, &offset);
        context->VSSetShader(vertexShader_, nullptr, 0);
        context->GSSetShader(nullptr, nullptr, 0);
        context->PSSetShader(pixelShader_, nullptr, 0);
        context->Draw(static_cast<UINT>(data.size()), 0);
        // Do not leave a submitted/copy-source texture bound for writing.
        context->OMSetRenderTargets(0, nullptr, nullptr);
    }
    Release(rtv);
    return hr;
}
}
