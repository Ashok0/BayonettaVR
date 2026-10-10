#pragma once
#include "SharedFrameTransport.h"
namespace bvr {
class CompanionClient {
public:
    bool Start(const std::wstring& directory, bool test=false);
    bool Ready() const { return ready_; }
    bool Poll(ipc::Control& control);
    bool Publish(ID3D11Device* device, ID3D11DeviceContext* context,
                 ID3D11Texture2D* left, ID3D11Texture2D* right, ipc::Frame frame);
    void SetGrid(bool enabled);
    void Stop();
    unsigned Dropped() const { return publisher_.Dropped(); }
    ipc::Session& SessionForTest() { return session_; }
private:
    ipc::Session session_;
    HANDLE process_=nullptr;
    bool ready_=false;
    uint64_t sequence_=0;
    ipc::Control lastControl_{};
    SharedFramePublisher publisher_;
};
CompanionClient& Companion();
}
