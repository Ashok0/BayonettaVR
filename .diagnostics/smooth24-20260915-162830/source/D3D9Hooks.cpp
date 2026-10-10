#include "D3D9Hooks.h"
#include "Config.h"
#include "Log.h"
#include "Matrix.h"
#include "VR.h"
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
SetVSFn g_setVS = nullptr;
SetConstFn g_setConst = nullptr;
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
    std::string assembly;
};
std::unordered_map<IDirect3DVertexShader9*, ShaderInfo> g_shaders;
IDirect3DVertexShader9* g_currentShader = nullptr;
float g_constants[256][4]{};
bool g_constantValid[256]{};
unsigned long long g_adjustedDraws = 0;
thread_local bool g_insidePresent = false;
thread_local bool g_insideNativeDraw = false;
struct ClipCacheEntry {
    UINT width = 0;
    UINT height = 0;
    Mat4 transform{};
};
ClipCacheEntry g_clipCache[2][8]{};
int g_clipCacheCount[2]{};
IDirect3DDevice9* g_device = nullptr;
bool g_nativePairReady = false;
unsigned long long g_nativePairs = 0;

constexpr int kSubmitRingSize = 4;
struct EyeTexture {
    IDirect3DTexture9* d3d9 = nullptr;
    IDirect3DSurface9* readback = nullptr;
    ID3D11Texture2D* shared11 = nullptr;
    ID3D11Texture2D* submit11[kSubmitRingSize]{};
    bool ready = false;
    unsigned long long pixelHash = 0;
};
EyeTexture g_eyes[2];
ID3D11Device* g_d3d11 = nullptr;
ID3D11DeviceContext* g_context11 = nullptr;
UINT g_width = 0, g_height = 0;
bool g_gpuShared = false;
int g_submitSlot = 0;
bool g_snapshotPending = false;
bool g_snapshotSaved[2]{};
unsigned g_snapshotSequence = 0;
bool g_stackCapturePending = true;

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
    size_t lineStart = 0;
    while (lineStart < text.size()) {
        const size_t lineEnd = text.find('\n', lineStart);
        const std::string line = text.substr(lineStart,
            lineEnd == std::string::npos ? std::string::npos : lineEnd - lineStart);
        std::string lower = line;
        std::transform(lower.begin(), lower.end(), lower.begin(), [](unsigned char c) {
            return static_cast<char>(tolower(c));
        });
        const bool cameraName = lower.find("worldviewproj") != std::string::npos ||
                                lower.find("modelviewproj") != std::string::npos ||
                                lower.find("viewprojection") != std::string::npos ||
                                lower.find("viewprojmatrix") != std::string::npos;
        if (cameraName) {
            for (size_t i = 0; i + 1 < lower.size(); ++i) {
                if (lower[i] == 'c' && lower[i + 1] >= '0' && lower[i + 1] <= '9')
                    return atoi(lower.c_str() + i + 1);
            }
        }
        if (lineEnd == std::string::npos) break;
        lineStart = lineEnd + 1;
    }
    return -1;
}

