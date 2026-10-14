#pragma once
#include "FirstPersonCostumes.h"
#include <d3d9.h>
namespace bvr {
template<class Draw> HRESULT DrawFirstPersonCostumeRanges(IDirect3DDevice9* d,unsigned start,const FirstPersonBodyRule& rule,Draw&& draw) {
    if(!rule.rangeCount)return S_OK;
    DWORD originalCull=0;
    HRESULT hr=d->GetRenderState(D3DRS_CULLMODE,&originalCull);
    if(FAILED(hr))return hr;
    DWORD currentCull=originalCull;
    for(unsigned i=0;i<rule.rangeCount;++i) {
        const auto& range=kFirstPersonRanges[rule.rangeStart+i];
        DWORD cull=originalCull;
        // Starting on an odd triangle reverses a strip's winding. Preserve
        // the game's faces without modifying its index buffer or vertices.
        if(rule.primitiveType==D3DPT_TRIANGLESTRIP&&(range.offset&1)) {
            if(cull==D3DCULL_CW)cull=D3DCULL_CCW;
            else if(cull==D3DCULL_CCW)cull=D3DCULL_CW;
        }
        if(cull!=currentCull) {
            hr=d->SetRenderState(D3DRS_CULLMODE,cull);
            if(FAILED(hr))break;
            currentCull=cull;
        }
        hr=draw(start+range.offset,range.primitives);
        if(FAILED(hr))break;
    }
    if(currentCull!=originalCull) {
        const HRESULT restored=d->SetRenderState(D3DRS_CULLMODE,originalCull);
        if(SUCCEEDED(hr))hr=restored;
    }
    return hr;
}
}
