#pragma once
#include "DepthOfField.h"
#include <cstdio>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace bvr {
// A single left-eye frame, enabled only by F12. No shader or render-state
// mutations. Read surfaces at the point of use, before the game reuses them.
class SceneCapture9 {
public:
    using Disassemble = std::string(*)(const DWORD*);
    using SaveSurface = HRESULT(WINAPI*)(LPCWSTR,int,IDirect3DSurface9*,const PALETTEENTRY*,const RECT*);
    ~SceneCapture9(){Finish();}
    bool Begin(const wchar_t* directory, Disassemble disassemble) {
        Finish();
        if(!CreateDirectoryW(directory,nullptr))return false;
        directory_=directory;disassemble_=disassemble;
        HMODULE dll=GetModuleHandleW(L"d3dx9_43.dll");
        save_=dll?reinterpret_cast<SaveSurface>(GetProcAddress(dll,"D3DXSaveSurfaceToFileW")):nullptr;
        const std::wstring path=directory_+L"\\draws.txt";
        if(_wfopen_s(&file_,path.c_str(),L"w")!=0)return false;
        fprintf(file_,"Left-eye draw order. Constants are original game state before HMD adjustment.\n");
        draw_=images_=0;bytes_=0;lastVs_=lastPs_=0;
        return true;
    }
    bool Active()const{return file_!=nullptr&&!busy_;}
    unsigned Images()const{return images_;}
    void Finish() {
        if(file_){fprintf(file_,"end draws=%u images=%u estimatedRawBytes=%llu\n",draw_,images_,bytes_);fclose(file_);file_=nullptr;}
        for(auto* object:held_)object->Release();
        held_.clear();shaders_.clear();inputs_.clear();screenPasses_.clear();
    }
    void Snapshot(IDirect3DSurface9* surface,const char* label) {
        if(!Active()||!surface||!save_||images_>=80)return;
        D3DSURFACE_DESC desc{};if(FAILED(surface->GetDesc(&desc)))return;
        const auto estimate=static_cast<unsigned long long>(desc.Width)*desc.Height*16;
        if(bytes_+estimate>512ull*1024*1024)return;
        busy_=true;
        wchar_t name[160]{};swprintf_s(name,L"\\%03u_draw%04u_%hs.png",images_,draw_,label);
        const auto path=directory_+name;
        const HRESULT png=save_(path.c_str(),3,surface,nullptr,nullptr);
        HRESULT raw=S_FALSE;
        // Keep float depth/normal values: their PNG previews can look black.
        if(desc.Format==D3DFMT_R32F||desc.Format==D3DFMT_G32R32F||desc.Format==D3DFMT_A32B32G32R32F||
           desc.Format==D3DFMT_R16F||desc.Format==D3DFMT_G16R16F||desc.Format==D3DFMT_A16B16G16R16F) {
            swprintf_s(name,L"\\%03u_draw%04u_%hs.dds",images_,draw_,label);
            raw=save_((directory_+name).c_str(),4,surface,nullptr,nullptr);
        }
        fprintf(file_,"image=%u draw=%u label=%s surface=%p size=%ux%u format=%u png=%08lX dds=%08lX\n",
            images_++,draw_,label,surface,desc.Width,desc.Height,desc.Format,png,raw);
        bytes_+=estimate;busy_=false;
    }
    template<class Shader> uint32_t ShaderId(Shader* shader,const wchar_t* kind) {
        if(!shader)return 0;
        const auto found=shaders_.find(shader);if(found!=shaders_.end())return found->second;
        UINT size=0;if(FAILED(shader->GetFunction(nullptr,&size))||size<8||size>65536)return 0;
        std::vector<DWORD> code((size+3)/4);
        if(FAILED(shader->GetFunction(code.data(),&size)))return 0;
        const uint32_t crc=ShaderCrc32(code.data(),size);
        shaders_[shader]=crc;shader->AddRef();held_.push_back(shader);
        const auto assembly=disassemble_?disassemble_(code.data()):std::string{};
        wchar_t name[64]{};swprintf_s(name,L"\\%s_%08X.asm",kind,crc);
        FILE* out=nullptr;
        if(_wfopen_s(&out,(directory_+name).c_str(),L"wb")==0){fwrite(assembly.data(),1,assembly.size(),out);fclose(out);}
        return crc;
    }
    // The caller holds this target until AfterDraw, even if game state changes.
    IDirect3DSurface9* BeforeDraw(IDirect3DDevice9* device,UINT primitives) {
        if(!device||!Active()||draw_>=8192)return nullptr;
        ++draw_;
        IDirect3DVertexShader9* vs=nullptr;IDirect3DPixelShader9* ps=nullptr;
        device->GetVertexShader(&vs);device->GetPixelShader(&ps);
        const auto vi=ShaderId(vs,L"vs"),pi=ShaderId(ps,L"ps");
        if(vs)vs->Release();if(ps)ps->Release();
        IDirect3DSurface9* target=nullptr;device->GetRenderTarget(0,&target);
        D3DSURFACE_DESC desc{};if(target)target->GetDesc(&desc);
        DWORD z=0,zw=0,blend=0,src=0,dst=0;
        device->GetRenderState(D3DRS_ZENABLE,&z);device->GetRenderState(D3DRS_ZWRITEENABLE,&zw);
        device->GetRenderState(D3DRS_ALPHABLENDENABLE,&blend);device->GetRenderState(D3DRS_SRCBLEND,&src);device->GetRenderState(D3DRS_DESTBLEND,&dst);
        D3DVIEWPORT9 vp{};device->GetViewport(&vp);
        fprintf(file_,"draw=%u primitives=%u vs=%08X ps=%08X rt=%p %ux%u fmt=%u viewport=%u,%u,%u,%u depth=%u/%u blend=%u/%u/%u\n",
            draw_,primitives,vi,pi,target,desc.Width,desc.Height,desc.Format,vp.X,vp.Y,vp.Width,vp.Height,z,zw,blend,src,dst);
        if(vi!=lastVs_||pi!=lastPs_) {
            float vc[36][4]{},pc[16][4]{},post[20][4]{};
            device->GetVertexShaderConstantF(0,&vc[0][0],36);
            device->GetPixelShaderConstantF(0,&pc[0][0],16);
            device->GetPixelShaderConstantF(184,&post[0][0],20);
            for(int i=0;i<36;++i)fprintf(file_,"vc%d %.9g %.9g %.9g %.9g\n",i,vc[i][0],vc[i][1],vc[i][2],vc[i][3]);
            for(int i=0;i<16;++i)fprintf(file_,"pc%d %.9g %.9g %.9g %.9g\n",i,pc[i][0],pc[i][1],pc[i][2],pc[i][3]);
            for(int i=0;i<20;++i)fprintf(file_,"pc%d %.9g %.9g %.9g %.9g\n",184+i,post[i][0],post[i][1],post[i][2],post[i][3]);
        }
        bool renderTexture=false;
        for(unsigned slot=0;slot<16;++slot) {
            IDirect3DBaseTexture9* texture=nullptr;device->GetTexture(slot,&texture);
            if(texture&&texture->GetType()==D3DRTYPE_TEXTURE) {
                auto* tex=static_cast<IDirect3DTexture9*>(texture);D3DSURFACE_DESC td{};tex->GetLevelDesc(0,&td);
                fprintf(file_,"s%u=%p %ux%u fmt=%u usage=%lu\n",slot,tex,td.Width,td.Height,td.Format,td.Usage);
                if(td.Usage&D3DUSAGE_RENDERTARGET) {
                    renderTexture=true;
                    if(inputs_.insert(tex).second) {
                        tex->AddRef();held_.push_back(tex);
                        IDirect3DSurface9* surface=nullptr;tex->GetSurfaceLevel(0,&surface);
                        char label[48]{};sprintf_s(label,"input_s%u_%p",slot,tex);Snapshot(surface,label);
                        if(surface)surface->Release();
                    }
                }
            }
            if(texture)texture->Release();
        }
        lastVs_=vi;lastPs_=pi;
        if(renderTexture&&primitives<=4&&target&&screenPasses_[target].insert(pi).second) {
            // Sample each shader/target combination once so repeated particles
            // cannot consume the image budget before later composite passes.
            target->AddRef();held_.push_back(target);
            Snapshot(target,"before");return target;
        }
        if(target)target->Release();return nullptr;
    }
    void AfterDraw(IDirect3DSurface9* target){if(target){Snapshot(target,"after");target->Release();}}
    void TargetChange(IDirect3DDevice9* device,DWORD index,IDirect3DSurface9* next) {
        if(!Active()||index!=0)return;
        IDirect3DSurface9* previous=nullptr;device->GetRenderTarget(0,&previous);
        if(previous&&previous!=next)Snapshot(previous,"finished_target");
        if(previous)previous->Release();
    }
private:
    FILE* file_=nullptr;
    bool busy_=false;
    std::wstring directory_;
    SaveSurface save_=nullptr;
    Disassemble disassemble_=nullptr;
    unsigned draw_=0,images_=0;
    unsigned long long bytes_=0;
    uint32_t lastVs_=0,lastPs_=0;
    std::unordered_map<IUnknown*,uint32_t> shaders_;
    std::unordered_set<IDirect3DTexture9*> inputs_;
    std::unordered_map<IDirect3DSurface9*,std::unordered_set<uint32_t>> screenPasses_;
    std::vector<IUnknown*> held_;
};
struct SceneCaptureDraw {
    SceneCapture9& capture;
    IDirect3DSurface9* target;
    SceneCaptureDraw(SceneCapture9& c,IDirect3DDevice9* d,UINT primitives):capture(c),target(c.BeforeDraw(d,primitives)){}
    ~SceneCaptureDraw(){capture.AfterDraw(target);}
};
}
