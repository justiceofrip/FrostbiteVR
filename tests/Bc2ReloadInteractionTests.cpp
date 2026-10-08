#include "Bc2ReloadInteraction.h"
#include "Test.h"
#include <iostream>
using namespace fvr;using namespace fvr::bc2;using namespace fvr::interaction;
namespace {
#include "SpasAxialMotion20261002.inc"
auto Pose(float x=0,float y=0,float z=0){auto m=reload_insertion_detail::Identity();m.values[3]={x,y,z,1};return m;}
bool Same(const math::Matrix4& a,const math::Matrix4& b,float e=1e-5f){for(unsigned r=0;r<4;++r)for(unsigned c=0;c<4;++c)if(!Near(a.values[r][c],b.values[r][c],e))return false;return true;}
auto Roll(float angle){auto m=Pose();m.values[0][0]=m.values[1][1]=std::cos(angle);m.values[0][1]=std::sin(angle);m.values[1][0]=-std::sin(angle);return m;}
auto Tilted(float roll,float tilt){auto m=Pose();m.values[1][1]=m.values[2][2]=std::cos(tilt);m.values[1][2]=std::sin(tilt);m.values[2][1]=-std::sin(tilt);return Multiply(Roll(roll),m);}
auto Along(float distance,float lateral=0){const auto p=SpasReloadInsertionProfile();auto m=reload_insertion_detail::TravelPose(p,distance);
    const auto norm=std::hypot(p.travelDirection[0],p.travelDirection[1]);
    m.values[3][0]+=lateral*p.travelDirection[1]/norm;m.values[3][1]-=lateral*p.travelDirection[0]/norm;return m;}
struct Fixture {
    Bc2ReloadInteraction policy{true};Bc2ReloadInteractionSample s{};ReloadInsertionProfile p=SpasReloadInsertionProfile();
    Fixture(){
        s.assetName=SpasReloadAsset;s.meshPath=SpasReloadMesh;s.rigFingerprint=SpasReloadRig;s.selectedMeshIdentityVerified=true;
        auto& i=s.insertion;i.identity={{1,2,3,4},{5,6},{7,8},9};i.nowNs=1000000000ll;
        i.focused=i.itemTracked=i.weaponTracked=i.held=i.eligible=true;
        i.itemClaim.token={10,i.identity.owner,InteractionHand::Left,HandClaimKind::AmmoObject,i.identity.item,{30,1},0};
        i.weaponClaim.token={11,i.identity.owner,InteractionHand::Right,HandClaimKind::GunHold,i.identity.weapon,{31,1},0};
        s.native={i.identity.owner,i.identity.weapon,12,0,0,true};s.weaponWorldMeters=Pose(1,2,3);
    }
    Bc2ReloadInteractionResult SendRail(const math::Matrix4& rail,std::int64_t dt=30000000ll){
        auto& i=s.insertion;++i.sequence;i.geometrySequence=i.sequence;i.nowNs+=dt;i.observedNs=i.nowNs;i.deadlineNs=i.nowNs+100000000ll;
        i.itemClaim.inputSequence=i.weaponClaim.inputSequence=i.sequence;i.itemClaim.deadlineNs=i.weaponClaim.deadlineNs=i.deadlineNs;
        s.native.observedNs=i.nowNs;s.native.deadlineNs=i.nowNs+80000000ll;
        s.rawLeftWristWorldMeters=Multiply(p.itemFromHand,Multiply(*InverseRigid(p.itemFromInsertion),Multiply(rail,Multiply(p.weaponFromEntry,s.weaponWorldMeters))));
        return policy.Update(s);
    }
    Bc2ReloadInteractionResult Send(float z,float lateral=0,std::int64_t dt=30000000ll){return SendRail(Along(z,lateral),dt);}
};
int ProfileAndRail(){
    Fixture f;CHECK(ValidateReloadInsertionProfile(f.p));CHECK(f.p.id==0x535041530001ull&&f.p.family==ReloadInsertionFamily::SingleShell);
    auto r=f.Send(-.08f);CHECK(r.targets&&!r.insertion.captured);const auto raw=r.targets->weaponFromShellCenterMeters;
    CHECK(Same(Multiply(f.p.itemFromHand,raw),r.targets->weaponFromLeftWristMeters));
    r=f.Send(-.05f,.005f);CHECK(r.targets&&r.insertion.captured&&r.insertion.alignment==0);
    f.Send(-.02f,.005f);f.Send(0,.005f);f.Send(.025f,.005f);r=f.Send(.05f,.005f);CHECK(!r.insertion.seat);
    unsigned seats=0;for(unsigned n=0;n<4;++n){r=f.Send(.05f,.005f);if(r.insertion.seat){++seats;CHECK(r.insertion.seat->operation==ReloadOperation::InsertRound);}}
    CHECK(seats==1&&r.insertion.phase==ReloadInsertionPhase::Seated);
    CHECK(r.insertion.progress==1&&r.insertion.alignment==1&&r.targets);
    CHECK(Same(Multiply(Multiply(f.p.itemFromInsertion,r.targets->weaponFromShellCenterMeters),*InverseRigid(f.p.weaponFromEntry)),Along(.05f)));
    CHECK(r.targets->deadlineNs==f.s.native.deadlineNs);
    r=f.Send(.05f);CHECK(!r.insertion.seat);return 0;
}
int UnderReceiverApproach(){
    Fixture f;CHECK(f.p.revision==5&&f.p.approach==ReloadInsertionApproach::RailContact);
    CHECK(f.p.captureDistanceMeters==.06f&&f.p.maxStepMeters==.04f&&f.p.orientation==ReloadInsertionOrientation::AxialSymmetry);
    f.Send(-.01f,-.08f);auto r=f.Send(-.01f,-.055f);CHECK(r.insertion.captured&&r.targets);
    f.Send(-.01f,-.035f);f.Send(-.01f,-.02f);
    for(unsigned n=0;n<4;++n)r=f.Send(-.01f,-.02f);
    const auto rail=Multiply(Multiply(f.p.itemFromInsertion,r.targets->weaponFromShellCenterMeters),*InverseRigid(f.p.weaponFromEntry));
    CHECK(r.insertion.alignment==1&&Same(rail,Along(-.01f))&&!r.insertion.seat);
    Fixture inside;CHECK(inside.Send(-.01f,-.02f).insertion.captured);for(unsigned n=0;n<12;++n)CHECK(!inside.Send(-.01f,-.02f).insertion.seat);
    Fixture eitherSide;eitherSide.Send(.01f,-.08f);r=eitherSide.Send(.01f,-.055f);
    CHECK(r.insertion.captured&&!r.insertion.seat);
    for(unsigned n=0;n<12;++n)CHECK(!eitherSide.Send(.01f,-.055f).insertion.seat);return 0;
}
int DefaultOffAndGates(){
    Fixture f;f.Send(-.05f);Bc2ReloadInteraction off;auto r=off.Update(f.s);CHECK(r.reason==Bc2ReloadInteractionReason::Disabled&&!r.targets);
    for(unsigned k=0;k<9;++k){Fixture t;t.Send(-.05f);
        if(k==0)t.s.assetName="XM8_sp_s";if(k==1)t.s.meshPath="different";if(k==2)t.s.rigFingerprint=5;
        if(k==3)t.s.selectedMeshIdentityVerified=false;if(k==4)t.s.native.allFiringCopiesHeld=false;
        if(k==5)++t.s.native.owner.actorGeneration;if(k==6)++t.s.native.weapon.generation;
        if(k==7)t.s.native.deadlineNs=t.s.insertion.nowNs;if(k==8)t.s.insertion.itemClaim.token.hand=InteractionHand::Right;
        r=t.policy.Update(t.s);CHECK(!r.targets&&!r.insertion.seat);
    }return 0;
}
int DuplicateAndCancellation(){
    Fixture f;auto a=f.Send(-.05f);CHECK(a.targets);
    f.s.rawLeftWristWorldMeters=Pose(90,10,30);++f.s.insertion.nowNs;
    auto b=f.policy.Update(f.s);CHECK(b.targets&&!b.insertion.seat);
    CHECK(Same(a.targets->weaponFromShellCenterMeters,b.targets->weaponFromShellCenterMeters));
    CHECK(Same(a.targets->weaponFromLeftWristMeters,b.targets->weaponFromLeftWristMeters));
    f.Send(-.02f);f.s.native.cycle++;b=f.policy.Update(f.s);CHECK(b.reason==Bc2ReloadInteractionReason::NativeCycleChanged&&!b.targets);
    b=f.Send(-.02f);CHECK(b.insertion.captured&&!b.insertion.seat); // New cycle gets assist, never inherited stroke.
    for(unsigned n=0;n<8;++n)CHECK(!f.Send(-.02f).insertion.seat);
    f.s.insertion.focused=false;b=f.policy.Update(f.s);CHECK(!b.targets&&b.insertion.cancelled);
    return 0;
}
int OwnershipAndCoherentGeometry(){
    for(unsigned k=0;k<5;++k){Fixture f;f.Send(-.05f);f.Send(-.02f);
        if(k==0)f.s.insertion.itemClaim.token.kind=HandClaimKind::WeaponSupport;
        if(k==1)f.s.insertion.itemClaim.deadlineNs=f.s.insertion.nowNs;
        if(k==2)f.s.insertion.geometrySequence--;
        if(k==3)f.s.insertion.held=false;
        if(k==4)f.s.insertion.eligible=false;
        const auto r=f.policy.Update(f.s);CHECK(!r.targets&&!r.insertion.seat);
    }
    Fixture a,b;auto transform=Pose(7,8,9);transform.values[0]={0,0,-1,0};transform.values[2]={1,0,0,0};
    const auto x=a.Send(-.05f);b.Send(-.05f);b.s.weaponWorldMeters=Multiply(b.s.weaponWorldMeters,transform);
    b.s.rawLeftWristWorldMeters=Multiply(b.s.rawLeftWristWorldMeters,transform);Bc2ReloadInteraction fresh{true};
    const auto y=fresh.Update(b.s);CHECK(x.targets&&y.targets&&Same(x.targets->weaponFromShellCenterMeters,y.targets->weaponFromShellCenterMeters));return 0;
}
int ActualHandClaims(){
    // Real shared arbitration denies an AmmoObject acquisition over a held support grip.
    HandInteraction arbiter;HandInteractionSample s{{1,2,3,4},1,1000000000ll,1100000000ll,1000000000ll,true,{true,true},{true,true}};
    arbiter.Update(s);s.sequence=2;s.observedNs=s.nowNs+=10000000ll;s.deadlineNs=s.nowNs+100000000ll;s.released={false,false};
    auto gun=arbiter.Acquire(s,{s.owner,InteractionHand::Right,HandClaimKind::GunHold,{5,6},{{20,1},s.sequence,s.deadlineNs,true},1,0});CHECK(gun.claim);
    auto support=arbiter.Acquire(s,{s.owner,InteractionHand::Left,HandClaimKind::WeaponSupport,{5,6},{{21,1},s.sequence,s.deadlineNs,true},1,gun.claim->token.id});CHECK(support.claim);
    auto ammo=arbiter.Acquire(s,{s.owner,InteractionHand::Left,HandClaimKind::AmmoObject,{7,8},{{22,1},s.sequence,s.deadlineNs,true},2,0});CHECK(!ammo.claim);
    return 0;
}
int ObservedTwistDoesNotBlockRoundShell(){
    // Actual telemetry saved tip + total/axis angles, not the full rotation.
    // These are angle-constraint regressions, not current-entry replay: the
    // old-frame near positions below are merely in-sphere test positions after
    // the entry moved. Twist sign/tilt azimuth are synthetic, not captured matrices.
    struct NearContact {float x,y,z,keyed,axis;};
    const NearContact contacts[]={{-.00507008f,-.00692987f,-.00173146f,.645959f,.0986057f},
        {-.00559617f,.0110866f,-.0013395f,.991082f,.593379f}};
    for(const auto& c:contacts){Fixture f;
        const float roll=2*std::acos(std::cos(c.keyed*.5f)/std::cos(c.axis*.5f));
        auto near=Tilted(roll,c.axis);near.values[3]={c.x,c.y,c.z,1};
        CHECK(Near(reload_insertion_detail::AlignmentAngle(near,ReloadInsertionOrientation::Keyed),c.keyed));
        CHECK(Near(reload_insertion_detail::AlignmentAngle(near,ReloadInsertionOrientation::AxialSymmetry),c.axis));
        CHECK(c.keyed>f.p.captureAngleRadians&&c.axis<f.p.captureAngleRadians);
        auto r=f.SendRail(near);CHECK(r.insertion.captured&&r.targets&&r.insertion.alignment==0);
        const auto rawTarget=*r.insertion.rawItem;CHECK(Same(*r.insertion.guidedItem,rawTarget));
        for(unsigned n=0;n<4;++n)r=f.SendRail(near);
        CHECK(r.targets&&r.insertion.alignment==1&&!r.insertion.seat);
        const auto guided=Multiply(Multiply(f.p.itemFromInsertion,r.targets->weaponFromShellCenterMeters),*InverseRigid(f.p.weaponFromEntry));
        auto expected=Roll(roll);const auto along=reload_insertion_detail::TravelCoordinate(near,f.p);
        for(unsigned n=0;n<3;++n)expected.values[3][n]=float(along*f.p.travelDirection[n]);
        CHECK(Same(guided,expected,.00002f)); // Full axis alignment preserves the axial roll.
        CHECK(Same(Multiply(f.p.itemFromHand,r.targets->weaponFromShellCenterMeters),r.targets->weaponFromLeftWristMeters));
    }
    return 0;
}
int AxialCaptureKeepsDirectionAndStepGuards(){
    Fixture f;auto tilt=Tilted(1.2f,.8f);tilt.values[3][2]=-.05f;f.SendRail(tilt);tilt.values[3][2]=-.02f;
    CHECK(f.SendRail(tilt).insertion.phase==ReloadInsertionPhase::Guided); // Orientation now aligns after contact.
    Fixture inside;auto roll=Roll(2.4f);roll.values[3][2]=-.01f;CHECK(inside.SendRail(roll).insertion.captured);
    Fixture back;roll.values[3][2]=.01f;CHECK(back.SendRail(roll).insertion.captured);roll.values[3][2]=-.01f;CHECK(!back.SendRail(roll).insertion.seat);
    Fixture jump;roll.values[3][2]=-.2f;jump.SendRail(roll);roll.values[3][2]=-.01f;
    auto r=jump.SendRail(roll);CHECK(!r.insertion.captured&&r.insertion.reason==ReloadInsertionReason::PoseJump);
    Fixture twist;roll=Roll(0);roll.values[3][2]=-.05f;CHECK(twist.SendRail(roll).insertion.captured);roll.values[3][2]=-.02f;twist.SendRail(roll);
    roll=Roll(1.2f);roll.values[3][2]=-.02f;r=twist.SendRail(roll);
    CHECK(r.insertion.cancelled&&r.insertion.reason==ReloadInsertionReason::PoseJump); // Sudden60+degree twist remains a tracking jump.
    return 0;
}
int RetainedHeadsetPathNeedsEntrySphereAsWellAsAxialFit(){
    const auto run=[](ReloadInsertionProfile profile){
        struct Result {unsigned captures=0,seats=0,jumps=0;std::uint64_t firstCapture=0,firstSeat=0;};Result result;
        ReloadInsertion insertion(profile);Fixture fixture;
        for(const auto& c:RecordedSpasContacts){auto s=fixture.s.insertion;
            s.profile={profile.id,profile.revision};s.sequence=s.geometrySequence=c.sequence;
            s.observedNs=c.observed;s.deadlineNs=c.deadline;s.nowNs=c.processing;
            s.itemClaim.inputSequence=s.weaponClaim.inputSequence=c.sequence;s.itemClaim.deadlineNs=s.weaponClaim.deadlineNs=c.deadline;
            const float twist=2*std::acos(std::clamp(std::cos(c.keyed*.5f)/std::cos(c.axis*.5f),-1.f,1.f));
            auto rail=Tilted(twist,c.axis);rail.values[3]={c.x,c.y,c.z,1};
            s.weaponFromHand=Multiply(profile.itemFromHand,Multiply(*InverseRigid(profile.itemFromInsertion),Multiply(rail,profile.weaponFromEntry)));
            const auto r=insertion.Update(s);
            if(r.captured){++result.captures;if(!result.firstCapture)result.firstCapture=c.sequence;}
            if(r.seat){++result.seats;if(!result.firstSeat)result.firstSeat=c.sequence;}
            if(r.reason==ReloadInsertionReason::PoseJump)++result.jumps;
        }return result;
    };
    // Frozen revision3 entry: these old telemetry coordinates must not be
    // silently relabeled as positions relative to revision4's moved entry.
    auto revision3=SpasReloadInsertionProfile();revision3.revision=3;revision3.travelDirection={0,0,1};
    revision3.approach=ReloadInsertionApproach::EntrySphere;revision3.captureDistanceMeters=.03f;revision3.releaseDistanceMeters=.06f;revision3.postCaptureTravelMeters=0;
    revision3.weaponFromEntry.values[3]={-.0001406920859f,-.04552212652f,-.4853635506f,1};
    auto front=revision3;front.approach=ReloadInsertionApproach::FrontHemisphere;
    auto old=front;old.revision=2;old.orientation=ReloadInsertionOrientation::Keyed;
    const auto oldV2=run(old);CHECK(oldV2.captures==0&&oldV2.seats==0&&oldV2.jumps==0);
    const auto axialOnly=run(front);CHECK(axialOnly.captures==0&&axialOnly.seats==0&&axialOnly.jumps==0);
    const auto candidate=run(revision3);CHECK(candidate.captures==1&&candidate.firstCapture==3613&&candidate.jumps==0);
    CHECK(candidate.seats==1&&candidate.firstSeat>3629);
    std::cout<<"Historical revision3 192-contact representative replay: capture="<<candidate.firstCapture<<", seat="<<candidate.firstSeat<<"; old-v2 0 captures; axial/front-only 0 captures\n";
    return 0;
}
int BottomStartAndMeasuredTerminalStayDistinctFromOrientation(){
    const auto p=SpasReloadInsertionProfile();
    const auto center=[&](float distance){return Multiply(*InverseRigid(p.itemFromInsertion),Multiply(reload_insertion_detail::TravelPose(p,distance),p.weaponFromEntry));};
    const auto start=center(0),seat=center(.05f);
    CHECK(Near(seat.values[3][0],-.0000283390138f,.000001f)&&Near(seat.values[3][1],-.04518763864f,.000001f)&&Near(seat.values[3][2],-.5035302898f,.000001f));
    for(unsigned row=0;row<3;++row)for(unsigned col=0;col<3;++col)CHECK(start.values[row][col]==seat.values[row][col]);
    const std::array<float,3> measured{.0311778902f,.8161839678f,-.5769503184f};
    for(unsigned n=0;n<3;++n)CHECK(Near((seat.values[3][n]-start.values[3][n])/.05f,measured[n],.000003f));
    CHECK(start.values[2][2]>.999f&&seat.values[2][2]>.999f); // Shell stays longitudinal; travel rises diagonally.
    // Saved real asset bounds in its center frame, transformed through current
    // nominal entry. Entire conservative shell bounds start below the plate.
    float top=-1;for(float x:{-.01029205322f,.01029205322f})for(float y:{-.01025390625f,.01025390625f})for(float z:{-.03182983398f,.03182983398f}){
        const float worldY=x*start.values[0][1]+y*start.values[1][1]+z*start.values[2][1]+start.values[3][1];top=std::max(top,worldY);}
    CHECK(top<-.0489809475f-.025f);
    Fixture f;f.Send(-.08f);auto r=f.Send(-.05f);CHECK(r.insertion.captured&&!r.insertion.seat);
    f.Send(-.02f);f.Send(0);f.Send(.025f);for(unsigned n=0;n<8;++n)r=f.Send(.05f);
    CHECK(r.targets&&r.insertion.phase==ReloadInsertionPhase::Seated&&Same(r.targets->weaponFromShellCenterMeters,seat));
    CHECK(Same(r.targets->weaponFromLeftWristMeters,Multiply(p.itemFromHand,seat)));return 0;
}
int InitialMouthAssistUsesNewRawStrokeAndPreservesTerminal(){
    Fixture f;auto raw=Tilted(1.1f,1.4f);raw.values[3]=Along(.05f,.03f).values[3];
    auto r=f.SendRail(raw);CHECK(r.insertion.captured&&r.targets&&Same(*r.insertion.rawItem,*r.insertion.guidedItem));
    for(unsigned n=0;n<12;++n){r=f.SendRail(raw);CHECK(!r.insertion.seat);}
    CHECK(r.insertion.alignment==1&&r.targets);
    auto stage=Roll(1.1f);stage.values[3]=Along(.03f).values[3];
    CHECK(Same(Multiply(Multiply(f.p.itemFromInsertion,r.targets->weaponFromShellCenterMeters),*InverseRigid(f.p.weaponFromEntry)),stage));
    raw.values[3]=Along(.071f,.03f).values[3];unsigned seats=0;
    for(unsigned n=0;n<6;++n){r=f.SendRail(raw);seats+=r.insertion.seat.has_value();}
    CHECK(seats==1&&r.insertion.phase==ReloadInsertionPhase::Seated&&r.targets);
    auto terminal=Roll(1.1f);terminal.values[3]=Along(.05f).values[3];
    CHECK(Same(Multiply(Multiply(f.p.itemFromInsertion,r.targets->weaponFromShellCenterMeters),*InverseRigid(f.p.weaponFromEntry)),terminal));
    CHECK(Same(Multiply(f.p.itemFromHand,r.targets->weaponFromShellCenterMeters),r.targets->weaponFromLeftWristMeters));
    return 0;
}
}
int main(){if(ProfileAndRail()||UnderReceiverApproach()||DefaultOffAndGates()||DuplicateAndCancellation()||OwnershipAndCoherentGeometry()||ActualHandClaims()||ObservedTwistDoesNotBlockRoundShell()||AxialCaptureKeepsDirectionAndStepGuards()||RetainedHeadsetPathNeedsEntrySphereAsWellAsAxialFit()||BottomStartAndMeasuredTerminalStayDistinctFromOrientation()||InitialMouthAssistUsesNewRawStrokeAndPreservesTerminal())return 1;std::cout<<"Bc2ReloadInteraction: 11 cases passed\n";}
