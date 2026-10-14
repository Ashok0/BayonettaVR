#include "SharedFrameTransport.h"
namespace bvr {
using Microsoft::WRL::ComPtr;
HRESULT FinishGpu(ID3D11Device* device, ID3D11DeviceContext* context, DWORD timeoutMs) {
    D3D11_QUERY_DESC desc{D3D11_QUERY_EVENT,0}; ComPtr<ID3D11Query> fence;
    HRESULT hr=device->CreateQuery(&desc,&fence); if(FAILED(hr))return hr;
    context->End(fence.Get()); context->Flush();
    const ULONGLONG deadline=GetTickCount64()+timeoutMs;
    while((hr=context->GetData(fence.Get(),nullptr,0,D3D11_ASYNC_GETDATA_DONOTFLUSH))==S_FALSE) {
        if(GetTickCount64()>=deadline)return HRESULT_FROM_WIN32(WAIT_TIMEOUT);
        SwitchToThread();
    }
    return hr;
}
bool SharedFramePublisher::Publish(ipc::Shared& shared, ID3D11Device* device, ID3D11DeviceContext* context,
                                  ID3D11Texture2D* left, ID3D11Texture2D* right, const ipc::Frame& frame) {
    error_=S_OK;
    if(!device||!context||!left||!right||!frame.sequence||!frame.sample){error_=E_INVALIDARG;return false;}
    D3D11_TEXTURE2D_DESC desc{},other{};left->GetDesc(&desc);right->GetDesc(&other);
    if(desc.Width!=other.Width||desc.Height!=other.Height||desc.Format!=other.Format||
       desc.Format!=DXGI_FORMAT_B8G8R8A8_UNORM||desc.SampleDesc.Count!=1||other.SampleDesc.Count!=1){error_=E_INVALIDARG;return false;}
    int index=-1;
    for(unsigned i=0;i<ipc::SlotCount;++i)if(ipc::Claim(shared.slots[i],ipc::Free,ipc::Writing)){index=int(i);break;}
    if(index<0){++dropped_;return false;}
    auto& slot=shared.slots[index];
    ComPtr<ID3D11Device> previousDevice;
    if(textures_[index][0])textures_[index][0]->GetDevice(&previousDevice);
    if(previousDevice.Get()!=device||slot.width!=desc.Width||slot.height!=desc.Height) {
        ComPtr<ID3D11Texture2D> replacement[2]; uint64_t handles[2]{};
        desc.MipLevels=desc.ArraySize=1;desc.Usage=D3D11_USAGE_DEFAULT;
        desc.CPUAccessFlags=0;desc.BindFlags=D3D11_BIND_SHADER_RESOURCE|D3D11_BIND_RENDER_TARGET;
        desc.MiscFlags=D3D11_RESOURCE_MISC_SHARED;
        for(int e=0;e<2;++e){
            ComPtr<IDXGIResource> resource;HANDLE handle=nullptr;
            if(FAILED(error_=device->CreateTexture2D(&desc,nullptr,&replacement[e]))||
               FAILED(error_=replacement[e].As(&resource))||FAILED(error_=resource->GetSharedHandle(&handle))){ipc::Release(slot,ipc::Free);return false;}
            handles[e]=reinterpret_cast<uintptr_t>(handle);
        }
        for(int e=0;e<2;++e){textures_[index][e]=replacement[e];slot.handle[e]=handles[e];}
        slot.width=desc.Width;slot.height=desc.Height;slot.generation=++generation_;
    }
    context->CopyResource(textures_[index][0].Get(),left);
    context->CopyResource(textures_[index][1].Get(),right);
    error_=FinishGpu(device,context);
    if(FAILED(error_)) {
        // Quarantine a slot on timeout: queued writes may still be in flight.
        // Do not expose it to the reader or reuse it for another frame.
        return false;
    }
    slot.frame=frame;
    ipc::Release(slot,ipc::Ready);
    return true;
}
bool SharedFrameConsumer::Consume(ipc::Shared& shared, ID3D11Device* device, ID3D11DeviceContext* context) {
    int selected=-1;
    // Claim first, then inspect payload: never read concurrently with producer.
    for(unsigned i=0;i<ipc::SlotCount;++i) {
        if(!ipc::Claim(shared.slots[i],ipc::Ready,ipc::Reading))continue;
        if(shared.slots[i].frame.sequence<=frame_.sequence){ipc::Release(shared.slots[i],ipc::Free);continue;}
        if(selected<0||shared.slots[i].frame.sequence>shared.slots[selected].frame.sequence) {
            if(selected>=0)ipc::Release(shared.slots[selected],ipc::Free);selected=int(i);
        }else ipc::Release(shared.slots[i],ipc::Free);
    }
    if(selected<0)return false;
    auto& slot=shared.slots[selected];
    auto fail=[&](HRESULT hr){error_=hr;ipc::Release(slot,ipc::Free);return false;};
    if(!slot.width||!slot.height||slot.width>16384||slot.height>16384||!slot.frame.sample)return fail(E_INVALIDARG);
    if(generation_[selected]!=slot.generation) {
        for(int e=0;e<2;++e){
            input_[selected][e].Reset();
            auto hr=device->OpenSharedResource(reinterpret_cast<HANDLE>(static_cast<uintptr_t>(slot.handle[e])),
                __uuidof(ID3D11Texture2D),reinterpret_cast<void**>(input_[selected][e].GetAddressOf()));
            if(FAILED(hr))return fail(hr);
        }
        generation_[selected]=slot.generation;
    }
    const unsigned next=(outputSlot_+1)%4;
    for(int e=0;e<2;++e) {
        D3D11_TEXTURE2D_DESC source{},target{};input_[selected][e]->GetDesc(&source);
        if(source.Width!=slot.width||source.Height!=slot.height||source.Format!=DXGI_FORMAT_B8G8R8A8_UNORM||source.SampleDesc.Count!=1)return fail(E_INVALIDARG);
        if(output_[e][next])output_[e][next]->GetDesc(&target);
        if(target.Width!=source.Width||target.Height!=source.Height) {
            output_[e][next].Reset();source.MiscFlags=0;
            auto hr=device->CreateTexture2D(&source,nullptr,&output_[e][next]);if(FAILED(hr))return fail(hr);
        }
    }
    for(int e=0;e<2;++e)context->CopyResource(output_[e][next].Get(),input_[selected][e].Get());
    const HRESULT complete=FinishGpu(device,context);
    if(FAILED(complete)){error_=complete;return false;} // Quarantine Reading slot on uncertain GPU completion.
    frame_=slot.frame;outputSlot_=next;
    ipc::Release(slot,ipc::Free);error_=S_OK;return true;
}
}
