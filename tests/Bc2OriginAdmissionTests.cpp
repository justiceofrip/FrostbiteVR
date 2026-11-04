#ifdef NDEBUG
#error "Origin regression checks must remain active in Release builds"
#endif
// Fixture source only; ROOT must compile/run it in the normal test environment.
// All native addresses, route witnesses and active-state evidence here are
// synthetic. Registered profile geometry is real; no native qualification follows.
#include "CandidateOriginAdmission388.h"
#include "Bc2MuzzlePartProfiles294.h"
#include FVR_BC2_MUZZLE_ENROLLMENT_HEADER
#include "../profiles/weapons43/CombinedSupport.h"
#include "Test.h"
#include <cstring>
#include <limits>

using namespace fvr;
using namespace fvr::bc2;
namespace policy=origin388;
namespace part=muzzle_part_observation;
namespace {
constexpr std::int64_t nowNs=1000000000;
math::Matrix4 At(float x=0,float y=0,float z=0){math::Matrix4 m{};for(unsigned i=0;i<4;++i)m.values[i][i]=1;m.values[3]={x,y,z,1};return m;}
template<std::size_t N> void Text(std::array<char,N>& target,std::string_view s){target={};std::copy(s.begin(),s.end(),target.begin());}
policy::Route RouteOf(const NativeMuzzleConfig& c){return {c.speed,c.direction,c.position,c.inheritedSpeed,c.bulletsPerShell,c.bulletsPerShot,c.cameraFlags};}
NativeMuzzleConfig SyntheticNative(){NativeMuzzleConfig c;c.effects=0x10000;c.firingData=0x11000;c.function=0x12000;c.config=0x120a0;
    c.projectileData=0x13000;c.speed={0,0,600};c.direction={0,0,1};c.bulletsPerShell=c.bulletsPerShot=1;return c;}
policy::ProofKey Key(const WeaponEquipmentIdentity& equipment){policy::ProofKey k;
    k.owner={1,9,11,10,10,0,21,22,equipment.weapon,23,0,equipment,true};
    k.scopeSerial=31;k.invocation=32;k.callerLifetime=33;k.publicationSerial=34;k.thread=35;
    k.generation=20;k.ownerGeneration=12;k.equipmentGeneration=13;k.space=14;k.configurationSequence=19;
    k.tokenValid=true;k.token=0; // zero is a valid native token when tokenValid=true
    k.sourceP=0x20000;k.callerReturn=0x57aa3f;k.target=0x7155b0;
    const auto c=SyntheticNative();k.effects=c.effects;k.configuration=c.config;k.firingData=c.firingData;
    k.deadlineQpc=200;k.inputDeadlineNs=nowNs+90000000;k.configurationDeadlineNs=nowNs+200000000;return k;}
part::Source Source(const policy::ProofKey& k,std::string_view configuration,std::string_view mesh,std::uint64_t rig,float units){
    part::Source s;s.equipment=k.owner.equipment;s.configuration=configuration;s.mesh=mesh;s.rigFingerprint=rig;
    s.generation=k.generation;s.ownerGeneration=k.ownerGeneration;s.equipmentGeneration=k.equipmentGeneration;s.space=k.space;
    s.nowQpc=100;s.deadlineQpc=k.deadlineQpc;s.nowNs=nowNs;s.configurationDeadlineNs=k.configurationDeadlineNs;
    s.configurationSequence=k.configurationSequence;s.unitsPerMetre=units;
    s.selectedRigVerified=s.configurationVerified=s.attachmentReady=s.ordinaryPublishedPalette=true;return s;
}
policy::CandidateOriginEnrollment Enroll(const NativeMuzzleConfig& c){policy::CandidateOriginEnrollment p;
    p.admission=policy::Admission::Candidate;p.activeState=0;p.route=RouteOf(c);
    p.activeStateEvidence="synthetic fixture active-state route, not native evidence";
    p.coordinateBoundaryEvidence="synthetic fixture absolute native RH boundary, not target proof";
    p.boundary=policy::Boundary::AbsoluteNativeWorldRh;p.maximumDisplacementMetres=2;p.maximumAbsoluteWorldUnits=std::numeric_limits<float>::max();return p;
}
policy::Request Request(const policy::ProofKey& k,const part::Source& s,const RigIdentity& rig,
    const policy::CandidateOriginEnrollment& enrollment,const NativeMuzzleConfig& native){policy::Request r;
    r.capabilityEnabled=r.firstTargetEntry=true;r.nestedOrAmbiguous=false;r.captured=r.current=k;r.enrollment=&enrollment;
    r.evidence.proof=k;r.evidence.publication=s;r.evidence.rig=rig;r.evidence.activeState=0;
    r.evidence.firingAllowed=r.evidence.exitVisible=true;
    r.coordinate={k,policy::Boundary::AbsoluteNativeWorldRh,s.unitsPerMetre,{8,9,10}};
    r.capturedConfig=r.currentConfig=native;return r;
}
struct PartFixture {
    const part::Profile& profile;
    RigSnapshot rig{};std::vector<interaction::BoneWrite> writes;std::vector<std::array<std::byte,64>> packed;
    policy::ProofKey key{};part::Source source{};std::optional<part::Frame> frame;
    NativeMuzzleConfig native=SyntheticNative();policy::CandidateOriginEnrollment enrollment=Enroll(native);
    explicit PartFixture(const part::Profile& p,float units=1):profile(p){
        WeaponEquipmentIdentity equipment;equipment.weapon=15;equipment.data=16;Text(equipment.asset,p.asset);key=Key(equipment);
        source=Source(key,p.configuration,p.mesh,p.rigFingerprint,units);enrollment.observedPart=&p;
        rig.identity={10,11,13,14,15,16,17,18,19,147,true};rig.weaponBone=10;
        rig.names.resize(147);rig.parents.assign(147,-1);rig.evaluatedWorld.assign(147,At());rig.inverseBind.assign(147,At());
        rig.nativeEvaluated.resize(147);packed.resize(147);
        for(unsigned i=0;i<147;++i){rig.names[i]="other"+std::to_string(i);rig.nativeEvaluated[i].fill(std::byte{0x5a});}
        rig.names[9]="jntWpn_0";rig.names[10]="jntWpn_1";rig.parents[10]=9;
        rig.names[p.expectedIndex]=p.part;if(p.expectedIndex!=10)rig.parents[p.expectedIndex]=10;
        auto m=At(8,9,-10);m.values[0]={0,0,-1,0};m.values[2]={1,0,0,0};
        writes.push_back({p.expectedIndex,m});PackAndCapture();
    }
    void PackAndCapture(){packed=rig.nativeEvaluated;
        for(const auto& w:writes){auto skin=interaction::Multiply(rig.inverseBind[w.index],w.transform);
            for(unsigned i=0;i<4;++i){skin.values[2][i]=-skin.values[2][i];skin.values[i][2]=-skin.values[i][2];}
            for(unsigned row=0;row<4;++row)std::memcpy(packed[w.index].data()+row*16,skin.values[row].data(),12);}
        frame=part::Capture(part::Profiles294,source,rig,writes,packed,At(8,9,-10));
    }
    policy::Request Get(){auto r=Request(key,source,rig.identity,enrollment,native);r.evidence.observedPart=frame?&*frame:nullptr;return r;}
};
const MuzzleEnrollmentProfile* FindFirearm(std::string_view configuration){
    for(const auto& p:generated_muzzle::Firearm54MuzzleEnrollments)if(p.configurationPath==configuration)return &p;return nullptr;
}
struct FirearmFixture {
    const MuzzleEnrollmentProfile& profile;const AuthoredSupportProfile* support=nullptr;
    std::shared_ptr<SelectedMeshesSnapshot> selected=std::make_shared<SelectedMeshesSnapshot>();
    policy::ProofKey key{};part::Source source{};std::optional<MuzzleBinding> binding;
    math::Matrix4 weapon=At(8,9,-10),flash{};NativeMuzzleConfig native=SyntheticNative();
    policy::CandidateOriginEnrollment enrollment{};
    explicit FirearmFixture(const MuzzleEnrollmentProfile& p,float units):profile(p){
        for(const auto& s:generated::AuthoredSupports)if(s.bindingDigest==p.bindingDigest)support=&s;
        WeaponEquipmentIdentity equipment;equipment.weapon=15;equipment.data=16;Text(equipment.asset,p.asset);key=Key(equipment);
        source=Source(key,p.configurationPath,p.meshPath,p.rigFingerprint,units);
        auto& s=*selected;s.owner={9,10,11,15,12,18,14};s.sequence=key.configurationSequence;s.observedNs=nowNs-10000000;
        s.deadlineNs=key.configurationDeadlineNs;s.weaponData=16;s.stateTypeInfo=1;s.meshTypeInfo=2;s.inventory=3;
        Text(s.weaponName,p.asset);s.stateCount=1;s.soleConfiguredArray=5;s.states[0].array=5;s.states[0].state=6;s.states[0].count=1;
        auto& mesh=s.states[0].meshes[0];mesh.address=7;mesh.typeInfo=2;mesh.namePointer=8;Text(mesh.assetPath,p.meshPath);
        s.configurationPathVerified=true;s.configurationPathPointer=9;Text(s.configurationPath,p.configurationPath);
        auto local=p.flashInWeapon;for(unsigned i=0;i<3;++i)local.values[3][i]*=units;
        flash=interaction::Multiply(local,weapon);
        const AuthoredGripOwner owner{10,11,12,13,14,20,key.inputDeadlineNs};
        if(support){const auto b=BindAuthoredSupportReference({support,1},selected,equipment,owner,p.rigFingerprint,nowNs);
            if(b)binding=BindAuthoredMuzzle({&p,1},*b,weapon,flash,units,nowNs);}
        native.speed=p.initialSpeed;native.direction=p.initialDirection;native.position=p.initialPosition;
        native.inheritedSpeed=p.inheritedWeaponSpeed;native.bulletsPerShell=p.bulletsPerShell;native.bulletsPerShot=p.bulletsPerShot;
        native.cameraFlags[0]=static_cast<unsigned char>(p.activeForceSpawnToCamera);
        enrollment=Enroll(native);enrollment.authoredFirearm=&p;
    }
    policy::Request Get(){const RigIdentity rig{10,11,13,14,15,16,17,18,19,147,true};
        auto r=Request(key,source,rig,enrollment,native);r.evidence.authoredFirearm=binding?&*binding:nullptr;
        r.evidence.currentSelected=selected.get();r.evidence.trackedWeapon=weapon;r.evidence.trackedFlash=flash;return r;
    }
};
std::optional<policy::Result> Prepare(const policy::Request& r){policy::Once once;return policy::PrepareOriginOnce(once,r);}
int TwoMovingExitFamiliesAndUnits(){
    const std::array<unsigned,2> indices{0,11}; // registered XM320 hinged part / AN94 root exit
    CHECK(part::Profiles294[indices[0]].part!=part::Profiles294[indices[1]].part);
    for(const auto index:indices)for(const float units:{1.f,100.f}){
        PartFixture f(part::Profiles294[index],units);CHECK(f.frame);const auto r=f.Get();const auto out=Prepare(r);CHECK(out);
        const auto local=f.profile.localExitMeters;CHECK(local.z<0);
        // Analytic quarter-turn: local Z moves world X; reflect final world Z.
        CHECK(Near(out->nativeXYZ[0],8+local.z*units,.0001f));CHECK(Near(out->nativeXYZ[1],9+local.y*units,.0001f));
        CHECK(Near(out->nativeXYZ[2],10+local.x*units,.0001f));CHECK(out->proof==r.captured);
        CHECK(out->enrollment==&f.enrollment);
        f.rig.nativeHiddenLeaves.push_back(f.profile.expectedIndex);f.PackAndCapture();CHECK(!f.frame&&!Prepare(f.Get()));
    }return 0;
}
int ExistingFirearmBindings(){
    CHECK(generated_muzzle::Firearm54MuzzleEnrollments.size()==181); // Candidate inventory, not native verification
    for(const auto configuration:{"Objects/Weapons/Handheld/RU_lmg_PKM/RU_lmg_PKM","Objects/Weapons/Handheld/MEC_lmg_QJY88/MEC_lmg_QJY88"}){
        const auto* profile=FindFirearm(configuration);CHECK(profile);
        for(const float units:{1.f,100.f}){FirearmFixture f(*profile,units);CHECK(f.binding);const auto out=Prepare(f.Get());CHECK(out);
            CHECK(Near(out->nativeXYZ[0],8+profile->flashInWeapon.values[3][0]*units));
            CHECK(Near(out->nativeXYZ[1],9+profile->flashInWeapon.values[3][1]*units));
            CHECK(Near(out->nativeXYZ[2],10-profile->flashInWeapon.values[3][2]*units));
            auto r=f.Get();r.evidence.trackedFlash.values[3][0]+=.01f*units;CHECK(!Prepare(r)); // wrong local reference
            // The binding owns the captured shared snapshot. Mutate a separate
            // current view, so both sides of the comparison cannot change together.
            auto currentSelected=*f.selected;r=f.Get();r.evidence.currentSelected=&currentSelected;
            Text(currentSelected.configurationPath,"Objects/Weapons/UnlistedConfiguration");CHECK(!Prepare(r));
            currentSelected=*f.selected;++currentSelected.configurationPathPointer;CHECK(!Prepare(r));
            CHECK(Prepare(f.Get())); // captured snapshot remains unchanged
            // This AFTER-selection capability has its own exact force-camera
            // authorization; the old ordinary ComposeHook predicate still rejects it.
            f.native.cameraFlags[1]=1;f.enrollment.route=RouteOf(f.native);
            CHECK(!OrdinaryMuzzleConfig(f.native));CHECK(Prepare(f.Get()));
            auto binding=*f.binding;++binding.support->owner.equipmentGeneration;r=f.Get();r.evidence.authoredFirearm=&binding;CHECK(!Prepare(r));
            r=f.Get();r.evidence.publication.nowNs=r.captured.inputDeadlineNs;CHECK(!Prepare(r));
            // Exact enrollment remains independent from authored Candidate state.
            f.enrollment.admission=policy::Admission::Disabled;CHECK(!Prepare(f.Get()));
        }
    }return 0;
}
int RefusalsAndOriginalLeases(){
    PartFixture f(part::Profiles294[0]);CHECK(f.frame);const auto base=f.Get();CHECK(Prepare(base));
    const auto refused=[&](auto change){auto r=base;change(r);return !Prepare(r);};
    CHECK(refused([](auto& r){r.capabilityEnabled=false;}));CHECK(refused([](auto& r){r.enrollment=nullptr;}));
    CHECK(refused([](auto& r){r.firstTargetEntry=false;}));CHECK(refused([](auto& r){r.nestedOrAmbiguous=true;}));
    CHECK(refused([](auto& r){++r.current.owner.weak;}));CHECK(refused([](auto& r){++r.current.owner.equipment.persistence;}));
    CHECK(refused([](auto& r){++r.current.owner.router;}));CHECK(refused([](auto& r){++r.current.owner.role;}));
    CHECK(refused([](auto& r){++r.current.scopeSerial;}));CHECK(refused([](auto& r){++r.current.invocation;}));
    CHECK(refused([](auto& r){++r.current.callerLifetime;}));CHECK(refused([](auto& r){++r.current.sourceP;}));
    CHECK(refused([](auto& r){++r.current.target;}));CHECK(refused([](auto& r){++r.current.callerReturn;}));
    CHECK(refused([](auto& r){++r.current.token;}));CHECK(refused([](auto& r){r.current.tokenValid=false;}));
    CHECK(refused([](auto& r){++r.current.thread;}));CHECK(refused([](auto& r){++r.current.space;}));
    CHECK(refused([](auto& r){++r.evidence.proof.publicationSerial;})); // same input generation is insufficient
    CHECK(refused([](auto& r){r.evidence.publication.configuration="wrong";}));
    CHECK(refused([](auto& r){++r.evidence.publication.equipmentGeneration;}));
    CHECK(refused([](auto& r){++r.evidence.publication.configurationSequence;}));
    CHECK(refused([](auto& r){r.evidence.publication.nowQpc=r.captured.deadlineQpc;}));
    CHECK(refused([](auto& r){r.evidence.publication.nowNs=r.captured.inputDeadlineNs;}));
    CHECK(refused([](auto& r){r.evidence.publication.nowNs=r.captured.configurationDeadlineNs;}));
    CHECK(refused([](auto& r){++r.current.deadlineQpc;})); // fresh current input cannot renew original lease
    CHECK(refused([](auto& r){++r.evidence.publication.configurationDeadlineNs;}));
    CHECK(refused([](auto& r){r.evidence.exitVisible=false;}));CHECK(refused([](auto& r){r.evidence.firingAllowed=false;}));
    CHECK(refused([](auto& r){++r.evidence.activeState;}));CHECK(refused([](auto& r){r.evidence.publication.ordinaryPublishedPalette=false;}));
    CHECK(refused([](auto& r){r.evidence.publication.attachmentReady=false;}));
    CHECK(refused([](auto& r){++r.currentConfig.config;}));CHECK(refused([](auto& r){++r.currentConfig.projectileData;}));
    CHECK(refused([](auto& r){r.capturedConfig.position[2]=r.currentConfig.position[2]=.1f;}));
    CHECK(refused([](auto& r){r.capturedConfig.cameraFlags[1]=r.currentConfig.cameraFlags[1]=1;}));
    CHECK(refused([](auto& r){r.capturedConfig.cameraFlags[2]=r.currentConfig.cameraFlags[2]=1;}));
    CHECK(refused([](auto& r){r.capturedConfig.inheritedSpeed=r.currentConfig.inheritedSpeed=1;}));
    CHECK(refused([](auto& r){r.coordinate.boundary=policy::Boundary::Unreviewed;}));
    CHECK(refused([](auto& r){r.coordinate.unitsPerMetre*=100;}));
    CHECK(refused([](auto& r){r.coordinate.nativeInputXYZ[0]+=3;}));
    CHECK(refused([](auto& r){r.coordinate.nativeInputXYZ[0]=std::numeric_limits<float>::quiet_NaN();}));
    auto wrong=*f.frame;wrong.trackedPart.values[3][0]=std::numeric_limits<float>::infinity();
    auto r=base;r.evidence.observedPart=&wrong;CHECK(!Prepare(r));
    // Isolate the configuration deadline while the original input lease lives.
    r=base;r.captured.inputDeadlineNs=r.captured.configurationDeadlineNs+1000;
    r.current=r.evidence.proof=r.coordinate.proof=r.captured;
    r.evidence.publication.nowNs=r.captured.configurationDeadlineNs-1;CHECK(Prepare(r));
    ++r.evidence.publication.nowNs;CHECK(!Prepare(r));
    // Even a caller supplying a refreshed configuration cannot renew Frame's lease.
    ++r.captured.configurationDeadlineNs;r.current=r.evidence.proof=r.coordinate.proof=r.captured;
    r.evidence.publication.configurationDeadlineNs=r.captured.configurationDeadlineNs;CHECK(!Prepare(r));
    f.enrollment.maximumAbsoluteWorldUnits=1;CHECK(!Prepare(f.Get()));f.enrollment.maximumAbsoluteWorldUnits=std::numeric_limits<float>::max();
    f.enrollment.maximumDisplacementMetres=2.1f;CHECK(!Prepare(f.Get()));f.enrollment.maximumDisplacementMetres=2;
    f.enrollment.coordinateBoundaryEvidence={};CHECK(!Prepare(f.Get()));
    return 0;
}
int PostSelectionRoutes(){
    // Synthetic route witnesses with real two-family exit geometry; this does
    // not enroll a native configuration or claim that the game accepted a write.
    for(const unsigned index:{0u,11u})for(const float units:{1.f,100.f}){
        PartFixture f(part::Profiles294[index],units);CHECK(f.frame);
        const auto before=Prepare(f.Get());CHECK(before);
        for(const unsigned char active:{0,1})for(const unsigned char force:{0,1}){
            f.native.cameraFlags={active,force,0};f.native.position={.125f,-.25f,.5f};
            f.native.speed={0,0,50};f.native.bulletsPerShell=6;f.native.bulletsPerShot=3;
            f.enrollment.route=RouteOf(f.native);auto r=f.Get();const auto nativeBefore=r.currentConfig;
            const auto out=Prepare(r);CHECK(out);CHECK(out->nativeXYZ==before->nativeXYZ);
            CHECK(r.currentConfig==nativeBefore); // no repeated InitialPosition addition or velocity/count mutation
            auto mismatch=r;mismatch.currentConfig.cameraFlags[0]=mismatch.capturedConfig.cameraFlags[0]=static_cast<unsigned char>(1-active);
            CHECK(!Prepare(mismatch));
            mismatch=r;mismatch.currentConfig.cameraFlags[1]=mismatch.capturedConfig.cameraFlags[1]=static_cast<unsigned char>(1-force);
            CHECK(!Prepare(mismatch));
            mismatch=r;mismatch.currentConfig.bulletsPerShell=mismatch.capturedConfig.bulletsPerShell=5;CHECK(!Prepare(mismatch));
            mismatch=r;mismatch.currentConfig.bulletsPerShot=mismatch.capturedConfig.bulletsPerShot=2;CHECK(!Prepare(mismatch));
            mismatch=r;mismatch.currentConfig.position[2]=mismatch.capturedConfig.position[2]=.6f;CHECK(!Prepare(mismatch));
            mismatch=r;mismatch.currentConfig.speed[2]=mismatch.capturedConfig.speed[2]=51;CHECK(!Prepare(mismatch));
        }
        // Exact enrollment never opts into unreviewed relative-target/explosion,
        // inherited velocity, direction or malformed numeric/flag routes.
        const auto unsupported=[&](auto change){auto r=f.Get();auto p=f.enrollment;
            change(r.currentConfig);r.capturedConfig=r.currentConfig;p.route=RouteOf(r.currentConfig);r.enrollment=&p;
            return !Prepare(r);};
        CHECK(unsupported([](auto& c){c.cameraFlags[2]=1;}));
        CHECK(unsupported([](auto& c){c.cameraFlags[0]=2;}));
        CHECK(unsupported([](auto& c){c.cameraFlags[1]=2;}));
        CHECK(unsupported([](auto& c){c.muzzleExplosion=0x12345;}));
        CHECK(unsupported([](auto& c){c.inheritedSpeed=1;}));
        CHECK(unsupported([](auto& c){c.direction[0]=.1f;}));
        CHECK(unsupported([](auto& c){c.speed[0]=.1f;}));
        CHECK(unsupported([](auto& c){c.speed[2]=0;}));
        CHECK(unsupported([](auto& c){c.position[1]=std::numeric_limits<float>::infinity();}));
        CHECK(unsupported([](auto& c){c.speed[2]=std::numeric_limits<float>::quiet_NaN();}));
    }return 0;
}
int OnceAndNoNativeMutation(){
    PartFixture f(part::Profiles294[0]);auto r=f.Get();policy::Once once;policy::Gate gate{};
    std::array<float,16> nativeInput{1,.2f,.3f,77,.4f,1,.6f,78,.7f,.8f,1,79,8,9,10,80};
    std::array<float,3> velocity{20,-30,40};const auto original=nativeInput;const auto originalVelocity=velocity;
    r.coordinate.nativeInputXYZ={nativeInput[12],nativeInput[13],nativeInput[14]};
    const auto out=policy::PrepareOriginOnce(once,r,&gate);CHECK(out&&gate==policy::Gate::Passed);
    CHECK(nativeInput==original&&velocity==originalVelocity); // helper returns XYZ only
    nativeInput[12]=99;const auto afterNative=nativeInput; // later native projectile mutation belongs to native
    r.coordinate.nativeInputXYZ[0]=99;CHECK(!policy::PrepareOriginOnce(once,r,&gate));
    CHECK(gate==policy::Gate::AlreadyAttempted&&nativeInput==afterNative&&velocity==originalVelocity);
    policy::Once refused;r=f.Get();r.capabilityEnabled=false;CHECK(!policy::PrepareOriginOnce(refused,r,&gate));
    r.capabilityEnabled=true;CHECK(!policy::PrepareOriginOnce(refused,r,&gate));CHECK(gate==policy::Gate::AlreadyAttempted);
    CHECK(nativeInput==afterNative&&velocity==originalVelocity);return 0;
}
} // namespace
int main(){return TwoMovingExitFamiliesAndUnits()||ExistingFirearmBindings()||RefusalsAndOriginalLeases()||PostSelectionRoutes()||OnceAndNoNativeMutation();}
