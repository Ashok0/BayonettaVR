#include "FirstPerson.h"
#include "FirstPersonMath.h"
#include <windows.h>
#include <cassert>
#include <cstdint>
#include <cstdio>
#include <cstdarg>
#include <cstring>
using namespace bvr;
namespace bvr {void Log(const char* f,...){va_list a;va_start(a,f);vprintf(f,a);va_end(a);puts("");}}
static bool active=true;
static unsigned passed=0;
static unsigned faces=0,bodies=0,chestHairs=0;
static void* expectedCamera=nullptr;
static float observedYaw=0,observedX=0,observedY=0;
static void __fastcall RecordRoot(unsigned char* p,void*) {observedYaw=*reinterpret_cast<float*>(p+0xE4);}
static float __fastcall RecordJoy(unsigned char* p,void*) {
    observedX=*reinterpret_cast<float*>(p+0x94B54);observedY=*reinterpret_cast<float*>(p+0x94B58);return 1.234f;
}
static void __fastcall Record(unsigned char* player,void*,void* camera) {
    if(camera!=expectedCamera) {
        puts("FAIL: transMesh dropped or changed its stack camera argument");
        fflush(stdout);
        ExitProcess(1);
    }
    ++passed;
    const int count=*reinterpret_cast<int*>(player+0x1E8);
    auto** meshes=*reinterpret_cast<unsigned char***>(player+0x1E4);
    for(int i=0;i<count;++i) {
        auto* frame=*reinterpret_cast<unsigned char**>(meshes[i]+0x70);
        if(!(*reinterpret_cast<uint32_t*>(frame+0x10)&1))continue;
        const char* name=*reinterpret_cast<const char**>(meshes[i]+0x48);
        if(!strcmp(name,"face_game"))++faces;
        if(!strcmp(name,"body"))++bodies;
        if(!strcmp(name,"boin_hair"))++chestHairs;
    }
}
template<class T> static void Put(unsigned char* p,size_t o,T value){memcpy(p+o,&value,sizeof(value));}
// Check ESP and nonvolatile registers independently of the compiler's
// function epilogue, which could otherwise conceal incorrect ret cleanup.
__declspec(naked) static int __cdecl CheckedCall(void*,void*,void*) {
    __asm {
        push ebp
        mov ebp,esp
        push ebx
        push esi
        push edi
        mov ebx,esp
        mov esi,13579BDFh
        mov edi,2468ACE0h
        push dword ptr [ebp+16]
        mov ecx,dword ptr [ebp+12]
        call dword ptr [ebp+8]
        xor eax,eax
        cmp esp,ebx
        jne done
        cmp esi,13579BDFh
        jne done
        cmp edi,2468ACE0h
        jne done
        inc eax
    done:
        lea esp,[ebp-12]
        pop edi
        pop esi
        pop ebx
        pop ebp
        ret
    }
}
int main() {
    auto* image=static_cast<unsigned char*>(VirtualAlloc(nullptr,0x5800000,MEM_COMMIT|MEM_RESERVE,PAGE_EXECUTE_READWRITE));assert(image);
    auto* dos=reinterpret_cast<IMAGE_DOS_HEADER*>(image);dos->e_magic=IMAGE_DOS_SIGNATURE;dos->e_lfanew=0x100;
    auto* nt=reinterpret_cast<IMAGE_NT_HEADERS*>(image+0x100);nt->Signature=IMAGE_NT_SIGNATURE;
    nt->FileHeader.Machine=IMAGE_FILE_MACHINE_I386;nt->FileHeader.TimeDateStamp=0x591AD330;nt->OptionalHeader.SizeOfImage=0x5800000;
    const unsigned char queue[]={0x55,0x8B,0xEC,0x83,0xE4,0xF0,0x83,0xEC,0x64,0x53,0x56,0x57,0x8B,0xF9};
    const unsigned char parts[]={0x8B,0x44,0x24,0x04,0x56,0x8B,0xF1,0x85,0xC0,0x79,0x0B};
    const unsigned char root[]={0x53,0x56,0x8B,0xF1,0x57,0x8D,0x7E,0x10};
    const unsigned char joy[]={0x55,0x8B,0xEC,0x83,0xE4,0xF0,0x0F,0x57,0xDB};
    memcpy(image+0xA41C0,root,sizeof(root));memcpy(image+0x5CDA00,joy,sizeof(joy));
    memcpy(image+0xA6990,queue,sizeof(queue));memcpy(image+0xA4DC0,parts,sizeof(parts));
    nt->FileHeader.TimeDateStamp=0;assert(!InstallFirstPerson(image,[]{return active;}));nt->FileHeader.TimeDateStamp=0x591AD330;
    image[0xA6990]=0;assert(!InstallFirstPerson(image,[]{return active;}));image[0xA6990]=queue[0];
    assert(InstallFirstPerson(image,[]{return active;}));assert(InstallFirstPerson(image,[]{return active;}));
    // The trampoline executes the REAL overwritten x86 prologue then this
    // instrumented continuation. Record verifies callee stack cleanup/args.
    const unsigned char continuation[]={0x8B,0xE5,0x5D,0xE9}; // mov esp,ebp; pop ebp; jmp Record
    memcpy(image+0xA6996,continuation,sizeof(continuation));
    Put(image,0xA699A,static_cast<int32_t>(reinterpret_cast<uintptr_t>(&Record)-reinterpret_cast<uintptr_t>(image+0xA699E)));
    // Instrument all continuations AFTER signature validation. The real
    // overwritten prologues still execute through the installed trampolines.
    const unsigned char rootEnd[]={0x5F,0x5E,0x5B,0xE9};
    memcpy(image+0xA41C5,rootEnd,4);
    Put(image,0xA41C9,static_cast<int32_t>(reinterpret_cast<uintptr_t>(&RecordRoot)-reinterpret_cast<uintptr_t>(image+0xA41CD)));
    memcpy(image+0x5CDA06,continuation,4);
    Put(image,0x5CDA0A,static_cast<int32_t>(reinterpret_cast<uintptr_t>(&RecordJoy)-reinterpret_cast<uintptr_t>(image+0x5CDA0E)));
    static unsigned char player[0x98000]{};
    unsigned char head[0x118]{},body[0xB0]{},face[0xB0]{},chestHair[0xB0]{};
    unsigned char* meshes[]={body,face,chestHair};
    unsigned char frames[3][0x20]{};
    for(int i=0;i<3;++i){Put(meshes[i],0x70,frames[i]);Put(frames[i],0x10,uint32_t(0x04001031));}
    Put(body,0x48,"body");Put(face,0x48,"face_game");Put(chestHair,0x48,"boin_hair");
    Put(player,0x1E8,3);Put(player,0x1E4,meshes);Put(player,0x6B4,100);Put(player,0x38,1.f);Put(head,0x34,1.7f);
    // Isolated accessor returns the animated head; runtime signature was
    // already checked against the Steam bytes above before replacing it.
    image[0xA4DC0]=0xB8;Put(image,0xA4DC1,head);image[0xA4DC5]=0xC2;image[0xA4DC6]=4;image[0xA4DC7]=0;
    Put(image,0xAF5A60,player);Put(image,0x5729058,image+0xB2DAE0);Put(image,0xB2DAE0+0x1130,player);
    Put(image,0xB2DAE0+0x778,1); // CAM_MODE_PL: normal player-controlled gameplay.
    const auto p=PerspectiveFovLH(.9f,1.f,.1f,10000.f);Put(image,0xB2DCE0,Transpose(p));
    FlushInstructionCache(GetCurrentProcess(),image,0x5800000);
    // Native callers push the camera; transMesh reads [ebp+8] and returns
    // with ret 4. Exercise that ABI even when first person is switched off.
    auto call=[&](void* model) {
        // Auxiliary passes must keep their own camera, too.
        expectedCamera=image+0xB2DAE0+(passed&1?0x2000:0);
        assert(CheckedCall(image+0xA6990,model,expectedCamera));
    };
    Mat4 invP{},correction{};assert(Invert(p,invP));
    call(player);assert(passed==1&&faces==1&&bodies==1&&chestHairs==1);
    BeginFirstPersonFrame();assert(!GetFirstPersonCorrection(invP,correction));
    UpdateFirstPersonTracking(Identity(),true);ToggleFirstPerson();call(player);
    if(!(passed==2&&faces==1&&bodies==2&&chestHairs==2)) {
        puts("FAIL: R3 request blocked in CAM_MODE_PL (normal gameplay)");fflush(stdout);ExitProcess(1);
    }
    for(int i=0;i<3;++i)assert(*reinterpret_cast<uint32_t*>(frames[i]+0x10)==0x04001031); // Immediate exact restoration.
    static unsigned char otherPlayer[sizeof(player)]{};memcpy(otherPlayer,player,sizeof(player));
    call(otherPlayer);assert(passed==3&&faces==2&&bodies==3); // Other models unchanged.
    BeginFirstPersonFrame();assert(GetFirstPersonCorrection(invP,correction));
    assert(std::fabs(correction.m[1][3]+1.7f)<.001f&&std::fabs(correction.m[2][3]+.1f)<.001f);
    const auto first=correction;
    ToggleFirstPerson(); // Request cannot alter one eye of a queued pair.
    assert(GetFirstPersonCorrection(invP,correction));assert(!memcmp(&first,&correction,sizeof(first)));
    call(player);assert(passed==4&&faces==3&&bodies==4);
    BeginFirstPersonFrame();assert(!GetFirstPersonCorrection(invP,correction));
    auto rootCall=reinterpret_cast<void(__thiscall*)(void*)>(image+0xA41C0);
    auto joyCall=reinterpret_cast<float(__thiscall*)(void*)>(image+0x5CDA00);
    auto enter=[&] {ToggleFirstPerson();call(player);BeginFirstPersonFrame();assert(GetFirstPersonCorrection(invP,correction));};
    enter();
    // Animation bob no longer moves the camera; root movement still does.
    Put(head,0x30,3.f);Put(head,0x34,2.4f);
    call(player);BeginFirstPersonFrame();assert(GetFirstPersonCorrection(invP,correction));
    assert(std::fabs(correction.m[0][3])<.001f&&std::fabs(correction.m[1][3]+1.7f)<.001f);
    Put(player,0xD0,2.f);Put(player,0xD4,1.f);
    call(player);BeginFirstPersonFrame();assert(GetFirstPersonCorrection(invP,correction));
    assert(std::fabs(correction.m[0][3]+2.f)<.001f&&std::fabs(correction.m[1][3]+2.7f)<.001f);
    Mat4 turn=Identity();turn.m[0][0]=turn.m[2][2]=0;turn.m[0][2]=1;turn.m[2][0]=-1;
    UpdateFirstPersonTracking(turn,true);rootCall(player);assert(std::fabs(observedYaw-1.5707963f)<.001f);
    assert(GetFirstPersonCorrection(invP,correction));const auto beforeTurn=correction;
    UpdateFirstPersonTurn(1,1000);UpdateFirstPersonTurn(1,1100);
    rootCall(player);assert(std::fabs(observedYaw-1.727876f)<.001f); // HMD 90 + stick 9 degrees.
    assert(GetFirstPersonCorrection(invP,correction));assert(!memcmp(&beforeTurn,&correction,sizeof(correction)));
    call(player);BeginFirstPersonFrame();assert(GetFirstPersonCorrection(invP,correction));
    assert(std::fabs(correction.m[0][2])>.1f); // Both eyes commit the turn on the next queue.
    UpdateFirstPersonTurn(1,2000);rootCall(player);assert(std::fabs(observedYaw-1.727876f)<.001f); // No catch-up after stall.
    UpdateFirstPersonTurn(-1,2100);rootCall(player);assert(std::fabs(observedYaw-1.5707963f)<.001f);
    Put(otherPlayer,0xE4,.25f);rootCall(otherPlayer);assert(observedYaw==.25f);
    Put(player,0x94B54,1000.f);Put(player,0x94B58,1000.f);
    assert(std::fabs(joyCall(player)-1.234f)<.001f);
    assert(std::fabs(std::hypot(observedX,observedY)-450.f)<.01f&&observedX==observedY);
    Put(otherPlayer,0x94B54,1000.f);Put(otherPlayer,0x94B58,1000.f);joyCall(otherPlayer);assert(observedX==1000&&observedY==1000);
    // A live enemy forces an exit before native input/body processing.
    unsigned char enemy[0x700]{};Put(enemy,0x374,0x20u);Put(enemy,0x6B4,100);
    Put(image,0x56569F0,enemy);Put(image,0x56569F0+0x800,1);
    Put(player,0x94B54,1000.f);Put(player,0x94B58,0.f);joyCall(player);assert(observedX==1000);
    call(player);BeginFirstPersonFrame();assert(!GetFirstPersonCorrection(invP,correction));
    ToggleFirstPerson();call(player);BeginFirstPersonFrame();assert(!GetFirstPersonCorrection(invP,correction));
    Put(enemy,0x6B4,0); // Corpses/holes do not permanently block exploration.
    call(player);BeginFirstPersonFrame();assert(!GetFirstPersonCorrection(invP,correction)); // No automatic re-entry.
    enter();
    // Invalid lists and player replacement fail closed, with no write to
    // either model or access through a retained pointer after map loading.
    Put(image,0x56569F0+0x800,513);rootCall(player);call(player);BeginFirstPersonFrame();
    assert(!GetFirstPersonCorrection(invP,correction));
    Put(image,0x56569F0+0x800,1);enter();
    Put(image,0xAF5A60,otherPlayer);rootCall(player);BeginFirstPersonFrame();
    assert(!GetFirstPersonCorrection(invP,correction));Put(image,0xAF5A60,player);enter();
    const int traversal[]={11,11,22,16,10,12,13,14,18,24,25,26,0};
    for(unsigned i=0;i<sizeof(traversal)/sizeof(traversal[0]);++i) {
        Put(player,0x34C,traversal[i]);Put(player,0xD4,1.f+float(i%4));
        Put(head,0x34,2.4f+float(i%4));
        Put(player,0x94B54,1000.f);Put(player,0x94B58,0.f);joyCall(player);
        assert(observedX==(traversal[i]==0?450.f:1000.f));
        const auto beforeBodies=bodies,beforeFaces=faces,beforeHair=chestHairs;
        rootCall(player);call(player);BeginFirstPersonFrame();assert(GetFirstPersonCorrection(invP,correction));
        const bool jumping=traversal[i]==11||traversal[i]==18||traversal[i]==22||traversal[i]==25;
        assert(faces==beforeFaces);
        assert(bodies==beforeBodies+(jumping?0:1)&&chestHairs==beforeHair+(jumping?0:1));
        for(int n=0;n<3;++n)assert(*reinterpret_cast<uint32_t*>(frames[n]+0x10)==0x04001031);
        assert(std::fabs(correction.m[1][3]+2.4f+float(i%4))<.001f);
    }
    // Full-body masking also covers unnamed mesh entries, restores a mesh
    // that was already hidden, and never affects another model or third person.
    Put(player,0x34C,11);Put(chestHair,0x48,static_cast<const char*>(nullptr));
    auto visibleBodies=bodies;call(player);assert(bodies==visibleBodies);
    Put(frames[2],0x10,uint32_t(0x04001030));
    call(player);assert(bodies==visibleBodies);
    assert(*reinterpret_cast<uint32_t*>(frames[2]+0x10)==0x04001030);
    Put(chestHair,0x48,"boin_hair");Put(frames[2],0x10,uint32_t(0x04001031));
    call(otherPlayer);assert(bodies==visibleBodies+1);
    BeginFirstPersonFrame();assert(GetFirstPersonCorrection(invP,correction));
    ToggleFirstPerson();visibleBodies=bodies;call(player);assert(bodies==visibleBodies+1);
    BeginFirstPersonFrame();assert(!GetFirstPersonCorrection(invP,correction));
    enter();visibleBodies=bodies;call(player);assert(bodies==visibleBodies);
    BeginFirstPersonFrame();Put(player,0x34C,12);call(player);assert(bodies==visibleBodies+1);
    BeginFirstPersonFrame();assert(GetFirstPersonCorrection(invP,correction));
    Put(player,0x34C,56);rootCall(player);call(player);BeginFirstPersonFrame();assert(!GetFirstPersonCorrection(invP,correction)); // Air attack still exits.
    Put(player,0x34C,0);enter();
    UpdateFirstPersonTracking(Identity(),false);call(player);BeginFirstPersonFrame();assert(!GetFirstPersonCorrection(invP,correction));
    UpdateFirstPersonTracking(Identity(),true);enter();
    Put(image,0xB2DAE0+0x778,2);call(player);BeginFirstPersonFrame();assert(!GetFirstPersonCorrection(invP,correction));
    Put(image,0xB2DAE0+0x778,1);enter();
    Put(face,0x48,"unknown_costume_face");call(player);BeginFirstPersonFrame();assert(!GetFirstPersonCorrection(invP,correction));
    Put(face,0x48,"face_game");enter();active=false;call(player);BeginFirstPersonFrame();assert(!GetFirstPersonCorrection(invP,correction));
    active=true;enter();
    Put(frames[1],0x10,uint32_t(0x04001030));
    call(player);BeginFirstPersonFrame();assert(*reinterpret_cast<uint32_t*>(frames[1]+0x10)==0x04001030);
    BeginFirstPersonFrame();assert(!GetFirstPersonCorrection(invP,correction));
    const int nonPlayerModes[]={0,2,3,4,5};
    for(int mode:nonPlayerModes) {
        Put(image,0xB2DAE0+0x778,mode);ToggleFirstPerson();call(player);BeginFirstPersonFrame();
        assert(!GetFirstPersonCorrection(invP,correction));
    }
    Put(image,0xB2DAE0+0x778,1);
    // Run the actual Steam transMesh body for unloaded/empty models. Its
    // early exit still performs ret 4, which caused .58 to corrupt the stack.
    FILE* native=nullptr;fopen_s(&native,"tests/fixtures/firstperson/trans_mesh.bin","rb");assert(native);
    assert(fseek(native,6,SEEK_SET)==0);
    assert(fread(image+0xA6996,1,0x530-6,native)==0x530-6);fclose(native);
    FlushInstructionCache(GetCurrentProcess(),image+0xA6996,0x530-6);
    unsigned char emptyModel[0x300]{};
    for(int i=0;i<1000;++i) {
        expectedCamera=i&1?image+0xB2DAE0:nullptr;
        assert(CheckedCall(image+0xA6990,emptyModel,expectedCamera));
    }
    RemoveFirstPerson();assert(!FirstPersonAvailable());assert(!memcmp(image+0xA6990,queue,6));
    assert(!memcmp(image+0xA41C0,root,5)&&!memcmp(image+0x5CDA00,joy,6));
    VirtualFree(image,0,MEM_RELEASE);
    puts("First person hook: x86 trampoline/ABI, head-only suppression, body/other models, paired transitions, cutscene/menu/unknown-model fallback and restoration passed.");
}
