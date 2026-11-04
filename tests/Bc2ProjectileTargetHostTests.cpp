#include "Bc2ProjectileTargetHost.h"
#include <cstdlib>
#include <fstream>
#include <iostream>
#include <iterator>
#include <sstream>
#include <vector>
#include <cstring>
#include <thread>
#include <algorithm>
using namespace fvr::bc2;
using namespace fvr::bc2::projectile307;
static unsigned checks=0,failures=0;
static void Check(bool value,const char* label){++checks;if(!value){++failures;std::cerr<<label<<'\n';}}

static Join Saved() {
    Join j;
    j.grant={7,9,2,3,45,0,0x70000,0x80000,0x7155b0,0x57aa0d,0x90100,0x90000,0x92000};
    j.owner={1,2,3,4,5,6,7,8,9,10,{},true,0};
    j.owner.equipment.weapon=9;j.owner.equipment.data=11;j.owner.equipment.persistence=12;j.owner.equipment.asset[0]='x';
    j.effects=j.config.effects=0x50000;j.configuration=j.config.config=0x60000;
    j.config.firingData=0x51000;j.config.function=0x5ff60;j.config.projectileData=0x81000;
    j.config.speed={0,0,300};j.config.direction={0,0,1};j.config.bulletsPerShell=j.config.bulletsPerShot=1;
    j.serverPlayer=13;j.serverSoldier=14;j.receiver={0x70000,0x80000,0x7155b0,0x81000,0x82000};return j;
}
static Current Live(const Join& j) {
    Current c;c.scopeSerial=j.grant.scopeSerial;c.invocation=j.grant.invocation;c.ordinal=j.grant.ordinal;
    c.helperIndex=j.grant.helperIndex;c.thread=j.grant.thread;c.token=j.grant.token;c.tokenValid=true;
    c.helperAccepted=true;c.effects=j.effects;c.configuration=j.configuration;c.serverPlayer=j.serverPlayer;c.serverSoldier=j.serverSoldier;
    c.scopeOwner=c.resolvedOwner=j.owner;c.scopeConfig=c.resolvedConfig=j.config;c.receiver=j.receiver;return c;
}
static void Identity() {
    const auto j=Saved();const auto c=Live(j);
    Check(Matches(j,j.grant,c),"Exact full join including valid token zero");
    const auto reject=[&](auto mutate,const char* label){auto changed=c;mutate(changed);Check(!Matches(j,j.grant,changed),label);};
    reject([](Current& x){++x.scopeSerial;},"Stack scope reuse refused");
    reject([](Current& x){++x.invocation;},"Same token new invocation refused");
    reject([](Current& x){++x.ordinal;},"Newer helper invalidates prior grant");
    reject([](Current& x){x.helperIndex=UINT32_MAX;},"Missing/busy/rejected current helper refused");
    reject([](Current& x){++x.helperIndex;},"Wrong accepted retained row refused");
    reject([](Current& x){++x.thread;},"Wrong thread refused");
    reject([](Current& x){++x.token;},"Wrong token refused");
    reject([](Current& x){x.tokenValid=false;},"Unread token zero refused");
    reject([](Current& x){x.helperAccepted=false;},"Unaccepted helper refused");
    reject([](Current& x){x.helperReturned=true;},"Ended server scope refused");
    for(auto member:{&Current::effects,&Current::configuration,&Current::serverPlayer,&Current::serverSoldier})
        reject([&](Current& x){++(x.*member);},"Server source mismatch refused");
    for(auto member:{&Owner::manager,&Owner::player,&Owner::weak,&Owner::soldier,&Owner::controlled,&Owner::entry,
                    &Owner::router,&Owner::cache,&Owner::weapon,&Owner::aim,&Owner::role}){
        reject([&](Current& x){++(x.scopeOwner.*member);},"Full scope owner field mismatch refused");
        reject([&](Current& x){++(x.resolvedOwner.*member);},"Full live owner field mismatch refused");
    }
    reject([](Current& x){x.scopeOwner.foot=false;},"Scope foot mismatch refused");
    reject([](Current& x){x.resolvedOwner.foot=false;},"Live foot mismatch refused");
    for(auto member:{&WeaponEquipmentIdentity::weapon,&WeaponEquipmentIdentity::data,&WeaponEquipmentIdentity::persistence}){
        reject([&](Current& x){++(x.scopeOwner.equipment.*member);},"Scope equipment identity refused");
        reject([&](Current& x){++(x.resolvedOwner.equipment.*member);},"Native equipment identity refused");
    }
    reject([](Current& x){x.scopeOwner.equipment.asset[4]='z';},"Full equipment array compared");
    reject([](Current& x){x.resolvedOwner.equipment.asset[4]='z';},"Live full equipment array compared");
    for(auto member:{&NativeMuzzleConfig::effects,&NativeMuzzleConfig::firingData,&NativeMuzzleConfig::function,
        &NativeMuzzleConfig::config,&NativeMuzzleConfig::muzzleExplosion,&NativeMuzzleConfig::projectileData,
        &NativeMuzzleConfig::bulletsPerShell,&NativeMuzzleConfig::bulletsPerShot}){
        reject([&](Current& x){++(x.scopeConfig.*member);},"Full scope config scalar refused");
        reject([&](Current& x){++(x.resolvedConfig.*member);},"Full native config scalar refused");
    }
    for(auto member:{&NativeMuzzleConfig::speed,&NativeMuzzleConfig::direction,&NativeMuzzleConfig::position})for(unsigned n=0;n<3;++n){
        reject([&](Current& x){(x.scopeConfig.*member)[n]+=1;},"Full scope config vectors refused");
        reject([&](Current& x){(x.resolvedConfig.*member)[n]+=1;},"Full native config vectors refused");
    }
    reject([](Current& x){x.scopeConfig.inheritedSpeed=1;},"Scope inherited speed refused");
    reject([](Current& x){x.resolvedConfig.inheritedSpeed=1;},"Native inherited speed refused");
    for(unsigned n=0;n<3;++n){
        reject([&](Current& x){x.scopeConfig.cameraFlags[n]=1;},"Scope camera flags refused");
        reject([&](Current& x){x.resolvedConfig.cameraFlags[n]=1;},"Native camera flags refused");
    }
    for(auto member:{&Receiver::receiver,&Receiver::vtable,&Receiver::target,&Receiver::descriptor,&Receiver::descriptorVtable})
        reject([&](Current& x){++(x.receiver.*member);},"Receiver/config descriptor identity refused");
    auto g=j.grant;++g.source;Check(!Matches(j,g,c),"Changed source versus retained row refused");
    g=j.grant;++g.returnAddress;Check(!Matches(j,g,c),"Changed caller versus retained row refused");
    g=j.grant;++g.stackLow;Check(!Matches(j,g,c),"Changed retained stack bound refused");
}
// Synthetic native memory contains only the existing ServerShootHook fields and
// MatchServerPlayer's established indexed relationship. No game is loaded.
struct ServerFixture {
    std::array<unsigned char,0x20000> bytes{};unsigned failRead=0,failType=0;
    Owner owner{};ServerIdentityBinding binding{0x20000,0x10000,0x22000,0x10000,0x23000};
    static constexpr unsigned Effects=0x18000,Player=0x14000,Soldier=0x17000;
    void Put(unsigned at,unsigned value){std::memcpy(bytes.data()+at-0x10000,&value,4);}
    static bool Read(void* p,unsigned at,void* dst,std::size_t size)noexcept {
        const auto& f=*static_cast<ServerFixture*>(p);
        if(at==f.failRead||at<0x10000||std::uint64_t(at)+size>0x30000)return false;
        std::memcpy(dst,f.bytes.data()+at-0x10000,size);return true;
    }
    static bool Type(void* p,unsigned object,const char* name)noexcept {
        if(object==static_cast<ServerFixture*>(p)->failType)return false;
        return (object==Effects&&!std::strcmp(name,"ServerWeaponFiringEffects"))||
            (object==0x19000&&!std::strcmp(name,"ClientWeaponFiringEffects"))||
            (object==0x1a000&&!std::strcmp(name,"WeaponFiringData"))||
            ((object==Soldier||object==0x1d000)&&!std::strcmp(name,"ServerSoldierEntity"));
    }
    ServerIdentityMemory Api(){return {{this,Read},Type};}
    ServerFixture(){
        owner=Saved().owner;owner.player=0x13000;owner.soldier=0x16000;owner.weapon=0x1b000;
        Put(0x10008,0x11000);Put(0x11000,binding.managerVtable);Put(0x11004,64);Put(0x1106c,0x12000);Put(0x12000,Player);
        Put(owner.weapon+0x38,0x19000);Put(Effects+0x10,0x1a000);Put(Effects+0x128,Player);Put(0x19010,0x1a000);
        Put(Player,0x21000);Put(0x21030,binding.controlledGetter);Put(Player+0xc3c,Soldier);Put(Player+0xc6c,Soldier);
        Put(Effects+0xd4,0x1c000);Put(0x1c004,0x1a000);Put(Soldier+0xc,0x24000);Put(owner.soldier+0xc,0x24000);
        // Valid replacement identities let tests distinguish fresh joins from
        // equality with the originally granted player/soldier.
        Put(0x15000,0x21000);Put(0x15c3c,Soldier);Put(0x15c6c,Soldier);Put(0x1d00c,0x24000);
    }
};
static void ServerRelationships(){
    ServerFixture f;const auto good=f.bytes;
    const auto read=[&]{return ReadServerIdentity(f.Api(),f.binding,f.owner,ServerFixture::Effects);};
    const auto initial=read();Check(initial&&initial->player==ServerFixture::Player&&initial->soldier==ServerFixture::Soldier,"Fresh server identity including paired player ID zero");
    for(const auto mutation:{std::pair{0x18128u,0x15000u},std::pair{0x14c3cu,0x1d000u},std::pair{0x14c6cu,0x1d000u},
        std::pair{0x1700cu,0x24004u},std::pair{0x1600cu,0x24004u},std::pair{0x1c004u,0x1a004u},
        std::pair{0x19010u,0x1a004u},std::pair{0x21030u,0x22004u},std::pair{0x13154u,1u},
        std::pair{0x14154u,1u},std::pair{0x12000u,0x15000u}}){
        f.bytes=good;f.Put(mutation.first,mutation.second);Check(!read(),"Changed ServerShootHook relationship refused");
    }
    f.bytes=good;
    for(const auto at:{0x18128u,0x14c3cu,0x14c6cu,0x13154u,0x14154u,0x12000u,0x1700cu,0x1600cu}){
        f.failRead=at;Check(!read(),"Unread live relationship fails closed");
    }
    f.failRead=0;
    for(const auto object:{0x18000u,0x19000u,0x1a000u,0x17000u}){
        f.failType=object;Check(!read(),"Existing reflected type evidence required afresh");
    }
}