int FindPositionMatrix(const std::string& text) {
    if (text.empty()) return -1;

    // Bayonetta embeds D3DX constant-table names. These cover its skinned
    // shaders, where c8 (g_ViewProjection) is folded into temporary rows and
    // therefore never appears in the final dp4 quartet directly.
    const int namedMatrix = FindNamedCameraMatrix(text);
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
        Log("shader stats: rank=%zu ptr=%p draws=%llu matrix=c%d", i + 1,
            shaders[i].first, info->draws, info->matrixBase);
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

bool LooksOrthographic(const Mat4& m) {
    return std::fabs(m.m[3][0]) < 1e-5f && std::fabs(m.m[3][1]) < 1e-5f &&
           std::fabs(m.m[3][2]) < 1e-5f && std::fabs(m.m[3][3] - 1.0f) < 1e-4f;
}

template<class Call>
HRESULT DrawWithTrackedCamera(IDirect3DDevice9* device, Call&& call) {
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
    if (!VR().Active() || base < 0 || base > 252) return call();

    if (g_stackCapturePending) {
        g_stackCapturePending = false;
        DumpRenderStack();
    }

    Mat4 original{};
    // Effects and state blocks can restore constants without calling the
    // public Set* entry in the device vtable. Read the live device state at the
    // draw boundary; this is the authoritative matrix the shader will use.
    if (FAILED(device->GetVertexShaderConstantF(static_cast<UINT>(base), &original.m[0][0], 4))) return call();
    if (!Cfg().applyToUi && LooksOrthographic(original)) return call();
    VR().EnsureOrigin();

    D3DVIEWPORT9 viewport{};
    float aspect = 16.0f / 9.0f;
    if (SUCCEEDED(device->GetViewport(&viewport)) && viewport.Height) aspect = float(viewport.Width) / float(viewport.Height);
    Mat4 clipTransform{};
    bool cached = false;
    if (g_insideNativeDraw && viewport.Height) {
        const int eye = VR().Eye();
        for (int i = 0; i < g_clipCacheCount[eye]; ++i) {
            if (g_clipCache[eye][i].width == viewport.Width &&
                g_clipCache[eye][i].height == viewport.Height) {
                clipTransform = g_clipCache[eye][i].transform;
                cached = true;
                break;
            }
        }
        if (!cached) {
            clipTransform = VR().ClipTransform(aspect);
            cached = true;
            if (g_clipCacheCount[eye] < static_cast<int>(_countof(g_clipCache[eye]))) {
                auto& entry = g_clipCache[eye][g_clipCacheCount[eye]++];
                entry.width = viewport.Width;
                entry.height = viewport.Height;
                entry.transform = clipTransform;
            }
        }
    }
    if (!cached) clipTransform = VR().ClipTransform(aspect);
    const Mat4 adjusted = Mul(clipTransform, original);
    g_setConst(device, static_cast<UINT>(base), &adjusted.m[0][0], 4);
    ++g_adjustedDraws;
    const HRESULT result = call();
    g_setConst(device, static_cast<UINT>(base), &original.m[0][0], 4);
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
            td.Usage=D3D11_USAGE_DEFAULT; td.BindFlags=D3D11_BIND_SHADER_RESOURCE;
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
            td.Usage=D3D11_USAGE_DEFAULT; td.BindFlags=D3D11_BIND_SHADER_RESOURCE;
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
    g_width = desc.Width; g_height = desc.Height;
    Log("capture: %s eye textures ready at %ux%u, VR adapter=%d feature level=0x%X",
        g_gpuShared ? "GPU-shared" : "CPU-bridged", g_width, g_height, vrAdapter, level);
    return true;
}

void SynchronizeD3D9(IDirect3DDevice9* device) {
    IDirect3DQuery9* fence = nullptr;
    if (SUCCEEDED(device->CreateQuery(D3DQUERYTYPE_EVENT, &fence)) && fence) {
        fence->Issue(D3DISSUE_END);
        while (fence->GetData(nullptr, 0, D3DGETDATA_FLUSH) == S_FALSE) SwitchToThread();
        fence->Release();
    }
}

void SynchronizeD3D11Copies() {
    if (!g_d3d11 || !g_context11) return;
    D3D11_QUERY_DESC desc{};
    desc.Query = D3D11_QUERY_EVENT;
    ID3D11Query* fence = nullptr;
    if (FAILED(g_d3d11->CreateQuery(&desc, &fence)) || !fence) return;
    g_context11->End(fence);
    g_context11->Flush();
    HRESULT status = S_FALSE;
    while ((status = g_context11->GetData(
                fence, nullptr, 0, D3D11_ASYNC_GETDATA_DONOTFLUSH)) == S_FALSE) {
        SwitchToThread();
    }
    fence->Release();
    static bool logged = false;
    if (!logged) {
        Log("capture: D3D11 copy completion fence active (status=0x%08X)", status);
        logged = true;
    }
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
        SynchronizeD3D9(device);
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

void __fastcall HookOtManagerDraw(void* self, void*) {
    if (!g_otDraw) return;
    if (g_insideNativeDraw || !Cfg().nativeStereo || !VR().Active() || !g_device) {
        g_otDraw(self);
        return;
    }

    g_insideNativeDraw = true;
    // Sample the compositor's predicted pose at the last game-level point
    // before either eye is rendered. Both eyes then use this exact pose.
    VR().WaitForPose();
    g_clipCacheCount[0] = g_clipCacheCount[1] = 0;

    VR().SetEye(0);
    g_otDraw(self);
    const HRESULT left = CaptureEye(g_device, 0);

    VR().SetEye(1);
    g_otDraw(self);
    const HRESULT right = CaptureEye(g_device, 1);

    g_nativePairReady = SUCCEEDED(left) && SUCCEEDED(right) &&
                        g_eyes[0].ready && g_eyes[1].ready;
    if (g_nativePairReady) ++g_nativePairs;
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
    return true;
}

void RemoveNativeStereoHook() {
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

void CaptureAndSubmit(IDirect3DDevice9* device) {
    if (!VR().Active()) return;
    static bool keyWasDown[6]{};
    static ULONGLONG lastPress[6]{};
    const int keys[6] = {VK_F7, VK_F8, VK_F9, VK_F10, VK_F11, VK_F12};
    bool pressed[6]{};
    const ULONGLONG now = GetTickCount64();
    for (int i = 0; i < 6; ++i) {
        const bool down = (GetAsyncKeyState(keys[i]) & 0x8000) != 0;
        pressed[i] = down && !keyWasDown[i] && now - lastPress[i] >= 500;
        if (pressed[i]) lastPress[i] = now;
        keyWasDown[i] = down;
    }
    if (pressed[0]) VR().ToggleCompositorPoseDiagnostic();
    if (pressed[1]) VR().Recenter();
    if (pressed[2]) VR().CycleStereoScale();
    if (pressed[3]) VR().ToggleEyeSwap();
    if (pressed[4]) VR().CycleProjectionMode();
    if (pressed[5]) {
        ++g_snapshotSequence;
        g_snapshotPending = true;
        g_snapshotSaved[0] = g_snapshotSaved[1] = false;
        g_stackCapturePending = true;
        Log("capture: F12 eye-buffer snapshot requested");
        VR().LogPoseDiagnostic();
        DumpShaderDiagnostics();
    }
    IDirect3DSurface9* backbuffer = nullptr;
    if (FAILED(device->GetBackBuffer(0, 0, D3DBACKBUFFER_TYPE_MONO, &backbuffer)) || !backbuffer) return;
    if (!EnsureCapture(device, backbuffer)) { backbuffer->Release(); return; }

    const bool nativePair = g_nativePairReady;
    g_nativePairReady = false;
    const int eye = VR().Eye();
    const HRESULT readbackResult = nativePair ? S_OK : CaptureEyeSurface(device, backbuffer, eye);
    if (g_gpuShared) {
        SynchronizeD3D9(device);
        // Do not submit a resource that D3D9 will rewrite next frame. Copy the
        // completed shared surfaces into ordinary D3D11 textures so OpenVR's
        // asynchronous compositor sees a stable image for this submission.
        for (auto& eyeTexture : g_eyes) {
            if (eyeTexture.ready && eyeTexture.shared11 && eyeTexture.submit11[g_submitSlot])
                g_context11->CopyResource(eyeTexture.submit11[g_submitSlot], eyeTexture.shared11);
        }
        // Flush alone does not guarantee that the new eye pixels have reached
        // the submission texture. Pairing a current pose with old ring-buffer
        // contents produces severe motion-only reprojection judder.
        SynchronizeD3D11Copies();
    }
    if (g_snapshotPending && g_snapshotSaved[0] && g_snapshotSaved[1]) {
        g_snapshotPending = false;
        Log("capture: F12 eye-buffer snapshot pair complete");
    }
    ID3D11Texture2D* left = g_eyes[0].ready ? g_eyes[0].submit11[g_submitSlot]
                                             : g_eyes[eye].submit11[g_submitSlot];
    ID3D11Texture2D* right = g_eyes[1].ready ? g_eyes[1].submit11[g_submitSlot]
                                              : g_eyes[eye].submit11[g_submitSlot];
    const auto le = VR().Submit(0, left);
    const auto re = VR().Submit(1, right);
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
            g_gpuShared ? "shared" : "cpu", readbackResult,
            g_eyes[0].ready, g_eyes[1].ready,
            static_cast<int>(le), static_cast<int>(re), g_adjustedDraws,
            g_nativePairs,
            g_eyes[0].pixelHash, g_eyes[1].pixelHash);
        previousReportFrame = presents;
        previousReportTick = reportTick;
        VR().LogFrameTiming();
    }
    static bool loggedTransforms = false;
    if (!loggedTransforms) {
        Log("stereo: %llu transformed draws, %zu shaders inspected", g_adjustedDraws, g_shaders.size());
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
}

template<class Call>
HRESULT PresentOnce(IDirect3DDevice9* device, Call&& call) {
    // A device Present can enter the primary swap-chain Present internally.
    // Keep the guard set through the real call so the nested hook cannot
    // capture the side-by-side desktop mirror as a new HMD eye frame.
    if (g_insidePresent) return call();
    g_insidePresent = true;
    CaptureAndSubmit(device);
    const HRESULT result = call();
    VR().PostPresentHandoff();
    g_insidePresent = false;
    return result;
}

void UncapPresentation(D3DPRESENT_PARAMETERS* parameters) {
    if (parameters && Cfg().nativeStereo && Cfg().uncapPresent)
        parameters->PresentationInterval = D3DPRESENT_INTERVAL_IMMEDIATE;
}

HRESULT STDMETHODCALLTYPE HookReset(IDirect3DDevice9* d, D3DPRESENT_PARAMETERS* p) {
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
HRESULT STDMETHODCALLTYPE HookSetVS(IDirect3DDevice9* d,IDirect3DVertexShader9* s) { g_currentShader=s; return g_setVS(d,s); }
HRESULT STDMETHODCALLTYPE HookSetConst(IDirect3DDevice9* d,UINT start,const float* data,UINT count) {
    if(data&&start<256){ UINT n=std::min(count,256u-start); memcpy(&g_constants[start][0],data,n*4*sizeof(float)); for(UINT i=0;i<n;++i)g_constantValid[start+i]=true; }
    return g_setConst(d,start,data,count);
}
HRESULT STDMETHODCALLTYPE HookDrawPrimitive(IDirect3DDevice9*d,D3DPRIMITIVETYPE t,UINT s,UINT n){return DrawWithTrackedCamera(d,[&]{return g_drawPrimitive(d,t,s,n);});}
HRESULT STDMETHODCALLTYPE HookDrawIndexed(IDirect3DDevice9*d,D3DPRIMITIVETYPE t,INT b,UINT mi,UINT nv,UINT si,UINT pc){return DrawWithTrackedCamera(d,[&]{return g_drawIndexed(d,t,b,mi,nv,si,pc);});}
HRESULT STDMETHODCALLTYPE HookDrawPrimitiveUP(IDirect3DDevice9*d,D3DPRIMITIVETYPE t,UINT n,const void*v,UINT st){return DrawWithTrackedCamera(d,[&]{return g_drawPrimitiveUP(d,t,n,v,st);});}
HRESULT STDMETHODCALLTYPE HookDrawIndexedUP(IDirect3DDevice9*d,D3DPRIMITIVETYPE t,UINT mv,UINT nv,UINT pc,const void*i,D3DFORMAT f,const void*v,UINT st){return DrawWithTrackedCamera(d,[&]{return g_drawIndexedUP(d,t,mv,nv,pc,i,f,v,st);});}
HRESULT STDMETHODCALLTYPE HookPresentEx(IDirect3DDevice9Ex*d,const RECT*a,const RECT*b,HWND c,const RGNDATA*e,DWORD f) {
    return PresentOnce(d, [&]{ return g_presentEx(d,a,b,c,e,f); });
}
HRESULT STDMETHODCALLTYPE HookResetEx(IDirect3DDevice9Ex*d,D3DPRESENT_PARAMETERS*p,D3DDISPLAYMODEEX*m){ReleaseTextures();UncapPresentation(p);return g_resetEx(d,p,m);}
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
    g_device = device;
    void** v = *reinterpret_cast<void***>(device);
    PatchVtable(v,16,&HookReset,g_reset);
    PatchVtable(v,17,&HookPresent,g_present);
    PatchVtable(v,81,&HookDrawPrimitive,g_drawPrimitive);
    PatchVtable(v,82,&HookDrawIndexed,g_drawIndexed);
    PatchVtable(v,83,&HookDrawPrimitiveUP,g_drawPrimitiveUP);
    PatchVtable(v,84,&HookDrawIndexedUP,g_drawIndexedUP);
    PatchVtable(v,91,&HookCreateVS,g_createVS);
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
    RemoveNativeStereoHook();
    g_device = nullptr;
    ReleaseTextures();
    VR().Shutdown();
}
}
