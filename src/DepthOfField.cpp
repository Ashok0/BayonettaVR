#include "DepthOfField.h"
#include <vector>
namespace bvr {
uint32_t DepthOfFieldFilter::Register(IDirect3DPixelShader9* shader) {
    if(!shader)return 0;
    shaders_.erase(shader);
    UINT size=0;
    if(FAILED(shader->GetFunction(nullptr,&size))||size>64*1024||size<8)return 0;
    std::vector<DWORD> original((size+3)/4);
    if(FAILED(shader->GetFunction(original.data(),&size)))return 0;
    const uint32_t crc=ShaderCrc32(original.data(),size);
    if(!IsDepthOfFieldShader(crc,size))return 0;
    shaders_.insert(shader);return crc;
}
bool DepthOfFieldFilter::ShouldSkip(IDirect3DDevice9* device,bool disabled) const {
    if(!disabled||shaders_.empty())return false;
    // Read real state so state-block Apply cannot leave a cached binding stale.
    IDirect3DPixelShader9* current=nullptr;
    if(FAILED(device->GetPixelShader(&current)))return false;
    const bool skip=shaders_.count(current)!=0;
    if(current)current->Release();
    return skip;
}
}
