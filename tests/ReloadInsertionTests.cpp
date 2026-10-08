#include "fvr/interaction/ReloadInsertion.h"
#include "Test.h"
#include <iostream>
#include <limits>
using namespace fvr;
using namespace fvr::interaction;
namespace {
constexpr std::int64_t Ms=1000000;
math::Matrix4 Pose(float x=0,float y=0,float z=0,float angle=0){
    auto m=reload_insertion_detail::Identity();m.values[0][0]=m.values[1][1]=std::cos(angle);
    m.values[0][1]=std::sin(angle);m.values[1][0]=-std::sin(angle);
    m.values[3][0]=x;m.values[3][1]=y;m.values[3][2]=z;return m;
}
bool Near(float a,float b,float eps=1e-5f){return std::abs(a-b)<=eps;}
bool Near(const math::Matrix4& a,const math::Matrix4& b,float eps=1e-5f){for(unsigned i=0;i<4;++i)for(unsigned j=0;j<4;++j)if(!Near(a.values[i][j],b.values[i][j],eps))return false;return true;}
ReloadInsertionProfile Profile(ReloadInsertionFamily family=ReloadInsertionFamily::Magazine){
    ReloadInsertionProfile p{};p.id=15;p.revision=2;p.family=family;
    // Deliberately nontrivial authored grasp, item nose and rail orientation.
    p.itemFromHand=Pose(.04f,-.02f,-.03f,.35f);
    p.itemFromInsertion=Pose(.01f,0,.08f,-.15f);
    p.weaponFromEntry=Pose(.2f,-.3f,.1f,.6f);
    p.travelMeters=.1f;p.captureDistanceMeters=.03f;p.releaseDistanceMeters=.05f;
    p.captureAngleRadians=.3f;p.releaseAngleRadians=.6f;p.seatToleranceMeters=.001f;
    p.maxStepMeters=.045f;p.maxStepRadians=.5f;
    p.alignmentNs=40*Ms;p.seatDwellNs=20*Ms;p.maxSampleGapNs=100*Ms;p.maxGuidedNs=2000*Ms;return p;
}
struct Fixture {
    ReloadInsertionProfile profile;ReloadInsertion policy;ReloadInsertionSample s{};
    explicit Fixture(ReloadInsertionProfile p=Profile()):profile(p),policy(p){
        s.identity={{1,2,3,4},{5,6},{7,8},9};s.profile={p.id,p.revision};s.nowNs=1000*Ms;
        s.focused=s.itemTracked=s.weaponTracked=s.held=s.eligible=true;
        s.itemClaim.token={10,s.identity.owner,InteractionHand::Left,HandClaimKind::AmmoObject,s.identity.item,{30,1},0};
        s.weaponClaim.token={11,s.identity.owner,InteractionHand::Right,HandClaimKind::GunHold,s.identity.weapon,{31,1},0};
    }
    math::Matrix4 Hand(const math::Matrix4& rail)const {
        return Multiply(profile.itemFromHand,Multiply(*InverseRigid(profile.itemFromInsertion),Multiply(rail,profile.weaponFromEntry)));
    }
    ReloadInsertionResult SendRail(const math::Matrix4& rail,std::int64_t dt=10*Ms){
        ++s.sequence;s.geometrySequence=s.sequence;s.nowNs+=dt;s.observedNs=s.nowNs;s.deadlineNs=s.nowNs+profile.maxSampleGapNs;
        s.itemClaim.inputSequence=s.weaponClaim.inputSequence=s.sequence;
        s.itemClaim.deadlineNs=s.weaponClaim.deadlineNs=s.deadlineNs;s.weaponFromHand=Hand(rail);return policy.Update(s);
    }
    ReloadInsertionResult Send(float z,float x=0,float angle=0,std::int64_t dt=10*Ms){return SendRail(Pose(x,0,z,angle),dt);}
    ReloadInsertionResult Start(float x=.01f,float angle=.2f){Send(-.06f,x,angle);return Send(-.025f,x,angle);}
    math::Matrix4 Rail(const math::Matrix4& item)const {return Multiply(Multiply(profile.itemFromInsertion,item),*InverseRigid(profile.weaponFromEntry));}
    ReloadInsertionResult Seat(){Start(0,0);Send(.01f);Send(.045f);Send(.08f);Send(.1f);Send(.1f);return Send(.1f);}
};
int AuthoredPlacementAndContinuity(){
    Fixture f;auto r=f.Send(-.06f,.01f,.2f);CHECK(r.rawItem&&!r.guidedItem);
    CHECK(Near(Multiply(f.profile.itemFromHand,*r.rawItem),f.s.weaponFromHand));
    // Nose near socket, wrist far from it: capture must use the authored nose.
    r=f.Send(-.025f,.01f,.2f);CHECK(r.captured&&r.guidedItem&&r.guidedHand&&r.phase==ReloadInsertionPhase::Guided);
    CHECK(Near(*r.rawItem,*r.guidedItem)&&Near(*r.guidedHand,f.s.weaponFromHand));CHECK(r.alignment==0&&r.progress==0);
    r=f.Send(-.015f,.01f,.2f,20*Ms);CHECK(Near(r.alignment,.5f));
    CHECK(Near(f.Rail(*r.guidedItem),Pose(.005f,0,-.015f,.1f),2e-5f));
    CHECK(Near(Multiply(f.profile.itemFromHand,*r.guidedItem),*r.guidedHand));
    r=f.Send(.01f,.01f,.2f,20*Ms);CHECK(r.alignment==1&&Near(r.progress,.1f));
    CHECK(Near(f.Rail(*r.guidedItem),Pose(0,0,.01f)));return 0;
}
int MagazineSeatAndNativeSeparation(){
    Fixture f;auto r=f.Seat();CHECK(r.seat&&r.haptic==ReloadInsertionHaptic::Seated&&r.progress==1);
    CHECK(r.seat->operation==ReloadOperation::SeatMagazine&&r.seat->identity==f.s.identity&&r.seat->itemClaim==f.s.itemClaim.token);
    CHECK(r.seat->inputSequence==f.s.sequence&&r.seat->profile==f.s.profile);
    CHECK(Near(f.Rail(*r.guidedItem),Pose(0,0,.1f)));
    ManualReloadConfig c{};c.steps[0]={ReloadOperation::SeatMagazine,1};c.stepCount=1;c.maxSampleGapNs=100*Ms;c.ackTimeoutNs=300*Ms;c.transactionTimeoutNs=1000*Ms;
    ManualReload coordinator(c);ManualReloadSample sample{};sample.owner={1,2,5,3,4};sample.sequence=1;sample.nowNs=1000*Ms;
    sample.focused=sample.tracked=sample.bindingsVerified=sample.neutral=true;coordinator.Update(sample);
    ++sample.sequence;sample.nowNs+=10*Ms;sample.neutral=false;sample.gesture={r.seat->id,r.seat->operation};const auto requested=coordinator.Update(sample);
    CHECK(requested.request&&!requested.completed&&!requested.acknowledged);
    ++sample.sequence;sample.nowNs+=10*Ms;sample.gesture={};sample.acknowledgement={requested.request->id,requested.request->owner,requested.request->operation,ReloadAcknowledgement::Applied};
    CHECK(coordinator.Update(sample).completed);
    for(unsigned i=0;i<4;++i){r=f.Send(.1f);CHECK(!r.seat&&r.haptic==ReloadInsertionHaptic::None);}
    f.s.held=false;++f.s.nowNs;r=f.policy.Update(f.s);CHECK(r.cancelled&&r.reason==ReloadInsertionReason::Released&&!r.guidedItem);return 0;
}
int DuplicateCannotAdvanceOrSeat(){
    Fixture f;auto r=f.Start();CHECK(r.captured);
    const auto original=*r.guidedItem;f.s.weaponFromHand=f.Hand(Pose(0,0,.1f));f.s.nowNs+=30*Ms;
    r=f.policy.Update(f.s);CHECK(!r.seat&&r.progress==0&&r.alignment==0&&Near(*r.guidedItem,original));
    f.Send(.01f);f.Send(.045f);f.Send(.08f);r=f.Send(.1f);CHECK(!r.seat);
    f.s.nowNs+=30*Ms;r=f.policy.Update(f.s);CHECK(!r.seat);r=f.Send(.1f);CHECK(r.seat);
    for(unsigned i=0;i<3;++i){++f.s.nowNs;r=f.policy.Update(f.s);CHECK(!r.seat&&r.haptic==ReloadInsertionHaptic::None);}
    return 0;
}
int ReversalHysteresisAndWithdrawal(){
    Fixture f;CHECK(f.Start(0,0).captured);f.Send(.01f);auto r=f.Send(.04f);CHECK(Near(r.progress,.4f));
    r=f.Send(.025f);CHECK(Near(r.progress,.25f)&&r.phase==ReloadInsertionPhase::Guided&&!r.cancelled);
    // Larger hold volume/cone than entry: no reacquisition chatter.
    r=f.Send(.02f,.04f,.4f);CHECK(r.phase==ReloadInsertionPhase::Guided&&!r.cancelled);
    r=f.Send(-.01f,.04f,.4f);CHECK(r.phase==ReloadInsertionPhase::Guided);
    r=f.Send(-.045f,.04f,.4f);CHECK(!r.cancelled);
    r=f.Send(-.055f,.04f,.4f);CHECK(r.cancelled&&r.reason==ReloadInsertionReason::Withdrawn&&!r.seat&&!r.guidedItem);
    r=f.Send(-.025f,0,0);CHECK(!r.captured); // Must actually withdraw/rearm first.
    f.Send(-.06f);CHECK(f.Send(-.025f).captured);return 0;
}
int EntryGuardsAndPoseJumps(){
    for(float z:{0.f,.01f,.09f}){Fixture f;CHECK(!f.Send(z).captured);CHECK(!f.Send(z).captured);}
    Fixture far;far.Send(-.2f);CHECK(far.Send(-.01f).reason==ReloadInsertionReason::PoseJump);
    Fixture radial;radial.Send(-.06f,.04f);CHECK(!radial.Send(-.01f,.04f).captured);
    Fixture rotated;rotated.Send(-.06f,0,.4f);CHECK(!rotated.Send(-.025f,0,.4f).captured);
    Fixture back;back.Send(-.04f);CHECK(!back.Send(.005f).captured);CHECK(!back.Send(-.015f).captured);
    Fixture jump;CHECK(jump.Start(0,0).captured);auto r=jump.Send(.1f);CHECK(r.cancelled&&r.reason==ReloadInsertionReason::PoseJump&&!r.seat);
    Fixture twist;twist.Start(0,0);r=twist.Send(-.02f,0,.55f);CHECK(r.cancelled&&r.reason==ReloadInsertionReason::PoseJump);return 0;
}
int ExactCaptureBoundary(){
    auto p=Profile();p.itemFromHand=p.itemFromInsertion=p.weaponFromEntry=Pose();
    p.captureDistanceMeters=.03125f;p.releaseDistanceMeters=.0625f;p.travelMeters=.125f;p.maxStepMeters=.0625f;
    Fixture boundary(p);boundary.Send(-.04f,.03125f);CHECK(boundary.Send(0,.03125f).captured);
    Fixture outside(p);const float beyond=std::nextafter(.03125f,1.f);outside.Send(-.04f,beyond);CHECK(!outside.Send(0,beyond).captured);
    Fixture step(p);step.Send(-.0625f);CHECK(step.Send(-.015625f).captured);
    CHECK(step.Send(.046875f).phase==ReloadInsertionPhase::Guided); // Exactly 6.25cm permitted step.
    return 0;
}
int FrontHemisphereApproachIsExplicitAndBounded(){
    auto p=Profile(ReloadInsertionFamily::SingleShell);p.approach=ReloadInsertionApproach::FrontHemisphere;
    Fixture under(p);under.SendRail(Pose(0,-.055f,-.01f));under.SendRail(Pose(0,-.035f,-.01f));
    auto r=under.SendRail(Pose(0,-.02f,-.01f));CHECK(r.captured&&r.guidedItem&&r.alignment==0);
    r=under.SendRail(Pose(0,-.02f,-.01f),40*Ms);CHECK(r.alignment==1&&Near(under.Rail(*r.guidedItem),Pose(0,0,-.01f)));
    Fixture defaultAxial(Profile(ReloadInsertionFamily::SingleShell));defaultAxial.SendRail(Pose(0,-.055f,-.01f));
    defaultAxial.SendRail(Pose(0,-.035f,-.01f));CHECK(!defaultAxial.SendRail(Pose(0,-.02f,-.01f)).captured);
    Fixture inside(p);CHECK(!inside.SendRail(Pose(0,-.02f,-.01f)).captured);CHECK(!inside.Send(0).captured);
    Fixture back(p);back.SendRail(Pose(0,-.055f,.01f));back.SendRail(Pose(0,-.035f,.01f));
    CHECK(!back.SendRail(Pose(0,-.02f,.01f)).captured);CHECK(!back.SendRail(Pose(0,-.02f,-.01f)).captured);
    Fixture teleport(p);teleport.SendRail(Pose(0,-.2f,-.01f));r=teleport.SendRail(Pose(0,-.02f,-.01f));
    CHECK(r.reason==ReloadInsertionReason::PoseJump&&!r.captured&&!r.seat);
    Fixture angle(p);angle.SendRail(Pose(0,-.055f,-.01f,.4f));angle.SendRail(Pose(0,-.035f,-.01f,.4f));
    CHECK(!angle.SendRail(Pose(0,-.02f,-.01f,.4f)).captured);
    auto invalid=Profile();invalid.approach=ReloadInsertionApproach::FrontHemisphere;CHECK(!ValidateReloadInsertionProfile(invalid));
    invalid=p;invalid.approach=static_cast<ReloadInsertionApproach>(99);CHECK(!ValidateReloadInsertionProfile(invalid));return 0;
}
int EntrySphereStillRequiresObservedApproachAndSeatTravel(){
    auto p=Profile(ReloadInsertionFamily::SingleShell);p.orientation=ReloadInsertionOrientation::AxialSymmetry;
    p.approach=ReloadInsertionApproach::EntrySphere;
    Fixture positive(p);positive.Send(.04f);auto r=positive.Send(.02f);CHECK(r.captured&&!r.seat);
    for(unsigned n=0;n<10;++n){r=positive.Send(.02f);CHECK(r.phase==ReloadInsertionPhase::Guided&&!r.seat);}
    positive.Send(.055f);positive.Send(.085f);positive.Send(.1f);r=positive.Send(.1f);CHECK(!r.seat);
    r=positive.Send(.1f);CHECK(r.seat&&r.seat->operation==ReloadOperation::InsertRound);
    for(float z:{-.02f,0.f,.02f}){Fixture initial(p);for(unsigned n=0;n<10;++n)CHECK(!initial.Send(z).captured);}
    Fixture teleport(p);teleport.Send(.2f);r=teleport.Send(.02f);CHECK(!r.captured&&r.reason==ReloadInsertionReason::PoseJump);
    auto reverse=Pose();reverse.values[1][1]=reverse.values[2][2]=-1;reverse.values[3][2]=.04f;
    Fixture backwards(p);backwards.SendRail(reverse);reverse.values[3][2]=.02f;CHECK(!backwards.SendRail(reverse).captured);
    auto magazine=Profile();magazine.approach=ReloadInsertionApproach::EntrySphere;CHECK(!ValidateReloadInsertionProfile(magazine));
    auto tooDeep=p;tooDeep.captureDistanceMeters=tooDeep.travelMeters;CHECK(!ValidateReloadInsertionProfile(tooDeep));
    // Default magazine/keyed orientation and front-only entry remain intact.
    Fixture defaultMagazine;defaultMagazine.Send(.04f);CHECK(!defaultMagazine.Send(.02f).captured);
    defaultMagazine.Send(-.06f,0,1.f);CHECK(!defaultMagazine.Send(-.025f,0,1.f).captured);return 0;
}
int CurrentSafetyCancelsDuplicates(){
    for(unsigned n=0;n<8;++n){Fixture f;f.Start();++f.s.nowNs;
        switch(n){case 0:f.s.focused=false;break;case 1:f.s.itemTracked=false;break;case 2:f.s.weaponTracked=false;break;
        case 3:f.s.held=false;break;case 4:f.s.eligible=false;break;case 5:f.s.cancel=true;break;
        case 6:f.s.itemClaim.deadlineNs=f.s.nowNs-1;break;default:f.s.weaponClaim.deadlineNs=f.s.nowNs-1;break;}
        const auto r=f.policy.Update(f.s);CHECK(r.cancelled&&!r.seat&&!r.guidedItem);
    }return 0;
}
int AllIdentityBoundaries(){
    for(unsigned n=0;n<13;++n){Fixture f;f.Start();++f.s.nowNs;
        switch(n){case 0:++f.s.identity.owner.actor;break;case 1:++f.s.identity.owner.actorGeneration;break;
        case 2:++f.s.identity.owner.equipGeneration;break;case 3:++f.s.identity.owner.space;break;
        case 4:++f.s.identity.weapon.id;break;case 5:++f.s.identity.weapon.generation;break;
        case 6:++f.s.identity.item.id;break;case 7:++f.s.identity.item.generation;break;
        case 8:++f.s.identity.trackingEpoch;break;case 9:++f.s.itemClaim.token.id;break;
        case 10:++f.s.weaponClaim.token.id;break;case 11:f.s.itemClaim.token.hand=InteractionHand::Right;break;
        default:++f.s.itemClaim.token.contact.generation;break;}
        const auto r=f.policy.Update(f.s);CHECK(r.cancelled&&r.reason==ReloadInsertionReason::IdentityChanged&&!r.seat&&!r.guidedItem);
    }return 0;
}
int OwnershipNotJustBooleans(){
    for(unsigned n=0;n<7;++n){Fixture f;
        switch(n){case 0:f.s.itemClaim.token.kind=HandClaimKind::WeaponSupport;break;case 1:f.s.weaponClaim.token.kind=HandClaimKind::AmmoObject;break;
        case 2:f.s.itemClaim.token.item.id=99;break;case 3:f.s.itemClaim.token.owner.actor=99;break;
        case 4:f.s.itemClaim.token.hand=InteractionHand::Right;break;case 5:f.s.weaponClaim.token.contact={};break;
        default:f.s.itemClaim.token.prerequisiteClaim=11;break;}
        CHECK(f.Send(-.06f).reason==ReloadInsertionReason::OwnershipLost);
    }return 0;
}
int FreshnessAndChronology(){
    for(unsigned n=0;n<7;++n){Fixture f;f.Start();
        switch(n){case 0:f.s.nowNs=f.s.deadlineNs+1;break;
        case 1:f.s.nowNs+=101*Ms;f.s.observedNs=f.s.nowNs;f.s.deadlineNs=f.s.nowNs+100*Ms;++f.s.sequence;f.s.geometrySequence=f.s.sequence;f.s.itemClaim.deadlineNs=f.s.weaponClaim.deadlineNs=f.s.deadlineNs;break;
        case 2:--f.s.nowNs;break;case 3:--f.s.sequence;f.s.geometrySequence=f.s.sequence;break;
        case 4:--f.s.geometrySequence;break;case 5:++f.s.observedNs;++f.s.nowNs;break;
        default:f.s.deadlineNs=f.s.observedNs+101*Ms;break;}
        const auto r=f.policy.Update(f.s);CHECK(r.cancelled&&!r.seat&&!r.guidedItem);
    }
    auto p=Profile();p.maxGuidedNs=100*Ms;Fixture timeout(p);timeout.Start();
    ReloadInsertionResult r;for(unsigned i=0;i<11;++i)r=timeout.Send(0);CHECK(r.cancelled&&r.reason==ReloadInsertionReason::Timeout);return 0;
}
int SeatDwellBand(){
    Fixture f;f.Start(0,0);f.Send(.01f);f.Send(.045f);f.Send(.08f);CHECK(!f.Send(.0995f).seat);
    CHECK(!f.Send(.0985f).seat);auto r=f.Send(.0995f);CHECK(r.seat); // Stays inside 2mm hold band.
    Fixture reset;reset.Start(0,0);reset.Send(.01f);reset.Send(.045f);reset.Send(.08f);reset.Send(.0995f);
    CHECK(!reset.Send(.097f).seat);CHECK(!reset.Send(.0995f).seat);CHECK(!reset.Send(.0995f).seat);CHECK(reset.Send(.0995f).seat);
    r=f.Send(.097f);CHECK(r.cancelled&&r.reason==ReloadInsertionReason::Withdrawn);return 0;
}
int ShellAndResetIds(){
    auto p=Profile(ReloadInsertionFamily::SingleShell);p.travelMeters=.04f;p.captureDistanceMeters=.01f;p.releaseDistanceMeters=.02f;p.maxStepMeters=.015f;
    Fixture f(p);f.Send(-.02f);CHECK(f.Send(-.008f).captured);f.Send(.005f);f.Send(.018f);f.Send(.03f);f.Send(.04f);f.Send(.04f);auto r=f.Send(.04f);
    CHECK(r.seat&&r.seat->operation==ReloadOperation::InsertRound);const auto id=r.seat->id;
    CHECK(f.policy.Reset().cancelled);f.Send(-.02f);CHECK(f.Send(-.008f).captured);f.Send(.005f);f.Send(.018f);f.Send(.03f);f.Send(.04f);f.Send(.04f);r=f.Send(.04f);
    CHECK(r.seat&&r.seat->id>id);return 0;
}
int AxialSymmetryRequiresAuthoredOptIn(){
    auto p=Profile(ReloadInsertionFamily::SingleShell);p.orientation=ReloadInsertionOrientation::AxialSymmetry;
    Fixture round(p);round.Send(-.06f,0,2.f);auto r=round.Send(-.025f,0,2.f);CHECK(r.captured);
    r=round.Send(.01f,0,2.f,40*Ms);CHECK(r.alignment==1&&Near(round.Rail(*r.guidedItem),Pose(0,0,.01f,2.f),2e-5f));
    Fixture keyedShell(Profile(ReloadInsertionFamily::SingleShell));keyedShell.Send(-.06f,0,2.f);CHECK(!keyedShell.Send(-.025f,0,2.f).captured);
    auto invalid=Profile();invalid.orientation=ReloadInsertionOrientation::AxialSymmetry;CHECK(!ValidateReloadInsertionProfile(invalid));
    auto tilted=[](float z,float angle){auto pitch=Pose();pitch.values[1][1]=pitch.values[2][2]=std::cos(angle);pitch.values[1][2]=std::sin(angle);pitch.values[2][1]=-std::sin(angle);
        auto pose=Multiply(Pose(0,0,0,1.2f),pitch);pose.values[3][2]=z;return pose;};
    Fixture swing(p);swing.SendRail(tilted(-.06f,.2f));CHECK(swing.SendRail(tilted(-.025f,.2f)).captured);
    r=swing.SendRail(tilted(-.015f,.2f),20*Ms);CHECK(Near(swing.Rail(*r.guidedItem),tilted(-.015f,.1f),2e-5f));
    r=swing.SendRail(tilted(.01f,.2f),20*Ms);CHECK(Near(swing.Rail(*r.guidedItem),Pose(0,0,.01f,1.2f),2e-5f));
    // A valid nearly aligned authored round trip may have slightly non-unit
    // axis length. Its direction must not acquire a spurious acos(Z) tilt.
    auto almost=Pose(0,0,.01f,1.2f);almost.values[2]={.0000007f,-.0000002f,.999999f,0};
    CHECK(reload_insertion_detail::Rigid(almost));
    CHECK(reload_insertion_detail::AlignmentAngle(almost,ReloadInsertionOrientation::AxialSymmetry)<.000001f);
    CHECK(Near(reload_insertion_detail::AlignSymmetric(almost,1),Pose(0,0,.01f,1.2f),2e-5f));
    return 0;
}
int DeadlineEqualityExpires(){
    for(unsigned n=0;n<3;++n){Fixture f;f.Start();++f.s.nowNs;
        if(n==0)f.s.deadlineNs=f.s.nowNs;else if(n==1)f.s.itemClaim.deadlineNs=f.s.nowNs;else f.s.weaponClaim.deadlineNs=f.s.nowNs;
        const auto r=f.policy.Update(f.s);CHECK(r.cancelled&&r.reason==(n==0?ReloadInsertionReason::StaleInput:ReloadInsertionReason::LeaseExpired));
    }return 0;
}
int TravelDirectionDoesNotRotateTheItem(){
    auto p=Profile(ReloadInsertionFamily::SingleShell);p.travelDirection={0,.8f,.6f};
    p.orientation=ReloadInsertionOrientation::AxialSymmetry;CHECK(ValidateReloadInsertionProfile(p));Fixture f(p);
    const auto raw=[&](float distance,float lateral=0){auto m=Pose(0,0,0,1.2f);
        m.values[3]=reload_insertion_detail::TravelPose(p,distance).values[3];m.values[3][0]+=lateral;return m;};
    f.SendRail(raw(-.06f,.01f));auto r=f.SendRail(raw(-.025f,.01f));CHECK(r.captured&&r.alignment==0);
    CHECK(Near(*r.rawItem,*r.guidedItem));r=f.SendRail(raw(.01f,.01f),40*Ms);
    CHECK(r.alignment==1&&Near(r.progress,.1f)&&Near(f.Rail(*r.guidedItem),raw(.01f)));
    CHECK(Near(float(reload_insertion_detail::TravelRadial(raw(.01f,.01f),p)),.01f));
    for(float along:{.045f,.08f,.1f,.1f,.1f})r=f.SendRail(raw(along));
    CHECK(r.seat&&Near(f.Rail(*r.guidedItem),raw(.1f)));
    CHECK(Near(f.Rail(*r.guidedItem).values[3][2],.06f)); // +Z alone is not progress.
    r=f.SendRail(raw(.08f));CHECK(r.cancelled&&r.reason==ReloadInsertionReason::Withdrawn);
    Fixture withdrawal(p);withdrawal.SendRail(raw(-.06f));withdrawal.SendRail(raw(-.025f));
    r=withdrawal.SendRail(raw(-.055f));CHECK(r.cancelled&&r.reason==ReloadInsertionReason::Withdrawn);
    Fixture jumped(p);jumped.SendRail(raw(-.1f));r=jumped.SendRail(raw(-.02f));CHECK(r.reason==ReloadInsertionReason::PoseJump&&!r.captured);
    Fixture sideways(p);sideways.SendRail(raw(-.06f));sideways.SendRail(raw(-.025f));
    sideways.SendRail(raw(-.025f,.04f));r=sideways.SendRail(raw(-.025f,.06f));CHECK(r.reason==ReloadInsertionReason::ContactLost);
    Fixture backside(p);backside.SendRail(raw(.04f));CHECK(!backside.SendRail(raw(.02f)).captured);
    for(unsigned bad=0;bad<3;++bad){auto invalid=p;invalid.travelDirection=bad==0?std::array<float,3>{0,0,0}:std::array<float,3>{0,2,0};
        if(bad==2)invalid.travelDirection[0]=std::numeric_limits<float>::quiet_NaN();CHECK(!ValidateReloadInsertionProfile(invalid));}
    return 0;
}
int MovingWeaponAndHandTogether(){
    Fixture a,b;a.Start();b.Start();
    a.Send(.01f);auto ar=a.Send(.025f);
    const auto worldFromWeapon=Pose(3,2,-4,1.1f);
    b.Send(.01f);++b.s.sequence;b.s.geometrySequence=b.s.sequence;b.s.nowNs+=10*Ms;b.s.observedNs=b.s.nowNs;b.s.deadlineNs=b.s.nowNs+100*Ms;
    b.s.itemClaim.inputSequence=b.s.weaponClaim.inputSequence=b.s.sequence;b.s.itemClaim.deadlineNs=b.s.weaponClaim.deadlineNs=b.s.deadlineNs;
    const auto worldFromHand=Multiply(b.Hand(Pose(0,0,.025f)),worldFromWeapon);
    b.s.weaponFromHand=Multiply(worldFromHand,*InverseRigid(worldFromWeapon));const auto br=b.policy.Update(b.s);
    CHECK(Near(ar.progress,br.progress,2e-5f)&&ar.phase==br.phase&&Near(*ar.guidedItem,*br.guidedItem,2e-5f));return 0;
}
ReloadInsertionProfile AssistedProfile(){
    auto p=Profile(ReloadInsertionFamily::SingleShell);p.orientation=ReloadInsertionOrientation::AxialSymmetry;
    p.approach=ReloadInsertionApproach::RailContact;p.captureDistanceMeters=.04f;p.releaseDistanceMeters=.08f;
    p.postCaptureTravelMeters=.02f;return p;
}
auto AssistedPose(float along,float lateral,float tilt){
    auto m=Pose(lateral,0,along,1.1f),pitch=Pose();pitch.values[1][1]=pitch.values[2][2]=std::cos(tilt);
    pitch.values[1][2]=std::sin(tilt);pitch.values[2][1]=-std::sin(tilt);
    auto result=Multiply(m,pitch);result.values[3]=m.values[3];return result;
}
int RailContactCapturesPathBeforeOrientationAlignment(){
    auto p=AssistedProfile();Fixture f(p);
    f.SendRail(AssistedPose(.06f,.065f,1.4f));auto r=f.SendRail(AssistedPose(.06f,.035f,1.4f));
    CHECK(r.captured&&r.alignment==0&&Near(*r.rawItem,*r.guidedItem));
    CHECK(reload_insertion_detail::AlignmentAngle(AssistedPose(.06f,.035f,1.4f),p.orientation)>p.releaseAngleRadians);
    CHECK(reload_insertion_detail::CaptureGeometry(AssistedPose(.06f,.035f,1.4f),p));
    CHECK(reload_insertion_detail::CaptureDistance(AssistedPose(.06f,.035f,1.4f),p)<.036);
    r=f.SendRail(AssistedPose(.06f,.035f,1.4f),40*Ms);CHECK(r.alignment==1&&!r.seat);
    const auto guided=f.Rail(*r.guidedItem);CHECK(Near(guided,Pose(0,0,.06f,1.1f),2e-5f));
    // Final guide is physically ahead, but time at capture never loads a round.
    for(unsigned n=0;n<20;++n)CHECK(!f.SendRail(AssistedPose(.06f,.035f,1.4f)).seat);
    f.SendRail(AssistedPose(.08f,.035f,1.4f));f.SendRail(AssistedPose(.1f,.035f,1.4f));
    f.SendRail(AssistedPose(.1f,.035f,1.4f));r=f.SendRail(AssistedPose(.1f,.035f,1.4f));
    CHECK(r.seat&&r.seat->operation==ReloadOperation::InsertRound&&Near(f.Rail(*r.guidedItem),Pose(0,0,.1f,1.1f),2e-5f));return 0;
}
int RailContactAtMouthNeedsPostCaptureMotion(){
    Fixture f(AssistedProfile());f.Send(.1f,.065f);auto r=f.Send(.1f,.035f);
    CHECK(r.captured&&Near(*r.rawItem,*r.guidedItem));
    r=f.Send(.1f,.035f,0,40*Ms);CHECK(r.alignment==1&&!r.seat&&Near(f.Rail(*r.guidedItem),Pose(0,0,.08f)));
    for(unsigned n=0;n<15;++n)CHECK(!f.Send(.1f,.035f).seat);
    // Lateral motion cannot substitute for the required inward 20mm travel.
    for(float x:{.02f,-.015f,.02f})CHECK(!f.Send(.1f,x).seat);
    f.Send(.111f,.02f);for(unsigned n=0;n<5;++n)CHECK(!f.Send(.111f,.02f).seat);
    f.Send(.121f,.02f);f.Send(.121f,.02f);r=f.Send(.121f,.02f);
    CHECK(r.seat&&Near(f.Rail(*r.guidedItem),Pose(0,0,.1f)));
    r=f.Send(.111f,.02f);CHECK(r.cancelled&&r.reason==ReloadInsertionReason::Withdrawn);return 0;
}
int RailContactInitialAssistNeverAutomaticallySeats(){
    Fixture inside(AssistedProfile());auto r=inside.Send(.1f);CHECK(r.captured&&r.phase==ReloadInsertionPhase::Guided&&!r.seat);
    for(unsigned n=0;n<20;++n)CHECK(!inside.Send(.1f).seat);
    inside.Send(.121f);inside.Send(.121f);r=inside.Send(.121f);CHECK(r.seat);
    // Net displacement is required; back-and-forth travel cannot accumulate it.
    Fixture wiggle(AssistedProfile());CHECK(wiggle.Send(.1f).captured);
    for(unsigned n=0;n<20;++n){CHECK(!wiggle.Send(.111f).seat);CHECK(!wiggle.Send(.1f).seat);}
    wiggle.Send(.1f,.035f);wiggle.Send(.1f,.065f);r=wiggle.Send(.1f,.09f);CHECK(r.cancelled);
    r=wiggle.Send(.1f,.055f);CHECK(!r.captured);r=wiggle.Send(.1f,.02f);CHECK(r.captured&&!r.seat);
    for(unsigned n=0;n<20;++n)CHECK(!wiggle.Send(.1f,.02f).seat);
    return 0;
}
int RailContactBoundsStayPhysical(){
    Fixture distant(AssistedProfile());distant.Send(.06f,.13f);distant.Send(.06f,.10f);CHECK(!distant.Send(.06f,.07f).captured);
    Fixture backside(AssistedProfile());backside.Send(.18f);backside.Send(.15f);auto r=backside.Send(.13f);
    CHECK(r.captured&&!r.seat);for(unsigned n=0;n<10;++n)CHECK(!backside.Send(.13f).seat);
    Fixture radial(AssistedProfile());radial.Send(.05f,.065f);radial.Send(.05f,.035f);radial.Send(.05f,.065f);r=radial.Send(.05f,.09f);
    CHECK(r.cancelled&&r.reason==ReloadInsertionReason::ContactLost);return 0;
}
int RailContactMaintainsTrackingAndLeaseSafety(){
    Fixture jump(AssistedProfile());jump.Send(.06f,.1f);auto r=jump.Send(.06f,.01f);CHECK(!r.captured&&r.reason==ReloadInsertionReason::PoseJump);
    Fixture twist(AssistedProfile());twist.Send(.06f,.065f);twist.Send(.06f,.035f);r=twist.Send(.06f,.035f,.6f);
    CHECK(r.cancelled&&r.reason==ReloadInsertionReason::PoseJump);
    Fixture stale(AssistedProfile());stale.Send(.06f,.065f);stale.Send(.06f,.035f);stale.s.nowNs=stale.s.deadlineNs;
    r=stale.policy.Update(stale.s);CHECK(r.cancelled&&r.reason==ReloadInsertionReason::StaleInput);
    Fixture release(AssistedProfile());release.Send(.06f,.065f);release.Send(.06f,.035f);release.s.held=false;
    r=release.policy.Update(release.s);CHECK(r.cancelled&&r.reason==ReloadInsertionReason::Released);
    Fixture duplicate(AssistedProfile());duplicate.Send(.1f,.065f);duplicate.Send(.1f,.035f);
    duplicate.s.weaponFromHand=duplicate.Hand(Pose(0,0,.13f));duplicate.s.nowNs+=40*Ms;
    r=duplicate.policy.Update(duplicate.s);CHECK(!r.seat&&r.alignment==0);return 0;
}
int RailContactAntiparallelShellRemainsRigid(){
    for(float tilt:{3.1415926535f,3.141592f}){Fixture f(AssistedProfile());
        f.SendRail(AssistedPose(.06f,.065f,tilt));auto r=f.SendRail(AssistedPose(.06f,.035f,tilt));CHECK(r.captured);
        r=f.SendRail(AssistedPose(.06f,.035f,tilt),20*Ms);CHECK(r.guidedItem&&reload_insertion_detail::Rigid(*r.guidedItem));
        r=f.SendRail(AssistedPose(.06f,.035f,tilt),20*Ms);CHECK(r.guidedItem&&reload_insertion_detail::Rigid(*r.guidedItem));
        CHECK(reload_insertion_detail::AlignmentAngle(f.Rail(*r.guidedItem),ReloadInsertionOrientation::AxialSymmetry)<.00001f);
    }return 0;
}
int RailContactIsExplicitSingleShellAssistance(){
    auto p=AssistedProfile();CHECK(ValidateReloadInsertionProfile(p));
    for(unsigned n=0;n<7;++n){auto bad=p;
        if(n==0)bad.family=ReloadInsertionFamily::Magazine;if(n==1)bad.orientation=ReloadInsertionOrientation::Keyed;
        if(n==2)bad.postCaptureTravelMeters=0;if(n==3)bad.postCaptureTravelMeters=bad.travelMeters;
        if(n==4)bad.postCaptureTravelMeters=bad.seatToleranceMeters;
        if(n==5)bad.postCaptureTravelMeters=std::numeric_limits<float>::quiet_NaN();
        if(n==6)bad.approach=ReloadInsertionApproach::EntrySphere;
        CHECK(!ValidateReloadInsertionProfile(bad));
    }
    auto keyed=Profile(ReloadInsertionFamily::SingleShell);Fixture old(keyed);old.SendRail(AssistedPose(.02f,.06f,1.4f));
    CHECK(!old.SendRail(AssistedPose(.02f,.02f,1.4f)).captured);return 0;
}
int InvalidGeometryAndProfile(){
    for(unsigned n=0;n<3;++n){Fixture f;f.Start();++f.s.sequence;f.s.geometrySequence=f.s.sequence;f.s.nowNs+=Ms;f.s.observedNs=f.s.nowNs;
        if(n==0)f.s.weaponFromHand.values[0][0]=std::numeric_limits<float>::quiet_NaN();
        else if(n==1)for(unsigned i=0;i<3;++i)f.s.weaponFromHand.values[0][i]*=-1;
        else for(unsigned i=0;i<3;++i)f.s.weaponFromHand.values[0][i]*=1.1f;
        const auto r=f.policy.Update(f.s);CHECK(r.cancelled&&r.reason==ReloadInsertionReason::InvalidGeometry);
    }
    for(unsigned n=0;n<10;++n){auto p=Profile();switch(n){case 0:p.family=ReloadInsertionFamily::Unknown;break;case 1:p.revision=0;break;
        case 2:p.itemFromHand={};break;case 3:p.travelMeters=0;break;case 4:p.releaseDistanceMeters=p.captureDistanceMeters;break;
        case 5:p.releaseAngleRadians=3.f;break;case 6:p.seatToleranceMeters=.05f;break;case 7:p.maxStepMeters=p.travelMeters;break;
        case 8:p.alignmentNs=0;break;default:p.seatDwellNs=p.maxGuidedNs;break;}
        CHECK(!ValidateReloadInsertionProfile(p));Fixture f(p);CHECK(f.Send(-.06f).reason==ReloadInsertionReason::InvalidProfile);
    }return 0;
}
}
int main(){
    CHECK(ValidateReloadInsertionProfile(Profile()));
    CHECK(AuthoredPlacementAndContinuity()==0);CHECK(MagazineSeatAndNativeSeparation()==0);
    CHECK(DuplicateCannotAdvanceOrSeat()==0);CHECK(ReversalHysteresisAndWithdrawal()==0);
    CHECK(EntryGuardsAndPoseJumps()==0);CHECK(ExactCaptureBoundary()==0);CHECK(FrontHemisphereApproachIsExplicitAndBounded()==0);CHECK(EntrySphereStillRequiresObservedApproachAndSeatTravel()==0);CHECK(CurrentSafetyCancelsDuplicates()==0);
    CHECK(AllIdentityBoundaries()==0);CHECK(OwnershipNotJustBooleans()==0);CHECK(FreshnessAndChronology()==0);
    CHECK(SeatDwellBand()==0);CHECK(ShellAndResetIds()==0);CHECK(AxialSymmetryRequiresAuthoredOptIn()==0);CHECK(DeadlineEqualityExpires()==0);CHECK(TravelDirectionDoesNotRotateTheItem()==0);CHECK(MovingWeaponAndHandTogether()==0);CHECK(InvalidGeometryAndProfile()==0);
    CHECK(RailContactCapturesPathBeforeOrientationAlignment()==0);CHECK(RailContactAtMouthNeedsPostCaptureMotion()==0);
    CHECK(RailContactInitialAssistNeverAutomaticallySeats()==0);CHECK(RailContactBoundsStayPhysical()==0);CHECK(RailContactMaintainsTrackingAndLeaseSafety()==0);
    CHECK(RailContactAntiparallelShellRemainsRigid()==0);CHECK(RailContactIsExplicitSingleShellAssistance()==0);
    std::cout<<"Reload insertion: authored grasp, continuous rail capture, reversal, exact ownership, packet safety, single seat and native-ack separation passed\n";return 0;
}
