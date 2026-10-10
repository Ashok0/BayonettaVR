#include "MenuPanel9.h"
#include <d3dcompiler.h>

namespace bvr {
namespace {
template<class T> void Release(T*& p) { if(p) p->Release(); p=nullptr; }
}
void MenuPanel9::Reset() {
    for(auto*& p:surfaces_) Release(p);
    for(auto*& p:textures_) Release(p);
    Release(depth_); Release(oldTarget_); Release(oldDepth_);
    Release(vs_); Release(ps_); Release(declaration_); Release(state_);
    width_=height_=0; BeginFrame();
}
void MenuPanel9::BeginFrame() { ready_[0]=ready_[1]=false; }
bool MenuPanel9::Ensure(IDirect3DDevice9* device, UINT width, UINT height) {
    if(width_==width && height_==height && state_) return true;
    Reset();
    static const char source[] =
        "float4 r0:register(c0); float4 r1:register(c1);"
        "float4 r2:register(c2); float4 r3:register(c3);"
        "sampler2D tex:register(s0);"
        "struct V {float4 p:POSITION; float2 uv:TEXCOORD0;};"
        "V vs(float2 p:POSITION,float2 uv:TEXCOORD0){V o; float4 v=float4(p,0,1);"
        "o.p=float4(dot(r0,v),dot(r1,v),dot(r2,v),dot(r3,v));o.uv=uv;return o;}"
        "float4 ps(float2 uv:TEXCOORD0):COLOR{return tex2D(tex,uv);}";
    ID3DBlob* vertex=nullptr; ID3DBlob* pixel=nullptr;
    HRESULT hr=D3DCompile(source,sizeof(source)-1,nullptr,nullptr,nullptr,"vs","vs_2_0",0,0,&vertex,nullptr);
    if(SUCCEEDED(hr)) hr=D3DCompile(source,sizeof(source)-1,nullptr,nullptr,nullptr,"ps","ps_2_0",0,0,&pixel,nullptr);
    if(SUCCEEDED(hr)) hr=device->CreateVertexShader(static_cast<const DWORD*>(vertex->GetBufferPointer()),&vs_);
    if(SUCCEEDED(hr)) hr=device->CreatePixelShader(static_cast<const DWORD*>(pixel->GetBufferPointer()),&ps_);
    Release(vertex); Release(pixel);
    const D3DVERTEXELEMENT9 elements[]={{0,0,D3DDECLTYPE_FLOAT2,D3DDECLMETHOD_DEFAULT,D3DDECLUSAGE_POSITION,0},
        {0,8,D3DDECLTYPE_FLOAT2,D3DDECLMETHOD_DEFAULT,D3DDECLUSAGE_TEXCOORD,0},D3DDECL_END()};
    if(SUCCEEDED(hr)) hr=device->CreateVertexDeclaration(elements,&declaration_);
    for(int i=0;i<2 && SUCCEEDED(hr);++i) {
        hr=device->CreateTexture(width,height,1,D3DUSAGE_RENDERTARGET,D3DFMT_A8R8G8B8,D3DPOOL_DEFAULT,&textures_[i],nullptr);
        if(SUCCEEDED(hr)) hr=textures_[i]->GetSurfaceLevel(0,&surfaces_[i]);
    }
    if(SUCCEEDED(hr)) hr=device->CreateDepthStencilSurface(width,height,D3DFMT_D24S8,D3DMULTISAMPLE_NONE,0,TRUE,&depth_,nullptr);
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
    if(FAILED(device->GetViewport(&oldViewport_)) || FAILED(device->GetRenderTarget(0,&oldTarget_))) return false;
    device->GetDepthStencilSurface(&oldDepth_); // A null depth surface is valid.
    device->SetDepthStencilSurface(nullptr);
    hr=device->SetRenderTarget(0,surfaces_[layer]);
    if(SUCCEEDED(hr)) hr=device->SetDepthStencilSurface(depth_);
    if(SUCCEEDED(hr)) hr=device->SetViewport(&oldViewport_);
    if(SUCCEEDED(hr)) hr=device->Clear(0,nullptr,D3DCLEAR_TARGET|D3DCLEAR_ZBUFFER|D3DCLEAR_STENCIL,0,1,0);
    if(FAILED(hr)) { End(device,layer); ready_[layer]=false; return false; }
    return true;
}
void MenuPanel9::End(IDirect3DDevice9* device,int layer) {
    device->SetDepthStencilSurface(nullptr);
    device->SetRenderTarget(0,oldTarget_);
    device->SetDepthStencilSurface(oldDepth_);
    device->SetViewport(&oldViewport_);
    Release(oldTarget_); Release(oldDepth_);
    ready_[layer]=true;
}
HRESULT MenuPanel9::Draw(IDirect3DDevice9* device,int layer,const Mat4& clip) {
    if(!ready_[layer] || !state_) return E_FAIL;
    HRESULT hr=state_->Capture();
    if(FAILED(hr)) return hr;
    device->SetVertexDeclaration(declaration_);
    device->SetVertexShader(vs_); device->SetPixelShader(ps_);
    device->SetVertexShaderConstantF(0,&clip.m[0][0],4);
    device->SetTexture(0,textures_[layer]);
    device->SetSamplerState(0,D3DSAMP_MINFILTER,D3DTEXF_LINEAR);
    device->SetSamplerState(0,D3DSAMP_MAGFILTER,D3DTEXF_LINEAR);
    device->SetSamplerState(0,D3DSAMP_MIPFILTER,D3DTEXF_NONE);
    device->SetSamplerState(0,D3DSAMP_ADDRESSU,D3DTADDRESS_CLAMP);
    device->SetSamplerState(0,D3DSAMP_ADDRESSV,D3DTADDRESS_CLAMP);
    device->SetSamplerState(0,D3DSAMP_SRGBTEXTURE,FALSE);
    device->SetRenderState(D3DRS_ZENABLE,FALSE);
    device->SetRenderState(D3DRS_ZWRITEENABLE,FALSE);
    device->SetRenderState(D3DRS_STENCILENABLE,FALSE);
    device->SetRenderState(D3DRS_SCISSORTESTENABLE,FALSE);
    device->SetRenderState(D3DRS_ALPHATESTENABLE,FALSE);
    device->SetRenderState(D3DRS_ALPHABLENDENABLE,TRUE);
    device->SetRenderState(D3DRS_SRCBLEND,D3DBLEND_ONE);
    device->SetRenderState(D3DRS_DESTBLEND,D3DBLEND_INVSRCALPHA);
    device->SetRenderState(D3DRS_BLENDOP,D3DBLENDOP_ADD);
    device->SetRenderState(D3DRS_SEPARATEALPHABLENDENABLE,FALSE);
    device->SetRenderState(D3DRS_COLORWRITEENABLE,15);
    device->SetRenderState(D3DRS_CULLMODE,D3DCULL_NONE);
    device->SetRenderState(D3DRS_FILLMODE,D3DFILL_SOLID);
    device->SetRenderState(D3DRS_SRGBWRITEENABLE,FALSE);
    device->SetRenderState(D3DRS_CLIPPLANEENABLE,0);
    device->SetStreamSourceFreq(0,1);
    const float vertices[4][4]={{-1,1,0,0},{1,1,1,0},{-1,-1,0,1},{1,-1,1,1}};
    hr=device->DrawPrimitiveUP(D3DPT_TRIANGLESTRIP,2,vertices,sizeof(vertices[0]));
    const HRESULT restore=state_->Apply();
    return FAILED(hr) ? hr : restore;
}
}
