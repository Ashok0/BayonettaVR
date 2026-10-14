#define NOMINMAX
#include "FirstPersonDraw9.h"
#include "Matrix.h"
#include <wrl/client.h>
#include <vector>
#include <cstdio>
#include <cstdlib>
#include <cstring>
using namespace bvr;
using Microsoft::WRL::ComPtr;
static void Check(bool ok){if(!ok){puts("Costume GPU check failed");fflush(stdout);ExitProcess(1);}}
static void HR(HRESULT hr){if(FAILED(hr)){printf("Costume GPU HRESULT %08lX\n",hr);fflush(stdout);ExitProcess(2);}}
struct Vertex{float x,y,z;DWORD color;};
int main(){
    HWND window=CreateWindowExW(0,L"STATIC",L"Costume regression",WS_OVERLAPPEDWINDOW,0,0,128,128,nullptr,nullptr,GetModuleHandleW(nullptr),nullptr);
    ComPtr<IDirect3D9Ex> api;HR(Direct3DCreate9Ex(D3D_SDK_VERSION,&api));
    D3DPRESENT_PARAMETERS pp{};pp.Windowed=TRUE;pp.SwapEffect=D3DSWAPEFFECT_DISCARD;pp.hDeviceWindow=window;
    pp.BackBufferWidth=pp.BackBufferHeight=128;pp.BackBufferFormat=D3DFMT_X8R8G8B8;
    ComPtr<IDirect3DDevice9Ex> d;HR(api->CreateDeviceEx(0,D3DDEVTYPE_HAL,window,D3DCREATE_HARDWARE_VERTEXPROCESSING,&pp,nullptr,&d));
    ComPtr<IDirect3DSurface9> target,read;HR(d->CreateRenderTarget(128,128,D3DFMT_A8R8G8B8,D3DMULTISAMPLE_NONE,0,FALSE,&target,nullptr));
    HR(d->CreateOffscreenPlainSurface(128,128,D3DFMT_A8R8G8B8,D3DPOOL_SYSTEMMEM,&read,nullptr));HR(d->SetRenderTarget(0,target.Get()));
    HR(d->SetFVF(D3DFVF_XYZ|D3DFVF_DIFFUSE));HR(d->SetRenderState(D3DRS_LIGHTING,FALSE));HR(d->SetRenderState(D3DRS_ZENABLE,FALSE));
    HR(d->SetTextureStageState(0,D3DTSS_COLOROP,D3DTOP_SELECTARG1));HR(d->SetTextureStageState(0,D3DTSS_COLORARG1,D3DTA_DIFFUSE));
    const Mat4 identity=Identity();HR(d->SetTransform(D3DTS_VIEW,reinterpret_cast<const D3DMATRIX*>(&identity)));
    FILE* f=nullptr;fopen_s(&f,"tests/fixtures/firstperson_costumes/geometry.bin","rb");Check(f!=nullptr);
    char magic[4];Check(fread(magic,1,4,f)==4&&!memcmp(magic,"FPC7",4));unsigned count=0;Check(fread(&count,4,1,f)==1);
    unsigned cases=0;uint64_t compared=0,visible=0;bool odd=false;
    auto pixels=[&]{HR(d->GetRenderTargetData(target.Get(),read.Get()));D3DLOCKED_RECT lock{};HR(read->LockRect(&lock,nullptr,D3DLOCK_READONLY));
        std::vector<DWORD> p(128*128);for(unsigned y=0;y<128;++y)memcpy(p.data()+y*128,static_cast<char*>(lock.pBits)+y*lock.Pitch,512);
        HR(read->UnlockRect());return p;};
    for(unsigned fixture=0;fixture<count;++fixture){
        unsigned h[5];Check(fread(h,4,5,f)==5);const auto& costume=kFirstPersonCostumes[h[0]];
        const auto& rule=kFirstPersonBodyRules[costume.ruleStart+h[1]];
        std::vector<Vertex> v(h[2]);for(unsigned i=0;i<h[2];++i){Check(fread(&v[i],4,3,f)==3);v[i].color=0xff000000u|((i*437u+0x6F7B99u)&0xffffff);}
        std::vector<unsigned short> indices(h[3]),reference(h[4]);Check(fread(indices.data(),2,indices.size(),f)==indices.size());
        Check(fread(reference.data(),2,reference.size(),f)==reference.size());
        ComPtr<IDirect3DVertexBuffer9> vb;ComPtr<IDirect3DIndexBuffer9> ib,ref;
        HR(d->CreateVertexBuffer(UINT(v.size()*sizeof(Vertex)),0,D3DFVF_XYZ|D3DFVF_DIFFUSE,D3DPOOL_DEFAULT,&vb,nullptr));
        void* data=nullptr;HR(vb->Lock(0,0,&data,0));memcpy(data,v.data(),v.size()*sizeof(Vertex));HR(vb->Unlock());
        HR(d->CreateIndexBuffer(UINT(indices.size()*2),0,D3DFMT_INDEX16,D3DPOOL_DEFAULT,&ib,nullptr));HR(ib->Lock(0,0,&data,0));
        memcpy(data,indices.data(),indices.size()*2);HR(ib->Unlock());
        if(!reference.empty()) {HR(d->CreateIndexBuffer(UINT(reference.size()*2),0,D3DFMT_INDEX16,D3DPOOL_DEFAULT,&ref,nullptr));
            HR(ref->Lock(0,0,&data,0));memcpy(data,reference.data(),reference.size()*2);HR(ref->Unlock());}
        HR(d->SetStreamSource(0,vb.Get(),0,sizeof(Vertex)));
        for(float yaw:{-.7f,0.f,.9f})for(float eye:{-.032f,.032f})for(DWORD cull:{DWORD(D3DCULL_CW),DWORD(D3DCULL_CCW)}) {
            Mat4 world=Identity();world.m[0][0]=world.m[2][2]=std::cos(yaw);world.m[0][2]=std::sin(yaw);world.m[2][0]=-std::sin(yaw);
            world.m[0][3]=-eye;world.m[1][3]=-1.2f;world.m[2][3]=2.4f;
            const auto row=Transpose(world),projection=Transpose(PerspectiveFovLH(.9f,1,.1f,100));
            HR(d->SetTransform(D3DTS_WORLD,reinterpret_cast<const D3DMATRIX*>(&row)));HR(d->SetTransform(D3DTS_PROJECTION,reinterpret_cast<const D3DMATRIX*>(&projection)));
            HR(d->SetRenderState(D3DRS_CULLMODE,cull));HR(d->Clear(0,nullptr,D3DCLEAR_TARGET,0,1,0));HR(d->BeginScene());
            if(ref){HR(d->SetIndices(ref.Get()));HR(d->DrawIndexedPrimitive(D3DPT_TRIANGLELIST,0,0,UINT(v.size()),0,UINT(reference.size()/3)));}
            HR(d->EndScene());const auto expected=pixels();
            HR(d->SetIndices(ib.Get()));HR(d->Clear(0,nullptr,D3DCLEAR_TARGET,0,1,0));HR(d->BeginScene());
            HR(DrawFirstPersonCostumeRanges(d.Get(),0,rule,[&](unsigned start,unsigned n){return d->DrawIndexedPrimitive(D3DPRIMITIVETYPE(rule.primitiveType),0,0,UINT(v.size()),start,n);}));
            HR(d->EndScene());const auto actual=pixels();
            unsigned mismatch=0;for(unsigned i=0;i<actual.size();++i){if(actual[i]!=expected[i])++mismatch;if(expected[i]&0xffffff)++visible;}
            if(mismatch){printf("Mismatch model=%s batch=%u pixels=%u\n",costume.model,rule.batch,mismatch);fflush(stdout);ExitProcess(3);}
            DWORD restored=0;HR(d->GetRenderState(D3DRS_CULLMODE,&restored));Check(restored==cull);
            ComPtr<IDirect3DIndexBuffer9> bound;HR(d->GetIndices(&bound));Check(bound.Get()==ib.Get());
            ++cases;compared+=actual.size();
        }
        for(unsigned i=0;i<rule.rangeCount;++i)odd|=bool(kFirstPersonRanges[rule.rangeStart+i].offset&1);
        if(rule.rangeCount){HR(d->SetRenderState(D3DRS_CULLMODE,D3DCULL_CCW));
            Check(FAILED(DrawFirstPersonCostumeRanges(d.Get(),0,rule,[](unsigned,unsigned){return E_FAIL;})));
            DWORD cull=0;HR(d->GetRenderState(D3DRS_CULLMODE,&cull));Check(cull==D3DCULL_CCW);}
    }
    fclose(f);Check(odd&&visible>1000);DestroyWindow(window);
    printf("Costume GPU: %u real-geometry eye/yaw/cull cases, %llu identical pixels; Witch waist trim, veil removal, odd strip winding and state restoration passed.\n",cases,compared);
}
