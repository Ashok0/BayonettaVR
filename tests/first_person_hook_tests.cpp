#include "FirstPerson.h"
#include "FirstPersonMath.h"
#include <windows.h>
#include <cassert>
#include <cstdint>
#include <cstdio>
#include <cstdarg>
#include <cstring>
#include <atomic>
#include <thread>
#include <vector>
#include "FirstPersonCostumeFixture.h"
using namespace bvr;
static std::atomic<bool> quiet{false};
namespace bvr {void Log(const char* f,...){if(quiet)return;va_list a;va_start(a,f);vprintf(f,a);va_end(a);puts("");}}
static bool active=true;
static unsigned passed=0;
static unsigned faces=0,bodies=0,chestHairs=0,guns=0;
static thread_local void* expectedCamera=nullptr;
static std::atomic<bool> pauseSubmission{false};
static HANDLE submissionEntered=nullptr,submissionRelease=nullptr;
static float observedYaw=0,observedX=0,observedY=0;
static bool checkCostume=false;
static uint64_t expectedCostumeVisible=0;
static void __fastcall RecordRoot(unsigned char* p,void*) {observedYaw=*reinterpret_cast<float*>(p+0xE4);}
static float __fastcall RecordJoy(unsigned char* p,void*) {
    observedX=*reinterpret_cast<float*>(p+0x94B54);observedY=*reinterpret_cast<float*>(p+0x94B58);return 1.234f;
}
static void __fastcall Record(unsigned char* player,void*,void* camera) {
    if(pauseSubmission.exchange(false)) {
        SetEvent(submissionEntered);
        assert(WaitForSingleObject(submissionRelease,5000)==WAIT_OBJECT_0);
    }
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
        if(checkCostume)assert(bool(*reinterpret_cast<uint32_t*>(frame+0x10)&1)==bool(expectedCostumeVisible&(1ull<<i)));
        if(!(*reinterpret_cast<uint32_t*>(frame+0x10)&1))continue;
        const char* name=*reinterpret_cast<const char**>(meshes[i]+0x48);
        if(!strcmp(name,"face_game"))++faces;
        if(!strcmp(name,"body"))++bodies;
        if(!strcmp(name,"boin_hair"))++chestHairs;
        if(!strcmp(name,"gun"))++guns;
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
    // Hand/heel guns are separate weapon models. Exercise both native
    // submission orders and every hidden air state, then landing/toggle.
    unsigned char weapons[4][0xB00]{},gunMeshes[4][0xB0]{},gunFrames[4][0x20]{};
    unsigned char* gunTables[4][1]{};
    for(int i=0;i<4;++i) {
        gunTables[i][0]=gunMeshes[i];Put(gunMeshes[i],0x48,"gun");Put(gunMeshes[i],0x70,gunFrames[i]);
        Put(gunFrames[i],0x10,uint32_t(0x04001031));
        Put(weapons[i],0x1E8,1);Put(weapons[i],0x1E4,gunTables[i]);
        Put(weapons[i],0x370,0x30000u+unsigned(i));Put(weapons[i],0xA00,player);Put(weapons[i],0x120,head);
    }
    auto gunCall=[&](int i,bool visible) {
        const auto before=guns;call(weapons[i]);assert(guns==before+(visible?1:0));
        assert(*reinterpret_cast<uint32_t*>(gunFrames[i]+0x10)==0x04001031);
    };
    const int gunMoves[]={11,18,22,25,12,0};
    for(int move:gunMoves) {
        Put(player,0x34C,move);const bool jumping=move==11||move==18||move==22||move==25;
        gunCall(0,!jumping);gunCall(1,!jumping);call(player);gunCall(2,!jumping);gunCall(3,!jumping);
        BeginFirstPersonFrame();assert(GetFirstPersonCorrection(invP,correction));
    }
    Put(player,0x34C,11);
    Put(weapons[0],0xA00,otherPlayer);gunCall(0,true); // Another character's gun.
    Put(weapons[0],0xA00,static_cast<unsigned char*>(nullptr));gunCall(0,true);
    Put(weapons[0],0xA00,player);Put(weapons[0],0x120,static_cast<unsigned char*>(nullptr));gunCall(0,true);
    Put(weapons[0],0x120,head);Put(weapons[0],0xA08,1u);gunCall(0,true); // Detached/thrown.
    Put(weapons[0],0xA08,0u);Put(weapons[0],0x370,0x20000u);gunCall(0,true); // Non-weapon.
    Put(weapons[0],0x370,0x30000u);gunCall(0,false);
    Put(gunFrames[0],0x10,uint32_t(0x04001030));const auto beforeHidden=guns;
    call(weapons[0]);assert(guns==beforeHidden&&*reinterpret_cast<uint32_t*>(gunFrames[0]+0x10)==0x04001030);
    Put(gunFrames[0],0x10,uint32_t(0x04001031));
    BeginFirstPersonFrame();assert(GetFirstPersonCorrection(invP,correction));
    ToggleFirstPerson();gunCall(0,true);call(player);BeginFirstPersonFrame();assert(!GetFirstPersonCorrection(invP,correction));
    ToggleFirstPerson();gunCall(0,false);call(player);BeginFirstPersonFrame();assert(GetFirstPersonCorrection(invP,correction));
    // An attachment arriving first still validates combat and scene changes.
    Put(enemy,0x6B4,100);gunCall(0,true);call(player);BeginFirstPersonFrame();assert(!GetFirstPersonCorrection(invP,correction));
    Put(enemy,0x6B4,0);enter();Put(image,0xAF5A60,otherPlayer);
    gunCall(0,true);BeginFirstPersonFrame();assert(!GetFirstPersonCorrection(invP,correction));
    Put(image,0xAF5A60,player);enter();
    // Deterministically overlap a masked worker submission with frame reset.
    // .69 let BeginFirstPersonFrame clear queuedPlayer while Queue still
    // depended on it. Reset must wait until submission and flag restore finish.
    submissionEntered=CreateEvent(nullptr,TRUE,FALSE,nullptr);
    submissionRelease=CreateEvent(nullptr,TRUE,FALSE,nullptr);
    HANDLE frameEntered=CreateEvent(nullptr,TRUE,FALSE,nullptr);
    HANDLE frameDone=CreateEvent(nullptr,TRUE,FALSE,nullptr);
    pauseSubmission=true;
    std::thread submitter([&]{call(weapons[0]);});
    assert(WaitForSingleObject(submissionEntered,5000)==WAIT_OBJECT_0);
    std::thread resetter([&]{SetEvent(frameEntered);BeginFirstPersonFrame();SetEvent(frameDone);});
    assert(WaitForSingleObject(frameEntered,5000)==WAIT_OBJECT_0);
    const bool resetBlocked=WaitForSingleObject(frameDone,50)==WAIT_TIMEOUT;
    SetEvent(submissionRelease);submitter.join();resetter.join();
    if(!resetBlocked){puts("FAIL: frame reset overlapped an unfinished masked submission");ExitProcess(42);}
    assert(*reinterpret_cast<uint32_t*>(gunFrames[0]+0x10)==0x04001031);
    CloseHandle(submissionEntered);CloseHandle(submissionRelease);CloseHandle(frameEntered);CloseHandle(frameDone);
    // Stress queue workers against reset, tracking, turning and R3 changes.
    // Immutable scene fixtures isolate synchronization inside FirstPerson.
    quiet=true;
    std::atomic<bool> start{false};
    auto worker=[&](void* model) {
        expectedCamera=image+0xB2DAE0;
        while(!start.load())std::this_thread::yield();
        for(int i=0;i<10000;++i)assert(CheckedCall(image+0xA6990,model,expectedCamera));
    };
    const auto beforeConcurrent=passed;
    std::thread bodyWorker(worker,player),gunWorker(worker,weapons[0]);
    start=true;
    for(int i=0;i<10000;++i) {
        BeginFirstPersonFrame();UpdateFirstPersonTracking(Identity(),true);
        if(i%8==0)ToggleFirstPerson();
        UpdateFirstPersonTurn(.5f,3000+i);GetFirstPersonCorrection(invP,correction);
    }
    bodyWorker.join();gunWorker.join();quiet=false;
    assert(passed==beforeConcurrent+20000);
    for(int i=0;i<3;++i)assert(*reinterpret_cast<uint32_t*>(frames[i]+0x10)==0x04001031);
    assert(*reinterpret_cast<uint32_t*>(gunFrames[0]+0x10)==0x04001031);
    UpdateFirstPersonTracking(Identity(),false);UpdateFirstPersonTracking(Identity(),true);enter();
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
    // Every installed human costume, including generic/dummy face names and
    // body0/body2. Reverse native material order to verify topology matching.
    unsigned costumeCases=0,costumeRules=0;
    for(const auto& costume:kFirstPersonCostumes) {
        const unsigned index=unsigned(&costume-kFirstPersonCostumes);
        unsigned char modelData[128]{};memcpy(modelData,"WMB\0",4);Put(modelData,12,costume.vertices);
        Put(modelData,92,costume.polygons);Put(modelData,96,costume.shaders);Put(modelData,88,costume.meshData);
        std::vector<unsigned char> meshData(64*0xB0),frameData(64*0xA8),fixedData(64*0xBC),batchData(64*64*0x60);
        unsigned char* table[64]{};
        for(unsigned i=0;i<costume.meshCount;++i) {
            table[i]=meshData.data()+i*0xB0;
            Put(table[i],0x48,kCostumeFixtureNames[index][i]);
            Put(table[i],0x70,frameData.data()+i*0xA8);Put(frameData.data()+i*0xA8,0x10,0x04001031u);
            Put(table[i],0x6C,fixedData.data()+i*0xBC);
        }
        for(unsigned n=0;n<costume.ruleCount;++n) {
            const auto& rule=kFirstPersonBodyRules[costume.ruleStart+n];
            auto* fixed=fixedData.data()+rule.mesh*0xBC;
            auto* batches=batchData.data()+rule.mesh*64*0x60;
            Put(fixed,0x98,rule.meshBatches);Put(fixed,0x9C,batches);
            auto* batch=batches+(rule.meshBatches-1-rule.batch)*0x60;
            Put(batch,0,reinterpret_cast<void*>(0x10000+rule.mesh*0x100+rule.batch));
            Put(batch,4,1000+rule.batch*20000);Put(batch,0xC,rule.indexCount);Put(batch,0x20,rule.primitiveType);
            Put(batch,0x2C,rule.vertexMax);Put(batch,0x30,rule.vertexMin);
        }
        Put(player,0x1B0,modelData);Put(player,0x1E8,int(costume.meshCount));Put(player,0x1E4,table);
        const uint64_t all=(1ull<<costume.meshCount)-1;
        checkCostume=true;expectedCostumeVisible=all&~costume.headMask;
        enter();assert(FirstPersonCostumeDrawsActive()==bool(costume.ruleCount));
        for(unsigned n=0;n<costume.ruleCount;++n) {
            const auto& rule=kFirstPersonBodyRules[costume.ruleStart+n];
            auto* buffer=reinterpret_cast<void*>(0x10000+rule.mesh*0x100+rule.batch);
            const unsigned start=1000+rule.batch*20000,primitives=rule.primitiveType==5?rule.indexCount-2:rule.indexCount/3;
            const auto* actual=GetFirstPersonCostumeDraw(buffer,rule.primitiveType,start,primitives);
            assert(actual&&actual->rangeCount==rule.rangeCount&&actual->vertexMin==rule.vertexMin&&actual->vertexMax==rule.vertexMax);
            assert(!memcmp(kFirstPersonRanges+actual->rangeStart,kFirstPersonRanges+rule.rangeStart,rule.rangeCount*sizeof(FirstPersonDrawRange)));
            assert(!GetFirstPersonCostumeDraw(buffer,rule.primitiveType,start+1,primitives));
            assert(!GetFirstPersonCostumeDraw(buffer,rule.primitiveType,start,primitives+1));
            assert(!GetFirstPersonCostumeDraw(reinterpret_cast<void*>(1),rule.primitiveType,start,primitives));
            ++costumeRules;
        }
        for(unsigned i=0;i<costume.meshCount;++i)assert(*reinterpret_cast<uint32_t*>(frameData.data()+i*0xA8+0x10)==0x04001031);
        Put(player,0x34C,11);expectedCostumeVisible=0;call(player);BeginFirstPersonFrame();assert(GetFirstPersonCorrection(invP,correction));
        Put(player,0x34C,0);expectedCostumeVisible=all&~costume.headMask;call(player);BeginFirstPersonFrame();
        Put(frameData.data()+8*0xA8,0x10,0x04001030u);expectedCostumeVisible&=~(1ull<<8);
        call(player);BeginFirstPersonFrame();assert(*reinterpret_cast<uint32_t*>(frameData.data()+8*0xA8+0x10)==0x04001030u);
        Put(frameData.data()+8*0xA8,0x10,0x04001031u);
        ToggleFirstPerson();expectedCostumeVisible=all;call(player);BeginFirstPersonFrame();assert(!FirstPersonCostumeDrawsActive());
        if(costume.ruleCount) {
            const auto& rule=kFirstPersonBodyRules[costume.ruleStart];
            // A changed/missing native material table cannot leave a partial
            // head mask paired with an active first-person camera.
            Put(fixedData.data()+rule.mesh*0xBC,0x98,0u);
            ToggleFirstPerson();call(player);BeginFirstPersonFrame();
            assert(!GetFirstPersonCorrection(invP,correction)&&!FirstPersonCostumeDrawsActive());
        }
        checkCostume=false;++costumeCases;
    }
    Put(player,0x1B0,static_cast<unsigned char*>(nullptr));Put(player,0x1E8,3);Put(player,0x1E4,meshes);
    printf("Costumes: %u complete layouts, %u reordered native batch matches, jumps/landing/toggle and exact visibility restoration passed.\n",costumeCases,costumeRules);
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
    puts("First person hook: x86 ABI, body/gun masking, owner isolation, blocked concurrent frame reset, 20000 worker submissions with 10000 frame resets, scene fallback and restoration passed.");
}