using namespace fvr::bc2::projectile302;
// Value-only producer seam: exact embedded address and slot+4 are the only
// readable regions. Treating *(outer+2c) as the receiver cannot pass this fixture.
struct ProducerFixture {
    static constexpr Word Outer=0x10000,Table=0x20000,Target=0x30000;
    std::array<Word,4> words{Table,11,22,33};Word target=Target;
    std::array<Word,4> addresses{};unsigned reads=0,validations=0,failRead=0,denyValidation=0,tornRead=0,tornWord=0;
    bool ordered=true,returnBoundary=false;
    static bool __cdecl Read(void* p,Word at,void* dst,std::size_t size)noexcept {
        auto& f=*static_cast<ProducerFixture*>(p);++f.reads;
        f.ordered&=f.validations==f.reads;
        if(f.reads<=f.addresses.size())f.addresses[f.reads-1]=at;
        if(f.reads==f.failRead)return false;
        if(at==Outer+0x2c&&size==16){std::memcpy(dst,f.words.data(),size);
            if(f.reads==f.tornRead)static_cast<Word*>(dst)[f.tornWord]^=4;return true;}
        if(at==f.words[0]+4&&size==4){std::memcpy(dst,&f.target,4);
            if(f.reads==f.tornRead)*static_cast<Word*>(dst)^=4;return true;}
        return false;
    }
    static bool __cdecl Validate(void* p,const Grant& g,Boundary b)noexcept {
        auto& f=*static_cast<ProducerFixture*>(p);++f.validations;
        f.ordered&=(b==Boundary::Return)==f.returnBoundary;
        return g.receiver==Outer&&f.validations!=f.denyValidation;
    }
    ObserverApi Api(){return {this,Read,Validate,true};}
    Grant Offered(){auto g=Saved().grant;g.receiver=Outer;return g;}
};
static void ProducerDiagnostics(){
    ProducerFixture good;const auto sample=CopyProducerBoundary(good.Api(),good.Offered(),Boundary::Entry);
    Check(sample.status==SampleStatus::Complete&&sample.embedded==ProducerFixture::Outer+0x2c&&
        sample.words==good.words&&sample.slot4Target==ProducerFixture::Target&&sample.wordsRead&&sample.targetRead,
        "Producer copies embedded receiver words and its vtable slot four");
    Check(good.addresses==std::array<Word,4>{0x1002c,0x20004,0x1002c,0x20004}&&good.ordered&&good.validations==5,
        "Each bounded producer read revalidates full grant and both pointers repeat");
    ProducerFixture off;auto disabled=off.Api();disabled.producerDiagnostics=false;
    Check(CopyProducerBoundary(disabled,off.Offered(),Boundary::Entry).status==SampleStatus::Unattempted&&
        off.reads==0&&off.validations==0,"Disabled producer diagnostics perform no validation or read");
    for(unsigned n=1;n<=4;++n){ProducerFixture failed;failed.failRead=n;
        const auto s=CopyProducerBoundary(failed.Api(),failed.Offered(),Boundary::Entry);
        Check(s.status==SampleStatus::ReadFailure&&failed.reads==n&&failed.ordered&&
            s.wordsRead==(n>1)&&s.targetRead==(n>2),"Producer read failure keeps independent partial-read status");}
    for(unsigned n=1;n<=5;++n){ProducerFixture refused;refused.denyValidation=n;
        const auto s=CopyProducerBoundary(refused.Api(),refused.Offered(),Boundary::Entry);
        Check(s.status==SampleStatus::IdentityChanged&&refused.reads==n-1&&refused.ordered,
            "Changed owner/helper/thread/primary or deadline refuses next diagnostic read");}
    for(unsigned n:{3u,4u}){ProducerFixture torn;torn.tornRead=n;
        const auto s=CopyProducerBoundary(torn.Api(),torn.Offered(),Boundary::Entry);
        Check(s.status==SampleStatus::Torn&&torn.reads==n&&torn.ordered&&s.wordsRead&&s.targetRead,
            "Changed embedded vtable or slot target is torn without following changed pointer");}
    for(unsigned n=1;n<4;++n){ProducerFixture torn;torn.tornRead=3;torn.tornWord=n;
        Check(CopyProducerBoundary(torn.Api(),torn.Offered(),Boundary::Entry).status==SampleStatus::Torn&&torn.reads==3,
            "All four embedded words must be stable within a diagnostic sample");}
    ProducerFixture returned;returned.returnBoundary=true;
    Check(CopyProducerBoundary(returned.Api(),returned.Offered(),Boundary::Return,&sample).status==SampleStatus::Complete&&returned.ordered,
        "Return independently validates and retains same producer identity");
    for(bool vtable:{false,true}){ProducerFixture changed;changed.returnBoundary=true;
        if(vtable)changed.words[0]+=0x1000;else ++changed.target;
        Check(CopyProducerBoundary(changed.Api(),changed.Offered(),Boundary::Return,&sample).status==SampleStatus::IdentityChanged,
            "Stable changed producer identity is diagnostic only");}
    ProducerFixture overflow;auto bad=overflow.Offered();bad.receiver=UINT32_MAX-0x30;
    Check(CopyProducerBoundary(overflow.Api(),bad,Boundary::Entry).status==SampleStatus::InvalidArguments&&overflow.reads==0,
        "Overflowing embedded object range is never read");
}
static Lifecycle life; // Retained, like Observer/Original, for the entire process.
struct HostFixture {
    ServerFixture native{};Join saved{};
    alignas(16) unsigned char receiver[256]{},vtable[256]{},backing[128]{},producerTable[16]{};
    const unsigned char* primary=nullptr;
    unsigned mutationAt=0,mutationValue=0,secondAt=0,secondValue=0,failOnReturn=0;
    unsigned entryValidations=0,returnValidations=0,reads=0,completed=0;
    unsigned producerFault=0,embeddedReads=0;
    std::atomic<unsigned> nativeCalls{0};std::atomic<bool> block{false};
    HANDLE entered=CreateEventW(nullptr,TRUE,FALSE,nullptr),release=CreateEventW(nullptr,TRUE,FALSE,nullptr);
} host;
static constexpr Word ReturnMarker=0x311bc200;
static void __cdecl NativeBehavior(){
    ++host.nativeCalls;
    if(host.mutationAt)host.native.Put(host.mutationAt,host.mutationValue);
    if(host.secondAt)host.native.Put(host.secondAt,host.secondValue);
    host.native.failRead=host.failOnReturn;
    if(host.block.load()){SetEvent(host.entered);WaitForSingleObject(host.release,5000);}
}
__declspec(naked) static void __cdecl SyntheticOriginal(){
    __asm {
        call NativeBehavior
        mov eax, 0311bc200h
        ret 2ch
    }
}
struct HookFixture {
    unsigned creates=0,disables=0;bool failCreate=false,failDisable=false;
    void* seenTarget=nullptr;MachineEntry seenReplacement=nullptr;
    static bool Create(void* p,void* target,MachineEntry replacement,void** original)noexcept {
        auto& f=*static_cast<HookFixture*>(p);++f.creates;f.seenTarget=target;f.seenReplacement=replacement;
        if(f.failCreate)return false;*original=reinterpret_cast<void*>(SyntheticOriginal);return true;
    }
    static bool Disable(void* p,void* target)noexcept {
        auto& f=*static_cast<HookFixture*>(p);++f.disables;return target==f.seenTarget&&!f.failDisable;
    }
    HookApi Api(){return {this,Create,Disable};}
} hookFixture;
static void* hookSlot=nullptr;
static bool __cdecl ReadHost(void*,Word at,void* dst,std::size_t size)noexcept {
    ++host.reads;
    const bool embedded=at==reinterpret_cast<Word>(host.receiver)+0x2c;
    if(embedded){++host.embeddedReads;
        if(host.producerFault==1)return false;
        if(host.producerFault==3)RaiseException(EXCEPTION_ACCESS_VIOLATION,0,0,nullptr);}
    if(at==reinterpret_cast<Word>(host.producerTable)+4&&host.producerFault==2)return false;
    struct Region {const unsigned char* begin;std::size_t size;};
    for(const auto r:{Region{host.receiver,sizeof(host.receiver)},Region{host.vtable,sizeof(host.vtable)},
        Region{host.backing,sizeof(host.backing)},Region{host.primary,64},Region{host.producerTable,sizeof(host.producerTable)}}){
        const auto begin=reinterpret_cast<Word>(r.begin);
        if(r.begin&&at>=begin&&std::uint64_t(at)+size<=std::uint64_t(begin)+r.size){std::memcpy(dst,reinterpret_cast<void*>(at),size);
            if(embedded&&host.producerFault==4&&host.embeddedReads%2==0)static_cast<Word*>(dst)[0]^=4;return true;}
    }return false;
}
static bool __cdecl ValidateHost(void*,const Grant& grant,Boundary boundary)noexcept {
    if(boundary==Boundary::Entry)++host.entryValidations;else ++host.returnValidations;
    if(host.producerFault==5&&(boundary==Boundary::Entry?host.entryValidations:host.returnValidations)==3)return false;
    const auto native=ReadServerIdentity(host.native.Api(),host.native.binding,host.native.owner,ServerFixture::Effects);
    if(!native)return false;
    auto current=Live(host.saved);current.serverPlayer=native->player;current.serverSoldier=native->soldier;
    return life.Window(2000001)&&Matches(host.saved,grant,current);
}
static ObserverApi HostApi(){return {&host,ReadHost,ValidateHost,true};}
static bool Install(Lifecycle& l,Session& s,MachineEntry& original,void*& slot,HookFixture& hook,
    bool request=true,bool retained=true,bool signatures=true,bool helper=true){
    return l.Install(request,retained,signatures,helper,150,s,original,slot,reinterpret_cast<void*>(0x7155b0),HostApi(),hook.Api());
}
static void InstallLifecycle(){
    Lifecycle off;Session s;MachineEntry original=nullptr;void* slot=nullptr;HookFixture hook;
    Check(!Install(off,s,original,slot,hook,false)&&!off.attempted&&!off.Start()&&hook.creates==0,"Default observer cannot create or start");
    for(unsigned n=0;n<3;++n){Lifecycle refused;
        Check(!Install(refused,s,original,slot,hook,true,n!=0,n!=1,n!=2)&&hook.creates==0,"Host/signature/helper refusal precedes real create seam");
        Check(!Install(refused,s,original,slot,hook),"Refused installation cannot retry");
    }
    Lifecycle failed;hook.failCreate=true;
    Check(!Install(failed,s,original,slot,hook)&&hook.creates==1,"Injected create failure reaches production installer");
    Check(!failed.created&&!original&&!slot&&!s.api.read&&!s.enabled.load()&&!failed.Start(),"Create failure publishes no live dependency");
    Check(!Install(failed,s,original,slot,hook)&&hook.creates==1,"Create failure is one-shot");
    failed.Close();Check(failed.PollDrain()&&failed.Retired(),"Uncreated closed observer retires without disable");
    Check(Install(life,Observer,Original,hookSlot,hookFixture),"Production installer accepts retained dependencies");
    Check(hookFixture.creates==1&&hookFixture.seenReplacement==Thunk&&Original==SyntheticOriginal&&
        hookSlot==hookFixture.seenTarget&&Observer.api.context==&host,"Installer publishes actual adapter and original once");
    Check(life.Start()&&Observer.enabled.load()&&!life.Start(),"Production start enables session once");
    Check(!life.Window(1000000)&&life.deadline.load()==0,"Startup does not consume window");
    Check(life.Window(2000000,true)&&life.deadline.load()==2000150,"First eligible helper anchors window");
    Check(life.Window(2000149,true)&&life.deadline.load()==2000150,"Later helper cannot renew window");
    Check(!life.Window(2000150,true)&&!life.Window(1999999),"Expiry and pre-window time refused");
}
static void BoundaryMutation(unsigned at=0,unsigned value=0,unsigned second=0,unsigned secondValue=0,unsigned fail=0,unsigned producerFault=0){
    host.native=ServerFixture{};host.mutationAt=at;host.mutationValue=value;host.secondAt=second;host.secondValue=secondValue;host.failOnReturn=fail;
    host.entryValidations=host.returnValidations=0;
    host.producerFault=producerFault;host.embeddedReads=0;
    alignas(16) unsigned char primary[64]{};host.primary=primary;
    const auto put=[](unsigned char* p,Word v){std::memcpy(p,&v,4);};
    put(primary+0x30,0x7fc00000);put(host.receiver,reinterpret_cast<Word>(host.vtable));
    put(host.vtable+0xe8,reinterpret_cast<Word>(SyntheticOriginal));put(host.receiver+0x28,reinterpret_cast<Word>(host.backing));
    put(host.receiver+0x2c,reinterpret_cast<Word>(host.producerTable));put(host.receiver+0x30,11);
    put(host.receiver+0x34,22);put(host.receiver+0x38,33);put(host.producerTable+4,ProducerFixture::Target);
    host.saved=Saved();host.saved.owner=host.native.owner;host.saved.effects=host.saved.config.effects=ServerFixture::Effects;
    host.saved.serverPlayer=ServerFixture::Player;host.saved.serverSoldier=ServerFixture::Soldier;
    const auto* tib=reinterpret_cast<const NT_TIB*>(NtCurrentTeb());auto& g=host.saved.grant;
    g.invocation=Observer.offered+1;g.helperIndex=Observer.offered;g.thread=GetCurrentThreadId();
    g.receiver=reinterpret_cast<Word>(host.receiver);g.vtable=reinterpret_cast<Word>(host.vtable);g.target=reinterpret_cast<Word>(SyntheticOriginal);
    g.source=reinterpret_cast<Word>(primary)+0x30;g.stackLow=reinterpret_cast<Word>(tib->StackLimit);g.stackHigh=reinterpret_cast<Word>(tib->StackBase);
    host.saved.receiver.receiver=g.receiver;host.saved.receiver.vtable=g.vtable;host.saved.receiver.target=g.target;
    Entry entry{};entry.registers.ecx=g.receiver;entry.returnAddress=g.returnAddress;entry.args[1]=reinterpret_cast<Word>(primary);entry.args[5]=g.token;
    Frame frame{};frame.entry=&entry;Frame* address=&frame;
    __asm {mov eax,address}
    __asm {fxsave [eax]}
    const auto index=Observer.offered;const auto calls=host.nativeCalls.load();
    Check(Observer.Offer(g),"Accepted helper publishes adapter grant");Dispatch(&frame);
    const auto& row=Observer.rows[index];const bool changed=at||fail;
    const unsigned expectedValidations=(producerFault==1||producerFault==3||producerFault==5)?3:
        producerFault==2?4:producerFault==4?5:7;
    Check(host.nativeCalls.load()==calls+1&&frame.result.eax==ReturnMarker&&row.originalReturned,"Real adapter forwards retained original exactly once");
    Check(row.complete.load(std::memory_order_acquire)&&row.before.status==SampleStatus::Complete&&host.entryValidations==expectedValidations,
        "Entry reads both fresh native identity bookends");
    Check(row.outcome==(changed?Outcome::RejectedIdentity:Outcome::ReturnedUnchanged)&&host.returnValidations==(changed?1:expectedValidations),
        "Fresh return identity refuses native mutation while preserving original call");
    Check(!changed||row.after.status==SampleStatus::Unattempted,"Changed server identity refuses return payload read");
    const auto diagnostic=producerFault==5?SampleStatus::IdentityChanged:producerFault==4?SampleStatus::Torn:
        producerFault?SampleStatus::ReadFailure:SampleStatus::Complete;
    Check(row.before.producer.status==diagnostic&&
        row.after.producer.status==(changed?SampleStatus::Unattempted:diagnostic),"Producer faults have independent entry/return status");
    if(!changed){for(const auto* s:{&row.before,&row.after}){
        Check(s->status==SampleStatus::Complete&&s->identity.receiver==g.receiver&&s->identity.vtable==g.vtable&&
            s->identity.target==g.target&&s->identity.backing==reinterpret_cast<Word>(host.backing)&&s->identity.selected==s->identity.backing&&
            !s->identity.indexed&&s->identity.index==0&&s->flags==0&&s->source==g.source&&
            s->flagsRead&&s->sourceRead&&s->selectedRead&&s->velocityRead&&
            s->sourceBits==std::array<Word,3>{0x7fc00000,0,0}&&s->selectedBits==std::array<Word,3>{}&&s->velocityCacheBits==std::array<Word,3>{},
            "Producer diagnostics preserve every existing position sample field");}}
    Check(!life.Active()&&Depth==0,"Real adapter unregisters after return");++host.completed;
    host.primary=nullptr;host.mutationAt=host.secondAt=host.failOnReturn=host.producerFault=0;
}
static void BoundaryMutations(){
    BoundaryMutation();
    BoundaryMutation(0x18128,0x15000); // Effects changes server player.
    BoundaryMutation(0x18128,0x15000,0x12000,0x15000); // Fresh valid pair differs from saved grant.
    BoundaryMutation(0x14c3c,0x1d000);
    BoundaryMutation(0x14c6c,0x1d000);
    BoundaryMutation(0x14c3c,0x1d000,0x14c6c,0x1d000); // Both fresh links agree, saved soldier differs.
    BoundaryMutation(0x1700c,0x24004);
    BoundaryMutation(0x13154,1);
    BoundaryMutation(0x14154,1);
    BoundaryMutation(0x12000,0x15000);
    BoundaryMutation(0,0,0,0,0x18128);
    for(unsigned fault=1;fault<=5;++fault)BoundaryMutation(0,0,0,0,0,fault);
}
// Synthetic typed caller merely drives the tested ECX + eleven words / RET 2c
// gateway. The marker is fixture-only, not an inferred native return type.
static Word InvokeThunk(){
    using Call=Word(__thiscall*)(void*,Word,Word,Word,Word,Word,Word,Word,Word,Word,Word,Word);
    return reinterpret_cast<Call>(Thunk)(host.receiver,1,2,3,4,5,6,7,8,9,10,11);
}
static void StopLifecycle(){
    Check(host.entered&&host.release,"Drain synchronization fixtures available");
    HANDLE delayedReady=CreateEventW(nullptr,TRUE,FALSE,nullptr),delayedRelease=CreateEventW(nullptr,TRUE,FALSE,nullptr);
    std::atomic<Word> delayedResult{0},registeredResult{0};
    const auto api=Observer.api;const auto original=Original;const auto slot=hookSlot;
    const auto offers=Observer.offered;const auto calls=host.nativeCalls.load();
    std::thread delayed([&]{SetEvent(delayedReady);if(WaitForSingleObject(delayedRelease,5000)==WAIT_OBJECT_0)delayedResult=InvokeThunk();});
    Check(WaitForSingleObject(delayedReady,2000)==WAIT_OBJECT_0&&!life.Active(),"Caller parked before adapter registration");
    host.block=true;
    std::thread registered([&]{registeredResult=InvokeThunk();});
    Check(WaitForSingleObject(host.entered,2000)==WAIT_OBJECT_0&&life.Active(),"Real adapter registers callback held inside original");
    Check(Observer.Offer(host.saved.grant),"Keep incomplete row to verify final bounded report");
    life.Close();Check(!Observer.enabled.load()&&!life.Window(2000100,true)&&!life.Start(),"Production close is permanent");
    hookFixture.failDisable=true;
    Check(!life.Disable()&&hookFixture.disables==1&&!life.PollDrain()&&!life.Retired(),"Injected disable failure and active adapter block retirement");
    // A successful disable still cannot retire a registered callback.
    hookFixture.failDisable=false;
    Check(life.Disable()&&!life.PollDrain()&&!life.Retired(),"Successful disable cannot bypass registered drain");
    hookFixture.failDisable=true;Check(!life.Disable(),"Retain explicit failed-disable state until acknowledged");
    SetEvent(host.release);registered.join();host.block=false;
    Check(registeredResult==ReturnMarker&&life.PollDrain()&&!life.Retired(),"Drained original return cannot hide disable failure");
    std::ostringstream refused;Report(refused,life,Observer);
    Check(refused.str().find("\"snapshot_available\":0")!=std::string::npos,"Failed disable refuses final snapshot");
    hookFixture.failDisable=false;Check(life.Disable()&&life.PollDrain()&&life.Retired(),"Acknowledged disable plus real drain permits retirement");
    const auto reads=host.reads;const auto validations=host.entryValidations+host.returnValidations;
    SetEvent(delayedRelease);delayed.join();
    Check(delayedResult==ReturnMarker&&host.nativeCalls.load()==calls+2&&!life.Active(),"Delayed pre-registration caller still forwards retained trampoline after retirement");
    Check(host.reads==reads&&host.entryValidations+host.returnValidations==validations,"Closed delayed entry performs no borrowed native reads");
    Check(Original==original&&hookSlot==slot&&Observer.api.context==api.context&&Observer.api.read==api.read&&Observer.api.validate==api.validate&&
        Observer.api.producerDiagnostics==api.producerDiagnostics&&
        Observer.offered==offers+1&&life.session==&Observer&&life.entry==slot,"Original API session rows and hook entry retained without reset");
    Check(!life.Start()&&!Observer.Offer(host.saved.grant)&&!Install(life,Observer,Original,hookSlot,hookFixture)&&hookFixture.creates==1,"No post-retirement enable offer or reinstall");
    CloseHandle(delayedReady);CloseHandle(delayedRelease);CloseHandle(host.entered);CloseHandle(host.release);
}
static void Serialization(){
    std::ostringstream out;Report(out,life,Observer);const auto value=out.str();
    Check(value.find("\"complete_rows\":"+std::to_string(host.completed))!=std::string::npos&&value.find("\"incomplete_rows_omitted\":1")!=std::string::npos,"Only real acquire-completed adapter rows serialized");
    Check(value.find("\"bits\":[2143289344,0,0],\"finite_xyz\":null")!=std::string::npos,"Nonfinite numeric XYZ suppressed, raw bits retained");
    Check(value.find("\"producer_diagnostics\":1")!=std::string::npos&&
        value.find("\"factory_return_observed\":false,\"primary_nonretention_proven\":false")!=std::string::npos,
        "Report does not promote passive producer values to factory return or nonretention proof");
    Check(value.find("\"producer\":{\"status\":1,\"embedded_address\":"+std::to_string(reinterpret_cast<Word>(host.receiver)+0x2c))!=std::string::npos&&
        value.find("\"slot4_target_read\":1,\"slot4_target\":196608")!=std::string::npos&&
        value.find("\"producer\":{\"status\":3")!=std::string::npos&&value.find("\"producer\":{\"status\":4")!=std::string::npos&&
        value.find("\"producer\":{\"status\":5")!=std::string::npos,
        "Report preserves complete unread identity-changed and torn producer diagnostics separately");
}
static void Signatures(const char* image) {
    Check(!VerifySignatures({}, {}, {}, {}, {}),"Missing signatures refused");
    if(!image)return;
    // Optional exact executable file, read only. This never loads or calls it.
    std::ifstream input(image,std::ios::binary);std::vector<char> chars((std::istreambuf_iterator<char>(input)),{});
    if(chars.size()<TargetRva+TargetSize){Check(false,"Exact executable fixture unreadable");return;}
    const auto bytes=std::span<const std::byte>(reinterpret_cast<const std::byte*>(chars.data()),chars.size());
    const auto target=bytes.subspan(TargetRva,TargetSize),setter=bytes.subspan(SetterRva,SetterSize);
    Check(VerifySignatures(bytes,target,setter,target,setter),"Exact retained executable and complete signatures accepted");
    std::array<std::byte,TargetSize> badTarget{};std::copy(target.begin(),target.end(),badTarget.begin());badTarget[100]^=std::byte{1};
    Check(!VerifySignatures(bytes,target,setter,badTarget,setter),"Interior target mismatch refused");
    std::array<std::byte,SetterSize> badSetter{};std::copy(setter.begin(),setter.end(),badSetter.begin());badSetter[100]^=std::byte{1};
    Check(!VerifySignatures(bytes,target,setter,target,badSetter),"Interior setter mismatch refused");
    chars.back()^=1;Check(!VerifySignatures(bytes,target,setter,target,setter),"Whole executable identity mismatch refused");
}
// Disabled origin adapter: synthetic native storage only, never a game or hook.
namespace origin_fixture386 {
using origin386::Bits;
static Word nativeCalls = 0, seenError = 0;
static Entry seen{};
static FxState seenFx{}, outputFx{};
static Registers outputRegs{};
static constexpr Word originReturnError = 0x38660001, defaultMxcsr = 0x1f80;
static std::uint64_t nextScope = 1000000;
static Bits atNativeEntry{};
struct Fixture {
    alignas(16) unsigned char primary[64]{};
    OriginKey key{++nextScope, 1, nextScope, GetCurrentThreadId()};
    Entry entry{};
    Frame frame{};
    Bits wanted{0x41200000,0xc1a00000,0x41f00000}; // +10,-20,+30 native units.
    Bits mutation{0x42800000,0x42a00000,0x42c00000};
    unsigned prepares=0,validations=0;
    bool accept=true,valid=true,fault=false,mutate=false,raise=false;
    Fixture() {
        for (unsigned i=0;i<64;++i) primary[i]=static_cast<unsigned char>(i^0xa6);
        const Bits original{0x80000000,0x7fc01234,0x40400000};
        std::memcpy(primary+0x30,original.data(),12);
        entry.registers={0x17171717,0x16161616,0x15151515,0,0x1b1b1b1b,
            0x1d1d1d1d,0x1c1c1c1c,0x1a1a1a1a,0xed7};
        entry.returnAddress=0x38603860; // Synthetic caller marker, never a hook.
        for (unsigned i=0;i<11;++i) entry.args[i]=0x38600000+i;
        entry.args[1]=reinterpret_cast<Word>(primary);
        frame.entry=&entry;frame.incomingLastError=0x38661234;
    }
    Bits Xyz()const {Bits out{};std::memcpy(out.data(),primary+0x30,12);return out;}
};
static Fixture* current=nullptr;
static void ResetFloating() {
    __asm {fninit}
    __asm {ldmxcsr defaultMxcsr}
    __asm {cld}
}
static void MakeFx(FxState& f,unsigned seed) {
    ResetFloating();FxState* at=&f;
    __asm {fld1}
    __asm {fldpi}
    __asm {mov eax,at}
    __asm {fxsave [eax]}
    ResetFloating();
    const std::uint16_t control=seed?0x0b7f:0x077f;
    const Word mxcsr=seed?0x5fa0:0x3f81;
    std::memcpy(f.bytes,&control,2);std::memcpy(f.bytes+24,&mxcsr,4);
    for(unsigned i=0;i<128;++i) f.bytes[160+i]=static_cast<unsigned char>(i^seed);
}
static bool SameFx(const FxState& a,const FxState& b) {
    if(std::memcmp(a.bytes,b.bytes,5)||std::memcmp(a.bytes+6,b.bytes+6,22))return false;
    for(unsigned i=0;i<8;++i)if(std::memcmp(a.bytes+32+i*16,b.bytes+32+i*16,10))return false;
    return !std::memcmp(a.bytes+160,b.bytes+160,128);
}
static bool SameRegs(const Registers& a,const Registers& b) {
    return a.edi==b.edi&&a.esi==b.esi&&a.ebp==b.ebp&&a.ebx==b.ebx&&a.edx==b.edx&&
        a.ecx==b.ecx&&a.eax==b.eax&&((a.flags^b.flags)&0xed5)==0;
}
static void DirtyAdmissionState() {
    __asm {fninit}
    __asm {fldz}
    __asm {pxor xmm0,xmm0}
    __asm {pxor xmm7,xmm7}
    SetLastError(0xbadc0386);
}
static bool __cdecl Prepare(void*,const OriginKey& key,const Entry& entry,OriginAdmission& out)noexcept {
    auto& f=*current;++f.prepares;DirtyAdmissionState();
    if(f.fault)RaiseException(0xe0386002,0,0,nullptr);
    out.key=f.key;out.primary=reinterpret_cast<Word>(f.primary);out.absoluteNativeXyz=f.wanted;
    out.positiveNativeBoundBits=0x447a0000;out.proof={1,2,3,100}; // Synthetic authorization only.
    return f.accept&&key==f.key&&entry.args[1]==out.primary;
}
static bool __cdecl Validate(void*,const OriginKey& key,const Entry& entry,const OriginAdmission& a)noexcept {
    auto& f=*current;++f.validations;DirtyAdmissionState();
    return f.valid&&key==f.key&&a.key==f.key&&a.proof.evidence==1&&a.proof.activeExit==2&&
        a.proof.referenceSpace==3&&a.proof.originalDeadline==100&&entry.args[1]==a.primary;
}
static void __cdecl NativeBehavior386() {
    auto& f=*current;atNativeEntry=f.Xyz();
    if(f.mutate)std::memcpy(f.primary+0x30,f.mutation.data(),12);
    if(f.raise){const ULONG_PTR payload[]={0x386,reinterpret_cast<Word>(f.primary)};
        RaiseException(0xe0386010,0,2,payload);}
}
__declspec(naked) static void __cdecl Native386() {
    __asm {
        pushfd
        pushad
        fxsave seenFx
        mov eax,fs:[34h]
        mov seenError,eax
        mov esi,esp
        mov edi,offset seen
        mov ecx,21
        cld
        rep movsd
        inc nativeCalls
        fninit
        ldmxcsr defaultMxcsr
        call NativeBehavior386
        lea esp,[esp+36]
        fxrstor outputFx
        mov eax,originReturnError
        mov fs:[34h],eax
        push dword ptr [outputRegs+32]
        popfd
        mov edi,[outputRegs]
        mov esi,[outputRegs+4]
        mov ebp,[outputRegs+8]
        mov ebx,[outputRegs+16]
        mov edx,[outputRegs+20]
        mov ecx,[outputRegs+24]
        mov eax,[outputRegs+28]
        ret 2ch
    }
}
static void Arm(Fixture& f) {
    current=&f;MakeFx(f.frame.incoming,0);MakeFx(outputFx,0x59);
    outputRegs={0x77777777,0x66666666,0x55555555,0,0xbbbbbbbb,0xdddddddd,
        0xcccccccc,0xaaaaaaaa,0xe93};
}
static int ExceptionFilter(EXCEPTION_POINTERS* exception)noexcept {
    const auto& r=*exception->ExceptionRecord;
    if(r.ExceptionCode!=0xe0386010)return EXCEPTION_CONTINUE_SEARCH;
    Check(r.ExceptionFlags==0&&r.NumberParameters==2&&r.ExceptionInformation[0]==0x386&&
        r.ExceptionInformation[1]==reinterpret_cast<Word>(current->primary),"Native SEH code flags and payload preserved");
    return EXCEPTION_EXECUTE_HANDLER;
}
static bool DispatchCaught(Frame* f) {
    __try {Dispatch(f);}
    __except(ExceptionFilter(GetExceptionInformation())) {return true;}
    return false;
}
static void DispatchAndClose(Frame* f,OriginScope* scope) {
    __try {Dispatch(f);}
    __finally {scope->Close();}
}
static bool CatchAndClose(Frame* f,OriginScope* scope) {
    __try {DispatchAndClose(f,scope);}
    __except(ExceptionFilter(GetExceptionInformation())) {return true;}
    return false;
}
static void Invoke(Fixture& f,bool exception=false) {
    const auto before=f.entry;const auto calls=nativeCalls;
    const bool caught=DispatchCaught(&f.frame);ResetFloating();
    Check(caught==exception&&nativeCalls==calls+1,"Origin commit/refusal/SEH forwards exactly once");
    Check(!std::memcmp(&before,&f.entry,sizeof(Entry)),"Origin preserves caller entry and all eleven argument words");
    Check(!std::memcmp(seen.args,f.entry.args,sizeof(seen.args))&&seen.args[1]==f.entry.args[1],
        "Native receives identical P address and eleven words; earlier aliases retain same object");
    Check(SameRegs(seen.registers,f.entry.registers)&&SameFx(seenFx,f.frame.incoming)&&seenError==f.frame.incomingLastError,
        "Origin callbacks cannot alter native incoming registers flags x87 XMM MXCSR or LastError");
    if(!exception)Check(SameRegs(f.frame.result,outputRegs)&&SameFx(f.frame.returned,outputFx)&&
        f.frame.returnedLastError==originReturnError,"Origin preserves normal native outgoing machine state");
    Check(Depth==0&&Observer.active==0,"Origin native return or SEH drains adapter depth/activity");
}
static void Transactions() {
    const Bits before{0x7fc01234,0x80000000,0x40400000},desired{1,2,3};
    for(unsigned failed=1;failed<=3;++failed){
        Bits memory=before;unsigned writes=0,reads=0;
        const auto result=origin386::Commit(before,desired,[&](unsigned i,Word v)noexcept {
            ++writes;if(writes==failed){memory[i]=(memory[i]&0xffffff00)|(v&0xff);return false;}
            memory[i]=v;return true;
        },[&](unsigned i,Word& v)noexcept {++reads;v=memory[i];return true;});
        Check(result==origin386::WriteResult::Restored&&memory==before&&writes==failed+3&&reads==3,
            "Partial pre-native write at each lane restores exact NaN/signed-zero bytes before forwarding");
    }
    for(bool unread:{false,true}){
        Bits memory=before;unsigned writes=0;
        const auto result=origin386::Commit(before,desired,[&](unsigned i,Word v)noexcept {
            ++writes;if(writes==2){memory[i]=v;return false;}
            if(!unread&&writes>2)return false;memory[i]=v;return true;
        },[&](unsigned i,Word& v)noexcept {v=memory[i];return !unread;});
        Check(result==origin386::WriteResult::Unresolved,"Failed or unread rollback is never an untouched fallback");
    }
    Check(origin386::BoundedAbsolute({0,0x80000000,0x3f800000},0x3f800000),"Finite bound includes signed zero and endpoints");
    for(const auto bad:{0x7f800000u,0xff800000u,0x7fc01234u,0x40000000u})
        Check(!origin386::BoundedAbsolute({bad,0,0},0x3f800000),"Nonfinite or out-of-bound authorized XYZ refused");
    Check(!origin386::BoundedAbsolute({0,0,0},0)&&!origin386::BoundedAbsolute({0,0,0},0x7f800000),"Reviewed finite positive native bound required");
}
static void Run() {
    Transactions();
    // TEST ONLY: prior offline observer fixture is fully drained. Production
    // never replaces Original or resets/reopens a process-lifetime capability.
    const auto original=Original;Original=Native386;
    Origin.api={nullptr,Prepare,Validate};
    {
        Fixture f;Arm(f);const auto initial=f.Xyz();OriginScope shot(f.key);
        Invoke(f);Check(shot.Attempted()&&shot.Status()==OriginStatus::Disabled&&f.prepares==0,"Disabled first entry records attempt without admission callback");
        Origin.enabled.store(true);Invoke(f);
        Check(f.Xyz()==initial&&f.prepares==0,"Later enabling cannot arm an already attempted whole shot");
    }
    {
        Fixture f;Arm(f);const auto initial=f.Xyz();OriginScope shot(f.key);f.accept=false;Invoke(f);
        f.accept=true;Invoke(f);Check(f.Xyz()==initial&&f.prepares==1&&shot.Status()==OriginStatus::Refused,
            "Missing active exit on first entry forbids late admission on later projectile");
    }
    {
        Fixture f;Arm(f);std::array<unsigned char,64> before{};std::memcpy(before.data(),f.primary,64);
        const auto* alias=f.primary;OriginScope shot(f.key);Invoke(f);
        Check(shot.Status()==OriginStatus::Committed&&shot.NativeStarted()&&shot.Primary()==reinterpret_cast<Word>(alias)&&
            atNativeEntry==f.wanted&&f.Xyz()==f.wanted,"One committed absolute origin is visible through original P aliases");
        Check(!std::memcmp(before.data(),f.primary,0x30)&&!std::memcmp(before.data()+0x3c,f.primary+0x3c,4),
            "All basis and padding bytes remain exact");
        f.mutate=true;Invoke(f);f.mutate=false;Invoke(f);
        Check(atNativeEntry==f.mutation&&f.Xyz()==f.mutation&&f.prepares==1&&f.validations==1,
            "Native XYZ mutation survives all subsequent projectiles; no absolute reapply or copyback");
    }
    for(unsigned refusal=0;refusal<5;++refusal){
        Fixture f;Arm(f);const auto initial=f.Xyz();OriginScope shot(f.key);
        if(refusal==0)f.valid=false;
        if(refusal==1)++f.key.primaryLifetime;
        if(refusal==2)f.wanted[1]=0x7fc01234;
        if(refusal==3)f.wanted[2]=0x7f000000;
        if(refusal==4)f.fault=true;
        Invoke(f);Check(f.Xyz()==initial&&shot.Status()!=OriginStatus::Committed,
            "Changed live scope, invalid proof/value, or admission SEH refuses before any native write");
    }
    {
        Fixture f;Arm(f);const auto initial=f.Xyz();++f.key.thread;
        {OriginScope shot(f.key);Invoke(f);
            Check(shot.Status()==OriginStatus::StaleScope&&f.prepares==0&&f.Xyz()==initial,"Foreign-thread shot identity never reaches admission");}
        --f.key.thread;
        {OriginScope retry(f.key);Invoke(f);
            Check(retry.Status()==OriginStatus::StaleScope&&f.prepares==0,"Correcting a refused scope cannot reconstruct eligibility for that invocation");}
    }
    for(unsigned bad=0;bad<3;++bad){
        Fixture f;Arm(f);const auto initial=f.Xyz();OriginScope shot(f.key);
        f.entry.args[1]=bad==0?reinterpret_cast<Word>(f.primary)+4:bad==1?0x10000u:UINT32_MAX-31;
        Invoke(f);Check(f.Xyz()==initial&&f.prepares==0&&shot.Status()==OriginStatus::Refused,
            "Unaligned, nonstack or overflowing P extent refused without admission or borrowed read");
    }
    {
        Fixture f;Arm(f);const auto initial=f.Xyz();const auto oldKey=f.key;
        {OriginScope first(f.key);Invoke(f);}
        std::memcpy(f.primary+0x30,initial.data(),12);
        ++f.key.primaryLifetime; // Same native address/new claimed lifetime cannot reopen same whole shot.
        {OriginScope stale(f.key);Invoke(f);Check(stale.Status()==OriginStatus::StaleScope&&f.Xyz()==initial,"Recycled scope/address cannot reset first-entry eligibility");}
        f.key={++nextScope,1,nextScope,GetCurrentThreadId()};
        {OriginScope fresh(f.key);const auto live=f.key;f.key=oldKey;Invoke(f);f.key=live;
            Check(f.Xyz()==initial&&fresh.Status()==OriginStatus::Refused,"Old proof rejected at same P address under a fresh whole-shot identity");}
        f.key={++nextScope,1,nextScope,GetCurrentThreadId()};
        {OriginScope fresh(f.key);Invoke(f);Check(fresh.Status()==OriginStatus::Committed,"Fresh authorized P lifetime can reuse stack address");}
    }
    {
        Fixture f;Arm(f);const auto initial=f.Xyz();OriginScope outer(f.key);
        const OriginKey nested{++nextScope,1,nextScope,GetCurrentThreadId()};
        {OriginScope inner(nested);Invoke(f);Check(inner.Status()==OriginStatus::Nested,"Nested shot scope never borrows outer authorization");}
        Invoke(f);Check(f.Xyz()==initial&&outer.Status()==OriginStatus::Nested,"Ambiguous nested ownership cannot later reopen outer commit");
    }
    {
        Fixture f;Arm(f);OriginScope shot(f.key);f.mutate=f.raise=true;Invoke(f,true);
        Check(shot.Status()==OriginStatus::Committed&&shot.NativeStarted()&&f.Xyz()==f.mutation,
            "Native SEH preserves committed edit and intervening native XYZ effects");
        f.mutate=f.raise=false;Invoke(f);Check(atNativeEntry==f.mutation&&f.prepares==1,"Exception cannot renew or reset origin attempt");
    }
    {
        Fixture f;Arm(f);f.mutate=true;
        {OriginScope shot(f.key);Invoke(f);}
        Check(f.Xyz()==f.mutation,"Whole-shot scope exit performs no restoration of native effects");
    }
    {
        Fixture f;Arm(f);f.mutate=f.raise=true;OriginScope shot(f.key);
        const auto calls=nativeCalls;
        Check(CatchAndClose(&f.frame,&shot),"Host explicit finally closes scope during propagated native SEH");
        ResetFloating();shot.Close();
        Check(f.Xyz()==f.mutation&&nativeCalls==calls+1&&shot.Status()==OriginStatus::Committed&&Depth==0,
            "SEH finally cleanup is idempotent and never rolls back native effects");
        f.key={++nextScope,1,nextScope,GetCurrentThreadId()};f.mutate=f.raise=false;
        OriginScope next(f.key);Invoke(f);
        Check(next.Status()==OriginStatus::Committed,"Explicit SEH closure leaves no dangling prior scope");
    }
    const auto diagnosticOffers=Observer.offered;const auto diagnosticReads=host.reads;
    for(unsigned i=0;i<96;++i){Fixture f;Arm(f);OriginScope shot(f.key);Invoke(f);
        Check(shot.Status()==OriginStatus::Committed,"Origin eligibility is per shot with no 64-record diagnostic budget");}
    Check(!Observer.enabled.load()&&Observer.offered==diagnosticOffers&&host.reads==diagnosticReads,
        "Origin remains independent of closed optional observer window and diagnostic storage");
    {
        Fixture f;Arm(f);f.mutate=true;OriginScope shot(f.key);Invoke(f);Origin.Close();
        f.mutate=false;Invoke(f);Check(f.Xyz()==f.mutation,"VR/capability stop never restores committed or native XYZ writes");
    }
    Original=original;current=nullptr;ResetFloating();
}
} // namespace origin_fixture386
static void OriginCandidateFixtures(){origin_fixture386::Run();}

int main(int argc,char** argv){
    if(!FloatingStateSupported()){std::cerr<<"Required x86 floating state unavailable\n";return EXIT_FAILURE;}
    Identity();ServerRelationships();ProducerDiagnostics();InstallLifecycle();BoundaryMutations();StopLifecycle();Serialization();Signatures(argc>1?argv[1]:nullptr);OriginCandidateFixtures();
    std::cout<<checks<<" checks, "<<failures<<" failures\n";return failures?EXIT_FAILURE:EXIT_SUCCESS;}
