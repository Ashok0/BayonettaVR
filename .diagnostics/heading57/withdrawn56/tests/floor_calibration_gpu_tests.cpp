#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <initializer_list>
#include "GameCameraFixture.h"
#include <d3d9.h>
#include <wrl/client.h>
#include <vector>
#include <cstdlib>
using namespace bvr;
using Microsoft::WRL::ComPtr;
static void Require(bool ok,const char* why){if(!ok){printf("calibration GPU FAILED: %s\n",why);exit(1);}}
static void Check(HRESULT hr){if(FAILED(hr)){printf("calibration GPU HRESULT %08lX\n",hr);exit(2);}}
struct Vertex {float x,y,z;DWORD color;};
// Reproduce .55 precisely enough to prove this test catches its reflection.
static Mat4 OldCorrection(const Mat4& camera,float offset) {
    Mat4 level=Identity();float x=camera.m[0][2],z=camera.m[2][2];const float n=std::sqrt(x*x+z*z);x/=n;z/=n;
    level.m[0][0]=z;level.m[2][0]=-x;level.m[0][2]=x;level.m[2][2]=z;
    for(int i=0;i<3;++i)level.m[i][3]=camera.m[i][3];level.m[1][3]+=offset;
    return Mul(InvertRigid(level),camera);
}
int main() {
    GameCameraFixture game;
    HWND window=CreateWindowExW(0,L"STATIC",L"Floor calibration regression",WS_OVERLAPPEDWINDOW,0,0,128,128,nullptr,nullptr,GetModuleHandleW(nullptr),nullptr);
    ComPtr<IDirect3D9Ex> api;Check(Direct3DCreate9Ex(D3D_SDK_VERSION,&api));
    D3DPRESENT_PARAMETERS pp{};pp.Windowed=TRUE;pp.SwapEffect=D3DSWAPEFFECT_DISCARD;pp.hDeviceWindow=window;
    pp.BackBufferWidth=pp.BackBufferHeight=128;pp.BackBufferFormat=D3DFMT_X8R8G8B8;
    ComPtr<IDirect3DDevice9Ex> d;Check(api->CreateDeviceEx(0,D3DDEVTYPE_HAL,window,D3DCREATE_HARDWARE_VERTEXPROCESSING,&pp,nullptr,&d));
    ComPtr<IDirect3DSurface9> target,read;
    Check(d->CreateRenderTarget(128,128,D3DFMT_A8R8G8B8,D3DMULTISAMPLE_NONE,0,FALSE,&target,nullptr));
    Check(d->CreateOffscreenPlainSurface(128,128,D3DFMT_A8R8G8B8,D3DPOOL_SYSTEMMEM,&read,nullptr));
    Check(d->SetRenderTarget(0,target.Get()));
    Check(d->SetFVF(D3DFVF_XYZ|D3DFVF_DIFFUSE));
    Check(d->SetRenderState(D3DRS_LIGHTING,FALSE));Check(d->SetRenderState(D3DRS_ZENABLE,FALSE));
    Check(d->SetTextureStageState(0,D3DTSS_COLOROP,D3DTOP_SELECTARG1));
    Check(d->SetTextureStageState(0,D3DTSS_COLORARG1,D3DTA_DIFFUSE));
    const Mat4 identity=Identity();Check(d->SetTransform(D3DTS_WORLD,reinterpret_cast<const D3DMATRIX*>(&identity)));
    Check(d->SetTransform(D3DTS_VIEW,reinterpret_cast<const D3DMATRIX*>(&identity)));
    std::vector<Vertex> vertices;
    auto draw=[&](const Mat4& clip,D3DCULL cull) {
        const auto row=Transpose(clip);Check(d->SetTransform(D3DTS_PROJECTION,reinterpret_cast<const D3DMATRIX*>(&row)));
        Check(d->SetRenderState(D3DRS_CULLMODE,cull));Check(d->Clear(0,nullptr,D3DCLEAR_TARGET,0,1,0));
        Check(d->BeginScene());Check(d->DrawPrimitiveUP(D3DPT_TRIANGLELIST,UINT(vertices.size()/3),vertices.data(),sizeof(Vertex)));Check(d->EndScene());
        Check(d->GetRenderTargetData(target.Get(),read.Get()));D3DLOCKED_RECT lock{};Check(read->LockRect(&lock,nullptr,D3DLOCK_READONLY));
        std::vector<DWORD> pixels(128*128);for(int y=0;y<128;++y)memcpy(pixels.data()+128*y,static_cast<char*>(lock.pBits)+y*lock.Pitch,128*4);
        Check(read->UnlockRect());return pixels;
    };
    auto lit=[](const std::vector<DWORD>& p){unsigned n=0;for(auto color:p)if(color&0xffffff)++n;return n;};
    auto redX=[](const std::vector<DWORD>& p){double sum=0;unsigned n=0;for(unsigned i=0;i<p.size();++i)if((p[i]&0xffffff)==0xff0000){sum+=i%128;++n;}Require(n>20,"red direction marker exists");return sum/n;};
    unsigned cases=0,pixelsChecked=0,oldBlack=0;
    for(float yaw:{-1.5f,0.0f,1.2f})for(float pitch:{-.45f,0.0f,.45f})for(float eyeX:{-.032f,.032f}) {
        const float ground=-42,headHeight=1.262f;
        float eye[]={10,-39.935f,20},up[]={0,1,0};
        float aim[]={eye[0]+std::sin(yaw)*std::cos(pitch),eye[1]+std::sin(pitch),eye[2]+std::cos(yaw)*std::cos(pitch)};
        const auto rowView=game.View(eye,aim,up);const auto rowProjection=game.Projection(.872664626f,1,.1f,1000000);
        const auto rowVP=Mul(rowView,rowProjection);Mat4 projection{},camera{},inverse{};
        Require(GameProjectionFromViewProjection(rowVP,projection)&&RecoverGameCamera(projection,Transpose(rowVP),camera)&&Invert(projection,inverse),"recover engine matrices");
        FloorCalibration calibration;Require(calibration.Calibrate(camera,ground,headHeight,1),"calibrate");
        // Independently build the desired camera using the engine's OWN
        // look-at code, not the calibration implementation.
        float desiredEye[]={eye[0],ground+headHeight,eye[2]};
        float desiredAim[]={desiredEye[0]+std::sin(yaw),desiredEye[1],desiredEye[2]+std::cos(yaw)};
        const auto desiredView=game.View(desiredEye,desiredAim,up);
        const auto desiredCamera=InvertRigid(Transpose(desiredView));
        vertices.clear();
        auto vertex=[&](float x,float y,float z,DWORD color){
            float p[]={x,y,z,1},w[3]{};for(int r=0;r<3;++r)for(int c=0;c<4;++c)w[r]+=desiredCamera.m[r][c]*p[c];
            vertices.push_back({w[0],w[1],w[2],color});
        };
        for(int side:{-1,1}){
            const float left=side<0?-1.4f:.2f,right=side<0?-.2f:1.4f;const DWORD color=side<0?0xff00ff00:0xffff0000;
            vertex(left,-.7f,-4,color);vertex(left,.7f,-4,color);vertex(right,.7f,-4,color);
            vertex(left,-.7f,-4,color);vertex(right,.7f,-4,color);vertex(right,-.7f,-4,color);
        }
        Mat4 eyeView=Identity();eyeView.m[0][3]=-eyeX;
        const auto eyeProjection=WithSceneDepth(PerspectiveRawLH(-1,1,-1,1,.1f,10000),projection);
        const auto prefix=Mul(eyeProjection,eyeView);
        const auto referenceClip=Mul(prefix,Mul(inverse,Transpose(Mul(desiredView,rowProjection))));
        const auto actualClip=Mul(prefix,Mul(calibration.Correction(camera),Mul(inverse,Transpose(rowVP))));
        const auto oldClip=Mul(prefix,Mul(OldCorrection(camera,calibration.HeightOffset()),Mul(inverse,Transpose(rowVP))));
        D3DCULL cull=D3DCULL_CCW;auto expected=draw(referenceClip,cull);
        if(lit(expected)==0){cull=D3DCULL_CW;expected=draw(referenceClip,cull);}
        Require(lit(expected)>300,"reference scene visible with backface culling");
        const auto actual=draw(actualClip,cull),old=draw(oldClip,cull);
        Require(lit(actual)>300,"corrected scene retains front faces");
        Require(lit(old)==0,".55 reproduces black scene with same culling");++oldBlack;
        unsigned mismatch=0;for(size_t i=0;i<actual.size();++i){if(actual[i]!=expected[i])++mismatch;++pixelsChecked;}
        Require(mismatch<12,"corrected GPU pixels match independent engine camera");
        Require(redX(actual)>64,"right remains right");
        Require(redX(draw(oldClip,D3DCULL_NONE))<64,".55 reproduces reversed horizontal direction");
        ++cases;
    }
    DestroyWindow(window);
    printf("Floor GPU: %u native-camera stereo cases, %u pixels; all %u .55 cases reproduce black/reversed view, .56 matches engine reference with culling intact.\n",cases,pixelsChecked,oldBlack);
}
