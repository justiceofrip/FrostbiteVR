#include "Bc2RigPublication.h"
#include "Bc2MuzzlePartProfiles294.h"
#include "Bc2MuzzlePartReport.h"
#include "Bc2ProjectileLaunch.h"
#include "Bc2MuzzlePartRetained319.h"
#include "Test.h"
#if defined(_MSC_VER) && defined(_M_IX86)
#include "Bc2ProjectileTargetHost.h"
#endif
#include <bit>
#include <cstdlib>
#include <iostream>
#include <limits>
#include <new>
#include <sstream>
#include <stdexcept>

using namespace fvr;using namespace fvr::bc2;using namespace fvr::bc2::muzzle_part_observation;
using Shot=rigPublication::WeaponShotFrame;
const Profile* MuzzlePartProfilePeer();std::size_t MuzzlePartLayoutPeer();
namespace allocation_watch {bool active=false;unsigned calls=0;}
void* operator new(std::size_t n){if(allocation_watch::active)++allocation_watch::calls;if(void* p=std::malloc(n?n:1))return p;throw std::bad_alloc();}
void* operator new[](std::size_t n){return ::operator new(n);}
void operator delete(void* p)noexcept{std::free(p);}void operator delete[](void* p)noexcept{std::free(p);}
void operator delete(void* p,std::size_t)noexcept{std::free(p);}void operator delete[](void* p,std::size_t)noexcept{std::free(p);}

