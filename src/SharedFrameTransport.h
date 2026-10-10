#pragma once
#include "CompanionProtocol.h"
#include <d3d11.h>
#include <dxgi.h>
#include <wrl/client.h>

namespace bvr {
// Ownership is transferred only after a GPU completion query. The helper copies
// each pair into private textures and completes that copy before freeing its slot.
// Thus neither process can overwrite pixels still being read by the other GPU queue.
class SharedFramePublisher {
public:
    bool Publish(ipc::Shared& shared, ID3D11Device* device, ID3D11DeviceContext* context,
                 ID3D11Texture2D* left, ID3D11Texture2D* right, const ipc::Frame& frame);
    unsigned Dropped() const { return dropped_; }
    HRESULT Error() const { return error_; }
private:
    Microsoft::WRL::ComPtr<ID3D11Texture2D> textures_[ipc::SlotCount][2];
    unsigned dropped_=0;
    uint32_t generation_=0;
    HRESULT error_=S_OK;
};
class SharedFrameConsumer {
public:
    // Copy newest completed stereo pair to private ring textures. On false,
    // retain the previous pair AND its pose. Never relabel old pixels as new.
    bool Consume(ipc::Shared& shared, ID3D11Device* device, ID3D11DeviceContext* context);
    ID3D11Texture2D* Eye(int eye) const { return output_[eye][outputSlot_].Get(); }
    const ipc::Frame& Frame() const { return frame_; }
    bool HasFrame() const { return frame_.sequence!=0; }
    HRESULT Error() const { return error_; }
private:
    Microsoft::WRL::ComPtr<ID3D11Texture2D> input_[ipc::SlotCount][2];
    uint32_t generation_[ipc::SlotCount]{};
    Microsoft::WRL::ComPtr<ID3D11Texture2D> output_[2][4];
    unsigned outputSlot_=0;
    ipc::Frame frame_{};
    HRESULT error_=S_OK;
};
HRESULT FinishGpu(ID3D11Device* device, ID3D11DeviceContext* context, DWORD timeoutMs=2000);
}
