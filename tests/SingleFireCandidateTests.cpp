#include "DisabledRegistry.h"
#include "fvr/interaction/WeaponActionGate.h"
#include <cstdio>
using namespace fvr::bc2;using namespace fvr::interaction;
#define CHECK(x) do {if(!(x)){std::printf("FAIL line %d: %s\n",__LINE__,#x);return 1;}} while(false)
int main(){
 CHECK(generated::MagazineNativeRegistrations.size()==2);
 for(const auto& r:generated::MagazineNativeRegistrations){
  CHECK(!r.enabled&&!r.profile->Reviewed()&&!ResolveMagazineNativeProfile(r.id));
  CHECK(r.profile->configuration.values.fireLogicType==0&&r.profile->configuration.values.baseCapacity==17);
  CHECK(!SelectGeneratedMagazineRegistration(generated::MagazineNativeRegistrations,r.id));
  WeaponMechanismDescriptor d;d.id=std::uint64_t(r.id);d.revision=1;d.afterEmptyFeed=WeaponAction::Slide;
  d.feedPlan.stepCount=2;d.feedPlan.steps[0]={ReloadOperation::UnseatMagazine,1};d.feedPlan.steps[1]={ReloadOperation::SeatMagazine,1};
  d.feedPlan.maxSampleGapNs=10;d.feedPlan.ackTimeoutNs=20;d.feedPlan.transactionTimeoutNs=30;
  CHECK(ValidWeaponMechanismDescriptor(d));
  for(auto intent:{WeaponMechanismIntent::TacticalFeed,WeaponMechanismIntent::EmptyFeed,WeaponMechanismIntent::AfterShot})
   CHECK(SelectWeaponMechanismPlan(d,intent,ChamberKnowledge::Unknown).status==WeaponMechanismStatus::UnknownChamber);
  WeaponActionEvidence e;e.owner={1,2,3,4};e.item={5,6};e.descriptor=d.id;e.revision=1;e.sequence=1;e.observedNs=100;e.deadlineNs=150;
  e.nativeReady=e.actionClosed=true;e.chamber=ChamberKnowledge::Unknown;
  WeaponActionGate gate;CHECK(!gate.Bind(d,e,101));
  CHECK(!gate.Bind(d,e,101,WeaponActionReadiness::NativeAfterShot));
  // The negative control is chamber knowledge, not malformed owner/timing.
  e.chamber=ChamberKnowledge::Occupied;CHECK(gate.Bind(d,e,101));
  CHECK(gate.Phase()==WeaponActionGatePhase::Ready);
  auto slide=d;slide.afterShot=WeaponAction::Slide;
  CHECK(ValidWeaponMechanismDescriptor(slide));
  WeaponActionGate slideGate;
  CHECK(!slideGate.Bind(slide,e,101,WeaponActionReadiness::NativeAfterShot));
  const auto plan=SelectWeaponMechanismPlan(slide,WeaponMechanismIntent::EmptyFeed,ChamberKnowledge::Empty);
  CHECK(plan.plan&&plan.plan->action==WeaponAction::Slide&&!plan.plan->gestureFamily);
 }
 std::puts("SingleFire2 exact compiled candidates remain disabled; unknown chamber/slide cannot borrow pump/bolt readiness");return 0;
}
