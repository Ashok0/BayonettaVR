#include "MoviePanel9.h"
#include <d3dcompiler.h>
namespace bvr {
namespace { template<class T> void Release(T*& value){if(value)value->Release();value=nullptr;} }
void MoviePanel9::Reset(){
    Release(texture_);Release(surface_);Release(oldTarget_);Release(oldDepth_);
    Release(vs_);Release(ps_);Release(declaration_);Release(state_);
    width_=height_=0;BeginFrame();
}
bool MoviePanel9::Ensure(IDirect3DDevice9* d,UINT width,UINT height){
    if(width_==width && height_==height && state_)return true;
    Reset();
    const char source[]=
        "float4 r0:register(c0);float4 r1:register(c1);float4 r2:register(c2);float4 r3:register(c3);"
        "sampler2D movie:register(s0);struct V{float4 p:POSITION;float2 uv:TEXCOORD0;};"
        "V vs(float2 p:POSITION,float2 uv:TEXCOORD0){V o;float4 v=float4(p,0,1);"
        "o.p=float4(dot(r0,v),dot(r1,v),dot(r2,v),dot(r3,v));o.uv=uv;return o;}"
        "float4 ps(float2 uv:TEXCOORD0):COLOR{return float4(tex2D(movie,uv).rgb,1);}";
    ID3DBlob* blob=nullptr;
    HRESULT hr=D3DCompile(source,sizeof(source)-1,nullptr,nullptr,nullptr,"vs","vs_2_0",0,0,&blob,nullptr);
    if(SUCCEEDED(hr))hr=d->CreateVertexShader(static_cast<const DWORD*>(blob->GetBufferPointer()),&vs_);
    Release(blob);
    if(SUCCEEDED(hr))hr=D3DCompile(source,sizeof(source)-1,nullptr,nullptr,nullptr,"ps","ps_2_0",0,0,&blob,nullptr);
    if(SUCCEEDED(hr))hr=d->CreatePixelShader(static_cast<const DWORD*>(blob->GetBufferPointer()),&ps_);
    Release(blob);
    const D3DVERTEXELEMENT9 elements[]={{0,0,D3DDECLTYPE_FLOAT2,0,D3DDECLUSAGE_POSITION,0},{0,8,D3DDECLTYPE_FLOAT2,0,D3DDECLUSAGE_TEXCOORD,0},D3DDECL_END()};
    if(SUCCEEDED(hr))hr=d->CreateVertexDeclaration(elements,&declaration_);
    if(SUCCEEDED(hr))hr=d->CreateTexture(width,height,1,D3DUSAGE_RENDERTARGET,D3DFMT_A8R8G8B8,D3DPOOL_DEFAULT,&texture_,nullptr);
    if(SUCCEEDED(hr))hr=texture_->GetSurfaceLevel(0,&surface_);
    if(SUCCEEDED(hr))hr=d->CreateStateBlock(D3DSBT_ALL,&state_);
    if(FAILED(hr)){Reset();return false;}
    width_=width;height_=height;return true;
}
bool MoviePanel9::Begin(IDirect3DDevice9* d){
    IDirect3DSurface9* target=nullptr;
    if(FAILED(d->GetRenderTarget(0,&target)))return false;
    D3DSURFACE_DESC desc{};const HRESULT described=target->GetDesc(&desc);target->Release();
    if(FAILED(described)||!Ensure(d,desc.Width,desc.Height))return false;
    BeginFrame();failed_=false;
    if(FAILED(d->GetViewport(&oldViewport_))||FAILED(d->GetScissorRect(&oldScissor_))||FAILED(d->GetRenderTarget(0,&oldTarget_)))return false;
    d->GetDepthStencilSurface(&oldDepth_);
    DWORD scissor=FALSE;d->GetRenderState(D3DRS_SCISSORTESTENABLE,&scissor);
    HRESULT hr=d->SetDepthStencilSurface(nullptr);
    if(SUCCEEDED(hr))hr=d->SetRenderTarget(0,surface_);
    if(SUCCEEDED(hr))hr=d->SetRenderState(D3DRS_SCISSORTESTENABLE,FALSE);
    if(SUCCEEDED(hr))hr=d->Clear(0,nullptr,D3DCLEAR_TARGET,0xff000000,1,0);
    d->SetRenderState(D3DRS_SCISSORTESTENABLE,scissor);
    d->SetViewport(&oldViewport_);d->SetScissorRect(&oldScissor_);
    if(FAILED(hr)){failed_=true;End(d);return false;}
    return true;
}
void MoviePanel9::End(IDirect3DDevice9* d){
    d->SetDepthStencilSurface(nullptr);
    const HRESULT target=d->SetRenderTarget(0,oldTarget_);
    const HRESULT depth=d->SetDepthStencilSurface(oldDepth_);
    const HRESULT viewport=d->SetViewport(&oldViewport_);
    const HRESULT scissor=d->SetScissorRect(&oldScissor_);
    Release(oldTarget_);Release(oldDepth_);
    ready_=draws_!=0 && !failed_ && SUCCEEDED(target)&&SUCCEEDED(depth)&&SUCCEEDED(viewport)&&SUCCEEDED(scissor);
}
bool MoviePanel9::CaptureFrame(IDirect3DDevice9* d,IDirect3DSurface9* source){
    BeginFrame();
    D3DSURFACE_DESC desc{};
    if(!source || FAILED(source->GetDesc(&desc)) || !Ensure(d,desc.Width,desc.Height))return false;
    ready_=SUCCEEDED(d->StretchRect(source,nullptr,surface_,nullptr,D3DTEXF_NONE));
    return ready_;
}
HRESULT MoviePanel9::PresentFrame(IDirect3DDevice9* d,IDirect3DSurface9* target,const Mat4& clip){
    if(!ready_)return E_FAIL;
    IDirect3DSurface9 *oldTarget=nullptr,*oldDepth=nullptr;
    D3DVIEWPORT9 viewport{};RECT scissor{};D3DSURFACE_DESC desc{};
    if(!target || FAILED(target->GetDesc(&desc)) || FAILED(d->GetViewport(&viewport)) ||
       FAILED(d->GetScissorRect(&scissor)) || FAILED(d->GetRenderTarget(0,&oldTarget)))return E_FAIL;
    d->GetDepthStencilSurface(&oldDepth);
    HRESULT hr=d->SetDepthStencilSurface(nullptr);
    if(SUCCEEDED(hr))hr=d->SetRenderTarget(0,target);
    const D3DVIEWPORT9 full{0,0,desc.Width,desc.Height,0,1};
    if(SUCCEEDED(hr))hr=d->SetViewport(&full);
    // The engine owns BeginScene/EndScene outside cOtManager::draw. Nesting
    // another BeginScene here produces D3DERR_INVALIDCALL in the live game.
    if(SUCCEEDED(hr))hr=Draw(d,clip,true);
    d->SetDepthStencilSurface(nullptr);
    const HRESULT rt=d->SetRenderTarget(0,oldTarget),depth=d->SetDepthStencilSurface(oldDepth);
    const HRESULT vp=d->SetViewport(&viewport),sc=d->SetScissorRect(&scissor);
    Release(oldTarget);Release(oldDepth);
    if(FAILED(hr))return hr;
    if(FAILED(rt))return rt;if(FAILED(depth))return depth;if(FAILED(vp))return vp;return sc;
}
HRESULT MoviePanel9::Draw(IDirect3DDevice9* d,const Mat4& clip,bool clearBackground){
    if(!ready_||!state_)return S_FALSE;
    HRESULT hr=state_->Capture();if(FAILED(hr))return hr;
    d->SetVertexDeclaration(declaration_);d->SetVertexShader(vs_);d->SetPixelShader(ps_);
    d->SetVertexShaderConstantF(0,&clip.m[0][0],4);d->SetTexture(0,texture_);
    d->SetSamplerState(0,D3DSAMP_MINFILTER,D3DTEXF_LINEAR);d->SetSamplerState(0,D3DSAMP_MAGFILTER,D3DTEXF_LINEAR);
    d->SetSamplerState(0,D3DSAMP_MIPFILTER,D3DTEXF_NONE);d->SetSamplerState(0,D3DSAMP_ADDRESSU,D3DTADDRESS_CLAMP);
    d->SetSamplerState(0,D3DSAMP_ADDRESSV,D3DTADDRESS_CLAMP);d->SetSamplerState(0,D3DSAMP_SRGBTEXTURE,FALSE);
    d->SetRenderState(D3DRS_ZENABLE,FALSE);d->SetRenderState(D3DRS_ZWRITEENABLE,FALSE);
    d->SetRenderState(D3DRS_STENCILENABLE,FALSE);d->SetRenderState(D3DRS_SCISSORTESTENABLE,FALSE);
    d->SetRenderState(D3DRS_ALPHATESTENABLE,FALSE);d->SetRenderState(D3DRS_ALPHABLENDENABLE,FALSE);
    d->SetRenderState(D3DRS_COLORWRITEENABLE,15);d->SetRenderState(D3DRS_CULLMODE,D3DCULL_NONE);
    d->SetRenderState(D3DRS_FILLMODE,D3DFILL_SOLID);d->SetRenderState(D3DRS_SRGBWRITEENABLE,FALSE);
    d->SetRenderState(D3DRS_CLIPPLANEENABLE,0);d->SetStreamSourceFreq(0,1);
    // Do not expose the original cutscene's camera-facing cards around or
    // behind the viewer. Clear the whole eye, with scissor already disabled.
    if(clearBackground)hr=d->Clear(0,nullptr,D3DCLEAR_TARGET,0xff000000,1,0);
    const float vertices[][4]={{-1,1,0,0},{1,1,1,0},{-1,-1,0,1},{1,-1,1,1}};
    if(SUCCEEDED(hr))hr=d->DrawPrimitiveUP(D3DPT_TRIANGLESTRIP,2,vertices,sizeof(vertices[0]));
    const HRESULT restored=state_->Apply();return FAILED(hr)?hr:restored;
}
}
