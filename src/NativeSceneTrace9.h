#pragma once
#include "DepthOfField.h"
#include "Matrix.h"
#include "ShaderAnalysis.h"
#include <d3dcompiler.h>
#include <cstdio>
#include <string>
#include <unordered_map>
#include <vector>

namespace bvr {
// Explicit F12 only. Records one eye/frame, at most 64 texture previews and
// 2048 scene checkpoints. GPU checkpoints run at every scene draw boundary;
// texture previews and all readbacks run after the pair. No full-resolution
// dumps or per-draw CPU readbacks. Shader identities are capped at 256.
class NativeSceneTrace9 {
    template<class T> static void Release(T*& p){if(p)p->Release();p=nullptr;}
    struct Preview { IDirect3DSurface9* surface; unsigned draw; uintptr_t texture; bool scene=false; };
    struct Pending { IDirect3DTexture9* texture; unsigned draw; };
public:
    using Disassemble=std::string(*)(const DWORD*);
    ~NativeSceneTrace9(){Finish(nullptr);}
    bool Busy()const{return busy_;}
    bool Active()const{return file_ && !busy_;}
    bool Begin(IDirect3DDevice9* d,const wchar_t* directory,Disassemble disassemble){
        Finish(nullptr);directory_=directory;disassemble_=disassemble;draws_=0;sceneSamples_=0;
        if(!CreateDirectoryW(directory,nullptr))return false;
        if(_wfopen_s(&file_,(directory_+L"\\draws.txt").c_str(),L"w"))return false;
        setvbuf(file_,nullptr,_IOFBF,65536);
        fprintf(file_,"Native left-eye draw state before HMD projection; scene checkpoints include previous tracked draws. Limits: 2048 draws, 64 texture previews plus 2048 scene checkpoints at 256x144, 256 shader identities.\n");
        fprintf(file_,"Texture previews sample at stereo-pair end; render-target contents can differ from draw-time contents.\n");
        busy_=true;
        const char shader[]="sampler2D s:register(s0);float4 mode:register(c0);float4 ps(float2 uv:TEXCOORD0):COLOR{float4 t=tex2D(s,uv);return float4(t.rgb*lerp(1,t.a,mode.x),1);}";
        ID3DBlob* blob=nullptr;
        HRESULT hr=D3DCompile(shader,sizeof(shader)-1,nullptr,nullptr,nullptr,"ps","ps_2_0",0,0,&blob,nullptr);
        if(SUCCEEDED(hr))hr=d->CreatePixelShader(static_cast<const DWORD*>(blob->GetBufferPointer()),&previewPS_);
        Release(blob);
        if(SUCCEEDED(hr))hr=d->CreateStateBlock(D3DSBT_ALL,&state_);
        busy_=false;
        fprintf(file_,"preview setup=%08lX\n",hr);
        return true; // A failed preview setup still permits the metadata trace.
    }
    void Record(IDirect3DDevice9* d,UINT primitives,int matrixBase,const Mat4& original,bool orthographic){
        if(!Active() || draws_>=2048)return;
        const unsigned draw=++draws_;
        IDirect3DVertexShader9* vs=nullptr;IDirect3DPixelShader9* ps=nullptr;
        d->GetVertexShader(&vs);d->GetPixelShader(&ps);
        const uint32_t vi=Shader(vs,L"vs"),pi=Shader(ps,L"ps");Release(vs);Release(ps);
        IDirect3DSurface9* target=nullptr;D3DSURFACE_DESC rt{};
        d->GetRenderTarget(0,&target);if(target)target->GetDesc(&rt);
        D3DVIEWPORT9 vp{};d->GetViewport(&vp);
        DWORD z=0,zw=0,blend=0,src=0,dst=0,cull=0;
        d->GetRenderState(D3DRS_ZENABLE,&z);d->GetRenderState(D3DRS_ZWRITEENABLE,&zw);
        d->GetRenderState(D3DRS_ALPHABLENDENABLE,&blend);d->GetRenderState(D3DRS_SRCBLEND,&src);
        d->GetRenderState(D3DRS_DESTBLEND,&dst);d->GetRenderState(D3DRS_CULLMODE,&cull);
        fprintf(file_,"draw=%u primitives=%u vs=%08X ps=%08X matrix=c%d ortho=%d rt=%p %ux%u fmt=%u viewport=%u,%u,%u,%u depth=%u/%u blend=%u/%u/%u cull=%u\n",
            draw,primitives,vi,pi,matrixBase,orthographic,target,rt.Width,rt.Height,rt.Format,
            vp.X,vp.Y,vp.Width,vp.Height,z,zw,blend,src,dst,cull);
        // Do not let early scenery exhaust the checkpoint budget before the
        // late effect passes. One bounded GPU snapshot per draw; all CPU
        // readbacks still run after the stereo pair, only on explicit F12.
        const bool effect=!orthographic&&matrixBase==24;
        if(sceneSamples_<2048&&target&&vp.Width>=640&&vp.Width>vp.Height) {
            IDirect3DTexture9* scene=nullptr;
            if(SUCCEEDED(target->GetContainer(__uuidof(IDirect3DTexture9),reinterpret_cast<void**>(&scene)))) {
                ++sceneSamples_;PreviewTexture(d,scene,draw,true);Release(scene);
            } else if(rt.Format==D3DFMT_A8R8G8B8||rt.Format==D3DFMT_X8R8G8B8) {
                ++sceneSamples_;PreviewSurface(d,target,draw);
            }
        }
        Release(target);
        float effectConstants[12]{};
        if(effect&&SUCCEEDED(d->GetPixelShaderConstantF(184,effectConstants,3))) {
            for(int i=0;i<3;++i)fprintf(file_,"pc%d %.9g %.9g %.9g %.9g\n",184+i,
                effectConstants[i*4],effectConstants[i*4+1],effectConstants[i*4+2],effectConstants[i*4+3]);
        }
        for(int row=0;row<4;++row)fprintf(file_,"m%d %.9g %.9g %.9g %.9g\n",row,
            original.m[row][0],original.m[row][1],original.m[row][2],original.m[row][3]);
        float view[16*4]{},material[24*4]{};
        if(SUCCEEDED(d->GetVertexShaderConstantF(0,view,16)))
            for(int r=0;r<16;++r)fprintf(file_,"vc%d %.9g %.9g %.9g %.9g\n",r,view[r*4],view[r*4+1],view[r*4+2],view[r*4+3]);
        if(SUCCEEDED(d->GetPixelShaderConstantF(40,material,24)))
            for(int r=0;r<24;++r)fprintf(file_,"pc%d %.9g %.9g %.9g %.9g\n",40+r,material[r*4],material[r*4+1],material[r*4+2],material[r*4+3]);
        for(DWORD stage=0;stage<16;++stage){
            IDirect3DBaseTexture9* texture=nullptr;d->GetTexture(stage,&texture);
            if(texture && texture->GetType()==D3DRTYPE_TEXTURE){
                D3DSURFACE_DESC desc{};auto* tex=static_cast<IDirect3DTexture9*>(texture);tex->GetLevelDesc(0,&desc);
                fprintf(file_,"texture%u=%p %ux%u fmt=%u usage=%08lX\n",stage,texture,desc.Width,desc.Height,desc.Format,desc.Usage);
                // The reported artifact is a flat scene card. Keep previews
                // Include orthographic cards too: not every cinematic layer
                // uses the scene's perspective projection.
                if(stage==0 && primitives<=4 && vp.Width>=640 && vp.Width>vp.Height && desc.Width>=128 && desc.Height>=64 &&
                   !(desc.Usage&D3DUSAGE_DEPTHSTENCIL) && pending_.size()<64 && !previewed_.count(texture)) {
                    previewed_[texture]=true;tex->AddRef();pending_.push_back({tex,draw});
                    void* stack[10]{};const USHORT count=CaptureStackBackTrace(0,10,stack,nullptr);
                    fprintf(file_,"stack");for(USHORT i=0;i<count;++i)fprintf(file_," %p",stack[i]);fprintf(file_,"\n");
                }
            } else if(texture&&texture->GetType()==D3DRTYPE_CUBETEXTURE) {
                D3DSURFACE_DESC desc{};
                if(SUCCEEDED(static_cast<IDirect3DCubeTexture9*>(texture)->GetLevelDesc(0,&desc)))
                    fprintf(file_,"cube%u=%p %ux%u fmt=%u usage=%08lX\n",stage,texture,desc.Width,desc.Height,desc.Format,desc.Usage);
            }
            Release(texture);
        }
    }
    unsigned Finish(IDirect3DDevice9* d){
        unsigned saved=0;busy_=true;
        if(file_ && d){
            for(const auto& pending:pending_)PreviewTexture(d,pending.texture,pending.draw);
            busy_=true;
            IDirect3DSurface9* read=nullptr;
            if(SUCCEEDED(d->CreateOffscreenPlainSurface(256,144,D3DFMT_A8R8G8B8,D3DPOOL_SYSTEMMEM,&read,nullptr))){
                for(const auto& preview:previews_){
                    HRESULT hr=d->GetRenderTargetData(preview.surface,read);
                    if(SUCCEEDED(hr))hr=SavePreview(read,preview.draw,preview.scene)?S_OK:E_FAIL;
                    fprintf(file_,"preview draw=%u texture=%p scene=%d status=%08lX\n",preview.draw,reinterpret_cast<void*>(preview.texture),preview.scene,hr);
                    if(SUCCEEDED(hr))++saved;
                }
            }
            Release(read);
        }
        for(auto& preview:previews_)Release(preview.surface);
        for(auto& pending:pending_)Release(pending.texture);pending_.clear();
        previews_.clear();previewed_.clear();shaders_.clear();
        Release(previewPS_);Release(state_);
        if(file_){fprintf(file_,"end draws=%u savedPreviews=%u\n",draws_,saved);fclose(file_);file_=nullptr;}
        busy_=false;return saved;
    }
private:
    void PreviewSurface(IDirect3DDevice9* d,IDirect3DSurface9* source,unsigned draw){
        // The main scene is a standalone render target, so GetContainer
        // cannot supply a texture. Copy/downsample it directly on the GPU.
        // No render bindings or scene boundaries are changed; read back only
        // after both eyes, just like the texture-backed checkpoints.
        busy_=true;IDirect3DSurface9* preview=nullptr;
        HRESULT hr=d->CreateRenderTarget(256,144,D3DFMT_A8R8G8B8,D3DMULTISAMPLE_NONE,0,FALSE,&preview,nullptr);
        if(SUCCEEDED(hr))hr=d->StretchRect(source,nullptr,preview,nullptr,D3DTEXF_NONE);
        if(SUCCEEDED(hr)){previews_.push_back({preview,draw,reinterpret_cast<uintptr_t>(source),true});preview=nullptr;}
        else fprintf(file_,"surface preview draw=%u status=%08lX\n",draw,hr);
        Release(preview);busy_=false;
    }
    template<class T> uint32_t Shader(T* shader,const wchar_t* type){
        if(!shader)return 0;
        auto found=shaders_.find(shader);if(found!=shaders_.end())return found->second;
        if(shaders_.size()>=256)return 0;
        UINT size=0;if(FAILED(shader->GetFunction(nullptr,&size)) || size<8 || size>65536)return 0;
        std::vector<DWORD> code((size+3)/4);if(FAILED(shader->GetFunction(code.data(),&size)))return 0;
        const auto crc=ShaderCrc32(code.data(),size);shaders_[shader]=crc;
        if(disassemble_){
            const auto assembly=disassemble_(code.data());wchar_t name[64]{};swprintf_s(name,L"\\%s_%08X.asm",type,crc);
            FILE* out=nullptr;if(!_wfopen_s(&out,(directory_+name).c_str(),L"wb")){fwrite(assembly.data(),1,assembly.size(),out);fclose(out);}
        }
        return crc;
    }
    void PreviewTexture(IDirect3DDevice9* d,IDirect3DTexture9* texture,unsigned draw,bool scene=false){
        if(!state_ || !previewPS_)return;
        busy_=true;
        IDirect3DSurface9 *preview=nullptr,*target=nullptr,*depth=nullptr;
        IDirect3DSurface9* extraTargets[3]{};
        D3DCAPS9 caps{};
        D3DVIEWPORT9 vp{};RECT scissor{};
        HRESULT hr=d->CreateRenderTarget(256,144,D3DFMT_A8R8G8B8,D3DMULTISAMPLE_NONE,0,FALSE,&preview,nullptr);
        if(SUCCEEDED(hr))hr=d->GetRenderTarget(0,&target);
        if(SUCCEEDED(hr))hr=d->GetViewport(&vp);
        if(SUCCEEDED(hr))hr=d->GetScissorRect(&scissor);
        if(SUCCEEDED(hr))hr=d->GetDeviceCaps(&caps);
        const DWORD targetCount=std::min<DWORD>(caps.NumSimultaneousRTs,4);
        for(DWORD slot=1;SUCCEEDED(hr) && slot<targetCount;++slot){
            const HRESULT get=d->GetRenderTarget(slot,&extraTargets[slot-1]);
            if(FAILED(get) && get!=D3DERR_NOTFOUND)hr=get;
        }
        if(SUCCEEDED(hr))hr=state_->Capture();
        if(SUCCEEDED(hr)){
            d->GetDepthStencilSurface(&depth);
            hr=d->SetDepthStencilSurface(nullptr);
            // MRT bindings are not in D3DSBT_ALL. Leaving the engine's normal
            // buffer attached lets this diagnostic overwrite scene data.
            for(DWORD slot=1;SUCCEEDED(hr) && slot<targetCount;++slot)hr=d->SetRenderTarget(slot,nullptr);
            if(SUCCEEDED(hr))hr=d->SetRenderTarget(0,preview);
            const D3DVIEWPORT9 previewViewport{0,0,256,144,0,1};
            if(SUCCEEDED(hr))hr=d->SetViewport(&previewViewport);
            d->SetVertexShader(nullptr);d->SetFVF(D3DFVF_XYZRHW|D3DFVF_TEX1);d->SetPixelShader(previewPS_);
            // Reveal alpha-only effect textures, but preserve scene RGB even
            // when the engine stores unrelated data in the target's alpha.
            const float previewMode[4]{scene?0.f:1.f,0,0,0};
            d->SetPixelShaderConstantF(0,previewMode,1);
            d->SetTexture(0,texture);d->SetStreamSourceFreq(0,1);
            d->SetSamplerState(0,D3DSAMP_MINFILTER,D3DTEXF_LINEAR);d->SetSamplerState(0,D3DSAMP_MAGFILTER,D3DTEXF_LINEAR);
            d->SetSamplerState(0,D3DSAMP_MIPFILTER,D3DTEXF_NONE);d->SetSamplerState(0,D3DSAMP_ADDRESSU,D3DTADDRESS_CLAMP);
            d->SetSamplerState(0,D3DSAMP_ADDRESSV,D3DTADDRESS_CLAMP);d->SetSamplerState(0,D3DSAMP_SRGBTEXTURE,FALSE);
            d->SetRenderState(D3DRS_ZENABLE,FALSE);d->SetRenderState(D3DRS_ZWRITEENABLE,FALSE);
            d->SetRenderState(D3DRS_STENCILENABLE,FALSE);d->SetRenderState(D3DRS_SCISSORTESTENABLE,FALSE);
            d->SetRenderState(D3DRS_ALPHATESTENABLE,FALSE);d->SetRenderState(D3DRS_ALPHABLENDENABLE,FALSE);
            d->SetRenderState(D3DRS_COLORWRITEENABLE,15);d->SetRenderState(D3DRS_CULLMODE,D3DCULL_NONE);
            d->SetRenderState(D3DRS_FILLMODE,D3DFILL_SOLID);d->SetRenderState(D3DRS_SRGBWRITEENABLE,FALSE);
            d->SetRenderState(D3DRS_CLIPPLANEENABLE,0);
            const float quad[][6]={{-.5f,-.5f,0,1,0,0},{255.5f,-.5f,0,1,1,0},{-.5f,143.5f,0,1,0,1},{255.5f,143.5f,0,1,1,1}};
            if(SUCCEEDED(hr))hr=d->DrawPrimitiveUP(D3DPT_TRIANGLESTRIP,2,quad,sizeof(quad[0]));
            d->SetDepthStencilSurface(nullptr);d->SetRenderTarget(0,target);d->SetDepthStencilSurface(depth);
            for(DWORD slot=1;slot<targetCount;++slot)d->SetRenderTarget(slot,extraTargets[slot-1]);
            state_->Apply();d->SetViewport(&vp);d->SetScissorRect(&scissor);
        }
        if(SUCCEEDED(hr)){previews_.push_back({preview,draw,reinterpret_cast<uintptr_t>(texture),scene});preview=nullptr;}
        else fprintf(file_,"preview setup draw=%u status=%08lX\n",draw,hr);
        for(auto& extra:extraTargets)Release(extra);
        Release(preview);Release(target);Release(depth);busy_=false;
    }
    bool SavePreview(IDirect3DSurface9* surface,unsigned draw,bool scene){
        D3DLOCKED_RECT locked{};if(FAILED(surface->LockRect(&locked,nullptr,D3DLOCK_READONLY)))return false;
        wchar_t name[64]{};swprintf_s(name,L"\\draw%04u_%s.bmp",draw,scene?L"scene":L"texture0");FILE* out=nullptr;
        bool ok=!_wfopen_s(&out,(directory_+name).c_str(),L"wb");
        if(ok){
            BITMAPFILEHEADER file{};BITMAPINFOHEADER info{};info.biSize=sizeof(info);info.biWidth=256;info.biHeight=-144;
            info.biPlanes=1;info.biBitCount=32;info.biSizeImage=256*144*4;
            file.bfType=0x4d42;file.bfOffBits=sizeof(file)+sizeof(info);file.bfSize=file.bfOffBits+info.biSizeImage;
            ok=fwrite(&file,1,sizeof(file),out)==sizeof(file)&&fwrite(&info,1,sizeof(info),out)==sizeof(info);
            for(int y=0;ok && y<144;++y)ok=fwrite(static_cast<const char*>(locked.pBits)+y*locked.Pitch,1,256*4,out)==256*4;
            fclose(out);
        }
        surface->UnlockRect();return ok;
    }
    FILE* file_=nullptr;std::wstring directory_;Disassemble disassemble_=nullptr;
    bool busy_=false;unsigned draws_=0,sceneSamples_=0;
    IDirect3DPixelShader9* previewPS_=nullptr;IDirect3DStateBlock9* state_=nullptr;
    std::vector<Preview> previews_;
    std::vector<Pending> pending_;
    std::unordered_map<IUnknown*,uint32_t> shaders_;
    std::unordered_map<IUnknown*,bool> previewed_;
};
}