namespace {
math::Matrix4 At(float x=0,float y=0,float z=0){math::Matrix4 m{};for(unsigned n=0;n<4;++n)m.values[n][n]=1;m.values[3]={x,y,z,1};return m;}
bool Equal(const math::Matrix4& a,const math::Matrix4& b){return a.values==b.values;}
template<std::size_t N> void Text(std::array<char,N>& target,std::string_view text){target={};std::copy(text.begin(),text.end(),target.begin());}
struct Fixture {
    Source s{};RigSnapshot rig{};std::vector<interaction::BoneWrite> writes;
    std::vector<std::array<std::byte,64>> packed;math::Matrix4 tracked=At(8,9,10);unsigned bone=0;
    explicit Fixture(const Profile& p=Profiles294[0]){
        s.equipment.weapon=15;s.equipment.data=16;s.equipment.persistence=17;Text(s.equipment.asset,p.asset);
        s.configuration=p.configuration;s.mesh=p.mesh;s.rigFingerprint=p.rigFingerprint;
        s.generation=7408;s.ownerGeneration=1;s.equipmentGeneration=2;s.space=2;
        s.nowQpc=100;s.deadlineQpc=200;s.nowNs=1000;s.configurationDeadlineNs=2000;s.configurationSequence=7400;
        s.unitsPerMetre=1;s.selectedRigVerified=s.configurationVerified=s.attachmentReady=s.ordinaryPublishedPalette=true;
        rig.identity={11,12,13,14,15,16,17,18,19,147,true};rig.weaponBone=10;bone=p.expectedIndex;
        rig.names.resize(147);rig.parents.assign(147,-1);rig.evaluatedWorld.assign(147,At());rig.inverseBind.assign(147,At());
        rig.nativeEvaluated.resize(147);packed.resize(147);
        for(unsigned n=0;n<147;++n){rig.names[n]="other"+std::to_string(n);rig.nativeEvaluated[n].fill(std::byte{0x5a});}
        rig.names[9]="jntWpn_0";rig.names[10]="jntWpn_1";rig.parents[10]=9;
        rig.names[bone]=p.part;if(bone!=10)rig.parents[bone]=10;
        rig.evaluatedWorld[10]=At(3,4,5);rig.evaluatedWorld[bone]=At(3.125f,4.25f,5.5f);
        writes.push_back({bone,At(8.125f,9.25f,10.5f)});Pack();
    }
    void Pack(){
        packed=rig.nativeEvaluated;
        for(const auto& w:writes){auto skin=interaction::Multiply(rig.inverseBind[w.index],w.transform);
            for(unsigned i=0;i<4;++i){skin.values[2][i]=-skin.values[2][i];skin.values[i][2]=-skin.values[i][2];}
            for(unsigned r=0;r<4;++r)std::memcpy(packed[w.index].data()+r*16,skin.values[r].data(),12);}
    }
    std::optional<Frame> Get(std::span<const Profile> profiles=Profiles294,Gate* outcome=nullptr){return Capture(profiles,s,rig,writes,packed,tracked,outcome);}
    Shot MakeShot(){Shot shot;shot.generation=s.generation;shot.ownerGeneration=s.ownerGeneration;shot.space=s.space;
        shot.deadline=s.deadlineQpc;shot.unitsPerMetre=s.unitsPerMetre;shot.nativeWeapon=rig.evaluatedWorld[rig.weaponBone];shot.trackedWeapon=tracked;
        rigPublication::ShotAimEvidence a;a.equipment=s.equipment;Text(a.configurationPath,s.configuration);
        a.meshPath=s.mesh;a.rigFingerprint=s.rigFingerprint;a.configurationSequence=s.configurationSequence;
        a.configurationDeadlineNs=s.configurationDeadlineNs;a.selectedRigVerified=s.selectedRigVerified;a.configurationPathVerified=s.configurationVerified;
        shot.aimEvidence=a;shot.muzzlePartObservation=Get(Profiles294,&shot.muzzlePartDiagnostic.capture);return shot;}
    void Retain(Shot& shot,bool allowed=true){RetainForShot(shot,s.equipment,s.equipmentGeneration,rig.identity.soldier,rig.identity.weak,s.nowQpc,s.nowNs,allowed,&shot.muzzlePartDiagnostic.retention);}
};
// Row-vector transformation. Reflect the resulting WORLD Z exactly once.
math::Vec3 NativeExit(const Frame& f){const auto p=f.profile->localExitMeters;const auto& m=f.trackedPart.values;
    const float x=p.x*f.unitsPerMetre,y=p.y*f.unitsPerMetre,z=p.z*f.unitsPerMetre;
    return {x*m[0][0]+y*m[1][0]+z*m[2][0]+m[3][0],x*m[0][1]+y*m[1][1]+z*m[2][1]+m[3][1],
        -(x*m[0][2]+y*m[1][2]+z*m[2][2]+m[3][2])};}
int A_ScopeValue(){
    CHECK(retained319::sampleGeneration==7408&&retained319::shotGeneration==7408&&retained319::delayMs==16);
    CHECK(retained319::sampleRoot!=retained319::shotRoot);
    const auto& saved=retained319::sampleRoot;const auto& fired=retained319::shotRoot;
    CHECK(Near(std::hypot(saved[12]-fired[12],saved[13]-fired[13],saved[14]-fired[14]),.008620496f,.00003f));
    Fixture f;auto published=f.MakeShot();const auto scope=published;const auto original=*scope.muzzlePartObservation;
    // Actual319 sample606 shares input7408 but is +16ms and +8.620496mm from
    // that shot's root. It is not an exact frame key or substitute measurement.
    f.rig.evaluatedWorld[10].values[3][0]+=.001603f;f.rig.evaluatedWorld[10].values[3][1]-=.006287f;
    f.rig.evaluatedWorld[10].values[3][2]-=.005676f;f.writes[0].transform.values[3][0]+=.25f;f.Pack();published=f.MakeShot();
    CHECK(published.generation==scope.generation);CHECK(!Equal(published.muzzlePartObservation->nativeWeapon,original.nativeWeapon));
    const auto helper=scope.muzzlePartObservation;CHECK(Equal(helper->nativePart,original.nativePart));CHECK(Equal(helper->trackedPart,original.trackedPart));
    CHECK(!Equal(helper->trackedPart,published.muzzlePartObservation->trackedPart));CHECK(helper->deadlineQpc==200);
    CHECK(MuzzlePartProfilePeer()==&Profiles294[0]);CHECK(MuzzlePartLayoutPeer()==sizeof(Shot));return 0;
}
int B_ExactFamilies(){
    CHECK(Profiles294.size()==80);unsigned roots=0,hinged=0,sliding=0;
    for(const auto& p:Profiles294){Fixture f(p);auto value=f.Get();CHECK(value&&value->profile==&p&&value->bone==p.expectedIndex);
        if(p.expectedIndex==10)++roots;else if(p.mesh.find("XM8")!=std::string_view::npos)++hinged;else ++sliding;
        const auto reject=[&](auto change){Fixture x(p);change(x);return !x.Get();};
        CHECK(reject([](auto& x){x.s.configuration="wrong";}));CHECK(reject([](auto& x){x.s.mesh="wrong";}));
        CHECK(reject([](auto& x){x.s.equipment.asset[0]='!';}));CHECK(reject([](auto& x){++x.s.rigFingerprint;}));
        CHECK(reject([](auto& x){x.rig.names[x.bone]="missing";}));CHECK(reject([&](auto& x){x.rig.names[100]=p.part;}));
        CHECK(reject([](auto& x){x.rig.parents[x.bone]=-1;}));CHECK(reject([](auto& x){x.rig.names[x.rig.parents[x.bone]]="wrong";}));
        CHECK(reject([](auto& x){x.rig.weaponBone=101;}));CHECK(reject([](auto& x){x.writes.clear();}));
        CHECK(reject([](auto& x){x.writes.push_back(x.writes[0]);}));CHECK(reject([](auto& x){x.rig.nativeHiddenLeaves.push_back(x.bone);}));
        CHECK(reject([](auto& x){x.packed[x.bone][0]^=std::byte{1};}));CHECK(reject([](auto& x){x.packed[x.bone][15]^=std::byte{1};}));
        auto wrong=p;wrong.state=1;CHECK(!f.Get({&wrong,1}));wrong=p;++wrong.expectedIndex;CHECK(!f.Get({&wrong,1}));
        const std::array duplicate{p,p};CHECK(!f.Get(duplicate));
    }
    CHECK(roots&&hinged&&sliding);Fixture wrongFamily;wrongFamily.s.configuration="Objects/Weapons/Handheld/US_rgl_HK416/US_rgl_HK416";CHECK(!wrongFamily.Get());return 0;
}
int C_Animation(){
    // Every moving family must use its part; a repeated weapon/root alone fails.
    for(const auto& p:Profiles294){if(p.expectedIndex==10)continue;Fixture f(p);const auto before=*f.Get();
        auto& part=f.writes[0].transform;part=At(8.625f,9.25f,10.5f);part.values[0]={0,0,-1,0};part.values[2]={1,0,0,0};f.Pack();
        const auto after=f.Get();CHECK(after);CHECK(Equal(before.trackedWeapon,after->trackedWeapon));CHECK(!Equal(before.trackedPart,after->trackedPart));
        const auto a=NativeExit(before),b=NativeExit(*after);CHECK(std::hypot(a.x-b.x,a.y-b.y,a.z-b.z)>.01f);
        CHECK(Equal(after->trackedPart,part));CHECK(!Equal(after->trackedPart,after->nativePart));
        CHECK(!Equal(after->trackedPart,f.rig.inverseBind[f.bone]));
        // Stale packed palette cannot claim the current animation.
        f.writes[0].transform.values[3][1]+=.25f;CHECK(!f.Get());
    }return 0;
}
int D_Guards(){
    const auto reject=[&](auto change){Fixture f;change(f);return !f.Get();};
    CHECK(reject([](auto& f){f.s.selectedRigVerified=false;}));CHECK(reject([](auto& f){f.s.configurationVerified=false;}));
    CHECK(reject([](auto& f){f.s.nowQpc=f.s.deadlineQpc;}));CHECK(reject([](auto& f){f.s.nowNs=f.s.configurationDeadlineNs;}));
    CHECK(reject([](auto& f){f.s.attachmentReady=false;}));CHECK(reject([](auto& f){f.s.ordinaryPublishedPalette=false;}));
    const auto revoke=[&](auto change){Fixture f;auto shot=f.MakeShot();change(f,shot);f.Retain(shot);return !shot.muzzlePartObservation;};
    CHECK(revoke([](auto&,auto& shot){shot.aimEvidence.reset();}));
    CHECK(revoke([](auto&,auto& shot){shot.aimEvidence->sightSelectedRig=true;shot.aimEvidence->selectedRigVerified=false;}));
    CHECK(revoke([](auto&,auto& shot){shot.aimEvidence->meshPath="wrong";}));
    CHECK(revoke([](auto&,auto& shot){++shot.aimEvidence->rigFingerprint;}));
    CHECK(revoke([](auto&,auto& shot){shot.aimEvidence->configurationPath[0]='!';}));
    CHECK(revoke([](auto& f,auto&){++f.s.equipment.weapon;}));CHECK(revoke([](auto& f,auto&){++f.s.equipment.data;}));
    CHECK(revoke([](auto& f,auto&){++f.s.equipment.persistence;}));CHECK(revoke([](auto& f,auto&){++f.s.equipmentGeneration;}));
    CHECK(revoke([](auto&,auto& shot){++shot.ownerGeneration;}));CHECK(revoke([](auto&,auto& shot){++shot.space;}));
    CHECK(revoke([](auto& f,auto&){++f.rig.identity.soldier;}));CHECK(revoke([](auto& f,auto&){++f.rig.identity.weak;}));
    CHECK(revoke([](auto& f,auto&){f.s.nowQpc=200;}));CHECK(revoke([](auto& f,auto&){f.s.nowNs=2000;}));
    CHECK(revoke([](auto&,auto& shot){++shot.deadline;}));CHECK(revoke([](auto&,auto& shot){++shot.aimEvidence->configurationDeadlineNs;}));
    Fixture f;auto shot=f.MakeShot();f.Retain(shot,false);CHECK(!shot.muzzlePartObservation);
    f.s.equipment.persistence=0;CHECK(f.Get());return 0;
}
int E_Helper(){
    Fixture f;const auto frame=*f.Get();
    const auto accepted=[&](const Frame& v,const WeaponEquipmentIdentity& equipment,bool okay=true,std::int64_t after=120,std::int64_t ns=1100){
        return SameAcceptedHelper(v,equipment,11,12,7408,1,2,110,after,ns,okay);};
    CHECK(accepted(frame,f.s.equipment));CHECK(!accepted(frame,f.s.equipment,false));CHECK(!accepted(frame,f.s.equipment,true,200));
    CHECK(!accepted(frame,f.s.equipment,true,120,2000));CHECK(!accepted(frame,f.s.equipment,true,109));
    auto other=f.s.equipment;++other.data;CHECK(!accepted(frame,other));auto expired=frame;++expired.ownerGeneration;CHECK(!accepted(expired,f.s.equipment));
    unsigned calls=0,before=0,after=0;const auto result=ObserveProjectileContextHelper([&]{++calls;return std::uintptr_t{0x1234};},
        [&]()noexcept{++before;},[&](std::uintptr_t value)noexcept{++after;(void)value;});
    CHECK(result==0x1234&&calls==1&&before==1&&after==1);
    // Original exceptions propagate; no after/acceptance claim occurs.
    after=0;try{ObserveProjectileContextHelper([]()->std::uintptr_t{throw 7;},[]()noexcept{},[&](auto)noexcept{++after;});CHECK(false);}catch(int value){CHECK(value==7);}CHECK(after==0);
    // Execute the unchanged helper ordering with 64 rows and contention. No
    // copy/acceptance can suppress or repeat the native original forwarding.
    std::array<std::optional<Frame>,64> rows;unsigned count=0,dropped=0,busy=0;calls=0;
    const auto dispatch=[&](bool contended,bool okay){const auto original=[&]{++calls;return std::uintptr_t{9};};
        if(count>=rows.size()){++dropped;return original();}auto copy=f.MakeShot().muzzlePartObservation;
        return ObserveProjectileContextHelper(original,[]()noexcept{},[&](auto)noexcept{
            if(copy&&!accepted(*copy,f.s.equipment,okay))copy.reset();if(contended){++busy;return;}rows[count++]=copy;});};
    CHECK(dispatch(true,true)==9&&busy==1&&count==0);CHECK(dispatch(false,false)==9&&!rows[0]);
    while(count<rows.size())CHECK(dispatch(false,true)==9);CHECK(dispatch(false,true)==9&&dropped==1&&calls==66);
#if defined(_MSC_VER) && defined(_M_IX86)
    projectile307::Join j;j.grant={7,9,2,3,45,0,0x70000,0x80000,0x7155b0,0x57aa0d,0x90100,0x90000,0x92000};
    j.owner.equipment=f.s.equipment;j.effects=j.config.effects=0x50000;j.configuration=j.config.config=0x60000;
    j.config.projectileData=0x81000;j.receiver={0x70000,0x80000,0x7155b0,0x81000,0x82000};
    projectile307::Current c;c.scopeSerial=7;c.invocation=9;c.ordinal=2;c.helperIndex=3;c.thread=45;c.token=0;
    c.tokenValid=c.helperAccepted=true;c.effects=j.effects;c.configuration=j.configuration;
    c.scopeOwner=c.resolvedOwner=j.owner;c.scopeConfig=c.resolvedConfig=j.config;c.receiver=j.receiver;
    CHECK(projectile307::Matches(j,j.grant,c));
    for(unsigned which=0;which<7;++which){auto changed=c;
        if(which==0)++changed.token;if(which==1)++changed.ordinal;if(which==2)++changed.scopeSerial;
        if(which==3)++changed.thread;if(which==4)++changed.resolvedConfig.config;if(which==5)++changed.resolvedOwner.equipment.persistence;
        if(which==6)changed.helperIndex=UINT32_MAX;CHECK(!projectile307::Matches(j,j.grant,changed));}
#endif
    return 0;
}
int F_Allocation(){
    Fixture f;auto shot=f.MakeShot();std::array<std::optional<Frame>,64> rows;
    allocation_watch::calls=0;allocation_watch::active=true;bool okay=true;
    for(auto& row:rows){row=f.Get();okay&=row.has_value();RetainForShot(shot,f.s.equipment,2,11,12,100,1000,true);
        row=shot.muzzlePartObservation;okay&=row&&SameAcceptedHelper(*row,f.s.equipment,11,12,7408,1,2,110,120,1100,true);}
    allocation_watch::active=false;CHECK(okay&&allocation_watch::calls==0);
    static_assert(std::is_trivially_copyable_v<Frame>);static_assert(std::is_trivially_copyable_v<std::optional<Frame>>);
    CHECK(sizeof(rows)==64*sizeof(std::optional<Frame>));CHECK(sizeof(Frame)<1024);return 0;
}
int G_SerializationCoordinates(){
    Fixture f;f.s.unitsPerMetre=100;f.tracked=At(800,900,1000);f.writes[0].transform=At(800,900,1000);f.Pack();
    const auto value=f.Get();CHECK(value);const auto point=NativeExit(*value);const auto local=value->profile->localExitMeters;
    CHECK(Near(point.x/100,8+local.x));CHECK(Near(point.y/100,9+local.y));CHECK(Near(point.z/100,-10-local.z));
    std::ostringstream out;out.precision(3);Report(out,value);CHECK(out.precision()==3);const auto text=out.str();
    for(const auto& named:std::array<std::pair<const char*,const math::Matrix4*>,4>{{
        {"native_part",&value->nativePart},{"tracked_part",&value->trackedPart},{"native_weapon",&value->nativeWeapon},{"tracked_weapon",&value->trackedWeapon}}}){
        const auto start=text.find(std::string("\"")+named.first+"\":[");CHECK(start!=std::string::npos);
        const char* next=text.c_str()+text.find('[',start)+1;for(unsigned n=0;n<16;++n){char* end=nullptr;
            const float actual=std::strtof(next,&end);CHECK(end!=next);CHECK(std::bit_cast<unsigned>(actual)==std::bit_cast<unsigned>(named.second->values[n/4][n%4]));next=end+1;}}
    CHECK(text.find(std::string(value->profile->tupleSha256))!=std::string::npos);CHECK(text.find("\"input_deadline_qpc\":200")!=std::string::npos);
    CHECK(text.find("\"configuration_deadline_ns\":2000")!=std::string::npos);CHECK(text.find("\"native_write_authority\":false")!=std::string::npos);
    std::ostringstream missing;Report(missing,{});CHECK(missing.str()=="null");
    for(unsigned mode=0;mode<4;++mode){Fixture bad;if(mode==0)bad.writes[0].transform.values[0][0]=std::numeric_limits<float>::quiet_NaN();
        if(mode==1)bad.writes[0].transform.values[0][0]=2;if(mode==2)bad.s.unitsPerMetre=0;
        if(mode==3)bad.rig.evaluatedWorld[bad.bone].values[3][1]=std::numeric_limits<float>::infinity();bad.Pack();CHECK(!bad.Get());}
    return 0;
}
// 335: only this group runs with --diagnostics-only; retained seven groups are not rerun by that command.
int H_DiagnosticOutcomes(){
    const auto reject=[&](auto change,Gate expected){Fixture f;change(f);Gate actual=Gate::NotAttempted;
        const auto value=f.Get(Profiles294,&actual);return !value&&actual==expected;};
    CHECK(reject([](auto& f){f.s.selectedRigVerified=false;f.s.configurationVerified=false;},Gate::SelectedRig));
    CHECK(reject([](auto& f){f.s.configurationVerified=false;},Gate::Configuration));
    CHECK(reject([](auto& f){f.s.attachmentReady=false;},Gate::Attachment));
    CHECK(reject([](auto& f){f.s.ordinaryPublishedPalette=false;},Gate::Palette));
    CHECK(reject([](auto& f){f.s.equipment.weapon=0;},Gate::Equipment));
    CHECK(reject([](auto& f){f.s.generation=0;},Gate::Generation));
    CHECK(reject([](auto& f){f.s.ownerGeneration=0;},Gate::Owner));
    CHECK(reject([](auto& f){f.s.equipmentGeneration=0;},Gate::EquipmentGeneration));
    CHECK(reject([](auto& f){f.s.space=0;},Gate::Space));
    CHECK(reject([](auto& f){f.s.configurationSequence=f.s.generation+1;},Gate::Sequence));
    CHECK(reject([](auto& f){f.s.nowQpc=f.s.deadlineQpc;},Gate::InputTime));
    CHECK(reject([](auto& f){f.s.nowNs=f.s.configurationDeadlineNs;},Gate::ConfigurationTime));
    CHECK(reject([](auto& f){f.s.unitsPerMetre=0;},Gate::Units));
    CHECK(reject([](auto& f){f.rig.identity.count=0;},Gate::RigCount));
    CHECK(reject([](auto& f){f.packed.pop_back();},Gate::RigLayout));
    CHECK(reject([](auto& f){f.rig.weaponBone=f.rig.identity.count;},Gate::WeaponBone));
    CHECK(reject([](auto& f){f.writes.clear();},Gate::Writes));
    CHECK(reject([](auto& f){f.s.configuration="unsupported";},Gate::ProfileMissing));
    CHECK(reject([](auto& f){f.rig.names[100]=f.rig.names[f.bone];},Gate::PartNameAmbiguous));
    CHECK(reject([](auto& f){f.rig.parents[f.bone]=-1;},Gate::PartHierarchy));
    CHECK(reject([](auto& f){f.rig.nativeHiddenLeaves.push_back(f.bone);},Gate::PartHidden));
    CHECK(reject([](auto& f){f.writes[0].index=0;},Gate::WriteMissing));
    CHECK(reject([](auto& f){f.writes.push_back(f.writes[0]);},Gate::WriteAmbiguous));
    CHECK(reject([](auto& f){f.writes[0].transform.values[0][0]=2;},Gate::TrackedPartMatrix));
    CHECK(reject([](auto& f){f.rig.evaluatedWorld[f.bone].values[0][0]=2;},Gate::NativePartMatrix));
    CHECK(reject([](auto& f){f.rig.evaluatedWorld[f.rig.weaponBone].values[0][0]=2;},Gate::NativeWeaponMatrix));
    CHECK(reject([](auto& f){f.tracked.values[0][0]=2;},Gate::TrackedWeaponMatrix));
    CHECK(reject([](auto& f){f.rig.inverseBind[f.bone].values[0][0]=2;},Gate::InverseBindMatrix));
    CHECK(reject([](auto& f){f.packed[f.bone][12]^=std::byte{1};},Gate::PackedPalette));
    Fixture profile;Gate gate=Gate::NotAttempted;
    const std::array duplicate{Profiles294[0],Profiles294[0]};CHECK(!profile.Get(duplicate,&gate)&&gate==Gate::ProfileAmbiguous);
    auto p=Profiles294[0];p.state=1;CHECK(!profile.Get({&p,1},&gate)&&gate==Gate::ProfileState);
    p=Profiles294[0];p.expectedIndex=147;CHECK(!profile.Get({&p,1},&gate)&&gate==Gate::PartIndex);
    const auto revoke=[&](auto change,Gate expected){Fixture f;auto shot=f.MakeShot();change(f,shot);f.Retain(shot);
        return !shot.muzzlePartObservation&&shot.muzzlePartDiagnostic.capture==Gate::Passed&&shot.muzzlePartDiagnostic.retention==expected;};
    CHECK(revoke([](auto&,auto& s){s.aimEvidence->sightSelectedRig=true;s.aimEvidence->selectedRigVerified=false;},Gate::SelectedRig));
    CHECK(revoke([](auto&,auto& s){s.aimEvidence->configurationPathVerified=false;},Gate::Configuration));
    CHECK(revoke([](auto& f,auto&){++f.s.equipment.data;},Gate::Equipment));
    CHECK(revoke([](auto& f,auto&){++f.rig.identity.soldier;},Gate::Soldier));
    CHECK(revoke([](auto& f,auto&){++f.rig.identity.weak;},Gate::Weak));
    CHECK(revoke([](auto&,auto& s){s.aimEvidence->meshPath="different";},Gate::ProfileTuple));
    CHECK(revoke([](auto&,auto& s){++s.generation;},Gate::Generation));
    CHECK(revoke([](auto&,auto& s){++s.ownerGeneration;},Gate::Owner));
    CHECK(revoke([](auto& f,auto&){++f.s.equipmentGeneration;},Gate::EquipmentGeneration));
    CHECK(revoke([](auto&,auto& s){++s.space;},Gate::Space));
    CHECK(revoke([](auto&,auto& s){++s.deadline;},Gate::Deadline));
    CHECK(revoke([](auto&,auto& s){++s.aimEvidence->configurationSequence;},Gate::Sequence));
    CHECK(revoke([](auto&,auto& s){++s.aimEvidence->configurationDeadlineNs;},Gate::ConfigurationDeadline));
    CHECK(revoke([](auto&,auto& s){s.unitsPerMetre=2;},Gate::Units));
    CHECK(revoke([](auto& f,auto&){f.s.nowQpc=f.s.deadlineQpc;},Gate::InputTime));
    CHECK(revoke([](auto& f,auto&){f.s.nowNs=f.s.configurationDeadlineNs;},Gate::ConfigurationTime));
    Fixture blocked;auto blockedShot=blocked.MakeShot();blocked.Retain(blockedShot,false);
    CHECK(!blockedShot.muzzlePartObservation&&blockedShot.muzzlePartDiagnostic.retention==Gate::FiringBlocked);
    // Capture failure and later retention revocation both remain distinguishable after an owned copy and null serialization.
    Fixture absent;absent.s.ordinaryPublishedPalette=false;auto noValue=absent.MakeShot();absent.Retain(noValue);
    noValue.muzzlePartDiagnostic.helper=Gate::Absent;const auto retainedNull=noValue;
    CHECK(!retainedNull.muzzlePartObservation&&retainedNull.muzzlePartDiagnostic.capture==Gate::Palette);
    CHECK(retainedNull.muzzlePartDiagnostic.retention==Gate::Absent);
    std::ostringstream nullOut;Report(nullOut,retainedNull.muzzlePartObservation);CHECK(nullOut.str()=="null");
    std::ostringstream reasonOut;ReportDiagnostic(reasonOut,retainedNull.muzzlePartDiagnostic,retainedNull.muzzlePartObservation);
    CHECK(reasonOut.str()=="{\"version\":1,\"capture\":\"alternate_palette\",\"retention\":\"observation_absent\",\"helper\":\"observation_absent\",\"value_present\":false,\"profile_present\":false}");
    // Exact332 clocks and immutable scope identity are a synthetic diagnostic case, not reconstructed geometry.
    Fixture f(Profiles294[10]);f.s.generation=9366;f.s.configurationSequence=9360;
    f.s.nowQpc=2773301788345;f.s.deadlineQpc=2773302586083;
    f.s.nowNs=277330178834500;f.s.configurationDeadlineNs=277330273153700;
    auto publication=f.MakeShot();f.Retain(publication);const auto scope=publication;
    publication.muzzlePartDiagnostic.capture=Gate::PackedPalette;publication.muzzlePartObservation.reset();
    auto value=scope.muzzlePartObservation;auto diagnostic=scope.muzzlePartDiagnostic;
    CHECK(value&&diagnostic.capture==Gate::Passed&&diagnostic.retention==Gate::Passed);
    CHECK(SameAcceptedHelper(*value,f.s.equipment,11,12,9366,1,2,2773301790644,2773301790841,277330179084100,true,&diagnostic.helper));
    CHECK(diagnostic.helper==Gate::Passed&&value->deadlineQpc==2773302586083&&value->configurationDeadlineNs==277330273153700);
    const auto helperReject=[&](auto change,Gate expected){auto v=*value;change(v);Gate outcome=Gate::NotAttempted;
        return !SameAcceptedHelper(v,f.s.equipment,11,12,9366,1,2,2773301790644,2773301790841,277330179084100,true,&outcome)&&outcome==expected;};
    CHECK(helperReject([](auto& v){v.profile=nullptr;},Gate::ProfileMissing));
    CHECK(helperReject([](auto& v){++v.equipment.weapon;},Gate::Equipment));
    CHECK(helperReject([](auto& v){++v.rig.soldier;},Gate::Soldier));
    CHECK(helperReject([](auto& v){++v.rig.weak;},Gate::Weak));
    CHECK(helperReject([](auto& v){++v.generation;},Gate::Generation));
    CHECK(helperReject([](auto& v){++v.ownerGeneration;},Gate::Owner));
    CHECK(helperReject([](auto& v){++v.space;},Gate::Space));
    CHECK(helperReject([](auto& v){v.deadlineQpc=2773301790841;},Gate::InputTime));
    CHECK(helperReject([](auto& v){v.configurationDeadlineNs=277330179084100;},Gate::ConfigurationTime));
    CHECK(!SameAcceptedHelper(*value,f.s.equipment,11,12,9366,1,2,0,1,1,true,&gate)&&gate==Gate::HelperTiming);
    CHECK(!SameAcceptedHelper(*value,f.s.equipment,11,12,9366,1,2,1,2,1,false,&gate)&&gate==Gate::HelperRejected);
    // New callback diagnostics and by-value copying add no allocation; streams are worker-only.
    allocation_watch::calls=0;allocation_watch::active=true;
    const auto captured=f.Get(Profiles294,&gate);auto copied=scope;f.Retain(copied);
    const bool helper=captured&&SameAcceptedHelper(*captured,f.s.equipment,11,12,9366,1,2,2773301790644,2773301790841,277330179084100,true,&gate);
    allocation_watch::active=false;CHECK(helper&&allocation_watch::calls==0);
    CHECK(sizeof(Diagnostic)==3);return 0;
}
}
int main(int argc,char** argv){
    if(argc==2&&std::string_view(argv[1])=="--diagnostics-only"){const int result=H_DiagnosticOutcomes();if(!result)std::cout<<"H diagnostic outcomes passed\n";return result;}
    const std::array<std::pair<const char*,int(*)()>,8> groups{{{"A scope value",A_ScopeValue},{"B 80 tuples",B_ExactFamilies},
        {"C animation",C_Animation},{"D guards",D_Guards},{"E helper",E_Helper},{"F allocation",F_Allocation},{"G serialization/coordinates",G_SerializationCoordinates},{"H diagnostic outcomes",H_DiagnosticOutcomes}}};
    for(const auto& [name,run]:groups){if(run())return 1;std::cout<<name<<" passed\n";}
    std::cout<<"frame_bytes="<<sizeof(Frame)<<" optional_bytes="<<sizeof(std::optional<Frame>)<<" capacity64_bytes="<<64*sizeof(std::optional<Frame>)<<'\n';
}
