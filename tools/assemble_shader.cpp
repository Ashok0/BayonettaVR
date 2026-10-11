#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <unknwn.h>
#include <cstdio>
#include <fstream>
#include <iterator>
#include <string>
struct Buffer : IUnknown {
    virtual void* STDMETHODCALLTYPE GetBufferPointer()=0;
    virtual DWORD STDMETHODCALLTYPE GetBufferSize()=0;
};
using Assemble=HRESULT(WINAPI*)(const char*,UINT,const void*,void*,DWORD,Buffer**,Buffer**);
int main(int argc,char** argv) {
    if(argc!=4)return 1;
    std::ifstream file(argv[1],std::ios::binary);
    if(!file)return 2;
    std::string source((std::istreambuf_iterator<char>(file)),{});
    HMODULE dll=LoadLibraryW(L"d3dx9_43.dll");if(!dll)return 3;
    auto assemble=reinterpret_cast<Assemble>(GetProcAddress(dll,"D3DXAssembleShader"));
    if(!assemble)return 4;
    Buffer *code=nullptr,*error=nullptr;
    HRESULT hr=assemble(source.data(),UINT(source.size()),nullptr,nullptr,0,&code,&error);
    if(error){printf("%s\n",static_cast<char*>(error->GetBufferPointer()));error->Release();}
    if(FAILED(hr))return 5;
    FILE* out=nullptr;fopen_s(&out,argv[2],"w");if(!out){code->Release();return 6;}
    fprintf(out,"// Generated from %s by tools/assemble_shader.cpp.\n#pragma once\nnamespace bvr {\ninline constexpr DWORD %s[] = {\n",argv[1],argv[3]);
    auto words=static_cast<const DWORD*>(code->GetBufferPointer());
    for(DWORD i=0;i<code->GetBufferSize()/4;++i)fprintf(out,"0x%08lx,%s",words[i],(i%8==7)?"\n":" ");
    fprintf(out,"\n};\n}\n");fclose(out);code->Release();return 0;
}
