#include "FirstPerson.h"
#include "FirstPersonMath.h"
#include "Log.h"
#include <windows.h>
#include <cstdint>
#include <cstring>

namespace bvr {
namespace {
// cModel::transMesh(cCamera*): ECX is the model, [ebp+8] is the
// caller's camera, and the native function returns with ret 4.
using QueueFn=void(__thiscall*)(void*,void*);
using PartsFn=unsigned char*(__thiscall*)(void*,int);
unsigned char* imageBase=nullptr;
unsigned char* trampoline=nullptr;
QueueFn original=nullptr;
bool (*enabled)()=nullptr;
bool requested=false,queued=false,prepared=false,rendering=false;
bool loggedUnavailable=false;
float queuedEyes[3]{},renderEyes[3]{};
unsigned char* queuedPlayer=nullptr;
unsigned char* sessionPlayer=nullptr;
float eyeHeight=0;
Mat4 fixedHeading=Identity(),trackedHead=Identity();
Mat4 renderHeading=Identity();
bool renderHeadingValid=false;
bool headingValid=false,trackingValid=false;
using RootFn=void(__thiscall*)(void*);
using JoyFn=float(__thiscall*)(void*);
RootFn originalRoot=nullptr;
JoyFn originalJoy=nullptr;
unsigned char* rootRelay=nullptr;
unsigned char* joyRelay=nullptr;
constexpr unsigned char rootSignature[]={0x53,0x56,0x8B,0xF1,0x57,0x8D,0x7E,0x10};
constexpr unsigned char joySignature[]={0x55,0x8B,0xEC,0x83,0xE4,0xF0,0x0F,0x57,0xDB};
// cModel::transMesh: six whole bytes, no relative instructions.
constexpr unsigned char queueSignature[]={0x55,0x8B,0xEC,0x83,0xE4,0xF0,0x83,0xEC,0x64,0x53,0x56,0x57,0x8B,0xF9};
constexpr unsigned char partsSignature[]={0x8B,0x44,0x24,0x04,0x56,0x8B,0xF1,0x85,0xC0,0x79,0x0B};

void ExitExploration(const char* reason) {
    if(requested)Log("first person: returned to third person: %s; tap R3 to re-enter",reason);
    requested=queued=false;sessionPlayer=nullptr;headingValid=false;eyeHeight=0;
}
bool ExplorationPlayer(unsigned char*& player,const char*& reason) {
    __try {
        auto* camera=imageBase+0xB2DAE0;
        reason="not a player-controlled scene";
        if(*reinterpret_cast<unsigned char**>(imageBase+0x5729058)!=camera ||
           *reinterpret_cast<int*>(camera+0x778)!=1)return false;
        player=*reinterpret_cast<unsigned char**>(imageBase+0xAF5A60);
        reason="player unavailable or changed";
        if(!player || (sessionPlayer&&sessionPlayer!=player) ||
           *reinterpret_cast<unsigned char**>(camera+0x1130)!=player ||
           *reinterpret_cast<int*>(player+0x6B4)<=0)return false;
        reason="action, transformation or wall walking";
        const int move=*reinterpret_cast<int*>(player+0x34C);
        if(move<0||move>7 || *reinterpret_cast<int*>(player+0x96B24)!=0 ||
           *reinterpret_cast<unsigned char*>(player+0x95ED4)!=0)return false;
        // emList: 512 slots, high-water count at +0x800. Removal leaves
        // holes, so count alone is not an active-enemy count. Verified at
        // 499FC0/49A050 and the native target scan at 49A7C0.
        auto* list=imageBase+0x56569F0;
        const int count=*reinterpret_cast<int*>(list+0x800);
        reason="enemy list unavailable";
        if(count<0||count>512)return false;
        reason="active enemies";
        for(int i=0;i<count;++i) {
            auto* enemy=reinterpret_cast<unsigned char**>(list)[i];
            if(enemy&&enemy!=player&&FirstPersonEnemy(
                *reinterpret_cast<unsigned*>(enemy+0x374),
                *reinterpret_cast<unsigned*>(enemy+0x378),
                *reinterpret_cast<int*>(enemy+0x6B4)))return false;
        }
        return true;
    } __except(EXCEPTION_EXECUTE_HANDLER) {reason="scene data unavailable";return false;}
}
bool ControlPlayer(void* self) {
    if(!requested||self!=sessionPlayer)return false;
    unsigned char* player=nullptr;const char* reason="VR or tracking inactive";
    if(!enabled()||!trackingValid||!ExplorationPlayer(player,reason)) {
        ExitExploration(reason);return false;
    }
    return player==self;
}
void __fastcall Root(void* self,void*) {
    float yaw=0;
    if(ControlPlayer(self)&&headingValid&&FirstPersonBodyYaw(fixedHeading,trackedHead,yaw)) {
        // cModel::calcMatrix builds the root and skeleton from rot at +E0.
        // Apply before that work, never rotate already-queued mesh pointers.
        *reinterpret_cast<float*>(static_cast<unsigned char*>(self)+0xE4)=yaw;
    }
    originalRoot(self);
}
float __fastcall Joy(void* self,void*) {
    if(ControlPlayer(self)) {
        auto* p=static_cast<unsigned char*>(self);
        // This snapshot is copied anew by the player update before getJoyDir.
        // All input devices have already been combined; no global input or
        // persistent speed/animation values are changed.
        auto& x=*reinterpret_cast<float*>(p+0x94B54);
        auto& y=*reinterpret_cast<float*>(p+0x94B58);
        FirstPersonWalkVector(x,y);
    }
    return originalJoy(self);
}
unsigned char* Detour(unsigned char* entry,const void* hook,unsigned size) {
    auto* relay=static_cast<unsigned char*>(VirtualAlloc(nullptr,32,MEM_COMMIT|MEM_RESERVE,PAGE_EXECUTE_READWRITE));
    if(!relay)return nullptr;
    memcpy(relay,entry,size);relay[size]=0xE9;
    *reinterpret_cast<int32_t*>(relay+size+1)=static_cast<int32_t>((entry+size)-(relay+size+5));
    DWORD protection=0;
    if(!VirtualProtect(entry,size,PAGE_EXECUTE_READWRITE,&protection)){VirtualFree(relay,0,MEM_RELEASE);return nullptr;}
    entry[0]=0xE9;*reinterpret_cast<int32_t*>(entry+1)=static_cast<int32_t>(static_cast<const unsigned char*>(hook)-(entry+5));
    for(unsigned i=5;i<size;++i)entry[i]=0x90;
    DWORD ignored=0;VirtualProtect(entry,size,protection,&ignored);
    FlushInstructionCache(GetCurrentProcess(),relay,size+5);FlushInstructionCache(GetCurrentProcess(),entry,size);
    return relay;
}
bool Undo(unsigned char* entry,unsigned char*& relay,unsigned size) {
    if(!relay)return true;
    DWORD protection=0;if(!VirtualProtect(entry,size,PAGE_EXECUTE_READWRITE,&protection))return false;
    memcpy(entry,relay,size);DWORD ignored=0;VirtualProtect(entry,size,protection,&ignored);
    FlushInstructionCache(GetCurrentProcess(),entry,size);VirtualFree(relay,0,MEM_RELEASE);relay=nullptr;return true;
}

bool ReadPlayer(unsigned char*& player,float* eyes,const char*& reason,int& cameraMode) {
    // Live pointers only; never retain skeleton or mesh pointers across frames.
    __try {
        auto* camera=imageBase+0xB2DAE0;
        reason="main camera is not active";
        if(*reinterpret_cast<unsigned char**>(imageBase+0x5729058)!=camera)return false;
        // eCamMode::CAM_MODE_PL is 1. CAM_MODE_MAIN (0) is the idle/menu
        // mode, not normal gameplay. Verified against the live Steam camera.
        reason="camera is not in player-control mode";
        cameraMode=*reinterpret_cast<int*>(camera+0x778);
        if(cameraMode!=1)return false;
        reason="camera has no matching live player";
        player=*reinterpret_cast<unsigned char**>(imageBase+0xAF5A60);
        if(!player || *reinterpret_cast<unsigned char**>(camera+0x1130)!=player ||
           *reinterpret_cast<int*>(player+0x6B4)<=0)return false;
        if(!ExplorationPlayer(player,reason))return false;
        reason="head bone 4 is unavailable";
        const auto part=reinterpret_cast<PartsFn>(imageBase+0xA4DC0)(player,4);
        if(!part)return false;
        // gameCamTypeFPS at A92B20 uses head bone 4 and a 0.1-unit offset
        // along the player's root +Z. Reuse that eye point, never its mode.
        reason="head position is invalid or too far from player";
        for(int r=0;r<3;++r) {
            eyes[r]=*reinterpret_cast<float*>(part+0x30+4*r)+
                .1f**reinterpret_cast<float*>(player+0x30+4*r);
            const float root=*reinterpret_cast<float*>(player+0xD0+4*r);
            if(!std::isfinite(eyes[r])||!std::isfinite(root)||std::fabs(eyes[r]-root)>10.f)return false;
        }
        if(eyeHeight==0) {
            const float height=eyes[1]-*reinterpret_cast<float*>(player+0xD4);
            if(height<.5f||height>3.5f)return false;
            eyeHeight=height;
        }
        // Follow the character's root, not its animated head bob/sway.
        float yaw=*reinterpret_cast<float*>(player+0xE4);
        if(headingValid)FirstPersonBodyYaw(fixedHeading,trackedHead,yaw);
        eyes[0]=*reinterpret_cast<float*>(player+0xD0)+.1f*std::sin(yaw);
        eyes[1]=*reinterpret_cast<float*>(player+0xD4)+eyeHeight;
        eyes[2]=*reinterpret_cast<float*>(player+0xD8)+.1f*std::cos(yaw);
        // Unknown models keep their original view rather than put the eye
        // inside a head we cannot remove. Identify both body and face.
        reason="player mesh table is unavailable";
        const int count=*reinterpret_cast<int*>(player+0x1E8);
        auto** meshes=*reinterpret_cast<unsigned char***>(player+0x1E4);
        if(!meshes||count<1||count>512)return false;
        bool body=false,face=false;
        for(int i=0;i<count;++i)if(meshes[i]) {
            const char* name=*reinterpret_cast<const char**>(meshes[i]+0x48);
            if(!name)continue;
            if(!strcmp(name,"body"))body=true;
            if(!strcmp(name,"face_game"))face=true;
        }
        reason="player model lacks recognized body/face meshes";
        return body&&face;
    } __except(EXCEPTION_EXECUTE_HANDLER) {reason="player/camera data could not be read";return false;}
}
struct HeadMask {
    uint32_t* flags[512]{};
    uint32_t visible[512]{};
    unsigned count=0;
    bool Collect(unsigned char* player) {
        __try {
            const int meshCount=*reinterpret_cast<int*>(player+0x1E8);
            auto** meshes=*reinterpret_cast<unsigned char***>(player+0x1E4);
            if(meshCount<1||meshCount>512||!meshes)return false;
            for(int i=0;i<meshCount;++i)if(meshes[i]) {
                const char* name=*reinterpret_cast<const char**>(meshes[i]+0x48);
                if(!name||!FirstPersonHeadMesh(name))continue;
                auto* frame=*reinterpret_cast<unsigned char**>(meshes[i]+0x70);
                if(!frame)return false;
                flags[count]=reinterpret_cast<uint32_t*>(frame+0x10);
                visible[count]=*flags[count]&1u;++count;
            }
            return count!=0;
        } __except(EXCEPTION_EXECUTE_HANDLER) {return false;}
    }
    void Hide(){for(unsigned i=0;i<count;++i)*flags[i]&=~1u;}
    void Restore(){for(unsigned i=0;i<count;++i)*flags[i]=(*flags[i]&~1u)|visible[i];}
};
void __fastcall Queue(void* self,void*,void* camera) {
    if(requested&&!prepared&&self==sessionPlayer)ControlPlayer(self);
    if(!prepared && requested && enabled() &&
       self==*reinterpret_cast<void**>(imageBase+0xAF5A60)) {
        unsigned char* player=nullptr;float eyes[3]{};
        const char* reason="player changed during queue";int cameraMode=-1;
        if(ReadPlayer(player,eyes,reason,cameraMode) && self==player) {
            prepared=queued=true;queuedPlayer=player;memcpy(queuedEyes,eyes,sizeof(eyes));
            loggedUnavailable=false;
        } else if(!loggedUnavailable) {
            loggedUnavailable=true;
            Log("first person: waiting: %s (camera mode=%d)",reason,cameraMode);
            ExitExploration(reason);
        }
    }
    if(queued&&self==queuedPlayer) {
        HeadMask mask;
        if(mask.Collect(queuedPlayer)) {
            // The engine's mesh-enabled bit gates color, alpha, extra material
            // and shadow queues. Restore immediately after submission so no
            // visibility setting survives a toggle, costume change or unload.
            mask.Hide();original(self,camera);mask.Restore();return;
        }
        queued=false;
    }
    original(self,camera);
}
}
bool FirstPersonAvailable(){return original&&enabled&&enabled();}
void UpdateFirstPersonTracking(const Mat4& relativeGameHead,bool valid) {
    trackedHead=relativeGameHead;trackingValid=valid;
    if(!valid&&requested)ExitExploration("tracking or game focus lost");
}
void ToggleFirstPerson() {
    if(!FirstPersonAvailable())return;
    if(requested){ExitExploration("R3");return;}
    unsigned char* player=nullptr;float eyes[3]{};const char* reason="tracking unavailable";int mode=-1;
    eyeHeight=0;
    if(!trackingValid||!ReadPlayer(player,eyes,reason,mode)) {
        eyeHeight=0;Log("first person: R3 blocked: %s",reason);return;
    }
    requested=true;sessionPlayer=player;headingValid=false;
    loggedUnavailable=false;
    Log("first person: requested %s; applies with next player render queue",requested?"head camera":"third person");
}
void BeginFirstPersonFrame() {
    if(requested) {
        unsigned char* player=nullptr;const char* reason="player not queued";
        if(!queued||!ExplorationPlayer(player,reason))ExitExploration(reason);
    }
    const bool next=queued&&FirstPersonAvailable();
    if(next!=rendering)Log("first person: %s",next?"head camera + head-only mesh mask active":"original camera + complete model active");
    rendering=next;memcpy(renderEyes,queuedEyes,sizeof(renderEyes));
    renderHeading=fixedHeading;renderHeadingValid=headingValid;
    queued=prepared=false;queuedPlayer=nullptr;
}
bool GetFirstPersonCorrection(const Mat4& inverseProjection,Mat4& correction) {
    if(!rendering)return false;
    const auto& row=*reinterpret_cast<const Mat4*>(imageBase+0xB2DCE0);
    Mat4 vp{};for(int r=0;r<4;++r)for(int c=0;c<4;++c)vp.m[r][c]=row.m[c][r];
    const bool ok=FirstPersonCorrection(inverseProjection,vp,renderEyes,correction,&renderHeading,!renderHeadingValid);
    if(ok) {
        renderHeadingValid=true;
        if(requested&&!headingValid){fixedHeading=renderHeading;headingValid=true;}
    }
    return ok;
}
bool InstallFirstPerson(unsigned char* image,bool (*active)()) {
    if(original)return true;
    if(!image||!active)return false;
    const auto* dos=reinterpret_cast<const IMAGE_DOS_HEADER*>(image);
    if(dos->e_magic!=IMAGE_DOS_SIGNATURE)return false;
    const auto* nt=reinterpret_cast<const IMAGE_NT_HEADERS*>(image+dos->e_lfanew);
    if(nt->Signature!=IMAGE_NT_SIGNATURE||nt->FileHeader.Machine!=IMAGE_FILE_MACHINE_I386||
       nt->FileHeader.TimeDateStamp!=0x591AD330||nt->OptionalHeader.SizeOfImage<0x572905C)return false;
    auto* entry=image+0xA6990;
    if(memcmp(entry,queueSignature,sizeof(queueSignature))||
       memcmp(image+0xA4DC0,partsSignature,sizeof(partsSignature))||
       memcmp(image+0xA41C0,rootSignature,sizeof(rootSignature))||
       memcmp(image+0x5CDA00,joySignature,sizeof(joySignature))) {
        Log("first person: unsupported mesh/head accessor; shortcut disabled");return false;
    }
    rootRelay=Detour(image+0xA41C0,reinterpret_cast<const void*>(&Root),5);
    if(!rootRelay)return false;
    originalRoot=reinterpret_cast<RootFn>(rootRelay);
    joyRelay=Detour(image+0x5CDA00,reinterpret_cast<const void*>(&Joy),6);
    if(!joyRelay){Undo(image+0xA41C0,rootRelay,5);originalRoot=nullptr;return false;}
    originalJoy=reinterpret_cast<JoyFn>(joyRelay);
    auto* relay=static_cast<unsigned char*>(VirtualAlloc(nullptr,16,MEM_COMMIT|MEM_RESERVE,PAGE_EXECUTE_READWRITE));
    if(!relay){Undo(image+0xA41C0,rootRelay,5);Undo(image+0x5CDA00,joyRelay,6);return false;}
    memcpy(relay,entry,6);relay[6]=0xE9;
    *reinterpret_cast<int32_t*>(relay+7)=static_cast<int32_t>((entry+6)-(relay+11));
    FlushInstructionCache(GetCurrentProcess(),relay,11);
    DWORD protection=0;
    if(!VirtualProtect(entry,6,PAGE_EXECUTE_READWRITE,&protection)){
        VirtualFree(relay,0,MEM_RELEASE);Undo(image+0xA41C0,rootRelay,5);Undo(image+0x5CDA00,joyRelay,6);return false;
    }
    imageBase=image;enabled=active;trampoline=relay;original=reinterpret_cast<QueueFn>(relay);
    unsigned char patch[6]={0xE9,0,0,0,0,0x90};
    *reinterpret_cast<int32_t*>(patch+1)=static_cast<int32_t>(reinterpret_cast<unsigned char*>(&Queue)-(entry+5));
    memcpy(entry,patch,6);DWORD ignored=0;VirtualProtect(entry,6,protection,&ignored);
    FlushInstructionCache(GetCurrentProcess(),entry,6);
    Log("first person: exploration only; R3 toggle; native walk radius=450; tracked body yaw; stable root eye height");return true;
}
void RemoveFirstPerson() {
    if(!original)return;
    ExitExploration("uninstall");
    if(!Undo(imageBase+0xA41C0,rootRelay,5)||!Undo(imageBase+0x5CDA00,joyRelay,6))return;
    originalRoot=nullptr;originalJoy=nullptr;
    auto* entry=imageBase+0xA6990;DWORD protection=0;
    if(!VirtualProtect(entry,6,PAGE_EXECUTE_READWRITE,&protection))return;
    memcpy(entry,queueSignature,6);DWORD ignored=0;VirtualProtect(entry,6,protection,&ignored);
    FlushInstructionCache(GetCurrentProcess(),entry,6);VirtualFree(trampoline,0,MEM_RELEASE);
    original=nullptr;imageBase=trampoline=nullptr;enabled=nullptr;
    requested=queued=prepared=rendering=false;queuedPlayer=nullptr;
}
}
