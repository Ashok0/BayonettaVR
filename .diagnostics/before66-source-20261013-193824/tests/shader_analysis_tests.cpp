#include "ShaderAnalysis.h"
#include <cstdio>
#include <fstream>
#include <iterator>

static bool Check(const char* name, const std::string& shader, int expected) {
    const int actual=bvr::NamedSceneCameraMatrix(shader);
    if(actual==expected) return true;
    std::printf("%s: expected %d, got %d\n",name,expected,actual);
    return false;
}

int main() {
    if(!Check("shadow only", "// g_ShadowViewProjection c28 4\n dp4 o0.x, v0, c28\n",-2) ||
       !Check("light only", "// g_LightViewProjection c32 4\n",-2) ||
       !Check("shadow plus scene", "// g_ShadowViewProjection c28 4\n// g_ViewProjection c8 4\n",8) ||
       !Check("scene plus shadow", "// g_WorldViewProjMatrix c24 4\n// g_ShadowViewProjection c28 4\n",24) ||
       !Check("deformed geometry", "// Rmodelviewproj c24 4\n",24) ||
       !Check("unnamed", "dp4 o0.x, v0, c24\n",-1)) return 1;
    const char* paths[]={"tests/fixtures/shadow_position.asm","tests/fixtures/scene_position.asm","tests/fixtures/skinned_position.asm"};
    const int expected[]={-2,24,8};
    for(int i=0;i<3;++i) {
        std::ifstream in(paths[i],std::ios::binary);
        if(!in) { std::printf("missing fixture %s\n",paths[i]); return 2; }
        const std::string text((std::istreambuf_iterator<char>(in)),std::istreambuf_iterator<char>());
        if(!Check(paths[i],text,expected[i])) return 3;
    }
    std::puts("shader tests passed (including captured scene/shadow/skinned shaders)");
    return 0;
}
