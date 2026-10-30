#include "Test.h"
#include "Bc2MuzzleAdmission.h"
#include "Bc2ProjectileLaunch.h"
#include "Bc2ServerFireObservation.h"
#include "fvr/engine/PeImage.h"
#include "../profiles/opening426/NambuMuzzle.generated.h"
#include "../profiles/opening426/NambuAuthoredGrip.generated.h"
#include <cstring>
#include <limits>
#include <fstream>
#include <sstream>
using namespace fvr;using namespace fvr::bc2;
namespace {
math::Matrix4 At(float x=0,float y=0,float z=0){math::Matrix4 m{};for(unsigned n=0;n<4;++n)m.values[n][n]=1;m.values[3]={x,y,z,1};return m;}
template<std::size_t N> void Text(std::array<char,N>& out,std::string_view s){out={};std::copy(s.begin(),s.end(),out.begin());}
struct Fixture {
    static constexpr std::int64_t now=1000000000;
    MuzzleEnrollmentProfile profile=generated_muzzle::MuzzleEnrollments[0];
    AuthoredGripProfile grip=generated::AuthoredGrips[0];
    std::shared_ptr<SelectedMeshesSnapshot> selected=std::make_shared<SelectedMeshesSnapshot>();
    WeaponEquipmentIdentity equipment{};AuthoredGripOwner owner{10,11,12,13,14,20,now+90000000};
    math::Matrix4 weapon=At(-558,27,-77),flash{};
    Fixture(){equipment.weapon=15;equipment.data=16;equipment.persistence=0;Text(equipment.asset,profile.asset);
        auto& s=*selected;s.owner={9,10,11,15,12,18,14};s.sequence=19;s.observedNs=now-10000000;s.deadlineNs=now+200000000;
        s.weaponData=16;s.stateTypeInfo=1;s.meshTypeInfo=2;s.inventory=3;s.selectedSlot=0;Text(s.weaponName,profile.asset);
        s.stateCount=1;s.soleConfiguredArray=5;s.states[0].array=5;s.states[0].state=6;s.states[0].count=1;
        auto& mesh=s.states[0].meshes[0];mesh.address=7;mesh.typeInfo=2;mesh.namePointer=8;Text(mesh.assetPath,profile.meshPath);
        s.configurationPathVerified=true;s.configurationPathPointer=9;Text(s.configurationPath,profile.configurationPath);
        flash=interaction::Multiply(profile.flashInWeapon,weapon);
    }
    std::optional<MuzzleBinding> Bind(){auto a=BindAuthoredGrip({&grip,1},selected,equipment,owner,grip.rigFingerprint,now,false);
        return a.binding?BindAuthoredMuzzle({&profile,1},*a.binding,weapon,flash,1,now):std::nullopt;}
    bool Current(const MuzzleBinding& b,std::int64_t at=now){return MuzzleBindingCurrent(b,selected.get(),equipment,owner,at);}
};
int ExactAuthoredEnrollment(){
    Fixture f;auto b=f.Bind();CHECK(b&&f.Current(*b));CHECK(b->profile->admission==MuzzleAdmission::Candidate);
    CHECK(!AuthoredGripBinding::muzzleCapability); // visual metadata alone is still not authority
    f.selected=std::make_shared<SelectedMeshesSnapshot>(*f.selected);f.selected->sequence++;f.selected->observedNs=Fixture::now;
    f.selected->deadlineNs=Fixture::now+250000000;CHECK(f.Current(*b));
    f.owner.inputDeadlineNs+=100000000;CHECK(!f.Current(*b,Fixture::now+90000000)); // original input lease
    f.owner=b->authored.owner;f.owner.inputDeadlineNs+=400000000;auto longInput=*b;longInput.authored.owner.inputDeadlineNs=f.owner.inputDeadlineNs;
    CHECK(!f.Current(longInput,Fixture::now+200000000)); // original selected-config lease
    f.owner=b->authored.owner;f.selected->configurationPathPointer++;CHECK(!f.Current(*b));--f.selected->configurationPathPointer;
    Text(f.selected->configurationPath,"Objects/Weapons/AnotherConfiguration");CHECK(!f.Current(*b)&&!f.Bind());
    Text(f.selected->configurationPath,f.profile.configurationPath);f.selected->configurationPathVerified=false;CHECK(!f.Bind());
    return 0;
}
int IdentityGeometryAndNoNameFallback(){
    Fixture f;const auto b=f.Bind();CHECK(b);
    ++f.equipment.persistence;CHECK(!f.Current(*b));f.equipment=b->authored.equipment;
    ++f.owner.equipmentGeneration;CHECK(!f.Current(*b));f.owner=b->authored.owner;
    ++f.owner.space;CHECK(!f.Current(*b));f.owner=b->authored.owner;
    f.flash.values[3][0]+=.02f;CHECK(!f.Bind());f.flash=interaction::Multiply(f.profile.flashInWeapon,f.weapon);
    f.grip.rigFingerprint++;CHECK(!f.Bind());f.grip.rigFingerprint--;
    f.grip.authoredAxisEvidence="0123456789abcdef0123456789abcdef0123456789abcdef0123456789abcdef";CHECK(!f.Bind());
    f.grip=generated::AuthoredGrips[0];f.profile.admission=MuzzleAdmission::Disabled;CHECK(!f.Bind());
    f.profile=generated_muzzle::MuzzleEnrollments[0];f.profile.firingDigest="invalid";CHECK(!f.Bind());
    f.profile=generated_muzzle::MuzzleEnrollments[0];auto a=BindAuthoredGrip({&f.grip,1},f.selected,f.equipment,f.owner,f.grip.rigFingerprint,Fixture::now,false);
    CHECK(a.binding);std::array<MuzzleEnrollmentProfile,2> duplicate{f.profile,f.profile};
    CHECK(!BindAuthoredMuzzle(duplicate,*a.binding,f.weapon,f.flash,1,Fixture::now));
    return 0;
}
struct NativeMemory {
    static constexpr unsigned base=0x10000,effects=base,data=base+0x100,function=base+0x200,config=function+0xa0;
    std::array<std::byte,4096> bytes{};unsigned fail=0,reads=0,effectReads=0;bool mutate=false;
    template<class T> void Put(unsigned at,const T& value){std::memcpy(bytes.data()+at-base,&value,sizeof(value));}
    NativeMemory(){Put(effects+0x10,data);Put(data+0x40,function);Put(config,std::array<float,3>{0,0,600});
        Put(config+0x10,std::array<float,3>{0,0,1});Put(config+0x34,1u);Put(config+0x38,1u);Put(config+0x44,base+0x400);
        Put(config+0x48,std::array<unsigned char,3>{1,0,0});}
    MuzzleReadMemory Reader(){return {this,[](void* p,unsigned at,void* out,std::size_t count){auto& m=*static_cast<NativeMemory*>(p);++m.reads;
        if(at==m.effects+0x10&&++m.effectReads==2&&m.mutate)m.Put(m.config+0x49,static_cast<unsigned char>(1));
        if(at==m.fail||at<m.base||std::uint64_t(at)+count>m.base+m.bytes.size())return false;
        std::memcpy(out,m.bytes.data()+at-m.base,count);return true;}};}
    std::optional<NativeMuzzleConfig> Read(){return ReadNativeMuzzleConfig(Reader(),effects,config);}
};
int ActualNativeReadPredicate(){
    NativeMemory m;const auto unchanged=m.bytes;auto c=m.Read();CHECK(c&&m.bytes==unchanged&&OrdinaryMuzzleConfig(*c));
    Fixture f;const auto b=f.Bind();CHECK(b&&MatchesMuzzleEnrollment(*c,*b));
    // Active=true/Force=false remains admitted; Force=true is the downstream rewrite branch.
    m.Put(m.config+0x49,static_cast<unsigned char>(1));c=m.Read();CHECK(c&&!OrdinaryMuzzleConfig(*c)&&!MatchesMuzzleEnrollment(*c,*b));
    m.Put(m.config+0x49,static_cast<unsigned char>(0));m.Put(m.config+0x4a,static_cast<unsigned char>(1));c=m.Read();CHECK(c&&!OrdinaryMuzzleConfig(*c));
    m.Put(m.config+0x4a,static_cast<unsigned char>(0));m.Put(m.config+0x40,0x12345u);c=m.Read();CHECK(c&&!OrdinaryMuzzleConfig(*c));
    m.Put(m.config+0x40,0u);m.fail=m.config+0x48;CHECK(!m.Read()); // failed flag read is not false
    m.fail=0;m.Put(m.config+0x38,2u);c=m.Read();CHECK(c&&OrdinaryMuzzleConfig(*c)&&!MatchesMuzzleEnrollment(*c,*b)); // legacy projectile counts remain possible
    m.Put(m.config+0x38,1u);m.Put(m.config,std::numeric_limits<float>::quiet_NaN());CHECK(!m.Read());
    NativeMemory changed;changed.mutate=true;CHECK(!changed.Read());
    CHECK(!ReadNativeMuzzleConfig(m.Reader(),UINT32_MAX-1,m.config));
    CHECK(!ReadNativeMuzzleConfig(m.Reader(),m.effects,m.config+4));
    return 0;
}
int SameEventAndChangedConfiguration(){
    Fixture f;const auto b=f.Bind();CHECK(b);interaction::FiringPoseHistory history;std::optional<MuzzleEventSource> last;
    MuzzleEventSource source{f.equipment,&f.profile,101,102,f.owner.equipmentGeneration};
    interaction::FiringPoseKey key{10,12,15,14,0};
    interaction::FiringPoseSample first{At(1,2,3),20,150},next{At(4,5,6),21,180};
    auto a=ResolveMuzzleEvent(history,last,source,key,first,100);auto server=ResolveMuzzleEvent(history,last,source,key,next,120);
    CHECK(a&&server&&server->generation==20&&server->deadline==150);
    CHECK(!ResolveMuzzleEvent(history,last,source,key,next,150)); // server cannot renew client muzzle
    source.configuration++;auto changed=ResolveMuzzleEvent(history,last,source,key,next,151);CHECK(changed&&changed->generation==21);
    source.equipmentGeneration++;first.deadline=200;auto equip=ResolveMuzzleEvent(history,last,source,key,first,152);CHECK(equip&&equip->generation==20);
    source.equipment.data++;auto pickup=ResolveMuzzleEvent(history,last,source,key,next,153);CHECK(pickup&&pickup->generation==21);
    auto other=*b;other.authored.owner.equipmentGeneration++;CHECK(!SameMuzzleSource(*b,other));
    Fixture lease;lease.owner.inputDeadlineNs=Fixture::now+400000000;const auto captured=lease.Bind();CHECK(captured);
    lease.selected=std::make_shared<SelectedMeshesSnapshot>(*lease.selected);lease.selected->sequence++;
    lease.selected->observedNs=Fixture::now;lease.selected->deadlineNs=Fixture::now+250000000;
    lease.owner.inputSequence++;const auto refreshed=lease.Bind();CHECK(refreshed&&SameMuzzleSource(*captured,*refreshed));
    CHECK(MuzzleBindingDeadlineNs(*captured)==Fixture::now+200000000);
    CHECK(MuzzleBindingContinues(*captured,*refreshed,Fixture::now+199999999));
    CHECK(!MuzzleBindingContinues(*captured,*refreshed,Fixture::now+200000000));
    CHECK(lease.Current(*refreshed,Fixture::now+200000000)); // new lease cannot renew the captured event
    history.Reset();last.reset();source={lease.equipment,&lease.profile,101,102,lease.owner.equipmentGeneration};
    first.deadline=MuzzleBindingDeadlineNs(*captured);next.deadline=MuzzleBindingDeadlineNs(*refreshed);
    CHECK(ResolveMuzzleEvent(history,last,source,key,first,Fixture::now));
    CHECK(!ResolveMuzzleEvent(history,last,source,key,next,Fixture::now+200000000));
    return 0;
}
struct LaunchMemory {
    static constexpr unsigned base=0x20000,slot=base+0x100;
    std::array<std::byte,1024> bytes{};unsigned fail=0;
    ProjectileLaunchBinding binding{0x556e90,0x57aa06,0x57aa0b,0x57aa0d,true};
    template<class T> void Put(unsigned at,const T& value){std::memcpy(bytes.data()+at-base,&value,sizeof(value));}
    LaunchMemory(){
        Put(slot,std::array<unsigned,12>{binding.helperReturn,slot+0x1b0,slot+0x90,slot+0x130,slot+0x60,
            std::bit_cast<unsigned>(.5f),503105,1,1,0x123456,0x234567,std::bit_cast<unsigned>(1.f)});
        Put(slot+0x90,At(1,2,3));Put(slot+0x130,At(4,5,6));Put(slot+0x60,std::array<float,3>{0,0,600});
    }
    MuzzleReadMemory Reader(){return {this,[](void* p,unsigned at,void* out,std::size_t size){const auto& m=*static_cast<LaunchMemory*>(p);
        if(at==m.fail||at<base||std::uint64_t(at)+size>base+m.bytes.size())return false;
        std::memcpy(out,m.bytes.data()+at-base,size);return true;}};}
    std::optional<ProjectileLaunchArguments> Read(){return ReadProjectileLaunchArguments(Reader(),binding,slot,base,base+unsigned(bytes.size()));}
};
int ActualLaunchStackAndObservationOrder(){
    LaunchMemory m;const auto untouched=m.bytes;auto a=m.Read();CHECK(a&&m.bytes==untouched);
    CHECK(a->token==503105&&a->context==0x123456&&a->composed[12]==1&&a->secondary[14]==6&&a->velocity[2]==600);
    CHECK(a->contextOutput==m.slot+0x1b0&&a->projectilesPerShell==1&&a->effectsScalar==.5f);
    // The real wrapper must observe after the original helper, preserving its
    // exact result even if the bounded before/after reads are unavailable.
    unsigned calls=0;std::optional<ProjectileLaunchArguments> before,after;
    const auto result=ObserveProjectileContextHelper([&]()->std::uintptr_t{++calls;m.Put(m.slot+0x60,std::array<float,3>{3,4,599});return 0x987654;},
        [&]()noexcept{before=m.Read();},[&](std::uintptr_t value)noexcept{if(value==0x987654)after=m.Read();});
    CHECK(result==0x987654&&calls==1&&before&&after&&before->velocity[0]==0&&after->velocity[0]==3);
    m.fail=m.slot+0x60;before.reset();after.reset();
    CHECK(ObserveProjectileContextHelper([&]()->std::uintptr_t{++calls;return 0x42;},[&]()noexcept{before=m.Read();},
        [&](std::uintptr_t)noexcept{after=m.Read();})==0x42&&calls==2&&!before&&!after);
    bool afterException=false;
    try {ObserveProjectileContextHelper([]()->std::uintptr_t{throw 7;},[]()noexcept{},[&](std::uintptr_t)noexcept{afterException=true;});CHECK(false);}
    catch(int code){CHECK(code==7&&!afterException);}
    m.fail=0;m.Put(m.slot,m.binding.helperReturn+1);CHECK(!m.Read());m.Put(m.slot,m.binding.helperReturn);
    m.Put(m.slot+16,m.slot+0x64);CHECK(!m.Read());m.Put(m.slot+16,m.slot+0x60);
    CHECK(!ReadProjectileLaunchArguments(m.Reader(),m.binding,m.slot,m.base,m.slot+0x1b3));
    CHECK(!ReadProjectileLaunchArguments(m.Reader(),m.binding,UINT32_MAX-3,m.base,UINT32_MAX));
    m.Put(m.slot+0x90,std::numeric_limits<float>::quiet_NaN());CHECK(!m.Read());
    CHECK(!VerifyProjectileLaunchCode(m.Reader(),0x400000,0x179dc0).verified); // unreadable code never enrolls
    CHECK(!VerifyProjectileLaunchCode(m.Reader(),0x400000,0x179dc1).verified);
    return 0;
}
ServerFireObservationSample FireObservation(std::uint64_t invocation,std::int64_t begin,std::string_view asset="late_m8_fixture"){
    ServerFireObservationSample s;s.invocation=invocation;s.returnedOrdinal=invocation;s.beginQpc=begin;s.endQpc=begin+10;s.beginNs=begin;
    s.key.player=1;s.key.soldier=2;s.key.weak=3;s.key.serverPlayer=4;s.key.serverSoldier=5;s.key.effects=6;s.key.configuration=7;
    s.key.equipment.weapon=8;s.key.equipment.data=9;Text(s.key.equipment.asset,asset);
    s.key.candidatePresent=true;s.key.candidateOwnerGeneration=10;s.key.space=11;s.candidateGeneration=invocation;
    s.candidateDeadlineQpc=begin+100;s.helperCallbacks=1;s.controllerPresent=true;
    s.controller.owner={2,10,12,11};s.controller.sequence=invocation;s.controller.observedNs=begin-1;
    s.controller.deadlineNs=begin+100;s.controller.nowNs=begin-1;s.controller.focused=true;s.controller.tracked={true,true};return s;
}
int LateServerFireContextAndGaps(){
    ServerFireObservation observation;
    // Synthetic late weapon: the native detailed-prefix budget stays64. This
    // observer receives only value copies, including after that budget fills.
    for(unsigned n=1;n<=252;++n)observation.Observe(FireObservation(n,n*100,n<=8?"Garand_fixture":n<=64?"Nambu_fixture":"late_m8_fixture"));
    CHECK(observation.Recorded()==252&&observation.Count()==3&&observation.EvictedReturns()==0);
    const auto& late=observation.Cohorts()[2];CHECK(late.key.equipment.Asset()=="late_m8_fixture"&&late.returned==188&&late.helperCallbacks==188);
    CHECK(late.firstValidBeginQpc==6500&&late.lastValidEndQpc==25210&&late.gaps==187);
    auto pause=FireObservation(253,100000);pause.helperCallbacks=0;pause.controller.deadlineNs=99999;pause.controller.observedNs=90000;
    pause.candidateDeadlineQpc=99999;observation.Observe(pause);
    CHECK(late.returned==189&&late.withoutHelper==1&&late.expiredCandidate==1&&late.expiredController==1);
    CHECK(late.latestGap.present&&late.latestGap.previous.invocation==252&&late.latestGap.current.invocation==253&&late.latestGap.qpc==74800);
    CHECK(late.worstGap.qpc==74800&&observation.WorstGap().qpc==74800);
    CHECK(late.latest.controller.observedNs==90000&&late.latest.controller.deadlineNs==99999&&late.latest.controller.sequence==253);
    CHECK(std::string_view(late.latest.ControllerTime())=="expired");
    std::ostringstream out;observation.Report(out);const auto json=out.str();
    CHECK(json.find("\"asset\":\"late_m8_fixture\"")!=std::string::npos&&json.find("\"returned_invocations\":189")!=std::string::npos);
    CHECK(json.find("\"controller_time_at_begin\":\"expired\"")!=std::string::npos&&json.find("\"begin_to_begin_qpc\":74800")!=std::string::npos);
    return 0;
}
int ServerFireRetentionAndObservedIdentity(){
    ServerFireObservation observation;auto initial=FireObservation(1,100);initial.endQpc=150;observation.Observe(initial);
    for(unsigned n=2;n<=17;++n){auto sample=FireObservation(n,n*100);sample.key.equipment.persistence=n;observation.Observe(sample);}
    CHECK(observation.Count()==16&&observation.Recorded()==17&&observation.EvictedReturns()==1);
    CHECK(observation.WorstSpan()&&observation.WorstSpan()->invocation==1&&observation.WorstSpan()->Span()==50);
    for(const auto& c:observation.Cohorts())CHECK(c.key.equipment.persistence!=0);
    auto reused=FireObservation(18,1800);observation.Observe(reused);
    CHECK(observation.EvictedReturns()==2);std::uint64_t retained=0;const ServerFireObservationCohort* reintroduced=nullptr;
    for(const auto& c:observation.Cohorts()){retained+=c.returned;if(c.key.equipment.persistence==0)reintroduced=&c;}
    CHECK(retained+observation.EvictedReturns()==observation.Recorded()&&reintroduced&&reintroduced->returned==1&&!reintroduced->latestGap.present);
    ServerFireObservation split;split.Observe(initial);auto changed=initial;changed.invocation=2;changed.key.candidateOwnerGeneration++;split.Observe(changed);
    changed=initial;changed.invocation=3;Text(changed.key.equipment.asset,"different_asset_same_addresses");split.Observe(changed);
    changed=initial;changed.invocation=4;changed.key.candidatePresent=false;changed.key.candidateOwnerGeneration=0;changed.key.space=0;split.Observe(changed);
    CHECK(split.Count()==4);return 0;
}
int ServerFireInvalidAndAbsentEvidence(){
    ServerFireObservation observation;observation.Observe(FireObservation(1,100));
    auto invalid=FireObservation(2,200);invalid.endQpc=199;invalid.helperCallbacks=0;invalid.controllerPresent=false;
    observation.Observe(invalid);const auto& c=observation.Cohorts()[0];
    CHECK(c.returned==2&&c.invalidTiming==1&&c.missingController==1&&c.withoutHelper==1&&c.unorderedOrInvalidPairs==1);
    CHECK(c.firstValidBeginQpc==100&&c.lastValidEndQpc==110&&c.maximumSpanQpc==10);
    CHECK(observation.LatestTimed()->invocation==1&&observation.WorstSpan()->invocation==1&&!c.latestGap.present);
    CHECK(std::string_view(c.latest.ControllerTime())=="absent");
    auto next=FireObservation(3,300);next.controller.deadlineNs=next.controller.observedNs;observation.Observe(next);
    CHECK(c.invalidControllerTime==1&&c.unorderedOrInvalidPairs==2&&!c.latestGap.present&&c.lastValidEndQpc==310);
    auto backwards=FireObservation(4,250);observation.Observe(backwards);
    CHECK(c.unorderedOrInvalidPairs==3&&c.lastValidEndQpc==310&&observation.LatestTimed()->invocation==3);
    auto unknown=FireObservation(5,0);unknown.endQpc=0;unknown.controllerPresent=false;unknown.key.candidatePresent=false;
    unknown.key.candidateOwnerGeneration=0;unknown.key.space=0;observation.Observe(unknown);
    CHECK(std::string_view(unknown.CandidateTime())=="absent"&&std::string_view(unknown.ControllerTime())=="absent");
    CHECK(observation.LatestTimed()->invocation==3&&observation.WorstSpan()->invocation==1);
    auto future=FireObservation(6,400);future.controller.observedNs=401;CHECK(std::string_view(future.ControllerTime())=="future");
    std::ostringstream out;observation.Report(out);CHECK(out.str().find("\"observed_span_qpc\":null")!=std::string::npos);
    return 0;
}
int InstalledLaunchCode(const char* path){
    std::ifstream file(path,std::ios::binary|std::ios::ate);CHECK(file);
    const auto length=file.tellg();CHECK(length>0&&length<256*1024*1024);
    std::vector<std::byte> bytes(static_cast<std::size_t>(length));file.seekg(0);CHECK(file.read(reinterpret_cast<char*>(bytes.data()),length));
    const auto pe=engine::InspectPe(bytes);CHECK(pe.valid&&pe.image.machine==0x14c);
    struct Code {const std::vector<std::byte>& bytes;const engine::PeImage& image;unsigned corrupt=0,fail=0;} code{bytes,pe.image};
    const MuzzleReadMemory memory{&code,[](void* p,unsigned address,void* out,std::size_t size){const auto& c=*static_cast<Code*>(p);
        if(address<0x400000||address==c.fail)return false;const auto rva=address-0x400000;
        for(const auto& s:c.image.sections)if(rva>=s.rva&&std::uint64_t(rva)+size<=std::uint64_t(s.rva)+s.rawSize){
            const auto offset=std::uint64_t(s.rawOffset)+rva-s.rva;if(offset+size>c.bytes.size())return false;
            std::memcpy(out,c.bytes.data()+offset,size);
            if(c.corrupt>=address&&std::uint64_t(c.corrupt)<std::uint64_t(address)+size)static_cast<std::byte*>(out)[c.corrupt-address]^=std::byte{1};
            return true;
        }return false;}};
    const auto binding=VerifyProjectileLaunchCode(memory,0x400000,0x179dc0);
    CHECK(binding.verified&&binding.helperReturn==0x57aa06&&binding.virtualCall==0x57aa0b&&binding.virtualReturn==0x57aa0d);
    for(const unsigned address:{0x57a51fu,0x57aa01u,0x556faeu,0x556fb1u}){
        code.corrupt=address;CHECK(!VerifyProjectileLaunchCode(memory,0x400000,0x179dc0).verified);
    }
    code.corrupt=0;code.fail=0x556e90;CHECK(!VerifyProjectileLaunchCode(memory,0x400000,0x179dc0).verified);
    return 0;
}
}
int main(int argc,char** argv){CHECK(!ExactAuthoredEnrollment());CHECK(!IdentityGeometryAndNoNameFallback());CHECK(!ActualNativeReadPredicate());CHECK(!SameEventAndChangedConfiguration());CHECK(!ActualLaunchStackAndObservationOrder());CHECK(!LateServerFireContextAndGaps());CHECK(!ServerFireRetentionAndObservedIdentity());CHECK(!ServerFireInvalidAndAbsentEvidence());if(argc==2)CHECK(!InstalledLaunchCode(argv[1]));return 0;}
