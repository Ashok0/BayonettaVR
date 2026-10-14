#pragma once
#include "DepthOfField.h"
#include "Matrix.h"

namespace bvr {
enum class FilmVertex { None, Copy, Card, Backdrop };
inline FilmVertex LiveFilmVertex(uint32_t crc,size_t size) {
    if(crc==0xCB1E6A15u&&size==288)return FilmVertex::Copy;
    if(crc==0x3379538Au&&size==1336)return FilmVertex::Card;
    if(crc==0x67B951BDu&&size==1468)return FilmVertex::Backdrop;
    return FilmVertex::None;
}
inline bool FilmCopyPixel(uint32_t crc,size_t size){return crc==0xB8690130u&&size==260;}
inline bool FilmCardPixel(uint32_t crc,size_t size){return crc==0xCA043AE5u&&size==932;}
inline bool FilmBackdropPixel(uint32_t crc,size_t size){return crc==0x0103F1A9u&&size==5788;}

// Live film strips paint a stereo scene, make two thumbnail copies, mask it,
// then sample it on a separately projected skinned card. Keep the current
// eye's scene immediately before that presentation, and restore it only when
// the captured card shader/camera/texture combination confirms the sequence.
class LiveFilmScene9 {
    template<class T> static void Release(T*& p){if(p)p->Release();p=nullptr;}
public:
    ~LiveFilmScene9(){Reset();}
    void Register(IDirect3DPixelShader9* s){copy_.Register(s);card_.Register(s);backdrop_.Register(s);}
    void BeginEye(){Release(target_);ready_=promoted_=false;}
    void ReleaseResources(){BeginEye();Release(snapshot_);}
    void Reset(){ReleaseResources();copy_.Reset();card_.Reset();backdrop_.Reset();}
    bool Busy()const{return busy_;}
    bool Promoted()const{return promoted_;}
    static bool SnapshotCamera(const Mat4& m) {
        const Mat4 expected={{{1,0,0,-1},{0,-1,0,1},{0,0,-1.52590219e-5f,.000152590219f},{0,0,0,1}}};
        return SameCamera(m,expected);
    }
    static bool CardCamera(const Mat4& m) {
        for(auto& row:m.m)for(float v:row)if(!std::isfinite(v))return false;
        // The presentation camera is axis-aligned, looks along -Z and has
        // a positive distance. No hard-coded FOV or animated camera distance.
        return m.m[0][0]>.05f&&m.m[1][1]>.05f&&m.m[3][3]>.1f&&
            std::fabs(m.m[0][1])<1e-4f&&std::fabs(m.m[0][2])<1e-4f&&std::fabs(m.m[0][3])<1e-4f&&
            std::fabs(m.m[1][0])<1e-4f&&std::fabs(m.m[1][2])<1e-4f&&std::fabs(m.m[1][3])<1e-4f&&
            std::fabs(m.m[2][0])<1e-4f&&std::fabs(m.m[2][1])<1e-4f&&
            std::fabs(m.m[3][0])<1e-4f&&std::fabs(m.m[3][1])<1e-4f&&
            std::fabs(m.m[3][2]+1)<1e-4f&&std::fabs(m.m[2][2]+1)<.01f;
    }
    // true means this presentation draw has been handled; native scene draws
    // continue through the regular stereo/6DOF camera path.
    bool Handle(IDirect3DDevice9* d,int base,const Mat4& matrix,FilmVertex vertex,bool enabled) {
        if(!enabled||busy_||vertex==FilmVertex::None)return false;
        if(vertex==FilmVertex::Backdrop) {
            if(!promoted_||base!=8||!SameCamera(matrix,camera_)||!backdrop_.ShouldSkip(d,true))return false;
            IDirect3DSurface9* current=nullptr;d->GetRenderTarget(0,&current);
            const bool match=current&&current==target_;Release(current);return match;
        }
        const bool capture=vertex==FilmVertex::Copy&&base==24&&!ready_&&SnapshotCamera(matrix)&&copy_.ShouldSkip(d,true);
        const bool present=vertex==FilmVertex::Card&&base==8&&ready_&&CardCamera(matrix)&&card_.ShouldSkip(d,true);
        if(!capture&&!present)return false;
        IDirect3DSurface9* current=nullptr;IDirect3DBaseTexture9* texture=nullptr;
        D3DSURFACE_DESC rt{},tex{};D3DVIEWPORT9 vp{};DWORD blend=0;
        bool valid=SUCCEEDED(d->GetRenderTarget(0,&current))&&current&&
            SUCCEEDED(current->GetDesc(&rt))&&SUCCEEDED(d->GetViewport(&vp))&&
            SUCCEEDED(d->GetTexture(0,&texture))&&texture&&texture->GetType()==D3DRTYPE_TEXTURE;
        if(valid)valid=SUCCEEDED(static_cast<IDirect3DTexture9*>(texture)->GetLevelDesc(0,&tex))&&
            rt.Format==D3DFMT_A8R8G8B8&&tex.Format==rt.Format&&
            (tex.Usage&D3DUSAGE_RENDERTARGET)&&tex.Width==rt.Width&&tex.Height==rt.Height&&
            vp.X==0&&vp.Y==0&&vp.Width==rt.Width&&vp.Height==rt.Height&&rt.Width>rt.Height;
        Release(texture);
        if(capture)valid=valid&&SUCCEEDED(d->GetRenderState(D3DRS_ALPHABLENDENABLE,&blend))&&!blend;
        if(present)valid=valid&&current==target_;
        if(!valid){Release(current);return false;}
        busy_=true;HRESULT hr=S_OK;
        if(capture){
            D3DSURFACE_DESC existing{};
            if(snapshot_&&(FAILED(snapshot_->GetDesc(&existing))||existing.Width!=rt.Width||existing.Height!=rt.Height))Release(snapshot_);
            if(!snapshot_)hr=d->CreateRenderTarget(rt.Width,rt.Height,rt.Format,D3DMULTISAMPLE_NONE,0,FALSE,&snapshot_,nullptr);
            if(SUCCEEDED(hr))hr=d->StretchRect(current,nullptr,snapshot_,nullptr,D3DTEXF_NONE);
            if(SUCCEEDED(hr)){target_=current;current=nullptr;ready_=true;}
        } else {
            hr=d->StretchRect(snapshot_,nullptr,current,nullptr,D3DTEXF_NONE);
            if(SUCCEEDED(hr)){camera_=matrix;promoted_=true;}
        }
        busy_=false;Release(current);
        return present&&SUCCEEDED(hr);
    }
private:
    static bool SameCamera(const Mat4& a,const Mat4& b){
        for(int r=0;r<4;++r)for(int c=0;c<4;++c)
            if(!std::isfinite(a.m[r][c])||std::fabs(a.m[r][c]-b.m[r][c])>1e-5f)return false;
        return true;
    }
    PixelShaderDrawFilter copy_{FilmCopyPixel},card_{FilmCardPixel},backdrop_{FilmBackdropPixel};
    IDirect3DSurface9 *snapshot_=nullptr,*target_=nullptr;
    Mat4 camera_{};bool ready_=false,promoted_=false,busy_=false;
};
}
