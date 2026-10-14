#pragma once
#include <d3d9.h>
#include <cstddef>
#include <cstdint>
#include <unordered_set>
namespace bvr {
// Verified against shader.dat in the Steam data13.cpk. Identifiers are from
// masterotaku's optional Bayonetta cutscene DOF removal (Helix Mod, 2017-04-19).
inline uint32_t ShaderCrc32(const void* bytes,size_t size) {
    const auto* data=static_cast<const unsigned char*>(bytes);
    uint32_t crc=0xffffffff;
    for(size_t i=0;i<size;++i){crc^=data[i];for(int bit=0;bit<8;++bit)crc=(crc>>1)^(0xedb88320u&uint32_t(-int(crc&1)));}
    return ~crc;
}
inline bool IsDepthOfFieldShader(uint32_t crc,size_t size) {
    return (crc==0xb959e4d7&&size==4140)||(crc==0x548c7346&&size==4496)||
           (crc==0xfc2a113c&&size==4608)||(crc==0xcb1d0471&&size==2376);
}
using CreatePixelShaderFn=HRESULT(STDMETHODCALLTYPE*)(IDirect3DDevice9*,const DWORD*,IDirect3DPixelShader9**);
class PixelShaderDrawFilter {
public:
    using Match = bool(*)(uint32_t,size_t);
    explicit PixelShaderDrawFilter(Match match=IsDepthOfFieldShader):match_(match){}
    // Recognition only: never replaces/releases a shader returned to the game.
    uint32_t Register(IDirect3DPixelShader9* shader);
    bool ShouldSkip(IDirect3DDevice9* device,bool disabled) const;
    void Reset(){shaders_.clear();}
private:
    Match match_;
    // Creation hooks erase reused addresses; lookup never dereferences a stale key.
    std::unordered_set<IDirect3DPixelShader9*> shaders_;
};
using DepthOfFieldFilter = PixelShaderDrawFilter;
}
