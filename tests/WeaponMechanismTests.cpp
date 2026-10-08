#include "fvr/interaction/WeaponMechanism.h"
#include <cstdio>
using namespace fvr::interaction;
#define CHECK(x) do{if(!(x)){std::fprintf(stderr,"failed line %d: %s\n",__LINE__,#x);return 1;}}while(false)
WeaponMechanismDescriptor Descriptor(){WeaponMechanismDescriptor d;d.id=1;d.revision=2;d.tacticalRetainsChamber=true;d.afterEmptyFeed=WeaponAction::ChargingHandle;d.feedPlan.stepCount=2;d.feedPlan.steps[0]={ReloadOperation::UnseatMagazine,1};d.feedPlan.steps[1]={ReloadOperation::SeatMagazine,1};d.feedPlan.maxSampleGapNs=10;d.feedPlan.ackTimeoutNs=20;d.feedPlan.transactionTimeoutNs=30;return d;}
int main(){
 auto d=Descriptor();CHECK(ValidWeaponMechanismDescriptor(d));
 auto t=SelectWeaponMechanismPlan(d,WeaponMechanismIntent::TacticalFeed,ChamberKnowledge::Occupied);CHECK(t.plan&&t.plan->feed.stepCount==2&&t.plan->purpose==WeaponCyclePurpose::None);
 auto e=SelectWeaponMechanismPlan(d,WeaponMechanismIntent::EmptyFeed,ChamberKnowledge::Empty);CHECK(e.plan&&e.plan->purpose==WeaponCyclePurpose::AfterFeed&&e.plan->action==WeaponAction::ChargingHandle&&!e.plan->gestureFamily);
 for(auto intent:{WeaponMechanismIntent::TacticalFeed,WeaponMechanismIntent::EmptyFeed,WeaponMechanismIntent::AfterShot})CHECK(SelectWeaponMechanismPlan(d,intent,ChamberKnowledge::Unknown).status==WeaponMechanismStatus::UnknownChamber);
 CHECK(!SelectWeaponMechanismPlan(d,WeaponMechanismIntent::EmptyFeed,ChamberKnowledge::Occupied).plan);CHECK(!SelectWeaponMechanismPlan(d,WeaponMechanismIntent::TacticalFeed,ChamberKnowledge::Empty).plan);
 d.feed=WeaponFeed::InternalTube;d.feedPlan.stepCount=1;d.feedPlan.steps[0]={ReloadOperation::InsertRound,1};d.afterShot=WeaponAction::Pump;
 auto s=SelectWeaponMechanismPlan(d,WeaponMechanismIntent::AfterShot,ChamberKnowledge::Empty);CHECK(s.plan&&s.plan->feed.stepCount==0&&s.plan->purpose==WeaponCyclePurpose::AfterShot&&s.plan->gestureFamily==WeaponCycleFamily::Pump);
 CHECK(SelectWeaponMechanismPlan(d,WeaponMechanismIntent::EmptyFeed,ChamberKnowledge::Empty).plan->action==WeaponAction::ChargingHandle);
 d.afterShot=WeaponAction::Bolt;CHECK(SelectWeaponMechanismPlan(d,WeaponMechanismIntent::AfterShot,ChamberKnowledge::Empty).plan->gestureFamily==WeaponCycleFamily::Bolt);
 d.afterEmptyFeed=WeaponAction::Slide;CHECK(!SelectWeaponMechanismPlan(d,WeaponMechanismIntent::EmptyFeed,ChamberKnowledge::Empty).plan->gestureFamily);
 d.afterShot=WeaponAction::Automatic;CHECK(SelectWeaponMechanismPlan(d,WeaponMechanismIntent::AfterShot,ChamberKnowledge::Occupied).plan->purpose==WeaponCyclePurpose::None);
  for(auto feed:{WeaponFeed::DetachableMagazine,WeaponFeed::InternalTube,WeaponFeed::Belt,WeaponFeed::Clip,WeaponFeed::Single}){
  auto q=Descriptor();q.feed=feed;
  if(feed==WeaponFeed::InternalTube){q.feedPlan.stepCount=1;q.feedPlan.steps[0]={ReloadOperation::InsertRound,1};}
  if(feed==WeaponFeed::Belt||feed==WeaponFeed::Clip||feed==WeaponFeed::Single){q.feedPlan.stepCount=3;q.feedPlan.steps[0]={ReloadOperation::OpenBreech,1};q.feedPlan.steps[1]={ReloadOperation::InsertRound,1};q.feedPlan.steps[2]={ReloadOperation::CloseBreech,1};}
  CHECK(ValidWeaponMechanismDescriptor(q));
  auto wrong=Descriptor();wrong.feed=feed;
  if(feed==WeaponFeed::DetachableMagazine){wrong.feedPlan.stepCount=1;wrong.feedPlan.steps[0]={ReloadOperation::InsertRound,1};}
  CHECK(!ValidWeaponMechanismDescriptor(wrong));
  q.feedPlan.steps[q.feedPlan.stepCount++]={ReloadOperation::SeatMagazine,1};CHECK(!ValidWeaponMechanismDescriptor(q));
 }
 d=Descriptor();
 auto bad=d;bad.feedPlan.steps[0].operation=ReloadOperation::CycleAction;CHECK(!ValidWeaponMechanismDescriptor(bad));bad=d;bad.feedPlan.steps[0].repeats=0;CHECK(!ValidWeaponMechanismDescriptor(bad));bad=d;bad.feedPlan.stepCount=9;CHECK(!ValidWeaponMechanismDescriptor(bad));bad=d;bad.feedPlan.steps[0].repeats=64;CHECK(!ValidWeaponMechanismDescriptor(bad));bad=d;bad.id=0;CHECK(!ValidWeaponMechanismDescriptor(bad));bad=d;bad.feed=static_cast<WeaponFeed>(255);CHECK(!ValidWeaponMechanismDescriptor(bad));bad=d;bad.afterShot=static_cast<WeaponAction>(255);CHECK(!ValidWeaponMechanismDescriptor(bad));d.tacticalRetainsChamber=false;CHECK(!SelectWeaponMechanismPlan(d,WeaponMechanismIntent::TacticalFeed,ChamberKnowledge::Occupied).plan);
 std::puts("WeaponMechanism declarative selection passed");return 0;
}


