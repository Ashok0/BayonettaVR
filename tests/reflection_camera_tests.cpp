#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include "ReflectionCamera.h"
#include "FirstPersonGameCameraFixture.h"
#include <d3dcompiler.h>
#include <wrl/client.h>
#include <fstream>
#include <vector>
#include <cstdlib>
using namespace bvr;
using Microsoft::WRL::ComPtr;
static void Require(bool ok,const char* why){if(!ok){printf("reflection camera FAILED: %s\n",why);exit(1);}}
static void Check(HRESULT hr){if(FAILED(hr)){printf("reflection camera HRESULT %08lX\n",hr);exit(2);}}
static HRESULT STDMETHODCALLTYPE Set(IDirect3DDevice9* d,UINT base,const float* data,UINT count){return d->SetVertexShaderConstantF(base,data,count);}
static std::vector<DWORD> Load(const char* name) {
    std::ifstream f(std::string("tests/fixtures/reflection_camera/")+name,std::ios::binary|std::ios::ate);
    Require(f.good(),"native fixture exists");size_t size=size_t(f.tellg());f.seekg(0);
    std::vector<DWORD> code((size+3)/4);f.read(reinterpret_cast<char*>(code.data()),size);return code;
}
struct Vertex {float position[3],normal[3],uv[2],indices[4],weights[4];};
int main() {
    GameCameraFixture game;unsigned mathCases=0;
    for(float yaw:{-2.f,0.f,1.3f})for(float pitch:{-.5f,0.f,.5f})for(float offset:{-.032f,.032f}) {
        float eye[]={-40+offset,7.3f,191.1f},up[]={0,1,0};
        float aim[]={eye[0]+std::sin(yaw)*std::cos(pitch),eye[1]+std::sin(pitch),eye[2]+std::cos(yaw)*std::cos(pitch)};
        const auto view=Transpose(game.View(eye,aim,up));
        for(int projection=0;projection<2;++projection) {
            const auto p=projection?PerspectiveRawLH(-1.3f,.9f,-1.1f,1.2f,.1f,10000):Transpose(game.Projection(.9f,1.77f,.1f,1000000));
            float found[3]{};Require(ReflectionCameraPosition(Mul(p,view),found),"actual native camera recovered");
            for(int r=0;r<3;++r)Require(std::fabs(found[r]-eye[r])<.001f,"native reflected/asymmetric eye agrees with independent camera input");
            ++mathCases;
        }
    }
    float position[3]={123,456,789};Mat4 bad{};
    Require(!ReflectionCameraPosition(bad,position)&&position[0]==123,"singular matrix does not change output");
    Require(!ReflectionCameraPosition(Identity(),position),"orthographic camera rejected");
    bad=Identity();bad.m[0][0]=NAN;Require(!ReflectionCameraPosition(bad,position),"nonfinite matrix rejected");
    for(const char* name:{"3C21215E.vso","C860EAEC.vso","FFD8347E.vso","90241E02.vso","5DD39D91.vso","AD68C3FE.vso","EF05A9C9.vso","BDAFACB9.vso","648FD477.vso","41D36437.vso","6955BB24.vso"}) {
        const auto code=Load(name);const auto crc=ShaderCrc32(code.data(),code.size()*4);
        Require(IsReflectionCameraShader(crc,code.size()*4),"captured native shader recognized");
        Require(!IsReflectionCameraShader(crc,code.size()*4+4)&&!IsReflectionCameraShader(crc^1,code.size()*4),"near matches rejected");
    }
    HWND window=CreateWindowExW(0,L"STATIC",L"Reflection camera regression",WS_OVERLAPPEDWINDOW,0,0,64,64,nullptr,nullptr,GetModuleHandleW(nullptr),nullptr);
    ComPtr<IDirect3D9Ex> api;Check(Direct3DCreate9Ex(D3D_SDK_VERSION,&api));
    D3DPRESENT_PARAMETERS pp{};pp.Windowed=TRUE;pp.SwapEffect=D3DSWAPEFFECT_DISCARD;pp.hDeviceWindow=window;
    pp.BackBufferWidth=pp.BackBufferHeight=64;pp.BackBufferFormat=D3DFMT_X8R8G8B8;
    ComPtr<IDirect3DDevice9Ex> d;Check(api->CreateDeviceEx(0,D3DDEVTYPE_HAL,window,D3DCREATE_HARDWARE_VERTEXPROCESSING,&pp,nullptr,&d));
    ComPtr<IDirect3DSurface9> target,read;
    Check(d->CreateRenderTarget(64,64,D3DFMT_A8R8G8B8,D3DMULTISAMPLE_NONE,0,FALSE,&target,nullptr));
    Check(d->CreateOffscreenPlainSurface(64,64,D3DFMT_A8R8G8B8,D3DPOOL_SYSTEMMEM,&read,nullptr));Check(d->SetRenderTarget(0,target.Get()));
    const auto code=Load("EF05A9C9.vso");ComPtr<IDirect3DVertexShader9> vs;Check(d->CreateVertexShader(code.data(),&vs));Check(d->SetVertexShader(vs.Get()));
    // Run the captured glass/reflection vertex shader unchanged. Visualize
    // its actual reflection-vector output to isolate camera position from art.
    const char shader[]="float4 main(float3 ray:TEXCOORD5):COLOR{return float4(normalize(ray)*.5+.5,1);}";
    ComPtr<ID3DBlob> psCode;Check(D3DCompile(shader,sizeof(shader)-1,nullptr,nullptr,nullptr,"main","ps_3_0",0,0,&psCode,nullptr));
    ComPtr<IDirect3DPixelShader9> ps;Check(d->CreatePixelShader(static_cast<const DWORD*>(psCode->GetBufferPointer()),&ps));Check(d->SetPixelShader(ps.Get()));
    const D3DVERTEXELEMENT9 elements[]={
        {0,0,D3DDECLTYPE_FLOAT3,0,D3DDECLUSAGE_POSITION,0},{0,12,D3DDECLTYPE_FLOAT3,0,D3DDECLUSAGE_NORMAL,0},
        {0,24,D3DDECLTYPE_FLOAT2,0,D3DDECLUSAGE_TEXCOORD,0},{0,32,D3DDECLTYPE_FLOAT4,0,D3DDECLUSAGE_BLENDINDICES,0},
        {0,48,D3DDECLTYPE_FLOAT4,0,D3DDECLUSAGE_BLENDWEIGHT,0},D3DDECL_END()};
    ComPtr<IDirect3DVertexDeclaration9> decl;Check(d->CreateVertexDeclaration(elements,&decl));Check(d->SetVertexDeclaration(decl.Get()));
    Check(d->SetRenderState(D3DRS_ZENABLE,FALSE));Check(d->SetRenderState(D3DRS_CULLMODE,D3DCULL_NONE));Check(d->SetRenderState(D3DRS_ALPHABLENDENABLE,FALSE));
    const auto identity=Identity();Check(Set(d.Get(),0,&identity.m[0][0],3));Check(Set(d.Get(),40,&identity.m[0][0],3));
    Vertex quad[4]{};
    for(int i=0;i<4;++i){quad[i].position[0]=i&1?2.f:-2.f;quad[i].position[1]=i&2?-2.f:2.f;quad[i].position[2]=4;quad[i].normal[2]=-1;quad[i].weights[0]=1;}
    auto render=[&] {
        Check(d->Clear(0,nullptr,D3DCLEAR_TARGET,0xff000000,1,0));Check(d->BeginScene());
        Check(d->DrawPrimitiveUP(D3DPT_TRIANGLESTRIP,2,quad,sizeof(Vertex)));Check(d->EndScene());
        Check(d->GetRenderTargetData(target.Get(),read.Get()));D3DLOCKED_RECT lock{};Check(read->LockRect(&lock,nullptr,D3DLOCK_READONLY));
        std::vector<DWORD> pixels(4096);for(int y=0;y<64;++y)memcpy(pixels.data()+y*64,static_cast<char*>(lock.pBits)+y*lock.Pitch,256);
        Check(read->UnlockRect());return pixels;
    };
    unsigned gpuCases=0,pixels=0;
    for(float yaw:{-.25f,0.f,.25f})for(float eye:{-.032f,.032f})for(float displacement:{0.f,.6f}) {
        Mat4 camera=Identity();camera.m[0][0]=camera.m[2][2]=std::cos(yaw);camera.m[0][2]=std::sin(yaw);camera.m[2][0]=-std::sin(yaw);
        camera.m[0][3]=eye+displacement;camera.m[1][3]=.2f;camera.m[2][3]=-.3f;
        const auto adjusted=Mul(PerspectiveRawLH(-.9f,1.1f,-1,1,.1f,10000),InvertRigid(camera));Check(Set(d.Get(),8,&adjusted.m[0][0],4));
        float original[12];memcpy(original,&identity.m[0][0],sizeof(original));
        // Deliberately stale third-person camera: same for both eyes.
        original[3]=-.8f;original[7]=1;original[11]=-2;
        float expected[12];memcpy(expected,original,sizeof(expected));for(int r=0;r<3;++r)expected[r*4+3]=camera.m[r][3];
        Check(Set(d.Get(),12,expected,3));const auto reference=render();Check(Set(d.Get(),12,original,3));const auto stale=render();
        unsigned difference=0,lit=0;for(unsigned i=0;i<4096;++i){difference+=stale[i]!=reference[i];lit+=(reference[i]&0xffffff)!=0;}
        Require(lit>200&&difference>100,"old shared camera visibly differs from independent per-eye reference");
        {
            ReflectionCameraScope scope(d.Get(),Set,adjusted,true);Require(scope.Active(),"scope activates");
            const auto fixed=render();unsigned mismatch=0;
            for(unsigned i=0;i<4096;++i){mismatch+=fixed[i]!=reference[i];++pixels;}
            Require(mismatch<8,"native GPU reflection vectors match independent eye-camera reference");
        }
        float restored[12];Check(d->GetVertexShaderConstantF(12,restored,3));Require(!memcmp(restored,original,sizeof(original)),"all native inverse-view constants restored");
        {
            ReflectionCameraScope scope(d.Get(),Set,adjusted,false);Require(!scope.Active(),"comparison off leaves native camera");
            Check(d->GetVertexShaderConstantF(12,restored,3));Require(!memcmp(restored,original,sizeof(original)),"disabled path unchanged");
        }
        ++gpuCases;
    }
    ComPtr<IDirect3DStateBlock9> state;Check(d->CreateStateBlock(D3DSBT_ALL,&state));Check(state->Capture());
    Check(Set(d.Get(),12,&identity.m[0][0],3));Check(state->Apply());float saved[12];Check(d->GetVertexShaderConstantF(12,saved,3));
    {
        ReflectionCameraScope scope(d.Get(),Set,PerspectiveFovLH(1,1,.1f,100),true);Require(scope.Active(),"state-block restored shader constants handled");
        float live[12];Check(d->GetVertexShaderConstantF(12,live,3));for(int i=0;i<12;++i)if(i%4!=3)Require(live[i]==saved[i],"basis entries unchanged");
    }
    float restored[12];Check(d->GetVertexShaderConstantF(12,restored,3));Require(!memcmp(saved,restored,sizeof(saved)),"restore authoritative live state");
    {ReflectionCameraScope scope(d.Get(),Set,Identity(),true);Require(!scope.Active(),"invalid camera leaves constants alone");}
    std::vector<DWORD> untouched(code.size());UINT byteCount=UINT(code.size()*4);Check(vs->GetFunction(untouched.data(),&byteCount));Require(untouched==code,"native shader bytecode unchanged");
    DestroyWindow(window);
    printf("Reflection camera: %u native camera cases, 11 shader signatures, %u GPU stereo/roomscale cases (%u pixels), stale-camera negative comparison and state restoration passed.\n",mathCases,gpuCases,pixels);
}
