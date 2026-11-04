#pragma once
// Pure candidate admission policy. No native reads/writes, clocks, allocation
// or hooks. Separate host enrollment is required; this is not NativeVerified.
#include "Bc2MuzzleAdmission.h"
#include "Bc2MuzzlePartObservation.h"
#include <limits>
#include <type_traits>

namespace fvr::bc2::origin388 {
namespace part = muzzle_part_observation;
enum class Admission : unsigned { Disabled, Candidate };
enum class Boundary : unsigned { Unreviewed, AbsoluteNativeWorldRh };
enum class Gate : unsigned {
    Passed, AlreadyAttempted, Disabled, Ordering, Identity, Enrollment, Publication,
    ActiveExit, NativeConfiguration, Route, Boundary, Geometry, Bounds
};
// Copies every field of Gameplay::Owner; role is also pinned conservatively.
struct Owner {
    unsigned manager=0,player=0,weak=0,soldier=0,controlled=0,entry=0,router=0,cache=0,weapon=0,aim=0,role=0;
    WeaponEquipmentIdentity equipment{};
    bool foot=false;
    bool operator==(const Owner&)const=default;
};
struct ProofKey {
    Owner owner{};
    std::uint64_t scopeSerial=0,invocation=0,callerLifetime=0,publicationSerial=0;
    std::uint64_t generation=0,ownerGeneration=0,equipmentGeneration=0,space=0,configurationSequence=0;
    unsigned thread=0,token=0,sourceP=0,callerReturn=0,target=0,effects=0,configuration=0,firingData=0;
    std::int64_t deadlineQpc=0,inputDeadlineNs=0,configurationDeadlineNs=0;
    bool tokenValid=false;
    bool operator==(const ProofKey&)const=default;
};
struct Route {
    std::array<float,3> speed{},direction{},position{};
    float inheritedSpeed=0;
    unsigned bulletsPerShell=0,bulletsPerShot=0;
    std::array<unsigned char,3> cameraFlags{}; // active, force, relative: distinct native fields
};
// ROOT selects an EXACT immutable tuple/profile and separately reviews its
// active state, firing route and coordinate boundary. Merely finding a passive
// Profile294 or a Candidate firearm profile never constructs this enrollment.
// Strings and pointed-to compiled profiles must outlive the entire shot scope.
struct CandidateOriginEnrollment {
    Admission admission=Admission::Disabled;
    const part::Profile* observedPart=nullptr;
    const MuzzleEnrollmentProfile* authoredFirearm=nullptr; // exactly one source
    std::string_view activeStateEvidence{},coordinateBoundaryEvidence{};
    Boundary boundary=Boundary::Unreviewed;
    unsigned activeState=UINT32_MAX;
    Route route{};
    // Explicit review limits, not guessed global world extents. ROOT may use
    // float::max as the numeric representability ceiling, never as map evidence.
    // Displacement may only narrow ComposeHook's existing two-metre bound.
    float maximumDisplacementMetres=0,maximumAbsoluteWorldUnits=0;
};
struct Evidence {
    ProofKey proof{}; // stamped when copying this ONE immutable shot publication
    part::Source publication{}; // original leases, with current supplied clocks
    RigIdentity rig{};
    const part::Frame* observedPart=nullptr;
    const MuzzleBinding* authoredFirearm=nullptr;
    const SelectedMeshesSnapshot* currentSelected=nullptr;
    math::Matrix4 trackedWeapon{},trackedFlash{}; // from that same copied shot
    unsigned activeState=UINT32_MAX;
    // Current selected mode/rig and ordinary visible palette; not a GPU receipt.
    // The adapter must derive these from live guards, never SourceFromShot's
    // convenience defaults alone. An off-screen weapon is not hidden geometry.
    bool firingAllowed=false,exitVisible=false;
};
struct CoordinateInput {
    ProofKey proof{};
    Boundary boundary=Boundary::Unreviewed;
    float unitsPerMetre=0;
    // Read at the reviewed P boundary. This is absolute native world XYZ,
    // never an unlabelled helper, camera-relative or diagnostic vector.
    std::array<float,3> nativeInputXYZ{};
};
struct Request {
    bool capabilityEnabled=false,firstTargetEntry=false,nestedOrAmbiguous=true;
    ProofKey captured{},current{};
    const CandidateOriginEnrollment* enrollment=nullptr;
    Evidence evidence{};
    CoordinateInput coordinate{};
    NativeMuzzleConfig capturedConfig{},currentConfig{};
};
struct Result {
    ProofKey proof{};
    const CandidateOriginEnrollment* enrollment=nullptr;
    std::array<float,3> nativeXYZ{};
};
// Construct once per immutable whole-shot scope/P lifetime; never reset/reuse.
// First refusal consumes preparation as well: no late admission after an entry.
struct Once { bool attempted=false; };
static_assert(std::is_trivially_copyable_v<Request> && std::is_trivially_copyable_v<Result>);

namespace detail {
inline bool Key(const ProofKey& k)noexcept {
    return k.scopeSerial&&k.invocation&&k.callerLifetime&&k.publicationSerial&&k.thread&&k.tokenValid&&
        k.sourceP&&k.callerReturn&&k.target&&k.effects&&k.configuration&&k.firingData&&
        k.generation&&k.ownerGeneration&&k.equipmentGeneration&&k.space&&k.configurationSequence&&
        k.configurationSequence<=k.generation&&k.deadlineQpc>0&&k.inputDeadlineNs>0&&k.configurationDeadlineNs>0&&
        k.owner.foot&&k.owner.player&&k.owner.soldier&&k.owner.weak&&
        k.owner.weapon==k.owner.equipment.weapon&&k.owner.equipment.weapon&&k.owner.equipment.data&&
        !k.owner.equipment.Asset().empty(); // valid token zero is intentionally allowed
}
inline bool Publication(const part::Source& s,const ProofKey& k)noexcept {
    return s.equipment==k.owner.equipment&&s.generation==k.generation&&s.ownerGeneration==k.ownerGeneration&&
        s.equipmentGeneration==k.equipmentGeneration&&s.space==k.space&&
        s.deadlineQpc==k.deadlineQpc&&s.configurationSequence==k.configurationSequence&&
        s.configurationDeadlineNs==k.configurationDeadlineNs&&s.nowQpc>0&&s.nowQpc<k.deadlineQpc&&
        s.nowNs>0&&s.nowNs<k.inputDeadlineNs&&s.nowNs<k.configurationDeadlineNs&&
        std::isfinite(s.unitsPerMetre)&&s.unitsPerMetre>0&&s.selectedRigVerified&&s.configurationVerified&&
        s.attachmentReady&&s.ordinaryPublishedPalette;
}
inline bool RouteMatches(const Route& p,const NativeMuzzleConfig& c)noexcept {
    // This is AFTER native camera selection at7155b0, not ComposeHook's earlier
    // ordinary route. ForceSpawnToCamera is therefore an exact matched input,
    // not a veto. InitialPosition was composed at579ed4..579f15; absolute P XYZ
    // is replaced once here, so never add that offset to the rendered exit.
    // Relative-target aiming/explosion and nonstandard velocity routes retain
    // their separate exclusions; their pre-target semantics are not enrolled.
    if(c.direction!=std::array<float,3>{0,0,1}||c.speed[0]!=0||c.speed[1]!=0||
       !std::isfinite(c.speed[2])||c.speed[2]<=0||c.inheritedSpeed!=0||
       !c.bulletsPerShell||!c.bulletsPerShot||c.muzzleExplosion||c.cameraFlags[2]||
       std::any_of(c.cameraFlags.begin(),c.cameraFlags.end(),[](auto v){return v>1;})||
       std::any_of(c.position.begin(),c.position.end(),[](float v){return !std::isfinite(v);}))return false;
    return c.speed==p.speed&&c.direction==p.direction&&c.position==p.position&&
        c.inheritedSpeed==p.inheritedSpeed&&c.bulletsPerShell==p.bulletsPerShell&&
        c.bulletsPerShot==p.bulletsPerShot&&c.cameraFlags==p.cameraFlags;
}
// The binding's immutable descriptor/geometry was validated by Authored below.
// Match its authored native fields without reusing OrdinaryMuzzleConfig's
// earlier-ComposeHook ForceSpawnToCamera exclusion. The explicit new origin
// enrollment additionally pins all three camera flags in RouteMatches.
inline bool AuthoredNativeMatches(const NativeMuzzleConfig& c,const MuzzleBinding& b)noexcept {
    if(!b.profile)return false;const auto& p=*b.profile;
    return c.speed==p.initialSpeed&&c.direction==p.initialDirection&&c.position==p.initialPosition&&
        c.inheritedSpeed==p.inheritedWeaponSpeed&&c.bulletsPerShell==p.bulletsPerShell&&
        c.bulletsPerShot==p.bulletsPerShot&&c.cameraFlags[0]==unsigned(p.activeForceSpawnToCamera);
}
inline bool Config(const NativeMuzzleConfig& c,const ProofKey& k)noexcept {
    return c.effects==k.effects&&c.firingData==k.firingData&&c.config==k.configuration&&
        c.effects>=0x10000&&c.firingData>=0x10000&&c.function>=0x10000&&
        std::uint64_t(c.function)+0xa0==c.config&&c.projectileData>=0x10000;
}
inline bool Authored(const Evidence& e,const CandidateOriginEnrollment& enrolled)noexcept {
    if(!e.authoredFirearm||e.observedPart||!e.currentSelected)return false;
    const auto& b=*e.authoredFirearm;const auto* p=enrolled.authoredFirearm;
    if(!p||b.profile!=p)return false;
    const auto& k=e.proof;const auto& s=e.publication;
    const AuthoredGripOwner owner{k.owner.soldier,k.owner.weak,k.ownerGeneration,k.equipmentGeneration,
        k.space,k.generation,k.inputDeadlineNs};
    const auto& originalOwner=b.support?b.support->owner:b.authored.owner;
    const auto selected=MuzzleBindingSelected(b);
    return selected&&originalOwner.inputSequence==k.generation&&originalOwner.inputDeadlineNs==k.inputDeadlineNs&&
        selected->sequence==k.configurationSequence&&selected->deadlineNs==k.configurationDeadlineNs&&
        p->configurationPath==s.configuration&&p->asset==s.equipment.Asset()&&p->meshPath==s.mesh&&
        p->rigFingerprint==s.rigFingerprint&&
        MuzzleBindingCurrent(b,e.currentSelected,s.equipment,owner,s.nowNs)&&
        muzzle_detail::Flash(*p,e.trackedWeapon,e.trackedFlash,s.unitsPerMetre);
}
inline std::optional<Result> Prepare(const Request& r,Gate* outcome)noexcept {
    const auto reject=[&](Gate g)->std::optional<Result>{if(outcome)*outcome=g;return {};};
    if(!r.capabilityEnabled)return reject(Gate::Disabled);
    if(!r.firstTargetEntry||r.nestedOrAmbiguous)return reject(Gate::Ordering);
    if(!Key(r.captured)||r.current!=r.captured||r.evidence.proof!=r.captured||r.coordinate.proof!=r.captured)
        return reject(Gate::Identity);
    if(!r.enrollment||r.enrollment->admission!=Admission::Candidate)return reject(Gate::Enrollment);
    const auto& p=*r.enrollment;const auto& e=r.evidence;const auto& s=e.publication;
    if(bool(p.observedPart)==bool(p.authoredFirearm)||p.activeStateEvidence.empty()||p.activeState==UINT32_MAX)
        return reject(Gate::Enrollment);
    if(!Publication(s,r.captured)||e.rig.soldier!=r.captured.owner.soldier||e.rig.weak!=r.captured.owner.weak||!e.rig.count)
        return reject(Gate::Publication);
    if(!e.firingAllowed||!e.exitVisible||e.activeState!=p.activeState)return reject(Gate::ActiveExit);
    if(r.capturedConfig!=r.currentConfig||!Config(r.currentConfig,r.captured))return reject(Gate::NativeConfiguration);
    if(!RouteMatches(p.route,r.currentConfig))return reject(Gate::Route);
    if(p.boundary!=Boundary::AbsoluteNativeWorldRh||p.coordinateBoundaryEvidence.empty()||
       r.coordinate.boundary!=p.boundary||r.coordinate.unitsPerMetre!=s.unitsPerMetre)return reject(Gate::Boundary);
    std::array<double,3> world{};
    if(p.observedPart){
        if(!e.observedPart||e.authoredFirearm||e.observedPart->profile!=p.observedPart||
           e.observedPart->rig!=e.rig||!part::Current(*e.observedPart,s)||
           e.observedPart->bone!=p.observedPart->expectedIndex||p.observedPart->state!=p.activeState||
           !authored_grip_detail::Digest(p.observedPart->tupleSha256))return reject(Gate::Enrollment);
        const auto& f=*e.observedPart;
        if(!interaction::InverseAnimatedTransform(f.trackedPart))return reject(Gate::Geometry);
        const auto local=p.observedPart->localExitMeters;
        const std::array<double,3> scaled{double(local.x)*s.unitsPerMetre,double(local.y)*s.unitsPerMetre,double(local.z)*s.unitsPerMetre};
        for(unsigned c=0;c<3;++c){
            world[c]=f.trackedPart.values[3][c];
            for(unsigned row=0;row<3;++row)world[c]+=scaled[row]*f.trackedPart.values[row][c];
        }
    }else{
        if(!Authored(e,p)||!AuthoredNativeMatches(r.currentConfig,*e.authoredFirearm))return reject(Gate::Enrollment);
        for(unsigned c=0;c<3;++c)world[c]=e.trackedFlash.values[3][c];
    }
    if(!std::isfinite(p.maximumDisplacementMetres)||p.maximumDisplacementMetres<=0||p.maximumDisplacementMetres>2||
       !std::isfinite(p.maximumAbsoluteWorldUnits)||p.maximumAbsoluteWorldUnits<=0)return reject(Gate::Bounds);
    // Both source transforms are already canonical LH WORLD units. Local metres
    // were scaled above; world translation is neither scaled nor added again.
    world[2]=-world[2]; // established ComposeHook LH -> absolute native RH boundary
    Result out{r.captured,r.enrollment,{}};double distanceSquared=0;
    for(unsigned c=0;c<3;++c){
        if(!std::isfinite(world[c])||std::abs(world[c])>p.maximumAbsoluteWorldUnits||
           !std::isfinite(r.coordinate.nativeInputXYZ[c])||
           std::abs(r.coordinate.nativeInputXYZ[c])>p.maximumAbsoluteWorldUnits)return reject(Gate::Bounds);
        out.nativeXYZ[c]=static_cast<float>(world[c]);
        if(!std::isfinite(out.nativeXYZ[c]))return reject(Gate::Bounds);
        const double d=double(out.nativeXYZ[c])-r.coordinate.nativeInputXYZ[c];distanceSquared+=d*d;
    }
    const double bound=double(p.maximumDisplacementMetres)*s.unitsPerMetre;
    if(distanceSquared>bound*bound)return reject(Gate::Bounds);
    if(outcome)*outcome=Gate::Passed;return out;
}
} // namespace detail
inline std::optional<Result> PrepareOriginOnce(Once& once,const Request& r,Gate* outcome=nullptr)noexcept {
    if(once.attempted){if(outcome)*outcome=Gate::AlreadyAttempted;return {};}
    once.attempted=true;return detail::Prepare(r,outcome);
}
} // namespace fvr::bc2::origin388
