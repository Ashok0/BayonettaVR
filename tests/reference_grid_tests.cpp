#include "ReferenceGridRenderer.h"
#include "ReferenceGrid.h"
#include <wrl/client.h>
#include <cstdio>
#include <cmath>
using Microsoft::WRL::ComPtr;
using namespace bvr;

int main() {
    ComPtr<ID3D11Device> device;
    ComPtr<ID3D11DeviceContext> context;
    HRESULT hr = D3D11CreateDevice(nullptr, D3D_DRIVER_TYPE_WARP, nullptr, 0,
        nullptr, 0, D3D11_SDK_VERSION, &device, nullptr, &context);
    if (FAILED(hr)) { std::printf("WARP device failed: %08lX\n", hr); return 1; }
    D3D11_TEXTURE2D_DESC desc{};
    desc.Width = desc.Height = 256;
    desc.MipLevels = desc.ArraySize = 1;
    desc.Format = DXGI_FORMAT_B8G8R8A8_UNORM;
    desc.SampleDesc.Count = 1;
    desc.BindFlags = D3D11_BIND_RENDER_TARGET | D3D11_BIND_SHADER_RESOURCE;
    ComPtr<ID3D11Texture2D> target, readback;
    if (FAILED(device->CreateTexture2D(&desc, nullptr, &target))) return 2;
    desc.Usage = D3D11_USAGE_STAGING;
    desc.BindFlags = 0;
    desc.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
    if (FAILED(device->CreateTexture2D(&desc, nullptr, &readback))) return 3;
    ComPtr<ID3D11RenderTargetView> rtv;
    if (FAILED(device->CreateRenderTargetView(target.Get(), nullptr, &rtv))) return 4;
    ReferenceGridRenderer renderer;
    const float background[4]{0.1f,0,0,1};
    unsigned cases = 0;
    for (int eye = 0; eye < 2; ++eye) for (int turn = -2; turn <= 2; ++turn) {
        const float angle = float(turn)*0.21f;
        Mat4 view = Identity();
        view.m[0][0] = view.m[2][2] = std::cos(angle);
        view.m[0][2] = std::sin(angle);
        view.m[2][0] = -std::sin(angle);
        view.m[0][3] = eye ? -0.0313f : 0.0313f;
        const Mat4 projection = PerspectiveRawLH(eye ? -0.8391f : -1.3764f,
            eye ? 1.3764f : 0.8391f, -1.4281f, 0.9657f, 0.1f, 10000);
        const Mat4 clip = Mul(projection, view);
        context->ClearRenderTargetView(rtv.Get(), background);
        hr = renderer.Draw(device.Get(), context.Get(), target.Get(), clip);
        if (FAILED(hr)) { std::printf("grid draw failed: %08lX\n", hr); return 5; }
        context->CopyResource(readback.Get(), target.Get());
        D3D11_MAPPED_SUBRESOURCE pixels{};
        if (FAILED(context->Map(readback.Get(), 0, D3D11_MAP_READ, 0, &pixels))) return 6;
        unsigned yellow = 0, cyan = 0, unchanged = 0;
        double sumX = 0, sumY = 0;
        for (unsigned y = 0; y < 256; ++y) for (unsigned x = 0; x < 256; ++x) {
            const auto* p = static_cast<const unsigned char*>(pixels.pData) + y*pixels.RowPitch + x*4;
            if (p[2] > 200 && p[1] > 200 && p[0] < 50) {
                ++yellow; sumX += x+0.5; sumY += y+0.5;
            }
            if (p[0] > 200 && p[1] > 200 && p[2] < 50) ++cyan;
            if (p[0] == 0 && p[1] == 0 && p[2] >= 25 && p[2] <= 26) ++unchanged;
        }
        context->Unmap(readback.Get(), 0);
        const float w = clip.m[3][2]*3 + clip.m[3][3];
        const float expectedX = (0.5f + 0.5f*(clip.m[0][2]*3+clip.m[0][3])/w)*256;
        const float expectedY = (0.5f - 0.5f*(clip.m[1][2]*3+clip.m[1][3])/w)*256;
        if (yellow < 8 || cyan < 500 || unchanged < 40000 ||
            std::fabs(sumX/yellow-expectedX) > 2 || std::fabs(sumY/yellow-expectedY) > 2) {
            std::printf("grid mismatch: eye=%d turn=%d yellow=%u cyan=%u background=%u centroid=%.2f/%.2f expected=%.2f/%.2f\n",
                eye, turn, yellow, cyan, unchanged, yellow ? sumX/yellow : 0,
                yellow ? sumY/yellow : 0, expectedX, expectedY);
            return 7;
        }
        ++cases;
        if (turn == 0) renderer.Reset(); // Recreate resources after release/reset.
    }
    context->ClearState();
    std::printf("reference grid: %u WARP render/readback cases passed (both eyes, turns, reset, background preservation)\n", cases);
    return 0;
}
