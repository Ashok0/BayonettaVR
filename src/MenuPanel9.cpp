#include "MenuPanel9.h"
#include <d3dcompiler.h>

namespace bvr {
namespace {
template<class T> void Release(T*& p) { if(p) p->Release(); p=nullptr; }
}
void MenuPanel9::Reset() {
    for(auto& pair:feedback_) {Release(pair.game);Release(pair.white);Release(pair.surface);}
    feedback_.clear();
    for(auto*& texture:savedTextures_)Release(texture);
    for(auto& layer:surfaces_) for(auto*& p:layer) Release(p);
    for(auto& layer:textures_) for(auto*& p:layer) Release(p);
    for(auto*& p:depth_) Release(p);
    Release(drawDepth_); Release(oldTarget_); Release(oldDepth_);
    Release(vs_); for(auto*& p:ps_) Release(p);
    Release(declaration_); Release(state_);
    width_=height_=0; BeginFrame();
}
void MenuPanel9::BeginFrame() {
    ready_[0]=ready_[1]=false;
    // Do not retain game scratch textures after the game stops using them.
    for(auto it=feedback_.begin();it!=feedback_.end();) {
        if(!it->ready) {
            Release(it->game);Release(it->white);Release(it->surface);
            it=feedback_.erase(it);
        } else {it->ready=false;++it;}
    }
}
bool MenuPanel9::Ensure(IDirect3DDevice9* device, UINT width, UINT height) {
    if(width_==width && height_==height && state_) return true;
    Reset();
    static const char source[] =
        "float4 r0:register(c0); float4 r1:register(c1);"
        "float4 r2:register(c2); float4 r3:register(c3);"
        "sampler2D black:register(s0); sampler2D white:register(s1);"
        "struct V {float4 p:POSITION; float2 uv:TEXCOORD0;};"
        "V vs(float2 p:POSITION,float2 uv:TEXCOORD0){V o; float4 v=float4(p,0,1);"
        "o.p=float4(dot(r0,v),dot(r1,v),dot(r2,v),dot(r3,v));o.uv=uv;return o;}"
        "float4 attenuate(float2 uv:TEXCOORD0):COLOR{return saturate(tex2D(white,uv)-tex2D(black,uv));}"
        "float4 boost(float2 uv:TEXCOORD0):COLOR{return saturate(tex2D(white,uv)-tex2D(black,uv)-1);}"
        "float4 emit(float2 uv:TEXCOORD0):COLOR{return max(tex2D(black,uv),0);}"
        "float4 subtract(float2 uv:TEXCOORD0):COLOR{return max(-tex2D(black,uv),0);}";
    ID3DBlob* vertex=nullptr;
    HRESULT hr=D3DCompile(source,sizeof(source)-1,nullptr,nullptr,nullptr,"vs","vs_2_0",0,0,&vertex,nullptr);
    if(SUCCEEDED(hr)) hr=device->CreateVertexShader(static_cast<const DWORD*>(vertex->GetBufferPointer()),&vs_);
    Release(vertex);
    const char* entries[]={"attenuate","boost","emit","subtract"};
    for(int i=0;i<4 && SUCCEEDED(hr);++i) {
        ID3DBlob* pixel=nullptr;
        hr=D3DCompile(source,sizeof(source)-1,nullptr,nullptr,nullptr,entries[i],"ps_2_0",0,0,&pixel,nullptr);
        if(SUCCEEDED(hr)) hr=device->CreatePixelShader(static_cast<const DWORD*>(pixel->GetBufferPointer()),&ps_[i]);
        Release(pixel);
    }
    const D3DVERTEXELEMENT9 elements[]={{0,0,D3DDECLTYPE_FLOAT2,D3DDECLMETHOD_DEFAULT,D3DDECLUSAGE_POSITION,0},
        {0,8,D3DDECLTYPE_FLOAT2,D3DDECLMETHOD_DEFAULT,D3DDECLUSAGE_TEXCOORD,0},D3DDECL_END()};
    if(SUCCEEDED(hr)) hr=device->CreateVertexDeclaration(elements,&declaration_);
    for(int i=0;i<2 && SUCCEEDED(hr);++i) for(int basis=0;basis<2 && SUCCEEDED(hr);++basis) {
        // Float targets retain additive values above one and subtractive values
        // below zero. UNORM clipping would corrupt white-minus-black transmission.
        hr=device->CreateTexture(width,height,1,D3DUSAGE_RENDERTARGET,D3DFMT_A16B16G16R16F,D3DPOOL_DEFAULT,&textures_[i][basis],nullptr);
        if(SUCCEEDED(hr)) hr=textures_[i][basis]->GetSurfaceLevel(0,&surfaces_[i][basis]);
    }
    for(int i=0;i<2 && SUCCEEDED(hr);++i)
        hr=device->CreateDepthStencilSurface(width,height,D3DFMT_D24S8,D3DMULTISAMPLE_NONE,0,TRUE,&depth_[i],nullptr);
    if(SUCCEEDED(hr)) hr=device->CreateStateBlock(D3DSBT_ALL,&state_);
    if(FAILED(hr)) { Reset(); return false; }
    width_=width; height_=height; return true;
}
bool MenuPanel9::Begin(IDirect3DDevice9* device,int layer) {
    IDirect3DSurface9* target=nullptr;
    if(FAILED(device->GetRenderTarget(0,&target))) return false;
    D3DSURFACE_DESC desc{};
    HRESULT hr=target->GetDesc(&desc);
    target->Release();
    if(FAILED(hr) || !Ensure(device,desc.Width,desc.Height)) return false;
    ready_[layer]=false;
    captureResult_=S_OK;
    if(FAILED(device->GetViewport(&oldViewport_)) || FAILED(device->GetScissorRect(&oldScissor_)) ||
       FAILED(device->GetRenderTarget(0,&oldTarget_))) return false;
    device->GetDepthStencilSurface(&oldDepth_); // A null depth surface is valid.
    DWORD scissorEnabled=FALSE;
    device->GetRenderState(D3DRS_SCISSORTESTENABLE,&scissorEnabled);
    device->SetRenderState(D3DRS_SCISSORTESTENABLE,FALSE);
    const D3DVIEWPORT9 fullViewport={0,0,width_,height_,0,1};
    for(int basis=1;basis>=0 && SUCCEEDED(hr);--basis) {
        device->SetDepthStencilSurface(nullptr);
        hr=device->SetRenderTarget(0,surfaces_[layer][basis]);
        if(SUCCEEDED(hr)) hr=device->SetDepthStencilSurface(depth_[basis]);
        if(SUCCEEDED(hr)) hr=device->SetViewport(&fullViewport);
        if(SUCCEEDED(hr)) hr=device->Clear(0,nullptr,D3DCLEAR_TARGET|D3DCLEAR_ZBUFFER|D3DCLEAR_STENCIL,
            basis ? 0xffffffff : 0xff000000,1,0);
    }
    device->SetViewport(&oldViewport_);
    device->SetScissorRect(&oldScissor_);
    device->SetRenderState(D3DRS_SCISSORTESTENABLE,scissorEnabled);
    if(FAILED(hr)) { End(device,layer); ready_[layer]=false; return false; }
    return true;
}
void MenuPanel9::End(IDirect3DDevice9* device,int layer) {
    device->SetDepthStencilSurface(nullptr);
    device->SetRenderTarget(0,oldTarget_);
    device->SetDepthStencilSurface(oldDepth_);
    device->SetViewport(&oldViewport_);
    device->SetScissorRect(&oldScissor_);
    Release(oldTarget_); Release(oldDepth_);
    ready_[layer]=SUCCEEDED(captureResult_);
}
bool MenuPanel9::BeginWhite(IDirect3DDevice9* device,int layer) {
    if(FAILED(device->GetViewport(&drawViewport_)) || FAILED(device->GetScissorRect(&drawScissor_))) return false;
    device->GetDepthStencilSurface(&drawDepth_);
    device->SetDepthStencilSurface(nullptr);
    HRESULT hr=device->SetRenderTarget(0,surfaces_[layer][1]);
    if(SUCCEEDED(hr)) hr=device->SetDepthStencilSurface(drawDepth_==depth_[0] ? depth_[1] : drawDepth_);
    if(SUCCEEDED(hr)) hr=device->SetViewport(&drawViewport_);
    if(SUCCEEDED(hr)) hr=device->SetScissorRect(&drawScissor_);
    // The game binds UI textures at s13/s14, not just the low sampler slots.
    // Each framebuffer snapshot needs its own white-background counterpart.
    if(SUCCEEDED(hr)) for(DWORD stage=0;stage<16;++stage) {
        IDirect3DBaseTexture9* texture=nullptr;
        device->GetTexture(stage,&texture);
        for(const auto& pair:feedback_) if(pair.ready && texture==pair.game) {
            savedTextures_[stage]=texture;texture=nullptr;
            hr=device->SetTexture(stage,pair.white);break;
        }
        Release(texture);
        if(FAILED(hr))break;
    }
    if(FAILED(hr)) EndWhite(device,layer);
    return SUCCEEDED(hr);
}
HRESULT MenuPanel9::EndWhite(IDirect3DDevice9* device,int layer) {
    HRESULT textures=S_OK;
    for(DWORD stage=0;stage<16;++stage) if(savedTextures_[stage]) {
        const HRESULT hr=device->SetTexture(stage,savedTextures_[stage]);
        if(FAILED(hr))textures=hr;
        Release(savedTextures_[stage]);
    }
    device->SetDepthStencilSurface(nullptr);
    const HRESULT target=device->SetRenderTarget(0,surfaces_[layer][0]);
    const HRESULT depth=device->SetDepthStencilSurface(drawDepth_);
    const HRESULT viewport=device->SetViewport(&drawViewport_);
    const HRESULT scissor=device->SetScissorRect(&drawScissor_);
    Release(drawDepth_);
    return FAILED(textures) ? textures : FAILED(target) ? target : FAILED(depth) ? depth : FAILED(viewport) ? viewport : scissor;
}
HRESULT MenuPanel9::CaptureCopy(IDirect3DDevice9* device,int layer,IDirect3DSurface9* source,const RECT* sourceRect,
    IDirect3DSurface9* destination,const RECT* destinationRect,D3DTEXTUREFILTERTYPE filter,CopyFn copy) {
    // cIdOtWork::drawBlur snapshots the engine's cached framebuffer between
    // horizontal and vertical passes. SetRenderTarget redirection alone leaves
    // those copies reading the untouched scene, overwriting labels with boxes.
    IDirect3DSurface9* whiteSource=nullptr;
    if(oldTarget_ && (source==oldTarget_ || source==surfaces_[layer][0])) {
        source=surfaces_[layer][0];whiteSource=surfaces_[layer][1];
    } else if(source) {
        IDirect3DTexture9* owner=nullptr;
        if(SUCCEEDED(source->GetContainer(__uuidof(IDirect3DTexture9),reinterpret_cast<void**>(&owner)))) {
            for(const auto& pair:feedback_)if(pair.ready && pair.game==owner)whiteSource=pair.surface;
            Release(owner);
        }
    }
    if(!whiteSource || !destination || destination==oldTarget_ ||
       destination==surfaces_[layer][0] || destination==surfaces_[layer][1])
        return copy(device,source,sourceRect,destination,destinationRect,filter);
    IDirect3DTexture9* owner=nullptr;
    HRESULT hr=destination->GetContainer(__uuidof(IDirect3DTexture9),reinterpret_cast<void**>(&owner));
    if(FAILED(hr) || !owner)return copy(device,source,sourceRect,destination,destinationRect,filter);
    size_t index=0;
    while(index<feedback_.size() && feedback_[index].game!=owner)++index;
    if(index==feedback_.size()) {
        D3DSURFACE_DESC desc{};hr=destination->GetDesc(&desc);
        FeedbackTexture pair{};pair.game=owner;
        if(SUCCEEDED(hr))hr=device->CreateTexture(desc.Width,desc.Height,1,D3DUSAGE_RENDERTARGET,
            D3DFMT_A16B16G16R16F,D3DPOOL_DEFAULT,&pair.white,nullptr);
        if(SUCCEEDED(hr))hr=pair.white->GetSurfaceLevel(0,&pair.surface);
        if(FAILED(hr)) {Release(pair.game);Release(pair.white);Release(pair.surface);captureResult_=hr;return hr;}
        feedback_.push_back(pair);
    } else Release(owner);
    // whiteSource may point into another pair; the COM surface itself remains
    // stable if appending to feedback_ moved its vector storage.
    auto& pair=feedback_[index];
    // Preserve untouched pixels when the first snapshot covers only a region.
    if(!pair.ready && destinationRect) {
        hr=copy(device,destination,nullptr,pair.surface,nullptr,D3DTEXF_NONE);
        if(FAILED(hr)){captureResult_=hr;return hr;}
    }
    const HRESULT black=copy(device,source,sourceRect,destination,destinationRect,filter);
    const HRESULT white=copy(device,whiteSource,sourceRect,pair.surface,destinationRect,filter);
    pair.ready=SUCCEEDED(black) && SUCCEEDED(white);
    if(!pair.ready)captureResult_=FAILED(black) ? black : white;
    return FAILED(black) ? black : white;
}
HRESULT MenuPanel9::Draw(IDirect3DDevice9* device,int layer,const Mat4& clip) {
    if(!ready_[layer] || !state_) return E_FAIL;
    HRESULT hr=state_->Capture();
    if(FAILED(hr)) return hr;
    device->SetVertexDeclaration(declaration_);
    device->SetVertexShader(vs_);
    device->SetVertexShaderConstantF(0,&clip.m[0][0],4);
    for(int i=0;i<2;++i) {
        device->SetTexture(i,textures_[layer][i]);
        device->SetSamplerState(i,D3DSAMP_MINFILTER,D3DTEXF_LINEAR);
        device->SetSamplerState(i,D3DSAMP_MAGFILTER,D3DTEXF_LINEAR);
        device->SetSamplerState(i,D3DSAMP_MIPFILTER,D3DTEXF_NONE);
        device->SetSamplerState(i,D3DSAMP_ADDRESSU,D3DTADDRESS_CLAMP);
        device->SetSamplerState(i,D3DSAMP_ADDRESSV,D3DTADDRESS_CLAMP);
        device->SetSamplerState(i,D3DSAMP_SRGBTEXTURE,FALSE);
    }
    device->SetRenderState(D3DRS_ZENABLE,FALSE);
    device->SetRenderState(D3DRS_ZWRITEENABLE,FALSE);
    device->SetRenderState(D3DRS_STENCILENABLE,FALSE);
    device->SetRenderState(D3DRS_SCISSORTESTENABLE,FALSE);
    device->SetRenderState(D3DRS_ALPHATESTENABLE,FALSE);
    device->SetRenderState(D3DRS_ALPHABLENDENABLE,TRUE);
    device->SetRenderState(D3DRS_BLENDOP,D3DBLENDOP_ADD);
    device->SetRenderState(D3DRS_SEPARATEALPHABLENDENABLE,FALSE);
    device->SetRenderState(D3DRS_COLORWRITEENABLE,7);
    device->SetRenderState(D3DRS_CULLMODE,D3DCULL_NONE);
    device->SetRenderState(D3DRS_FILLMODE,D3DFILL_SOLID);
    device->SetRenderState(D3DRS_SRGBWRITEENABLE,FALSE);
    device->SetRenderState(D3DRS_CLIPPLANEENABLE,0);
    device->SetStreamSourceFreq(0,1);
    const float vertices[4][4]={{-1,1,0,0},{1,1,1,0},{-1,-1,0,1},{1,-1,1,1}};
    // UI RGB = black + (white - black) * scene RGB. Keep separate per-channel
    // transmission (including brightening), positive emission and subtraction;
    // framebuffer alpha remains available to the game's own mask operations.
    const DWORD src[]={D3DBLEND_ZERO,D3DBLEND_DESTCOLOR,D3DBLEND_ONE,D3DBLEND_ONE};
    const DWORD dst[]={D3DBLEND_SRCCOLOR,D3DBLEND_ONE,D3DBLEND_ONE,D3DBLEND_ONE};
    for(int pass=0;pass<4 && SUCCEEDED(hr);++pass) {
        device->SetPixelShader(ps_[pass]);
        device->SetRenderState(D3DRS_SRCBLEND,src[pass]);
        device->SetRenderState(D3DRS_DESTBLEND,dst[pass]);
        device->SetRenderState(D3DRS_BLENDOP,pass==3 ? D3DBLENDOP_REVSUBTRACT : D3DBLENDOP_ADD);
        hr=device->DrawPrimitiveUP(D3DPT_TRIANGLESTRIP,2,vertices,sizeof(vertices[0]));
    }
    const HRESULT restore=state_->Apply();
    return FAILED(hr) ? hr : restore;
}
}
