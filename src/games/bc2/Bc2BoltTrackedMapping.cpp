#include "Bc2BoltTrackedMapping.h"
namespace fvr::bc2 {
namespace {
using namespace interaction;using namespace reload_insertion_detail;
bool SameSample(const HandInteractionSample& a,const HandInteractionSample& b)noexcept {
    return a.owner==b.owner&&a.sequence==b.sequence&&a.observedNs==b.observedNs&&a.deadlineNs==b.deadlineNs&&
        a.focused==b.focused&&a.tracked==b.tracked&&a.released==b.released;
}
bool SamePose(const math::Pose& a,const math::Pose& b)noexcept {
    const auto av=math::MakeLhViewFromOpenXRPose(a),bv=math::MakeLhViewFromOpenXRPose(b);
    return av&&bv&&Distance(*av,*bv)<.000001f&&Angle(*av,*bv)<.001f;
}
}
std::optional<std::array<math::Matrix4,2>> MapBoltTrackedWrists(const Bc2BoltControllerContact& raw,
    const interaction::InputFrame& input,const interaction::HandInteractionSample& safety,const Bc2BoltBodyFrame& frame)noexcept {
    using namespace interaction;using namespace reload_insertion_detail;
    if(!raw.raw.valid||!raw.mappingValid||!ValidInput(input)||!input.focused||!input.headValid||
       !input.hands[0].gripTracked||!input.hands[1].gripTracked||!safety.focused||!safety.tracked[0]||!safety.tracked[1]||
       input.generation!=safety.sequence||input.spaceGeneration!=safety.owner.space||
       raw.raw.input.owner!=safety.owner||raw.raw.nativeOwner!=frame.owner||frame.owner.space!=input.spaceGeneration||
       !SameSample(safety,frame.input)||!SamePose(input.referenceHead,frame.referenceHead)||
       !std::isfinite(frame.units)||frame.units<=0||frame.units!=input.worldUnitsPerMeter||frame.units!=raw.units||
       !Rigid(frame.bodyWorldMeters)||raw.raw.input.sequence>safety.sequence||raw.raw.input.observedNs>safety.observedNs||
       !weapon_cycle_detail::Window(raw.raw.input.observedNs,raw.raw.input.deadlineNs,safety.nowNs)||
       !weapon_cycle_detail::Window(safety.observedNs,safety.deadlineNs,safety.nowNs))return {};
    std::array<math::Matrix4,2> result;
    for(unsigned n=0;n<2;++n){
        const auto relative=math::MakeRelativePose(input.referenceHead,input.hands[n].grip);
        const auto view=relative?math::MakeLhViewFromOpenXRPose(*relative):std::nullopt;
        const auto grip=view?InverseRigid(*view):std::nullopt;
        if(!grip||!Rigid(raw.wristToGrip[n])||Distance(raw.wristToGrip[n],Identity())>.0001f)return {};
        result[n]=Multiply(Multiply(raw.wristToGrip[n],*grip),frame.bodyWorldMeters);
        if(!Rigid(result[n]))return {};
    }return result;
}
std::optional<math::Matrix4> ReprojectBoltCustodyWeapon(const Bc2BoltControllerContact& raw,
    const std::array<math::Matrix4,2>& current,interaction::InteractionHand receiving)noexcept {
    using namespace interaction;using namespace reload_insertion_detail;
    if(!raw.raw.valid||!raw.mappingValid||(receiving!=InteractionHand::Left&&receiving!=InteractionHand::Right)||
       !Rigid(raw.weaponWorldMeters))return {};
    const auto index=weapon_cycle_detail::HandIndex(receiving);
    const auto inverse=InverseRigid(raw.rawWristWorldMeters[index]);
    if(!inverse||!Rigid(current[index]))return {};
    const auto result=Multiply(Multiply(raw.weaponWorldMeters,*inverse),current[index]);
    return Rigid(result)?std::optional{result}:std::nullopt;
}
}
