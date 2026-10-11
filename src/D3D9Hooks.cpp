#include "D3D9Hooks.h"
#include "Config.h"
#include "Log.h"
#include "Matrix.h"
#include "ShaderAnalysis.h"
#include "WorldHud.h"
#include "MenuPanel9.h"
#include "MoviePanel9.h"
#include "MovieDrawQueue.h"
#include "NativeSceneTrace9.h"
#include "MapCinema9.h"
#include "CameraCulling.h"
#include "EffectStreamReplay.h"
#include "ReferenceGridRenderer.h"
#include "VR.h"
#include "IndependentGrid.h"
#include "SharedFrameTransport.h"
#include "ControllerHooks.h"
#include "DepthOfField.h"
#include "MotionBlur.h"
#include "AmbientOcclusion.h"
#include "SoftParticleDepth.h"
#include <d3d11.h>
#include <dxgi.h>
#include <windows.h>
#include <algorithm>
#include <cctype>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <string>
#include <unordered_map>
#include <vector>

extern "C" IMAGE_DOS_HEADER __ImageBase;

namespace bvr {
namespace {
template<class T> bool PatchVtable(void** table, size_t index, T hook, T& original) {
    if (table[index] == reinterpret_cast<void*>(hook)) return true;
    DWORD old = 0;
    if (!VirtualProtect(&table[index], sizeof(void*), PAGE_EXECUTE_READWRITE, &old)) return false;
    original = reinterpret_cast<T>(table[index]);
    table[index] = reinterpret_cast<void*>(hook);
    DWORD ignored = 0;
    VirtualProtect(&table[index], sizeof(void*), old, &ignored);
    FlushInstructionCache(GetCurrentProcess(), &table[index], sizeof(void*));
    return true;
}

using CreateDeviceExFn = HRESULT (STDMETHODCALLTYPE*)(IDirect3D9Ex*, UINT, D3DDEVTYPE, HWND, DWORD,
                                                       D3DPRESENT_PARAMETERS*, D3DDISPLAYMODEEX*, IDirect3DDevice9Ex**);
using ResetFn = HRESULT (STDMETHODCALLTYPE*)(IDirect3DDevice9*, D3DPRESENT_PARAMETERS*);
using PresentFn = HRESULT (STDMETHODCALLTYPE*)(IDirect3DDevice9*, const RECT*, const RECT*, HWND, const RGNDATA*);
using CreateVSFn = HRESULT (STDMETHODCALLTYPE*)(IDirect3DDevice9*, const DWORD*, IDirect3DVertexShader9**);
using SetVSFn = HRESULT (STDMETHODCALLTYPE*)(IDirect3DDevice9*, IDirect3DVertexShader9*);
using SetConstFn = HRESULT (STDMETHODCALLTYPE*)(IDirect3DDevice9*, UINT, const float*, UINT);
using SetRenderTargetFn = HRESULT (STDMETHODCALLTYPE*)(IDirect3DDevice9*, DWORD, IDirect3DSurface9*);
using ClearFn = HRESULT (STDMETHODCALLTYPE*)(IDirect3DDevice9*, DWORD, const D3DRECT*, DWORD, D3DCOLOR, float, DWORD);
using DrawPrimitiveFn = HRESULT (STDMETHODCALLTYPE*)(IDirect3DDevice9*, D3DPRIMITIVETYPE, UINT, UINT);
using DrawIndexedFn = HRESULT (STDMETHODCALLTYPE*)(IDirect3DDevice9*, D3DPRIMITIVETYPE, INT, UINT, UINT, UINT, UINT);
using DrawPrimitiveUPFn = HRESULT (STDMETHODCALLTYPE*)(IDirect3DDevice9*, D3DPRIMITIVETYPE, UINT, const void*, UINT);
using DrawIndexedUPFn = HRESULT (STDMETHODCALLTYPE*)(IDirect3DDevice9*, D3DPRIMITIVETYPE, UINT, UINT, UINT, const void*, D3DFORMAT, const void*, UINT);
using PresentExFn = HRESULT (STDMETHODCALLTYPE*)(IDirect3DDevice9Ex*, const RECT*, const RECT*, HWND, const RGNDATA*, DWORD);
using ResetExFn = HRESULT (STDMETHODCALLTYPE*)(IDirect3DDevice9Ex*, D3DPRESENT_PARAMETERS*, D3DDISPLAYMODEEX*);
using SwapPresentFn = HRESULT (STDMETHODCALLTYPE*)(IDirect3DSwapChain9*, const RECT*, const RECT*, HWND, const RGNDATA*, DWORD);

CreateDeviceExFn g_createDeviceEx = nullptr;
ResetFn g_reset = nullptr;
PresentFn g_present = nullptr;
CreateVSFn g_createVS = nullptr;
CreatePixelShaderFn g_createPS = nullptr;
DepthOfFieldFilter g_dofFilter;
PixelShaderDrawFilter g_motionBlurFilter(IsMotionBlurShader);
bool g_disableMotionBlur = true;
PixelShaderDrawFilter g_ambientOcclusionShader(IsAmbientOcclusionShader);
bool g_disableAmbientOcclusion = true;
SoftParticleDepth g_softParticleDepth;
bool g_fixSoftParticleDepth = true;
SetVSFn g_setVS = nullptr;
SetConstFn g_setConst = nullptr;
SetRenderTargetFn g_setRenderTarget = nullptr;
ClearFn g_clear = nullptr;
MenuPanel9::CopyFn g_stretchRect = nullptr;
DrawPrimitiveFn g_drawPrimitive = nullptr;
DrawIndexedFn g_drawIndexed = nullptr;
DrawPrimitiveUPFn g_drawPrimitiveUP = nullptr;
DrawIndexedUPFn g_drawIndexedUP = nullptr;
PresentExFn g_presentEx = nullptr;
ResetExFn g_resetEx = nullptr;
SwapPresentFn g_swapPresent = nullptr;

struct ShaderInfo {
    int matrixBase = -1;
    unsigned long long draws = 0;
    unsigned long long hudDraws = 0;
    std::string assembly;
};
std::unordered_map<IDirect3DVertexShader9*, ShaderInfo> g_shaders;
IDirect3DVertexShader9* g_currentShader = nullptr;
float g_constants[256][4]{};
bool g_constantValid[256]{};
unsigned long long g_adjustedDraws = 0;
unsigned long long g_hudDraws = 0;
thread_local bool g_insidePresent = false;
thread_local bool g_insideNativeDraw = false;
thread_local int g_capturingMenu = -1;
thread_local bool g_compositingMenu = false;
thread_local bool g_capturingMovie = false;
MoviePanel9 g_moviePanel;
bool g_movieCaptureFailed=false;
thread_local bool g_flatMovieFrame=false;
const MovieDrawQueue* g_movieQueue=nullptr;
NativeSceneTrace9 g_nativeSceneTrace;
MapCinema9 g_mapCinema;
bool g_nativeSceneTracePending=false;
bool g_menuHooksInstalled = false;
MenuPanel9 g_menuPanel;
unsigned long long g_menuCaptures[2]{}, g_menuEyeDraws[2]{};
struct ClipCacheEntry {
    UINT width = 0;
    UINT height = 0;
    bool hud = false;
    Mat4 transform{};
};
ClipCacheEntry g_clipCache[2][8]{};
int g_clipCacheCount[2]{};
IDirect3DDevice9* g_device = nullptr;
bool g_nativePairReady = false;
unsigned long long g_nativePairs = 0;
bool g_earlySubmitEnabled = false;
EarlySubmissionGate g_earlySubmission;
struct PipelineTiming {
    double waitStart = 0, poseReady = 0, pairReady = 0, handoff = 0;
} g_pipeline;

double PipelineSeconds() {
    static const double frequency = [] {
        LARGE_INTEGER f{}; QueryPerformanceFrequency(&f); return double(f.QuadPart);
    }();
    LARGE_INTEGER now{}; QueryPerformanceCounter(&now);
    return double(now.QuadPart)/frequency;
}

constexpr int kSubmitRingSize = 4;
constexpr UINT kCleanGridSize = 1024;
struct EyeTexture {
    IDirect3DTexture9* d3d9 = nullptr;
    IDirect3DSurface9* readback = nullptr;
    ID3D11Texture2D* shared11 = nullptr;
    ID3D11Texture2D* submit11[kSubmitRingSize]{};
    bool ready = false;
    unsigned long long pixelHash = 0;
};
EyeTexture g_eyes[2];
ID3D11Texture2D* g_cleanGrid[2][kSubmitRingSize]{};
ID3D11Device* g_d3d11 = nullptr;
ID3D11DeviceContext* g_context11 = nullptr;
UINT g_width = 0, g_height = 0;
bool g_gpuShared = false;
int g_submitSlot = 0;
bool g_snapshotPending = false;
bool g_snapshotSaved[2]{};
unsigned g_snapshotSequence = 0;
bool g_menuDiagnosticPending = false, g_menuDiagnosticActive = false;
unsigned g_menuDiagnosticDraw = 0;
struct MenuTextureDiagnostic { IDirect3DBaseTexture9* texture; };
std::vector<MenuTextureDiagnostic> g_menuDiagnosticTextures;
std::vector<IDirect3DVertexShader9*> g_menuDiagnosticVS;
std::vector<IDirect3DPixelShader9*> g_menuDiagnosticPS;
bool g_stackCapturePending = true;
bool g_referenceGrid = false;
ReferenceGridRenderer g_referenceRenderer;

void DumpRenderStack() {
    void* frames[32]{};
    const USHORT count = RtlCaptureStackBackTrace(0, static_cast<DWORD>(_countof(frames)),
                                                   frames, nullptr);
    const auto* exe = reinterpret_cast<const unsigned char*>(GetModuleHandleW(nullptr));
    DWORD imageSize = 0;
    if (exe) {
        const auto* dos = reinterpret_cast<const IMAGE_DOS_HEADER*>(exe);
        if (dos->e_magic == IMAGE_DOS_SIGNATURE) {
            const auto* nt = reinterpret_cast<const IMAGE_NT_HEADERS*>(exe + dos->e_lfanew);
            if (nt->Signature == IMAGE_NT_SIGNATURE) imageSize = nt->OptionalHeader.SizeOfImage;
        }
    }
    Log("render stack: %u frames, game=%p size=0x%X", count, exe, imageSize);
    for (USHORT i = 0; i < count; ++i) {
        const auto* address = static_cast<const unsigned char*>(frames[i]);
        if (exe && address >= exe && address < exe + imageSize)
            Log("render stack: %02u %p game+0x%08X", i, address,
                static_cast<unsigned>(address - exe));
        else
            Log("render stack: %02u %p", i, address);
    }
}

unsigned long long HashPixels(const D3DLOCKED_RECT& locked, UINT width, UINT height) {
    unsigned long long hash = 1469598103934665603ull;
    const UINT xStep = std::max(1u, width / 128u);
    const UINT yStep = std::max(1u, height / 72u);
    for (UINT y = 0; y < height; y += yStep) {
        const auto* row = static_cast<const unsigned char*>(locked.pBits) +
                          static_cast<size_t>(y) * locked.Pitch;
        for (UINT x = 0; x < width; x += xStep) {
            const auto* pixel = row + static_cast<size_t>(x) * 4;
            for (int channel = 0; channel < 4; ++channel) {
                hash ^= pixel[channel];
                hash *= 1099511628211ull;
            }
        }
    }
    return hash;
}

bool SaveEyeBitmap(int eye, const D3DLOCKED_RECT& locked, UINT width, UINT height) {
    wchar_t path[MAX_PATH]{};
    GetModuleFileNameW(reinterpret_cast<HMODULE>(&__ImageBase), path, MAX_PATH);
    if (wchar_t* slash = wcsrchr(path, L'\\')) *(slash + 1) = L'\0';
    wchar_t name[64]{};
    swprintf_s(name, L"BayonettaVR_capture%02u_%s_eye%d.bmp", g_snapshotSequence,
               VR().PoseModeLabel(), eye);
    wcscat_s(path, name);

    BITMAPFILEHEADER fileHeader{};
    BITMAPINFOHEADER infoHeader{};
    infoHeader.biSize = sizeof(infoHeader);
    infoHeader.biWidth = static_cast<LONG>(width);
    infoHeader.biHeight = -static_cast<LONG>(height); // top-down D3D image
    infoHeader.biPlanes = 1;
    infoHeader.biBitCount = 32;
    infoHeader.biCompression = BI_RGB;
    infoHeader.biSizeImage = width * height * 4;
    fileHeader.bfType = 0x4D42;
    fileHeader.bfOffBits = sizeof(fileHeader) + sizeof(infoHeader);
    fileHeader.bfSize = fileHeader.bfOffBits + infoHeader.biSizeImage;

    HANDLE file = CreateFileW(path, GENERIC_WRITE, FILE_SHARE_READ, nullptr,
                              CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (file == INVALID_HANDLE_VALUE) return false;
    DWORD written = 0;
    bool ok = WriteFile(file, &fileHeader, sizeof(fileHeader), &written, nullptr) != FALSE &&
              written == sizeof(fileHeader);
    ok = ok && WriteFile(file, &infoHeader, sizeof(infoHeader), &written, nullptr) != FALSE &&
         written == sizeof(infoHeader);
    for (UINT y = 0; ok && y < height; ++y) {
        const auto* row = static_cast<const unsigned char*>(locked.pBits) +
                          static_cast<size_t>(y) * locked.Pitch;
        ok = WriteFile(file, row, width * 4, &written, nullptr) != FALSE &&
             written == width * 4;
    }
    CloseHandle(file);
    return ok;
}

struct ID3DXBufferLocal : IUnknown {
    virtual LPVOID STDMETHODCALLTYPE GetBufferPointer() = 0;
    virtual DWORD STDMETHODCALLTYPE GetBufferSize() = 0;
};
using DisassembleFn = HRESULT (WINAPI*)(const DWORD*, BOOL, const char*, ID3DXBufferLocal**);

void ReleaseTextures() {
    g_earlySubmission.Reset();
    g_referenceRenderer.Reset();
    for (auto& eye : g_eyes) {
        for (auto*& texture : eye.submit11) {
            if (texture) texture->Release();
            texture = nullptr;
        }
        if (eye.shared11) eye.shared11->Release();
        if (eye.readback) eye.readback->Release();
        if (eye.d3d9) eye.d3d9->Release();
        eye = {};
    }
    for (auto& eye : g_cleanGrid) {
        for (auto*& texture : eye) {
            if (texture) texture->Release();
            texture = nullptr;
        }
    }
    if (g_context11) { g_context11->Release(); g_context11 = nullptr; }
    if (g_d3d11) { g_d3d11->Release(); g_d3d11 = nullptr; }
    g_width = g_height = 0;
    g_gpuShared = false;
    g_submitSlot = 0;
    g_nativePairReady = false;
}

std::string DisassembleShader(const DWORD* bytecode) {
    static DisassembleFn disassemble = nullptr;
    static bool attempted = false;
    if (!attempted) {
        attempted = true;
        HMODULE d3dx = GetModuleHandleW(L"d3dx9_43.dll");
        if (!d3dx) d3dx = LoadLibraryW(L"d3dx9_43.dll");
        if (d3dx) disassemble = reinterpret_cast<DisassembleFn>(GetProcAddress(d3dx, "D3DXDisassembleShader"));
    }
    if (!disassemble) return {};
    ID3DXBufferLocal* buffer = nullptr;
    if (FAILED(disassemble(bytecode, FALSE, nullptr, &buffer)) || !buffer) return {};
    std::string text(static_cast<const char*>(buffer->GetBufferPointer()), buffer->GetBufferSize());
    buffer->Release();
    return text;
}

std::string BaseRegister(std::string token) {
    while (!token.empty() && (token.back() == '\r' || token.back() == '\n' ||
           token.back() == ',' || token.back() == ' ')) token.pop_back();
    const size_t mask = token.find('.');
    if (mask != std::string::npos) token.resize(mask);
    return token;
}

int QueueMenuTexture(IDirect3DBaseTexture9* texture,bool output=false) {
    if (!texture) return -1;
    for (size_t i=0;i<g_menuDiagnosticTextures.size();++i)
        if (g_menuDiagnosticTextures[i].texture==texture) return int(i);
    if (g_menuDiagnosticTextures.size()>=(output ? 32u : 28u)) return -2;
    texture->AddRef();g_menuDiagnosticTextures.push_back({texture});
    return int(g_menuDiagnosticTextures.size()-1);
}
template<class Shader> int QueueMenuShader(Shader* shader,std::vector<Shader*>& shaders) {
    if(!shader) return -1;
    for(size_t i=0;i<shaders.size();++i) if(shaders[i]==shader) return int(i);
    if(shaders.size()>=32) return -2;
    shader->AddRef();shaders.push_back(shader);return int(shaders.size()-1);
}
void RecordMenuDraw(IDirect3DDevice9* device) {
    if (!g_menuDiagnosticActive || VR().Eye()!=0 || g_menuDiagnosticDraw>=160) return;
    IDirect3DVertexShader9* vs=nullptr;IDirect3DPixelShader9* ps=nullptr;
    device->GetVertexShader(&vs);device->GetPixelShader(&ps);
    const int vi=QueueMenuShader(vs,g_menuDiagnosticVS),pi=QueueMenuShader(ps,g_menuDiagnosticPS);
    if(vs)vs->Release();if(ps)ps->Release();
    IDirect3DSurface9* target=nullptr;device->GetRenderTarget(0,&target);
    D3DSURFACE_DESC desc{};if(target)target->GetDesc(&desc);
    const bool redirected=target==g_menuPanel.CaptureTarget(g_capturingMenu);
    if(target)target->Release();
    const D3DRENDERSTATETYPE keys[]={D3DRS_ALPHABLENDENABLE,D3DRS_SRCBLEND,D3DRS_DESTBLEND,
        D3DRS_BLENDOP,D3DRS_COLORWRITEENABLE,D3DRS_ALPHATESTENABLE,D3DRS_ALPHAFUNC,
        D3DRS_ALPHAREF,D3DRS_ZENABLE,D3DRS_ZWRITEENABLE,D3DRS_STENCILENABLE,D3DRS_SCISSORTESTENABLE};
    DWORD values[_countof(keys)]{};
    for(size_t i=0;i<_countof(keys);++i)device->GetRenderState(keys[i],&values[i]);
    Log("menu trace: draw=%u layer=%d vs=%d ps=%d target=%ux%u fmt=%u redirected=%d blend=%u/%u/%u/%u mask=%u alphaTest=%u/%u/%u depth=%u/%u stencil=%u scissor=%u",
        ++g_menuDiagnosticDraw,g_capturingMenu,vi,pi,desc.Width,desc.Height,desc.Format,redirected,
        values[0],values[1],values[2],values[3],values[4],values[5],values[6],values[7],values[8],values[9],values[10],values[11]);
    for(int stage=0;stage<16;++stage) {
        IDirect3DBaseTexture9* texture=nullptr;device->GetTexture(stage,&texture);
        const int index=QueueMenuTexture(texture);
        D3DSURFACE_DESC td{};
        if(texture && texture->GetType()==D3DRTYPE_TEXTURE)static_cast<IDirect3DTexture9*>(texture)->GetLevelDesc(0,&td);
        DWORD coordinate=0,transform=0;device->GetTextureStageState(stage,D3DTSS_TEXCOORDINDEX,&coordinate);
        device->GetTextureStageState(stage,D3DTSS_TEXTURETRANSFORMFLAGS,&transform);
        IDirect3DVertexBuffer9* buffer=nullptr;UINT offset=0,stride=0;
        if(stage<4)device->GetStreamSource(stage,&buffer,&offset,&stride);
        Log("menu trace: stage=%d texture=%d %ux%u fmt=%u usage=0x%X coord=%u transform=%u stream=%p offset=%u stride=%u",
            stage,index,td.Width,td.Height,td.Format,td.Usage,coordinate,transform,buffer,offset,stride);
        if(buffer)buffer->Release();if(texture)texture->Release();
    }
    float constants[8][4]{};device->GetPixelShaderConstantF(0,&constants[0][0],8);
    for(int i=0;i<8;++i)Log("menu trace: pc%d %.6g %.6g %.6g %.6g",i,
        constants[i][0],constants[i][1],constants[i][2],constants[i][3]);
}
void FinishMenuDiagnostic() {
    if(!g_menuDiagnosticActive) return;
    g_menuDiagnosticActive=false;
    wchar_t directory[MAX_PATH]{};
    GetModuleFileNameW(reinterpret_cast<HMODULE>(&__ImageBase),directory,MAX_PATH);
    if(wchar_t* slash=wcsrchr(directory,L'\\'))*(slash+1)=L'\0';
    SYSTEMTIME now{};GetLocalTime(&now);
    wchar_t name[96]{};swprintf_s(name,L"BayonettaVR_menu_%04u%02u%02u_%02u%02u%02u_%u",
        now.wYear,now.wMonth,now.wDay,now.wHour,now.wMinute,now.wSecond,g_snapshotSequence);
    wcscat_s(directory,name);
    const bool folderReady=CreateDirectoryW(directory,nullptr)!=FALSE;
    HMODULE d3dx=GetModuleHandleW(L"d3dx9_43.dll");
    using SaveTextureFn=HRESULT(WINAPI*)(LPCWSTR,int,IDirect3DBaseTexture9*,const PALETTEENTRY*);
    const auto save=d3dx ? reinterpret_cast<SaveTextureFn>(GetProcAddress(d3dx,"D3DXSaveTextureToFileW")) : nullptr;
    for(size_t i=0;i<g_menuDiagnosticTextures.size();++i) {
        wchar_t path[MAX_PATH]{};swprintf_s(path,L"%s\\texture%02zu.png",directory,i);
        const HRESULT hr=folderReady && save ? save(path,3,g_menuDiagnosticTextures[i].texture,nullptr) : E_FAIL;
        Log("menu trace: saved texture=%zu status=0x%08X",i,hr);
        g_menuDiagnosticTextures[i].texture->Release();
    }
    auto saveShaders=[&](auto& shaders,const wchar_t* kind) {
        for(size_t i=0;i<shaders.size();++i) {
            UINT bytes=0;shaders[i]->GetFunction(nullptr,&bytes);
            std::vector<DWORD> code((bytes+3)/4);
            if(folderReady && bytes && SUCCEEDED(shaders[i]->GetFunction(code.data(),&bytes))) {
                const auto assembly=DisassembleShader(code.data());
                wchar_t path[MAX_PATH]{};swprintf_s(path,L"%s\\%s%02zu.asm",directory,kind,i);
                HANDLE file=CreateFileW(path,GENERIC_WRITE,FILE_SHARE_READ,nullptr,CREATE_ALWAYS,FILE_ATTRIBUTE_NORMAL,nullptr);
                if(file!=INVALID_HANDLE_VALUE){DWORD written=0;WriteFile(file,assembly.data(),DWORD(assembly.size()),&written,nullptr);CloseHandle(file);}
            }
            shaders[i]->Release();
        }
        shaders.clear();
    };
    saveShaders(g_menuDiagnosticVS,L"vs");saveShaders(g_menuDiagnosticPS,L"ps");
    Log("menu trace: complete draws=%u textures=%zu directory=%ls (textures saved at stereo-pair boundary)",
        g_menuDiagnosticDraw,g_menuDiagnosticTextures.size(),directory);
    g_menuDiagnosticTextures.clear();
}

int FindDp4Matrix(const std::string& text, const std::string& target) {
    for (int base = 0; base < 256; ++base) {
        bool all = true;
        const char components[4] = {'x','y','z','w'};
        for (int i = 0; i < 4; ++i) {
            char needle[80]{};
            sprintf_s(needle, "dp4 %s.%c,", target.c_str(), components[i]);
            char constant[24]{};
            sprintf_s(constant, ", c%d", base + i);
            bool componentMatched = false;
            size_t pos = 0;
            while ((pos = text.find(needle, pos)) != std::string::npos) {
                const size_t end = text.find('\n', pos);
                const size_t hit = text.find(constant, pos);
                if (hit != std::string::npos &&
                    (end == std::string::npos || hit < end)) {
                    componentMatched = true;
                    break;
                }
                pos += strlen(needle);
            }
            if (!componentMatched) {
                all = false;
                break;
            }
        }
        if (all) return base;
    }
    return -1;
}

int FindNamedCameraMatrix(const std::string& text) {
    return NamedSceneCameraMatrix(text);
}

int FindPositionMatrix(const std::string& text) {
    if (text.empty()) return -1;

    // Bayonetta embeds D3DX constant-table names. These cover its skinned
    // shaders, where c8 (g_ViewProjection) is folded into temporary rows and
    // therefore never appears in the final dp4 quartet directly.
    const int namedMatrix = FindNamedCameraMatrix(text);
    if (namedMatrix == -2) return -1; // Light camera; preserve its projection.
    if (namedMatrix >= 0) return namedMatrix;

    std::string output = "oPos";
    size_t decl = text.find("dcl_position o");
    if (decl != std::string::npos) {
        char reg[16]{};
        if (sscanf_s(text.c_str() + decl, "dcl_position %15s", reg, static_cast<unsigned>(_countof(reg))) == 1) {
            output = reg;
            while (!output.empty() && (output.back() == '\r' || output.back() == '\n')) output.pop_back();
        }
    }

    std::vector<std::string> targets{BaseRegister(output)};
    // Shader model 3 compilers often calculate position into a temporary and
    // copy it to the declared output at the end. Follow those simple moves.
    size_t lineStart = 0;
    while (lineStart < text.size()) {
        const size_t lineEnd = text.find('\n', lineStart);
        const std::string line = text.substr(lineStart,
            lineEnd == std::string::npos ? std::string::npos : lineEnd - lineStart);
        char destination[32]{}, source[32]{};
        if (sscanf_s(line.c_str(), " mov %31[^,], %31s", destination,
                     static_cast<unsigned>(_countof(destination)), source,
                     static_cast<unsigned>(_countof(source))) == 2 &&
            BaseRegister(destination) == targets[0]) {
            const std::string sourceBase = BaseRegister(source);
            if (!sourceBase.empty() && sourceBase[0] == 'r') targets.push_back(sourceBase);
        }
        if (lineEnd == std::string::npos) break;
        lineStart = lineEnd + 1;
    }

    for (const std::string& target : targets) {
        char pattern[64]{};
        sprintf_s(pattern, "m4x4 %s,", target.c_str());
        const size_t at = text.find(pattern);
        if (at != std::string::npos) {
            const char* constant = strstr(text.c_str() + at, ", c");
            if (constant) return atoi(constant + 3);
        }
        const int base = FindDp4Matrix(text, target);
        if (base >= 0) return base;
    }
    return -1;
}

void DumpShaderDiagnostics() {
    std::vector<std::pair<IDirect3DVertexShader9*, ShaderInfo*>> shaders;
    shaders.reserve(g_shaders.size());
    for (auto& entry : g_shaders) shaders.push_back({entry.first, &entry.second});
    std::sort(shaders.begin(), shaders.end(), [](const auto& a, const auto& b) {
        return a.second->draws > b.second->draws;
    });
    Log("shader stats: %zu shaders, top draw counts follow", shaders.size());
    const size_t count = std::min<size_t>(16, shaders.size());
    for (size_t i = 0; i < count; ++i) {
        const auto* info = shaders[i].second;
        Log("shader stats: rank=%zu ptr=%p draws=%llu hud=%llu matrix=c%d", i + 1,
            shaders[i].first, info->draws, info->hudDraws, info->matrixBase);
        if (info->assembly.empty()) continue;
        wchar_t path[MAX_PATH]{};
        GetModuleFileNameW(reinterpret_cast<HMODULE>(&__ImageBase), path, MAX_PATH);
        if (wchar_t* slash = wcsrchr(path, L'\\')) *(slash + 1) = L'\0';
        wchar_t name[96]{};
        swprintf_s(name, L"BayonettaVR_shader_rank%02zu_c%d.asm", i + 1, info->matrixBase);
        wcscat_s(path, name);
        HANDLE file = CreateFileW(path, GENERIC_WRITE, FILE_SHARE_READ, nullptr,
                                  CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
        if (file != INVALID_HANDLE_VALUE) {
            DWORD written = 0;
            WriteFile(file, info->assembly.data(), static_cast<DWORD>(info->assembly.size()),
                      &written, nullptr);
            CloseHandle(file);
        }
    }
}

bool IsHudDraw(IDirect3DDevice9* device, const D3DVIEWPORT9& viewport) {
    // Orthographic matrices also occur in shadow maps and postprocessing.
    // Restrict the panel to blended/alpha-tested, depth-disabled UI on the
    // final backbuffer. Never move a render-target texture's fullscreen blit.
    DWORD depth = 0, blend = 0, alphaTest = 0;
    if (FAILED(device->GetRenderState(D3DRS_ZENABLE, &depth)) || depth != D3DZB_FALSE ||
        FAILED(device->GetRenderState(D3DRS_ALPHABLENDENABLE, &blend)) ||
        FAILED(device->GetRenderState(D3DRS_ALPHATESTENABLE, &alphaTest)) ||
        (!blend && !alphaTest)) return false;
    IDirect3DSurface9* target = nullptr;
    IDirect3DSurface9* backbuffer = nullptr;
    const HRESULT targetResult = device->GetRenderTarget(0, &target);
    const HRESULT backResult = device->GetBackBuffer(0, 0, D3DBACKBUFFER_TYPE_MONO, &backbuffer);
    D3DSURFACE_DESC desc{};
    const bool finalTarget = SUCCEEDED(targetResult) && SUCCEEDED(backResult) &&
        target && target == backbuffer && SUCCEEDED(target->GetDesc(&desc)) &&
        viewport.X == 0 && viewport.Y == 0 && viewport.Width == desc.Width &&
        viewport.Height == desc.Height;
    if (target) target->Release();
    if (backbuffer) backbuffer->Release();
    if (!finalTarget) return false;
    IDirect3DBaseTexture9* texture = nullptr;
    if (FAILED(device->GetTexture(0, &texture))) return false;
    bool renderTexture = false;
    if (texture) {
        renderTexture = texture->GetType() != D3DRTYPE_TEXTURE;
        if (!renderTexture) {
            D3DSURFACE_DESC textureDesc{};
            renderTexture = FAILED(static_cast<IDirect3DTexture9*>(texture)->GetLevelDesc(0, &textureDesc)) ||
                (textureDesc.Usage & (D3DUSAGE_RENDERTARGET | D3DUSAGE_DEPTHSTENCIL)) != 0;
        }
        texture->Release();
    }
    return !renderTexture;
}

template<class Call>
HRESULT DrawWithTrackedCamera(IDirect3DDevice9* device, UINT primitiveCount, Call&& call) {
    if(g_nativeSceneTrace.Busy() || g_mapCinema.Busy())return call();
    if(g_insideNativeDraw && VR().Eye()==0 && Cfg().worldLockedMapSequence &&
       !g_compositingMenu && !g_capturingMovie && g_capturingMenu<0)
        g_mapCinema.Observe(device,primitiveCount);
    const bool ambientPass=g_ambientOcclusionShader.ShouldSkip(device,Cfg().enabled&&VR().Active());
    AmbientOcclusionBypass ambient(device,ambientPass&&g_disableAmbientOcclusion);
    if(ambientPass) {
        static unsigned seen=0;
        if(++seen<=4||seen%600==0)Log("ambient shadows: draws=%u bypass=%d",seen,ambient.Active());
    }
    if(g_motionBlurFilter.ShouldSkip(device,Cfg().enabled&&VR().Active()&&g_disableMotionBlur)) {
        static unsigned skipped=0;
        if(++skipped<=4||skipped%600==0)Log("motion blur: suppressed draws=%u",skipped);
        return S_OK;
    }
    if(g_dofFilter.ShouldSkip(device,Cfg().enabled&&Cfg().disableDepthOfField)) {
        static unsigned skipped=0;
        if(++skipped<=4||skipped%600==0)Log("dof: suppressed draws=%u",skipped);
        return S_OK;
    }
    if (g_compositingMenu || g_flatMovieFrame) return call();
    if (g_capturingMovie) return g_moviePanel.CaptureDraw(device,call);
    if (g_capturingMenu >= 0) {
        RecordMenuDraw(device);
        return g_menuPanel.CaptureDraw(device, g_capturingMenu, call);
    }
    SoftParticleDepthScope softDepth(device,g_softParticleDepth,
        Cfg().enabled && VR().Active() && g_insideNativeDraw && g_fixSoftParticleDepth);
    if(softDepth.Active()) {
        static unsigned draws[2]{};
        auto& count=draws[VR().Eye()];
        if(++count<=2 || count%1200==0)Log("soft particle depth: eye=%d corrected draws=%u",VR().Eye(),count);
    }
    int base = Cfg().matrixRegisterOverride;
    ShaderInfo* shaderInfo = nullptr;
    IDirect3DVertexShader9* liveShader = nullptr;
    if (SUCCEEDED(device->GetVertexShader(&liveShader)) && liveShader) {
        auto it = g_shaders.find(liveShader);
        if (it != g_shaders.end()) {
            shaderInfo = &it->second;
            ++shaderInfo->draws;
            base = shaderInfo->matrixBase;
        }
    }
    if (liveShader) liveShader->Release();
    if (!VR().Active()) return call();
    if (base < 0 || base > 252) {
        if(g_insideNativeDraw && VR().Eye()==0 && g_nativeSceneTrace.Active())
            g_nativeSceneTrace.Record(device,primitiveCount,-1,Identity(),true);
        return call();
    }

    if (g_stackCapturePending) {
        g_stackCapturePending = false;
        DumpRenderStack();
    }

    Mat4 original{};
    // Effects and state blocks can restore constants without calling the
    // public Set* entry in the device vtable. Read the live device state at the
    // draw boundary; this is the authoritative matrix the shader will use.
    if (FAILED(device->GetVertexShaderConstantF(static_cast<UINT>(base), &original.m[0][0], 4))) return call();
    D3DVIEWPORT9 viewport{};
    float aspect = 16.0f / 9.0f;
    if (SUCCEEDED(device->GetViewport(&viewport)) && viewport.Height) aspect = float(viewport.Width) / float(viewport.Height);
    const bool orthographic = LooksOrthographic(original);
    if(g_insideNativeDraw && VR().Eye()==0 && g_nativeSceneTrace.Active())
        g_nativeSceneTrace.Record(device,primitiveCount,base,original,orthographic);
    bool hud = false;
    if (orthographic) {
        if (Cfg().worldLockedHud) {
            if (g_menuHooksInstalled) return call(); // Dedicated Id/IdFont capture owns UI.
            // With native stereo, only change the draws captured into each
            // eye. Do not project any later desktop-only UI a second time.
            hud = (!Cfg().nativeStereo || g_insideNativeDraw) && viewport.Width &&
                viewport.Height && IsHudDraw(device, viewport);
            if (!hud) return call();
        } else if (!Cfg().applyToUi) return call();
    }
    VR().EnsureOrigin();
    Mat4 clipTransform{};
    bool cached = false;
    if (g_insideNativeDraw && viewport.Height) {
        const int eye = VR().Eye();
        for (int i = 0; i < g_clipCacheCount[eye]; ++i) {
            if (g_clipCache[eye][i].width == viewport.Width &&
                g_clipCache[eye][i].height == viewport.Height &&
                g_clipCache[eye][i].hud == hud) {
                clipTransform = g_clipCache[eye][i].transform;
                cached = true;
                break;
            }
        }
        if (!cached) {
            clipTransform = hud ? VR().HudClipTransform(aspect) : VR().ClipTransform(aspect);
            cached = true;
            if (g_clipCacheCount[eye] < static_cast<int>(_countof(g_clipCache[eye]))) {
                auto& entry = g_clipCache[eye][g_clipCacheCount[eye]++];
                entry.width = viewport.Width;
                entry.height = viewport.Height;
                entry.hud = hud;
                entry.transform = clipTransform;
            }
        }
    }
    if (!cached) clipTransform = hud ? VR().HudClipTransform(aspect) : VR().ClipTransform(aspect);
    DWORD scissorEnabled = FALSE;
    RECT oldScissor{};
    bool restoreScissor = false;
    if (hud) {
        if (FAILED(device->GetRenderState(D3DRS_SCISSORTESTENABLE, &scissorEnabled))) return call();
        if (scissorEnabled) {
            if (FAILED(device->GetScissorRect(&oldScissor))) return call();
            const float width = float(viewport.Width), height = float(viewport.Height);
            float bounds[4]{};
            ProjectHudScissor(clipTransform,
                2.0f*float(oldScissor.left)/width - 1.0f,
                1.0f - 2.0f*float(oldScissor.bottom)/height,
                2.0f*float(oldScissor.right)/width - 1.0f,
                1.0f - 2.0f*float(oldScissor.top)/height, bounds);
            const RECT projected{
                LONG(std::floor((bounds[0]+1.0f)*0.5f*width)),
                LONG(std::floor((1.0f-bounds[3])*0.5f*height)),
                LONG(std::ceil((bounds[2]+1.0f)*0.5f*width)),
                LONG(std::ceil((1.0f-bounds[1])*0.5f*height))};
            if (projected.left >= projected.right || projected.top >= projected.bottom) return D3D_OK;
            if (FAILED(device->SetScissorRect(&projected))) return call();
            restoreScissor = true;
        }
    }
    const Mat4 adjusted = Mul(clipTransform, original);
    if (FAILED(g_setConst(device, static_cast<UINT>(base), &adjusted.m[0][0], 4))) {
        if (restoreScissor) device->SetScissorRect(&oldScissor);
        return call();
    }
    ++g_adjustedDraws;
    if (hud) {
        ++g_hudDraws;
        if (shaderInfo && shaderInfo->hudDraws++ == 0)
            Log("hud: projecting shader=%p matrix=c%d onto recenter-space panel", liveShader, base);
    }
    const HRESULT result = call();
    g_setConst(device, static_cast<UINT>(base), &original.m[0][0], 4);
    if (restoreScissor) device->SetScissorRect(&oldScissor);
    return result;
}

bool EnsureCapture(IDirect3DDevice9* device, IDirect3DSurface9* backbuffer) {
    D3DSURFACE_DESC desc{};
    if (FAILED(backbuffer->GetDesc(&desc))) return false;
    if (g_d3d11 && desc.Width == g_width && desc.Height == g_height) return true;
    ReleaseTextures();

    IDXGIAdapter* matchingAdapter = nullptr;
    IDXGIFactory1* factory = nullptr;
    const int vrAdapter = VR().DXGIAdapterIndex();
    if (vrAdapter >= 0 && SUCCEEDED(CreateDXGIFactory1(__uuidof(IDXGIFactory1), reinterpret_cast<void**>(&factory)))) {
        IDXGIAdapter1* candidate = nullptr;
        if (SUCCEEDED(factory->EnumAdapters1(static_cast<UINT>(vrAdapter), &candidate))) matchingAdapter = candidate;
        factory->Release();
    }

    D3D_FEATURE_LEVEL level{};
    UINT flags = D3D11_CREATE_DEVICE_BGRA_SUPPORT;
    HRESULT hr = D3D11CreateDevice(matchingAdapter,
                                   matchingAdapter ? D3D_DRIVER_TYPE_UNKNOWN : D3D_DRIVER_TYPE_HARDWARE,
                                   nullptr, flags,
                                   nullptr, 0, D3D11_SDK_VERSION, &g_d3d11, &level, &g_context11);
    if (matchingAdapter) matchingAdapter->Release();
    if (FAILED(hr)) { Log("capture: D3D11CreateDevice failed 0x%08X", hr); return false; }
    auto releaseEyes = [] {
        for (auto& eye : g_eyes) {
            for (auto*& texture : eye.submit11) {
                if (texture) texture->Release();
                texture = nullptr;
            }
            if (eye.shared11) eye.shared11->Release();
            if (eye.readback) eye.readback->Release();
            if (eye.d3d9) eye.d3d9->Release();
            eye = {};
        }
    };

    // D3D9Ex resources can be opened directly by D3D11 when both devices are
    // on the same adapter. This removes two full-resolution GPU->CPU->GPU
    // transfers from every native stereo frame.
    IDirect3DDevice9Ex* deviceEx = nullptr;
    if (SUCCEEDED(device->QueryInterface(__uuidof(IDirect3DDevice9Ex),
                                         reinterpret_cast<void**>(&deviceEx))) && deviceEx) {
        g_gpuShared = true;
        for (int i = 0; i < 2 && g_gpuShared; ++i) {
            HANDLE sharedHandle = nullptr;
            hr = deviceEx->CreateTexture(desc.Width, desc.Height, 1, D3DUSAGE_RENDERTARGET,
                                         D3DFMT_A8R8G8B8, D3DPOOL_DEFAULT,
                                         &g_eyes[i].d3d9, &sharedHandle);
            if (FAILED(hr) || !sharedHandle) {
                Log("capture: shared D3D9 eye texture %d failed 0x%08X", i, hr);
                g_gpuShared = false;
                break;
            }
            hr = g_d3d11->OpenSharedResource(sharedHandle, __uuidof(ID3D11Texture2D),
                                              reinterpret_cast<void**>(&g_eyes[i].shared11));
            if (FAILED(hr) || !g_eyes[i].shared11) {
                Log("capture: D3D11 OpenSharedResource eye %d failed 0x%08X", i, hr);
                g_gpuShared = false;
                break;
            }
            D3D11_TEXTURE2D_DESC td{};
            td.Width=desc.Width; td.Height=desc.Height; td.MipLevels=1; td.ArraySize=1;
            td.Format=DXGI_FORMAT_B8G8R8A8_UNORM; td.SampleDesc.Count=1;
            td.Usage=D3D11_USAGE_DEFAULT;
            td.BindFlags=D3D11_BIND_SHADER_RESOURCE | D3D11_BIND_RENDER_TARGET;
            for (int slot = 0; slot < kSubmitRingSize; ++slot) {
                hr = g_d3d11->CreateTexture2D(&td, nullptr, &g_eyes[i].submit11[slot]);
                if (FAILED(hr) || !g_eyes[i].submit11[slot]) {
                    Log("capture: D3D11 submit texture eye %d slot %d failed 0x%08X",
                        i, slot, hr);
                    g_gpuShared = false;
                    break;
                }
            }
            // Retain a system-memory surface only for requested diagnostics;
            // it is not touched in the normal frame path.
            device->CreateOffscreenPlainSurface(desc.Width, desc.Height, D3DFMT_A8R8G8B8,
                                                D3DPOOL_SYSTEMMEM, &g_eyes[i].readback, nullptr);
        }
        deviceEx->Release();
        if (!g_gpuShared) releaseEyes();
    }

    if (!g_gpuShared) {
        for (int i = 0; i < 2; ++i) {
            hr = device->CreateTexture(desc.Width, desc.Height, 1, D3DUSAGE_RENDERTARGET,
                                       D3DFMT_A8R8G8B8, D3DPOOL_DEFAULT,
                                       &g_eyes[i].d3d9, nullptr);
            if (FAILED(hr)) {
                Log("capture: D3D9 eye texture %d failed 0x%08X", i, hr);
                ReleaseTextures();
                return false;
            }
            hr = device->CreateOffscreenPlainSurface(desc.Width, desc.Height, D3DFMT_A8R8G8B8,
                                                      D3DPOOL_SYSTEMMEM,
                                                      &g_eyes[i].readback, nullptr);
            if (FAILED(hr)) {
                Log("capture: D3D9 readback %d failed 0x%08X", i, hr);
                ReleaseTextures();
                return false;
            }
            D3D11_TEXTURE2D_DESC td{};
            td.Width=desc.Width; td.Height=desc.Height; td.MipLevels=1; td.ArraySize=1;
            td.Format=DXGI_FORMAT_B8G8R8A8_UNORM; td.SampleDesc.Count=1;
            td.Usage=D3D11_USAGE_DEFAULT;
            td.BindFlags=D3D11_BIND_SHADER_RESOURCE | D3D11_BIND_RENDER_TARGET;
            for (int slot = 0; slot < kSubmitRingSize; ++slot) {
                hr = g_d3d11->CreateTexture2D(&td, nullptr, &g_eyes[i].submit11[slot]);
                if (FAILED(hr)) {
                    Log("capture: D3D11 eye texture %d slot %d failed 0x%08X",
                        i, slot, hr);
                    ReleaseTextures();
                    return false;
                }
            }
        }
    }
    D3D11_TEXTURE2D_DESC cleanDesc{};
    cleanDesc.Width = cleanDesc.Height = kCleanGridSize;
    cleanDesc.MipLevels = cleanDesc.ArraySize = cleanDesc.SampleDesc.Count = 1;
    cleanDesc.Format = DXGI_FORMAT_B8G8R8A8_UNORM;
    cleanDesc.Usage = D3D11_USAGE_DEFAULT;
    cleanDesc.BindFlags = D3D11_BIND_SHADER_RESOURCE | D3D11_BIND_RENDER_TARGET;
    for (int eye = 0; eye < 2; ++eye) {
        for (int slot = 0; slot < kSubmitRingSize; ++slot) {
            hr = g_d3d11->CreateTexture2D(&cleanDesc, nullptr, &g_cleanGrid[eye][slot]);
            if (FAILED(hr) || !g_cleanGrid[eye][slot]) {
                Log("capture: clean grid texture eye %d slot %d failed 0x%08X", eye, slot, hr);
                ReleaseTextures();
                return false;
            }
        }
    }
    // Compile and create the private grid pipeline before the user enables F5.
    // This draw lands in an unused clean texture and is cleared before submit.
    hr = g_referenceRenderer.Draw(g_d3d11, g_context11, g_cleanGrid[0][0], Identity());
    if (FAILED(hr)) {
        Log("capture: clean grid shader initialization failed 0x%08X", hr);
        ReleaseTextures();
        return false;
    }
    g_width = desc.Width; g_height = desc.Height;
    Log("capture: %s eye textures ready at %ux%u plus clean %ux%u grid, VR adapter=%d feature level=0x%X",
        g_gpuShared ? "GPU-shared" : "CPU-bridged", g_width, g_height,
        kCleanGridSize, kCleanGridSize, vrAdapter, level);
    return true;
}

HRESULT SynchronizeD3D9(IDirect3DDevice9* device) {
    IDirect3DQuery9* fence = nullptr;
    HRESULT status=device->CreateQuery(D3DQUERYTYPE_EVENT,&fence);
    if(FAILED(status)||!fence)return FAILED(status)?status:E_FAIL;
    status=fence->Issue(D3DISSUE_END);
    const ULONGLONG deadline=GetTickCount64()+2000;
    if(SUCCEEDED(status))while((status=fence->GetData(nullptr,0,D3DGETDATA_FLUSH))==S_FALSE){
        if(GetTickCount64()>=deadline){status=HRESULT_FROM_WIN32(WAIT_TIMEOUT);break;}
        SwitchToThread();
    }
    fence->Release();return status;
}

HRESULT SynchronizeD3D11Copies() {
    if(!g_d3d11||!g_context11)return E_FAIL;
    const HRESULT status=FinishGpu(g_d3d11,g_context11);
    static bool logged=false;
    if(!logged){Log("capture: D3D11 copy completion fence active (status=0x%08X)",status);logged=true;}
    return status;
}

HRESULT CaptureEyeSurface(IDirect3DDevice9* device, IDirect3DSurface9* backbuffer, int eye) {
    if (!device || !backbuffer || eye < 0 || eye > 1 || !EnsureCapture(device, backbuffer))
        return E_INVALIDARG;

    IDirect3DSurface9* target = nullptr;
    HRESULT result = g_eyes[eye].d3d9->GetSurfaceLevel(0, &target);
    if (SUCCEEDED(result) && target) {
        result = device->StretchRect(backbuffer, nullptr, target, nullptr, D3DTEXF_NONE);
        target->Release();
    }
    g_eyes[eye].ready = SUCCEEDED(result);

    const bool diagnosticReadback = g_snapshotPending && !g_snapshotSaved[eye];
    const bool needsCpuTransfer = !g_gpuShared || diagnosticReadback;
    HRESULT readbackResult = E_FAIL;
    if (g_eyes[eye].ready && needsCpuTransfer && g_eyes[eye].readback) {
        // Normal shared-texture frames synchronize once after both eyes. A
        // CPU fallback or explicit screenshot must complete before LockRect.
        const HRESULT sync=SynchronizeD3D9(device);
        if(FAILED(sync)){g_eyes[eye].ready=false;return sync;}
        IDirect3DSurface9* gpuSurface = nullptr;
        if (SUCCEEDED(g_eyes[eye].d3d9->GetSurfaceLevel(0, &gpuSurface)) && gpuSurface) {
            readbackResult = device->GetRenderTargetData(gpuSurface, g_eyes[eye].readback);
            gpuSurface->Release();
        }
        D3DLOCKED_RECT locked{};
        if (SUCCEEDED(readbackResult) &&
            SUCCEEDED(g_eyes[eye].readback->LockRect(&locked, nullptr, D3DLOCK_READONLY))) {
            g_eyes[eye].pixelHash = HashPixels(locked, g_width, g_height);
            if (g_snapshotPending && !g_snapshotSaved[eye]) {
                g_snapshotSaved[eye] = SaveEyeBitmap(eye, locked, g_width, g_height);
                Log("capture: eye %d snapshot %s hash=%016llX", eye,
                    g_snapshotSaved[eye] ? "saved" : "failed", g_eyes[eye].pixelHash);
            }
            if (!g_gpuShared) {
                g_context11->UpdateSubresource(g_eyes[eye].submit11[g_submitSlot], 0, nullptr, locked.pBits,
                                               static_cast<UINT>(locked.Pitch), 0);
            }
            g_eyes[eye].readback->UnlockRect();
        } else {
            g_eyes[eye].ready = false;
        }
    }
    if (!g_gpuShared && g_context11) g_context11->Flush();
    VR().MarkEyeRendered(eye);
    return g_gpuShared ? result : readbackResult;
}

HRESULT CaptureEye(IDirect3DDevice9* device, int eye) {
    IDirect3DSurface9* backbuffer = nullptr;
    if (!device || FAILED(device->GetBackBuffer(0, 0, D3DBACKBUFFER_TYPE_MONO, &backbuffer)) ||
        !backbuffer) return E_FAIL;
    const HRESULT result = CaptureEyeSurface(device, backbuffer, eye);
    backbuffer->Release();
    return result;
}

using OtDrawFn = void (__thiscall*)(void* self);
OtDrawFn g_otDraw = nullptr;
unsigned char* g_otDrawTarget = nullptr;
void* g_otDrawTrampoline = nullptr;
unsigned char g_otDrawOriginal[6]{};
bool CaptureAndSubmit(IDirect3DDevice9* device, bool early);

using GameThisCall = void (__thiscall*)(void*);
using GameCall = void (__cdecl*)();
GameThisCall g_drawMenuList = nullptr;
GameThisCall g_drawMovieList = nullptr;
GameThisCall g_rotateUiBuffers = nullptr, g_rotateFontBuffers = nullptr, g_releaseUiTextures = nullptr;
GameCall g_releaseMaterials = nullptr;
GameThisCall g_clearEffectStream = nullptr;
EffectStreamReplay<> g_effectStreams;
bool g_effectHooksInstalled = false;
unsigned long long g_effectStreamsRestored = 0, g_effectReplayFrames = 0;

void __fastcall HookClearEffectStream(void* stream, void*) {
    if (g_insideNativeDraw && !g_flatMovieFrame && VR().Eye() == 0 && !g_effectStreams.Remember(stream)) {
        static bool logged = false;
        if (!logged) { Log("effects: replay capacity exceeded; excess streams use original behavior"); logged = true; }
    }
    // Clear in BOTH eyes: subsequent passes in this eye must still see the
    // engine's consumed state. Restore only at the boundary between eyes.
    g_clearEffectStream(stream);
}

void DrawMenuList(void* self, int layer) {
    if (g_flatMovieFrame || !g_insideNativeDraw || !Cfg().worldLockedHud || !g_device) { g_drawMenuList(self); return; }
    if (VR().Eye() == 0 || !g_menuPanel.Ready(layer)) {
        if (!g_menuPanel.Begin(g_device, layer)) {
            static bool logged = false;
            if (!logged) { Log("menu: capture allocation failed; rendering original UI in both eyes"); logged = true; }
            g_drawMenuList(self); return;
        }
        g_capturingMenu = layer;
        g_drawMenuList(self);
        g_capturingMenu = -1;
        g_menuPanel.End(g_device, layer);
        if(g_menuDiagnosticActive && VR().Eye()==0) for(int basis=0;basis<2;++basis)
            Log("menu trace: output layer=%d basis=%d texture=%d",layer,basis,
                QueueMenuTexture(g_menuPanel.CaptureTexture(layer,basis),true));
        ++g_menuCaptures[layer];
    }
    VR().EnsureOrigin();
    g_compositingMenu = true;
    const HRESULT hr = g_menuPanel.Draw(g_device, layer, VR().HudClipTransform(g_menuPanel.Aspect()));
    g_compositingMenu = false;
    if (SUCCEEDED(hr)) ++g_menuEyeDraws[VR().Eye()];
    else Log("menu: layer=%d eye=%d composite failed 0x%08X", layer, VR().Eye(), hr);
}
void __fastcall HookIdList(void* self, void*) { DrawMenuList(self, 0); }
void __fastcall HookIdFontList(void* self, void*) { DrawMenuList(self, 1); }
void __fastcall HookMovieList(void* self, void*) {
    if(g_flatMovieFrame || !g_insideNativeDraw || !Cfg().worldLockedMovies || !g_device){g_drawMovieList(self);return;}
    if(VR().Eye()==0) {
        g_movieCaptureFailed=!g_moviePanel.Begin(g_device);
        if(!g_movieCaptureFailed) {
            g_capturingMovie=true;
            g_drawMovieList(self);
            g_capturingMovie=false;
            g_moviePanel.End(g_device);
        } else {
            static bool logged=false;
            if(!logged){Log("movies: capture allocation failed; using original renderer");logged=true;}
        }
    }
    if(g_movieCaptureFailed){g_drawMovieList(self);return;}
    if(!g_moviePanel.Ready())return; // Empty movie pass must not cover gameplay/UI.
    VR().EnsureOrigin();
    g_compositingMenu=true;
    const HRESULT hr=g_moviePanel.Draw(g_device,VR().MovieClipTransform(g_moviePanel.Aspect()));
    g_compositingMenu=false;
    static unsigned draws[2]{};
    if(SUCCEEDED(hr)) {
        auto& count=draws[VR().Eye()];
        if(++count<=2||count%600==0)Log("movies: eye=%d shared-frame screen draws=%u",VR().Eye(),count);
    } else Log("movies: eye=%d screen draw failed 0x%08X",VR().Eye(),hr);
}
bool FinishStereoFrame() { return g_flatMovieFrame || !g_insideNativeDraw || VR().Eye() == 1; }
void __fastcall HookRotateUiBuffers(void* self, void*) { if (FinishStereoFrame()) g_rotateUiBuffers(self); }
void __fastcall HookRotateFontBuffers(void* self, void*) { if (FinishStereoFrame()) g_rotateFontBuffers(self); }
void __fastcall HookReleaseUiTextures(void* self, void*) { if (FinishStereoFrame()) g_releaseUiTextures(self); }
void __cdecl HookReleaseMaterials() { if (FinishStereoFrame()) g_releaseMaterials(); }

struct MenuCallPatch { unsigned char* address = nullptr; unsigned char original[5]{}; };
// Only post-draw clears in seven cEspDraw variants. Constructor/allocation
// calls to the same reset function must retain their normal behavior.
struct EffectClearSite { uintptr_t rva; unsigned char opcode; };
constexpr EffectClearSite kEffectClearSites[] = {
    {0x163803,0xE8},{0x16380D,0xE9},
    {0x1680C3,0xE8},{0x1680CD,0xE9},
    {0x16AF42,0xE8},{0x16AF49,0xE8},{0x16AF54,0xE9},
    {0x16BE69,0xE8},{0x16BE74,0xE8},{0x16BE7B,0xE8},{0x16BE86,0xE9},
    {0x16CD39,0xE8},{0x16CD44,0xE8},{0x16CD4B,0xE8},{0x16CD56,0xE9},
    {0x16E988,0xE8},{0x16E98F,0xE8},
    {0x175AE6,0xE8},{0x175AF1,0xE8},{0x175AF8,0xE8},{0x175B03,0xE9}
};
MenuCallPatch g_effectPatches[_countof(kEffectClearSites)]{};
void RemoveEffectHooks() {
    g_effectStreams.Reset();
    for (auto& patch : g_effectPatches) if (patch.address) {
        DWORD old = 0;
        if (VirtualProtect(patch.address, 5, PAGE_EXECUTE_READWRITE, &old)) {
            memcpy(patch.address, patch.original, 5);
            DWORD ignored = 0; VirtualProtect(patch.address, 5, old, &ignored);
            FlushInstructionCache(GetCurrentProcess(), patch.address, 5);
        }
        patch = {};
    }
    g_effectHooksInstalled = false;
}
bool InstallEffectHooks(unsigned char* image) {
    // Verify the complete reset function as well as every CALL/tail-JMP.
    const unsigned char resetCode[] = {0x33,0xC0,0x89,0x01,0x89,0x41,0x04,
        0x89,0x41,0x08,0x89,0x41,0x0C,0x89,0x41,0x10,0x89,0x41,0x14,
        0x89,0x41,0x18,0xC7,0x41,0x1C,0x00,0x10,0x00,0x00,0xC3};
    auto* reset = image + 0x85F7A0;
    if (memcmp(reset, resetCode, sizeof(resetCode)) != 0) {
        Log("effects: stream reset signature mismatch"); return false;
    }
    for (const auto& entry : kEffectClearSites) {
        auto* site = image + entry.rva;
        int32_t displacement = 0; memcpy(&displacement, site + 1, 4);
        if (site[0] != entry.opcode || site + 5 + displacement != reset) {
            Log("effects: clear signature mismatch at RVA 0x%X", unsigned(entry.rva)); return false;
        }
    }
    g_clearEffectStream = reinterpret_cast<GameThisCall>(reset);
    for (size_t i = 0; i < _countof(kEffectClearSites); ++i) {
        auto* site = image + kEffectClearSites[i].rva;
        DWORD old = 0;
        if (!VirtualProtect(site, 5, PAGE_EXECUTE_READWRITE, &old)) {
            RemoveEffectHooks(); Log("effects: could not patch stream clear"); return false;
        }
        auto& patch = g_effectPatches[i]; patch.address = site; memcpy(patch.original, site, 5);
        const int32_t displacement = static_cast<int32_t>(
            reinterpret_cast<unsigned char*>(&HookClearEffectStream) - (site + 5));
        memcpy(site + 1, &displacement, 4); // Preserve CALL versus tail-JMP.
        DWORD ignored = 0; VirtualProtect(site, 5, old, &ignored);
        FlushInstructionCache(GetCurrentProcess(), site, 5);
    }
    g_effectHooksInstalled = true;
    Log("effects: stereo stream replay installed at %zu post-draw clear sites", _countof(kEffectClearSites));
    return true;
}
MenuCallPatch g_menuPatches[7]{};
void RemoveMenuHooks() {
    g_movieQueue=nullptr;
    for (auto& patch : g_menuPatches) if (patch.address) {
        DWORD old = 0;
        if (VirtualProtect(patch.address, 5, PAGE_EXECUTE_READWRITE, &old)) {
            memcpy(patch.address, patch.original, 5);
            DWORD ignored = 0; VirtualProtect(patch.address, 5, old, &ignored);
            FlushInstructionCache(GetCurrentProcess(), patch.address, 5);
        }
        patch = {};
    }
    g_menuHooksInstalled = false;
}
bool InstallMenuHooks(unsigned char* image) {
    // Steam 0x591AD330 cOtManager::draw: explicit Id/IdFont passes followed
    // by ClearBuffer and deferred texture/material releases. These call sites
    // are checked before any mutation; no global function hook is necessary.
    const uintptr_t sites[] = {0xE5358,0xE53E0,0xE5459,0xE5463,0xE54C3,0xE54C8,0xE5311};
    const uintptr_t targets[] = {0x8706E0,0x8706E0,0x1899E0,0x876B0,0xAE850,0x86DF50,0x8706E0};
    const void* hooks[] = {&HookIdList,&HookIdFontList,&HookRotateUiBuffers,
        &HookRotateFontBuffers,&HookReleaseUiTextures,&HookReleaseMaterials,&HookMovieList};
    // Verify the MovieDraw identity and the OT layout used by Pending before
    // reading its bank heads. These are specific to the supported Steam build.
    const unsigned char movieThis[]={0xB9,0xB8,0xDB,0xA7,0x05};
    const unsigned char otLayout[]={0x83,0xEC,0x10,0x8B,0x41,0x20,0x8B,0x51,0x28,0x53,0x8B,0x59,0x24};
    if(memcmp(image+0xE530C,movieThis,sizeof(movieThis)) ||
       memcmp(image+0x8706E0,otLayout,sizeof(otLayout))) {
        Log("movies: queue layout signature mismatch");return false;
    }
    for (int i=0;i<7;++i) {
        auto* site = image + sites[i];
        int32_t displacement = 0; memcpy(&displacement, site+1, 4);
        if (site[0] != 0xE8 || site+5+displacement != image+targets[i]) {
            Log("menu: call-site signature mismatch at RVA 0x%X", unsigned(sites[i])); return false;
        }
    }
    g_drawMenuList = reinterpret_cast<GameThisCall>(image+targets[0]);
    g_drawMovieList = reinterpret_cast<GameThisCall>(image+targets[6]);
    g_rotateUiBuffers = reinterpret_cast<GameThisCall>(image+targets[2]);
    g_rotateFontBuffers = reinterpret_cast<GameThisCall>(image+targets[3]);
    g_releaseUiTextures = reinterpret_cast<GameThisCall>(image+targets[4]);
    g_releaseMaterials = reinterpret_cast<GameCall>(image+targets[5]);
    for (int i=0;i<7;++i) {
        auto* site = image + sites[i];
        DWORD old = 0;
        if (!VirtualProtect(site, 5, PAGE_EXECUTE_READWRITE, &old)) { RemoveMenuHooks(); return false; }
        auto& patch = g_menuPatches[i]; patch.address = site; memcpy(patch.original, site, 5);
        const int32_t displacement = static_cast<int32_t>(reinterpret_cast<const unsigned char*>(hooks[i])-(site+5));
        memcpy(site+1, &displacement, 4);
        DWORD ignored = 0; VirtualProtect(site, 5, old, &ignored);
        FlushInstructionCache(GetCurrentProcess(), site, 5);
    }
    g_menuHooksInstalled = true;
    g_movieQueue=reinterpret_cast<const MovieDrawQueue*>(image+0x567DBB8);
    Log("menu: Id/IdFont dual-background capture + stereo replay installed; buffer cleanup runs once per pair");
    Log("movies: verified MovieDraw queue; complete cinematic composition captured once for both eyes");
    return true;
}

void __fastcall HookOtManagerDraw(void* self, void*) {
    if (!g_otDraw) return;
    if (g_insideNativeDraw || !Cfg().nativeStereo || !VR().Active() || !g_device) {
        g_otDraw(self);
        return;
    }

    g_insideNativeDraw = true;
    // Sample the compositor's predicted pose at the last game-level point
    // before either eye is rendered. Both eyes then use this exact pose.
    g_pipeline.waitStart = PipelineSeconds();
    const bool independent = IndependentVRGrid().OwnsFrames();
    if (!independent) VR().WaitForPose();
    g_pipeline.poseReady = PipelineSeconds();
    g_clipCacheCount[0] = g_clipCacheCount[1] = 0;
    g_menuPanel.BeginFrame();
    g_moviePanel.BeginFrame();
    g_mapCinema.BeginFrame();
    g_movieCaptureFailed=false;
    if(g_nativeSceneTracePending){
        g_nativeSceneTracePending=false;
        wchar_t path[MAX_PATH]{},name[96]{};
        GetModuleFileNameW(reinterpret_cast<HMODULE>(&__ImageBase),path,MAX_PATH);
        if(auto* slash=wcsrchr(path,L'\\'))*(slash+1)=L'\0';
        SYSTEMTIME now{};GetLocalTime(&now);
        swprintf_s(name,L"BayonettaVR_scene_%04u%02u%02u_%02u%02u%02u_%u",now.wYear,now.wMonth,now.wDay,
            now.wHour,now.wMinute,now.wSecond,g_snapshotSequence);
        wcscat_s(path,name);
        const bool started=g_nativeSceneTrace.Begin(g_device,path,&DisassembleShader);
        Log("scene trace: start=%d directory=%ls",started,path);
    }
    g_menuDiagnosticActive=g_menuDiagnosticPending;
    g_menuDiagnosticPending=false;
    if(g_menuDiagnosticActive){g_menuDiagnosticDraw=0;Log("menu trace: begin capture=%u",g_snapshotSequence);}
    if (g_effectHooksInstalled) g_effectStreams.BeginFrame();

    HRESULT left=E_PENDING,right=E_PENDING;
    const unsigned mapSamples=g_mapCinema.Samples();
    bool movie=!independent && ((Cfg().worldLockedMovies && g_movieQueue && g_movieQueue->Pending()) ||
        (Cfg().worldLockedMapSequence && g_mapCinema.Active()));
    const auto drawCinema=[&]{
        // Render the ORIGINAL composition exactly once: moving scene cards,
        // video, overlays, subtitles and cleanup all retain game draw order.
        // Applying HMD transforms to individual layers breaks their alignment.
        VR().SetEye(0);
        g_flatMovieFrame=true;
        g_otDraw(self);
        g_flatMovieFrame=false;
        if(Cfg().worldLockedMapSequence)g_mapCinema.Resolve(g_device);
        IDirect3DSurface9* frame=nullptr;
        g_device->GetBackBuffer(0,0,D3DBACKBUFFER_TYPE_MONO,&frame);
        const bool captured=g_moviePanel.CaptureFrame(g_device,frame);
        VR().EnsureOrigin();
        for(int eye=0;eye<2;++eye) {
            VR().SetEye(eye);
            g_compositingMenu=true;
            const HRESULT screen=captured ? g_moviePanel.PresentFrame(g_device,frame,
                VR().MovieClipTransform(g_moviePanel.Aspect())) : E_FAIL;
            g_compositingMenu=false;
            const HRESULT result=SUCCEEDED(screen)?CaptureEye(g_device,eye):screen;
            (eye==0?left:right)=result;
        }
        if(frame)frame->Release();
        static unsigned frames=0;
        if(++frames<=2 || frames%600==0 || FAILED(left) || FAILED(right))
            Log("movies: complete composition frame=%u captured=%d eyes=%08X/%08X",frames,captured,left,right);
    };
    if(movie)drawCinema();
    else {
        VR().SetEye(0);
        g_otDraw(self);
        // Recognize at the list boundary, before publishing either eye. The
        // first native pass deferred UI cleanup and retained effect streams,
        // so the entering frame can be recomposed with the original camera.
        const bool map=!independent && Cfg().worldLockedMapSequence && g_mapCinema.Resolve(g_device);
        const size_t restored = g_effectStreams.RestoreForRightEye();
        g_effectStreamsRestored += restored;
        if (restored) ++g_effectReplayFrames;
        if(map){movie=true;drawCinema();}
        else {
            left = independent ? E_PENDING : CaptureEye(g_device, 0);
            VR().SetEye(1);
            g_otDraw(self);
            right = independent ? E_PENDING : CaptureEye(g_device, 1);
        }
    }
    const bool wasMap=g_mapCinema.Active();g_mapCinema.EndFrame();
    if(g_mapCinema.Samples()!=mapSamples)Log("map cinema: sampled=%u lastCRC=%08X expected=%08X",g_mapCinema.Samples()-mapSamples,g_mapCinema.LastHash(),MapCinema9::MapFingerprint);
    if(wasMap!=g_mapCinema.Active())Log("map cinema: exact map texture %s",g_mapCinema.Active()?"recognized; original composition on fixed screen":"absent; returning to native stereo");
    if(g_nativeSceneTrace.Active()){
        const unsigned previews=g_nativeSceneTrace.Finish(g_device);
        Log("scene trace: complete movieFrame=%d smallPreviews=%u",movie,previews);
    }
    FinishMenuDiagnostic();
    g_pipeline.pairReady = PipelineSeconds();

    g_nativePairReady = SUCCEEDED(left) && SUCCEEDED(right) &&
                        g_eyes[0].ready && g_eyes[1].ready;
    if (g_nativePairReady) ++g_nativePairs;
    if (g_nativePairReady && g_earlySubmitEnabled && Cfg().directPoseTurning &&
        VR().NativeFullTracking() && CaptureAndSubmit(g_device, true)) {
        // Both VR images are complete. Like the standalone probe, submit and
        // release the compositor now rather than waiting for desktop Present.
        g_earlySubmission.MarkSubmitted();
        VR().PostPresentHandoff();
        g_pipeline.handoff = PipelineSeconds();
    }
    g_insideNativeDraw = false;
}

bool InstallNativeStereoHook() {
    if (!Cfg().nativeStereo) {
        Log("native stereo: disabled in configuration");
        return false;
    }
    if (g_otDraw) return true;

    auto* image = reinterpret_cast<unsigned char*>(GetModuleHandleW(nullptr));
    if (!image) return false;
    const auto* dos = reinterpret_cast<const IMAGE_DOS_HEADER*>(image);
    const auto* nt = dos->e_magic == IMAGE_DOS_SIGNATURE
        ? reinterpret_cast<const IMAGE_NT_HEADERS*>(image + dos->e_lfanew) : nullptr;
    if (!nt || nt->Signature != IMAGE_NT_SIGNATURE ||
        nt->FileHeader.TimeDateStamp != 0x591AD330) {
        Log("native stereo: unsupported Bayonetta executable");
        return false;
    }

    // Steam 1.0, cOtManager::draw. The Xbox PDB names the equivalent 6936-byte
    // function; the Steam call trace and matching renderer structure locate it
    // at VA 0x004E3A70 (RVA 0x000E3A70).
    g_otDrawTarget = image + 0x000E3A70;
    static const unsigned char expected[6] = {0x55, 0x8B, 0xEC, 0x83, 0xE4, 0xF0};
    if (memcmp(g_otDrawTarget, expected, sizeof(expected)) != 0) {
        Log("native stereo: cOtManager::draw signature mismatch at %p", g_otDrawTarget);
        g_otDrawTarget = nullptr;
        return false;
    }

    memcpy(g_otDrawOriginal, g_otDrawTarget, sizeof(g_otDrawOriginal));
    auto* trampoline = static_cast<unsigned char*>(VirtualAlloc(
        nullptr, 16, MEM_COMMIT | MEM_RESERVE, PAGE_EXECUTE_READWRITE));
    if (!trampoline) {
        Log("native stereo: trampoline allocation failed");
        g_otDrawTarget = nullptr;
        return false;
    }
    memcpy(trampoline, g_otDrawOriginal, sizeof(g_otDrawOriginal));
    trampoline[6] = 0xE9;
    *reinterpret_cast<int32_t*>(trampoline + 7) = static_cast<int32_t>(
        (g_otDrawTarget + 6) - (trampoline + 11));

    unsigned char patch[6]{0xE9, 0, 0, 0, 0, 0x90};
    *reinterpret_cast<int32_t*>(patch + 1) = static_cast<int32_t>(
        reinterpret_cast<unsigned char*>(&HookOtManagerDraw) - (g_otDrawTarget + 5));
    DWORD oldProtect = 0;
    if (!VirtualProtect(g_otDrawTarget, sizeof(patch), PAGE_EXECUTE_READWRITE, &oldProtect)) {
        VirtualFree(trampoline, 0, MEM_RELEASE);
        g_otDrawTarget = nullptr;
        Log("native stereo: could not make cOtManager::draw writable");
        return false;
    }
    g_otDrawTrampoline = trampoline;
    g_otDraw = reinterpret_cast<OtDrawFn>(trampoline);
    memcpy(g_otDrawTarget, patch, sizeof(patch));
    DWORD ignored = 0;
    VirtualProtect(g_otDrawTarget, sizeof(patch), oldProtect, &ignored);
    FlushInstructionCache(GetCurrentProcess(), g_otDrawTarget, sizeof(patch));
    Log("native stereo: cOtManager::draw dual-pass hook installed at %p", g_otDrawTarget);
    // This also defers cEspManager's heap-bank rotation until the right eye,
    // keeping the GPU buffers referenced by restored descriptors alive.
    if (InstallMenuHooks(image)) InstallEffectHooks(image);
    if(Cfg().fixCameraCulling)InstallCameraCullingHook(image,[] {
        return Cfg().enabled && Cfg().nativeStereo && VR().Active();
    });
    return true;
}

void RemoveNativeStereoHook() {
    RemoveCameraCullingHook();
    RemoveEffectHooks();
    RemoveMenuHooks();
    g_menuPanel.Reset();
    if (g_otDrawTarget && g_otDrawTrampoline) {
        DWORD oldProtect = 0;
        if (VirtualProtect(g_otDrawTarget, sizeof(g_otDrawOriginal),
                           PAGE_EXECUTE_READWRITE, &oldProtect)) {
            memcpy(g_otDrawTarget, g_otDrawOriginal, sizeof(g_otDrawOriginal));
            DWORD ignored = 0;
            VirtualProtect(g_otDrawTarget, sizeof(g_otDrawOriginal), oldProtect, &ignored);
            FlushInstructionCache(GetCurrentProcess(), g_otDrawTarget,
                                  sizeof(g_otDrawOriginal));
        }
        VirtualFree(g_otDrawTrampoline, 0, MEM_RELEASE);
    }
    g_otDraw = nullptr;
    g_otDrawTarget = nullptr;
    g_otDrawTrampoline = nullptr;
}

bool CaptureAndSubmit(IDirect3DDevice9* device, bool early) {
    if (!VR().Active() || IndependentVRGrid().OwnsFrames()) return false;
    const double entryTime = PipelineSeconds();
    static bool keyWasDown[10]{};
    static ULONGLONG lastPress[10]{};
    const int keys[10] = {VK_F7, VK_F8, VK_F9, VK_F10, VK_F11, VK_F12, VK_F6, VK_F5, VK_F4, VK_F1};
    bool pressed[10]{};
    const ULONGLONG now = GetTickCount64();
    for (int i = 0; i < 10; ++i) {
        const bool down = (GetAsyncKeyState(keys[i]) & 0x8000) != 0;
        pressed[i] = down && !keyWasDown[i] && now - lastPress[i] >= 500;
        if (pressed[i]) lastPress[i] = now;
        keyWasDown[i] = down;
    }
    if (pressed[0]) VR().ToggleCompositorPoseDiagnostic();
    if (pressed[9]) {
        if(GetAsyncKeyState(VK_SHIFT)&0x8000) {
            g_disableMotionBlur = !g_disableMotionBlur;
            Log("motion blur: Shift+F1 comparison, suppression %s",g_disableMotionBlur?"ON":"OFF");
        } else if(GetAsyncKeyState(VK_CONTROL)&0x8000) {
            g_disableAmbientOcclusion = !g_disableAmbientOcclusion;
            Log("ambient shadows: Ctrl+F1 comparison, bypass %s",g_disableAmbientOcclusion?"ON":"OFF");
        } else {
            g_fixSoftParticleDepth = !g_fixSoftParticleDepth;
            Log("soft particle depth: F1 comparison, correction %s",g_fixSoftParticleDepth?"ON":"OFF");
        }
    }
    if (pressed[1]) VR().Recenter();
    if (pressed[2]) VR().CycleStereoScale();
    if (pressed[3]) VR().ToggleEyeSwap();
    if (pressed[4]) VR().CycleProjectionMode();
    if (pressed[6]) VR().ToggleAutomaticTurning();
    if (pressed[7]) {
        g_referenceGrid = !g_referenceGrid;
        VR().SetExternalGrid(g_referenceGrid);
        Log(VR().External() ? "companion: F5 host grid %s" : "vr: F5 clean D3D11 grid-only submission %s (no D3D9 copy or game image)",
            g_referenceGrid ? "on" : "off");
    }
    if (pressed[8]) {
        g_earlySubmitEnabled = !g_earlySubmitEnabled;
        Log("vr: F4 native submission timing %s (only active during full native tracking)",
            g_earlySubmitEnabled ? "early: immediately after stereo pair" : "late: desktop Present (.27)");
    }
    if (pressed[5]) {
        // Ordinary F12 stays bounded. The older full-size UI texture dump is
        // still available explicitly with Shift+F12 for menu-only problems.
        g_menuDiagnosticPending=(GetAsyncKeyState(VK_SHIFT)&0x8000)!=0;
        g_nativeSceneTracePending=true;
        ++g_snapshotSequence;
        g_snapshotPending = true;
        g_snapshotSaved[0] = g_snapshotSaved[1] = false;
        g_stackCapturePending = true;
        Log("capture: F12 eye-buffer snapshot requested");
        VR().LogPoseDiagnostic();
        DumpShaderDiagnostics();
    }
    IDirect3DSurface9* backbuffer = nullptr;
    if (FAILED(device->GetBackBuffer(0, 0, D3DBACKBUFFER_TYPE_MONO, &backbuffer)) || !backbuffer) return false;
    if (!EnsureCapture(device, backbuffer)) { backbuffer->Release(); return false; }

    const bool nativePair = g_nativePairReady;
    g_nativePairReady = false;
    if (VR().External() && !nativePair) { backbuffer->Release(); return false; }
    const int eye = VR().Eye();
    const HRESULT readbackResult = nativePair ? S_OK : CaptureEyeSurface(device, backbuffer, eye);
    const double transferStart = PipelineSeconds();
    const bool cleanGridFrame = g_referenceGrid && !VR().External();
    if (g_gpuShared && !cleanGridFrame) {
        const HRESULT sync=SynchronizeD3D9(device);
        if(FAILED(sync)){Log("capture: D3D9 fence failed 0x%08X; pair skipped",sync);backbuffer->Release();return false;}
        // Do not submit a resource that D3D9 will rewrite next frame. Copy the
        // completed shared surfaces into ordinary D3D11 textures so OpenVR's
        // asynchronous compositor sees a stable image for this submission.
        for (auto& eyeTexture : g_eyes) {
            if (eyeTexture.ready && eyeTexture.shared11 && eyeTexture.submit11[g_submitSlot])
                g_context11->CopyResource(eyeTexture.submit11[g_submitSlot], eyeTexture.shared11);
        }
    }
    if (cleanGridFrame) {
        for (int e = 0; e < 2; ++e) {
            if (!g_eyes[e].ready || !VR().ReferencePoseValid(e)) continue;
            ID3D11RenderTargetView* target = nullptr;
            const HRESULT viewResult = g_d3d11->CreateRenderTargetView(
                g_cleanGrid[e][g_submitSlot], nullptr, &target);
            if (FAILED(viewResult) || !target) {
                Log("vr: clean grid target failed 0x%08X; disabled", viewResult);
                g_referenceGrid = false;
                backbuffer->Release();
                return false;
            }
            const float background[4]{0.004f, 0.008f, 0.045f, 1.0f};
            g_context11->ClearRenderTargetView(target, background);
            target->Release();
            const HRESULT hr = g_referenceRenderer.Draw(g_d3d11, g_context11,
                g_cleanGrid[e][g_submitSlot], VR().CapturedReferenceClip(e));
            if (FAILED(hr)) {
                Log("vr: clean reference grid failed 0x%08X; disabled", hr);
                g_referenceGrid = false;
                backbuffer->Release();
                return false;
            }
        }
    }
    if ((g_gpuShared && !cleanGridFrame) || cleanGridFrame) {
        // Flush alone does not guarantee that the new eye pixels have reached
        // the submission texture. Pairing a current pose with old ring-buffer
        // contents produces severe motion-only reprojection judder.
        const HRESULT sync=SynchronizeD3D11Copies();
        if(FAILED(sync)){Log("capture: D3D11 fence failed 0x%08X; pair skipped",sync);backbuffer->Release();return false;}
    }
    if (g_snapshotPending && g_snapshotSaved[0] && g_snapshotSaved[1]) {
        g_snapshotPending = false;
        Log("capture: F12 eye-buffer snapshot pair complete");
    }
    ID3D11Texture2D* left = cleanGridFrame ? g_cleanGrid[0][g_submitSlot] :
        g_eyes[0].ready ? g_eyes[0].submit11[g_submitSlot]
                                             : g_eyes[eye].submit11[g_submitSlot];
    ID3D11Texture2D* right = cleanGridFrame ? g_cleanGrid[1][g_submitSlot] :
        g_eyes[1].ready ? g_eyes[1].submit11[g_submitSlot]
                                              : g_eyes[eye].submit11[g_submitSlot];
    const double submitStart = PipelineSeconds();
    const bool published = VR().External() && VR().SubmitPair(g_d3d11,g_context11,left,right);
    const auto le = VR().External() ? vr::VRCompositorError_None : VR().Submit(0,left);
    const auto re = VR().External() ? vr::VRCompositorError_None : VR().Submit(1,right);
    if (VR().External() && published) {
        static unsigned remotePairs=0;
        if (++remotePairs%300==0) Log("companion: published %u complete stereo pairs",remotePairs);
    }
    const double submitEnd = PipelineSeconds();
    static unsigned long long presents = 0;
    static unsigned long long previousReportFrame = 0;
    static ULONGLONG previousReportTick = GetTickCount64();
    ++presents;
    if (presents == 1 || presents % 300 == 0 || le != vr::VRCompositorError_None || re != vr::VRCompositorError_None) {
        const ULONGLONG reportTick = GetTickCount64();
        const double seconds = (reportTick - previousReportTick) / 1000.0;
        const double rate = seconds > 0.0 ? (presents - previousReportFrame) / seconds : 0.0;
        Log("vr: frame=%llu rate=%.1fHz mode=%s bridge=%s readback=0x%08X ready=%d/%d submit=%d/%d transformed=%llu nativePairs=%llu hash=%016llX/%016llX",
            presents, rate, nativePair ? "native" : "alternating",
            cleanGridFrame ? "clean-grid" : g_gpuShared ? "shared" : "cpu", readbackResult,
            g_eyes[0].ready, g_eyes[1].ready,
            static_cast<int>(le), static_cast<int>(re), g_adjustedDraws,
            g_nativePairs,
            g_eyes[0].pixelHash, g_eyes[1].pixelHash);
        previousReportFrame = presents;
        previousReportTick = reportTick;
        VR().LogFrameTiming();
        if (g_menuHooksInstalled) Log("menu: captures=%llu/%llu eyeComposites=%llu/%llu",
            g_menuCaptures[0], g_menuCaptures[1], g_menuEyeDraws[0], g_menuEyeDraws[1]);
        if (g_effectHooksInstalled) Log("effects: replayFrames=%llu streamsRestored=%llu",
            g_effectReplayFrames, g_effectStreamsRestored);
        if (nativePair) Log("vr pipeline: early=%d wait=%.2fms pair=%.2fms queue=%.2fms transfer=%.2fms submit=%.2fms poseAgeAtSubmit=%.2fms",
            early, (g_pipeline.poseReady-g_pipeline.waitStart)*1000,
            (g_pipeline.pairReady-g_pipeline.poseReady)*1000,
            (entryTime-g_pipeline.pairReady)*1000, (submitStart-transferStart)*1000,
            (submitEnd-submitStart)*1000, (submitStart-g_pipeline.poseReady)*1000);
    }
    static bool loggedTransforms = false;
    if (!loggedTransforms) {
        Log("stereo: %llu transformed draws (%llu HUD), %zu shaders inspected", g_adjustedDraws, g_hudDraws, g_shaders.size());
        loggedTransforms = true;
    }

    if (Cfg().mirrorSideBySide && g_eyes[0].ready && g_eyes[1].ready) {
        RECT halves[2]{{0,0,static_cast<LONG>(g_width/2),static_cast<LONG>(g_height)},
                       {static_cast<LONG>(g_width/2),0,static_cast<LONG>(g_width),static_cast<LONG>(g_height)}};
        for (int i = 0; i < 2; ++i) {
            IDirect3DSurface9* src = nullptr;
            if (SUCCEEDED(g_eyes[i].d3d9->GetSurfaceLevel(0, &src))) {
                device->StretchRect(src, nullptr, backbuffer, &halves[i], D3DTEXF_LINEAR);
                src->Release();
            }
        }
    }
    backbuffer->Release();
    g_submitSlot = (g_submitSlot + 1) % kSubmitRingSize;
    if (nativePair) VR().SetEye(0);
    else VR().AdvanceEye();
    if (!nativePair) VR().WaitForPose();
    return true; // A pair was submitted/attempted; do not submit it twice.
}

template<class Call>
HRESULT PresentOnce(IDirect3DDevice9* device, Call&& call) {
    // A device Present can enter the primary swap-chain Present internally.
    // Keep the guard set through the real call so the nested hook cannot
    // capture the side-by-side desktop mirror as a new HMD eye frame.
    if (g_insidePresent) return call();
    g_insidePresent = true;
    auto& grid = IndependentVRGrid();
    static bool f3Down = false, f8Down = false;
    const bool f3 = (GetAsyncKeyState(VK_F3) & 0x8000) != 0;
    const bool f8 = (GetAsyncKeyState(VK_F8) & 0x8000) != 0;
    const bool finished = grid.Poll();
    if (finished) Log("independent grid: game regained compositor ownership");
    bool changedOwner = finished;
    if (f3 && !f3Down && VR().Active() && !VR().External()) {
        if (grid.OwnsFrames()) {
            grid.RequestStop();
            Log("independent grid: F3 stop requested; waiting for worker exit");
        } else if (!finished) {
            changedOwner = VR().StartIndependentGrid();
            Log("independent grid: F3 start %s", changedOwner ? "requested" : "FAILED");
        }
    }
    if (grid.OwnsFrames() && f8 && !f8Down) grid.Recenter();
    f3Down = f3; f8Down = f8;
    if (grid.OwnsFrames() || changedOwner) {
        g_nativePairReady = false;
        g_earlySubmission.Reset();
        for (auto& eye : g_eyes) eye.ready = false;
        // Keep desktop rendering/presentation running. The worker alone waits,
        // submits and hands off. Skip this transition frame on return as well.
        const HRESULT result = call();
        // Freeze at a completed frame boundary; do not skip engine draw calls
        // or let its queues/simulation grow. Only this render/game thread parks.
        // The worker continues its own tracking/render/submit loop unchanged.
        static bool parkNeedsRelease = false;
        const bool f2 = (GetAsyncKeyState(VK_F2) & 0x8000) != 0;
        if (!f2) parkNeedsRelease = false;
        if (f2 && !parkNeedsRelease && grid.OwnsFrames()) {
            parkNeedsRelease = true;
            Log("independent grid: parking game thread (F2 release or 4 seconds resumes)");
            grid.SetGameParked(true);
            const ULONGLONG parkDeadline = GetTickCount64()+4000;
            while ((GetAsyncKeyState(VK_F2) & 0x8000) && GetTickCount64()<parkDeadline) {
                // F3 remains an immediate exit, including while the game is parked.
                if (GetAsyncKeyState(VK_F3) & 0x8000) { grid.RequestStop(); f3Down = true; break; }
                if (grid.Poll()) { Log("independent grid: worker exited while parked"); break; }
                Sleep(10);
            }
            grid.SetGameParked(false);
            Log("independent grid: game thread resumed");
        }
        g_insidePresent = false;
        return result;
    }
    const bool alreadySubmitted = g_earlySubmission.ConsumeAtPresent();
    const bool hadNativeTiming = alreadySubmitted || g_nativePairReady;
    if (!alreadySubmitted) CaptureAndSubmit(device, false);
    const double presentStart = PipelineSeconds();
    const HRESULT result = call();
    const double presentEnd = PipelineSeconds();
    if (!alreadySubmitted) {
        VR().PostPresentHandoff();
        g_pipeline.handoff = PipelineSeconds();
    }
    static unsigned desktopFrames = 0;
    if (++desktopFrames % 300 == 0 && hadNativeTiming)
        Log("vr desktop: early=%d present=%.2fms poseToHandoff=%.2fms",
            alreadySubmitted, (presentEnd-presentStart)*1000,
            (g_pipeline.handoff-g_pipeline.poseReady)*1000);
    g_insidePresent = false;
    return result;
}

void UncapPresentation(D3DPRESENT_PARAMETERS* parameters) {
    if (parameters && Cfg().nativeStereo && Cfg().uncapPresent)
        parameters->PresentationInterval = D3DPRESENT_INTERVAL_IMMEDIATE;
}

HRESULT STDMETHODCALLTYPE HookReset(IDirect3DDevice9* d, D3DPRESENT_PARAMETERS* p) {
    g_mapCinema.Reset();g_nativeSceneTrace.Finish(nullptr);g_moviePanel.Reset();
    g_softParticleDepth.ReleaseReplacement();
    g_effectStreams.Reset();
    g_menuPanel.Reset();
    ReleaseTextures();
    UncapPresentation(p);
    return g_reset(d,p);
}
HRESULT STDMETHODCALLTYPE HookPresent(IDirect3DDevice9* d,const RECT*a,const RECT*b,HWND c,const RGNDATA*e) {
    return PresentOnce(d, [&]{ return g_present(d,a,b,c,e); });
}
HRESULT STDMETHODCALLTYPE HookCreateVS(IDirect3DDevice9* d,const DWORD* code,IDirect3DVertexShader9** out) {
    HRESULT hr=g_createVS(d,code,out);
    if(SUCCEEDED(hr)&&out&&*out){
        std::string assembly=DisassembleShader(code);
        int base=FindPositionMatrix(assembly);
        ShaderInfo info{};
        info.matrixBase=base;
        info.assembly=std::move(assembly);
        g_shaders[*out]=std::move(info);
        if(base>=0) Log("shader: %p position matrix c%d",*out,base);
    }
    return hr;
}
HRESULT STDMETHODCALLTYPE HookCreatePS(IDirect3DDevice9* d,const DWORD* code,IDirect3DPixelShader9** out) {
    const HRESULT hr=g_createPS(d,code,out);
    if(SUCCEEDED(hr)&&out&&*out) {
        const auto hash=g_dofFilter.Register(*out);
        if(hash)Log("dof: identified original pixel shader %08X (object unchanged)",hash);
        const auto motionHash=g_motionBlurFilter.Register(*out);
        if(motionHash)Log("motion blur: identified original pixel shader %08X (object unchanged)",motionHash);
        const auto aoHash=g_ambientOcclusionShader.Register(*out);
        if(aoHash)Log("ambient shadows: identified original pixel shader %08X (object unchanged)",aoHash);
        const auto softHash=g_softParticleDepth.Register(*out);
        if(softHash)Log("soft particle depth: identified original pixel shader %08X (object unchanged)",softHash);
    }
    return hr;
}
HRESULT STDMETHODCALLTYPE HookSetVS(IDirect3DDevice9* d,IDirect3DVertexShader9* s) { g_currentShader=s; return g_setVS(d,s); }
HRESULT STDMETHODCALLTYPE HookSetRenderTarget(IDirect3DDevice9* d,DWORD index,IDirect3DSurface9* target) {
    if(g_capturingMovie && index==0 && target==g_moviePanel.SourceTarget())target=g_moviePanel.CaptureTarget();
    if (g_capturingMenu >= 0 && index == 0 && target == g_menuPanel.SourceTarget())
        target = g_menuPanel.CaptureTarget(g_capturingMenu);
    return g_setRenderTarget(d,index,target);
}
HRESULT STDMETHODCALLTYPE HookClear(IDirect3DDevice9* d,DWORD count,const D3DRECT* rects,DWORD flags,D3DCOLOR color,float z,DWORD stencil) {
    const auto call=[&]{return g_clear(d,count,rects,flags,color,z,stencil);};
    return g_capturingMenu>=0 ? g_menuPanel.CaptureDraw(d,g_capturingMenu,call) : call();
}
HRESULT STDMETHODCALLTYPE HookStretchRect(IDirect3DDevice9* d,IDirect3DSurface9* source,const RECT* sourceRect,
    IDirect3DSurface9* destination,const RECT* destinationRect,D3DTEXTUREFILTERTYPE filter) {
    if(g_capturingMenu<0)return g_stretchRect(d,source,sourceRect,destination,destinationRect,filter);
    const HRESULT hr=g_menuPanel.CaptureCopy(d,g_capturingMenu,source,sourceRect,destination,destinationRect,filter,g_stretchRect);
    if(g_menuDiagnosticActive || FAILED(hr))Log("menu copy: layer=%d source=%p destination=%p status=0x%08X",g_capturingMenu,source,destination,hr);
    return hr;
}
HRESULT STDMETHODCALLTYPE HookSetConst(IDirect3DDevice9* d,UINT start,const float* data,UINT count) {
    if(data&&start<256){ UINT n=std::min(count,256u-start); memcpy(&g_constants[start][0],data,n*4*sizeof(float)); for(UINT i=0;i<n;++i)g_constantValid[start+i]=true; }
    return g_setConst(d,start,data,count);
}
HRESULT STDMETHODCALLTYPE HookDrawPrimitive(IDirect3DDevice9*d,D3DPRIMITIVETYPE t,UINT s,UINT n){return DrawWithTrackedCamera(d,n,[&]{return g_drawPrimitive(d,t,s,n);});}
HRESULT STDMETHODCALLTYPE HookDrawIndexed(IDirect3DDevice9*d,D3DPRIMITIVETYPE t,INT b,UINT mi,UINT nv,UINT si,UINT pc){return DrawWithTrackedCamera(d,pc,[&]{return g_drawIndexed(d,t,b,mi,nv,si,pc);});}
HRESULT STDMETHODCALLTYPE HookDrawPrimitiveUP(IDirect3DDevice9*d,D3DPRIMITIVETYPE t,UINT n,const void*v,UINT st){return DrawWithTrackedCamera(d,n,[&]{return g_drawPrimitiveUP(d,t,n,v,st);});}
HRESULT STDMETHODCALLTYPE HookDrawIndexedUP(IDirect3DDevice9*d,D3DPRIMITIVETYPE t,UINT mv,UINT nv,UINT pc,const void*i,D3DFORMAT f,const void*v,UINT st){return DrawWithTrackedCamera(d,pc,[&]{return g_drawIndexedUP(d,t,mv,nv,pc,i,f,v,st);});}
HRESULT STDMETHODCALLTYPE HookPresentEx(IDirect3DDevice9Ex*d,const RECT*a,const RECT*b,HWND c,const RGNDATA*e,DWORD f) {
    return PresentOnce(d, [&]{ return g_presentEx(d,a,b,c,e,f); });
}
HRESULT STDMETHODCALLTYPE HookResetEx(IDirect3DDevice9Ex*d,D3DPRESENT_PARAMETERS*p,D3DDISPLAYMODEEX*m){g_mapCinema.Reset();g_nativeSceneTrace.Finish(nullptr);g_moviePanel.Reset();g_softParticleDepth.ReleaseReplacement();g_effectStreams.Reset();g_menuPanel.Reset();ReleaseTextures();UncapPresentation(p);return g_resetEx(d,p,m);}
HRESULT STDMETHODCALLTYPE HookSwapPresent(IDirect3DSwapChain9* swap,const RECT*a,const RECT*b,HWND c,const RGNDATA*e,DWORD f) {
    IDirect3DDevice9* device = nullptr;
    if (SUCCEEDED(swap->GetDevice(&device)) && device) {
        const HRESULT result = PresentOnce(device, [&]{ return g_swapPresent(swap,a,b,c,e,f); });
        device->Release();
        return result;
    }
    return g_swapPresent(swap,a,b,c,e,f);
}

void HookSwapChain(IDirect3DSwapChain9* swap) {
    if (!swap) return;
    void** v = *reinterpret_cast<void***>(swap);
    if (PatchVtable(v,3,&HookSwapPresent,g_swapPresent)) Log("d3d9: swap-chain Present hook installed");
    else Log("d3d9: failed to patch swap-chain Present");
}

void HookDevice(IDirect3DDevice9Ex* device) {
    g_mapCinema.Reset();g_nativeSceneTrace.Finish(nullptr);g_moviePanel.Reset();
    g_softParticleDepth.Reset();
    g_fixSoftParticleDepth = Cfg().fixSoftParticleDepth;
    g_dofFilter.Reset();
    g_motionBlurFilter.Reset();
    g_disableMotionBlur = Cfg().disableMotionBlur;
    g_ambientOcclusionShader.Reset();
    g_disableAmbientOcclusion = Cfg().disableAmbientOcclusion;
    g_device = device;
    g_earlySubmitEnabled = Cfg().nativeEarlySubmit;
    void** v = *reinterpret_cast<void***>(device);
    PatchVtable(v,16,&HookReset,g_reset);
    PatchVtable(v,17,&HookPresent,g_present);
    PatchVtable(v,37,&HookSetRenderTarget,g_setRenderTarget);
    PatchVtable(v,34,&HookStretchRect,g_stretchRect);
    PatchVtable(v,43,&HookClear,g_clear);
    PatchVtable(v,81,&HookDrawPrimitive,g_drawPrimitive);
    PatchVtable(v,82,&HookDrawIndexed,g_drawIndexed);
    PatchVtable(v,83,&HookDrawPrimitiveUP,g_drawPrimitiveUP);
    PatchVtable(v,84,&HookDrawIndexedUP,g_drawIndexedUP);
    PatchVtable(v,91,&HookCreateVS,g_createVS);
    PatchVtable(v,106,&HookCreatePS,g_createPS);
    PatchVtable(v,92,&HookSetVS,g_setVS);
    PatchVtable(v,94,&HookSetConst,g_setConst);
    PatchVtable(v,121,&HookPresentEx,g_presentEx);
    PatchVtable(v,132,&HookResetEx,g_resetEx);
    Log("d3d9: device hooks installed");
    IDirect3DSwapChain9* primary = nullptr;
    if (SUCCEEDED(device->GetSwapChain(0, &primary)) && primary) {
        HookSwapChain(primary);
        primary->Release();
    }
    VR().Init();
    if(Cfg().enabled&&Cfg().vrControllers)InstallControllerHooks();
    InstallNativeStereoHook();
}

HRESULT STDMETHODCALLTYPE HookCreateDeviceEx(IDirect3D9Ex* d3d,UINT adapter,D3DDEVTYPE type,HWND window,DWORD flags,
                                              D3DPRESENT_PARAMETERS* pp,D3DDISPLAYMODEEX* mode,IDirect3DDevice9Ex** out) {
    const UINT requestedInterval = pp ? pp->PresentationInterval : 0;
    UncapPresentation(pp);
    HRESULT hr=g_createDeviceEx(d3d,adapter,type,window,flags,pp,mode,out);
    Log("d3d9: CreateDeviceEx result=0x%08X %ux%u windowed=%d interval=0x%X->0x%X",
        hr,pp?pp->BackBufferWidth:0,pp?pp->BackBufferHeight:0,pp?pp->Windowed:0,
        requestedInterval,pp?pp->PresentationInterval:0);
    if(SUCCEEDED(hr)&&out&&*out)HookDevice(*out);
    return hr;
}
}

void HookDirect3D9Ex(IDirect3D9Ex* d3d) {
    if (!d3d) return;
    void** v = *reinterpret_cast<void***>(d3d);
    if (PatchVtable(v,20,&HookCreateDeviceEx,g_createDeviceEx)) Log("d3d9: IDirect3D9Ex hook installed");
    else Log("d3d9: failed to patch IDirect3D9Ex vtable");
}

void ShutdownD3D() {
    g_mapCinema.Reset();g_nativeSceneTrace.Finish(nullptr);g_moviePanel.Reset();
    g_softParticleDepth.Reset();
    g_ambientOcclusionShader.Reset();
    g_dofFilter.Reset();
    g_motionBlurFilter.Reset();
    RemoveNativeStereoHook();
    g_device = nullptr;
    ReleaseTextures();
    VR().Shutdown();
}
}
