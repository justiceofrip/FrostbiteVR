#include "Test.h"
#include "Bc2SightContact.h"
#include <limits>
#include <vector>
using namespace fvr;using namespace bc2;
namespace {
math::Matrix4 Identity(){math::Matrix4 out{};for(unsigned n=0;n<4;++n)out.values[n][n]=1;return out;}
math::Matrix4 Translate(float x,float y,float z){auto out=Identity();out.values[3]={x,y,z,1};return out;}
WeaponSightObservation Sample(){
    WeaponSightObservation s;s.assetName="XM8_sp_s";s.rootName=s.sightParentName="jntWpn_1";s.sightName="jntWpn_9";
    s.skeletonFingerprint=0xa7f219a1426216abull;s.generation=12;s.deadline=34;s.attachmentReady=true;s.palmValid=true;s.minGrabAlongMeters=0;
    s.nativeWeapon=Translate(10,20,30);s.nativeSight=interaction::Multiply(Translate(.1f,.2f,.3f),s.nativeWeapon);
    s.placedWeapon=Translate(100,200,300);s.rawLeftTarget=interaction::Multiply(Translate(.1f,.2f,.245f),s.placedWeapon);return s;
}
}
int main(){
    auto s=Sample();auto out=MeasureWeaponSightContact(s);
    CHECK(out.valid&&out.generation==12&&out.deadline==34);
    CHECK(Near(out.pivotMeters.x,.1f)&&Near(out.pivotMeters.y,.2f)&&Near(out.pivotMeters.z,.3f));
    CHECK(Near(out.axis.x,1)&&Near(out.axis.y,0)&&Near(out.axis.z,0));
    CHECK(Near(out.handLocalMeters.z,.245f)&&out.contactDistanceMeters<.0001f);
    s.rawLeftTarget=interaction::Multiply(Translate(.13f,.24f,.245f),s.placedWeapon);
    CHECK(Near(MeasureWeaponSightContact(s).contactDistanceMeters,.05f));
    s.rawLeftTarget=interaction::Multiply(Translate(.1f,.2f,.35f),s.placedWeapon);
    CHECK(Near(MeasureWeaponSightContact(s).contactDistanceMeters,.05f)); // Behind the segment root.
    s.rawLeftTarget=interaction::Multiply(Translate(.1f,.2f,.14f),s.placedWeapon);
    CHECK(Near(MeasureWeaponSightContact(s).contactDistanceMeters,.05f)); // Beyond its tip.
    s.segmentLengthMeters=.16f;CHECK(MeasureWeaponSightContact(s).contactDistanceMeters<.0001f);
    // Both native and placed weapon motion disappear in weapon-local contact.
    s=Sample();auto turn=Identity();turn.values[0]={0,0,-1,0};turn.values[2]={1,0,0,0};
    s.nativeWeapon=interaction::Multiply(turn,Translate(-3,4,5));
    s.nativeSight=interaction::Multiply(Translate(.1f,.2f,.3f),s.nativeWeapon);
    s.placedWeapon=interaction::Multiply(turn,Translate(15,-10,4));
    s.rawLeftTarget=interaction::Multiply(Translate(.1f,.2f,.245f),s.placedWeapon);
    out=MeasureWeaponSightContact(s);CHECK(out.valid&&out.contactDistanceMeters<.0001f&&Near(out.axis.x,1));
    // A90-degree native sight animation changes the contact segment, not the gun.
    auto open=Translate(.1f,.2f,.3f);open.values[1]={0,0,1,0};open.values[2]={0,-1,0,0};
    s.nativeSight=interaction::Multiply(open,s.nativeWeapon);
    s.rawLeftTarget=interaction::Multiply(Translate(.1f,.255f,.3f),s.placedWeapon);
    out=MeasureWeaponSightContact(s);CHECK(out.valid&&out.contactDistanceMeters<.0001f&&Near(out.axis.x,1));
    s=Sample();s.unitsPerMetre=2;s.nativeSight=interaction::Multiply(Translate(.2f,.4f,.6f),s.nativeWeapon);
    s.rawLeftTarget=interaction::Multiply(Translate(.2f,.4f,.49f),s.placedWeapon);out=MeasureWeaponSightContact(s);
    CHECK(out.valid&&Near(out.pivotMeters.z,.3f)&&Near(out.handLocalMeters.z,.245f)&&out.contactDistanceMeters<.0001f);
    // Different raw hand position remains far even if a displayed support hand
    // would have been snapped onto the weapon; no snapped position enters API.
    s=Sample();s.rawLeftTarget=interaction::Multiply(Translate(.4f,.2f,.245f),s.placedWeapon);
    CHECK(Near(MeasureWeaponSightContact(s).contactDistanceMeters,.3f));
    s.assetName="40mmgl";CHECK(MeasureWeaponSightContact(s).valid);
    for(unsigned bad=0;bad<10;++bad){s=Sample();
        if(bad==0)s.assetName="SPAS12_sp";if(bad==1)s.assetName="XM8_sp_s_extra";
        if(bad==2)++s.skeletonFingerprint;if(bad==3)s.sightName="jntWpn_11";
        if(bad==4)s.sightParentName="jntWpn_0";if(bad==5)s.hidden=true;
        if(bad==6)s.unitsPerMetre=0;if(bad==7)s.segmentLengthMeters=.4f;
        if(bad==8)s.rawLeftTarget.values[3][0]=std::numeric_limits<float>::quiet_NaN();
        if(bad==9)s.nativeSight.values[0][0]=.0001f;
        CHECK(!MeasureWeaponSightContact(s).valid);
    }
    // Pending equip data is not a hinge, even with a valid rig and matrices.
    s=Sample();s.attachmentReady=false;s.nativeSight=s.nativeWeapon;
    CHECK(!MeasureWeaponSightContact(s).valid);
    s.nativeSight=Sample().nativeSight;CHECK(!MeasureWeaponSightContact(s).valid);
    s.attachmentReady=true;out=MeasureWeaponSightContact(s);
    CHECK(out.valid&&Near(out.pivotMeters.x,.1f)&&Near(out.pivotMeters.z,.3f));
    CHECK(!MeasureWeaponSightContact(WeaponSightObservation{}).valid); // readiness is opt-in
    // A real grip is measured at the finger-base center, not at wrist origin.
    s=Sample();s.minGrabAlongMeters=.04f;s.palmPointWristMeters={0,.03f,.07f};
    s.rawLeftTarget=interaction::Multiply(Translate(.1f,.17f,.17f),s.placedWeapon);
    out=MeasureWeaponSightContact(s);CHECK(out.valid&&out.contactDistanceMeters<.0001f);
    CHECK(Near(out.handLocalMeters.z,.24f)&&Near(out.graspPointMeters.z,.24f));
    s.rawLeftTarget=interaction::Multiply(Translate(.1f,.17f,.23f),s.placedWeapon);
    out=MeasureWeaponSightContact(s);CHECK(out.valid&&Near(out.contactDistanceMeters,.04f)&&Near(out.graspPointMeters.z,.26f));
    s.palmValid=false;CHECK(!MeasureWeaponSightContact(s).valid);
    std::vector<std::string> palmNames{"LeftHand","LeftHandIndex1","LeftHandMiddle1","LeftHandRing1","LeftHandPinky1"};
    std::vector<std::int32_t> palmParents{-1,0,0,0,0};
    std::vector<math::Matrix4> palmNative{Translate(4,5,6),Translate(4.01f,5.02f,6.06f),Translate(4.02f,5.02f,6.08f),Translate(4.03f,5.02f,6.08f),Translate(4.04f,5.02f,6.06f)};
    const auto palm=MeasureSightPalm(palmNames,palmParents,palmNative,0,1);
    CHECK(palm&&Near(palm->x,.025f)&&Near(palm->y,.02f)&&Near(palm->z,.07f));
    palmParents[2]=1;CHECK(!MeasureSightPalm(palmNames,palmParents,palmNative,0,1));palmParents[2]=0;
    palmNames[3]="unknown";CHECK(!MeasureSightPalm(palmNames,palmParents,palmNative,0,1));
    std::vector<std::string> names{"root","child"};std::vector<std::int32_t> parents{-1,0};
    std::vector<math::Matrix4> bind{Identity(),Identity()};const auto fingerprint=SightRigFingerprint(names,parents,bind);CHECK(fingerprint);
    names[1]="other";CHECK(SightRigFingerprint(names,parents,bind)!=fingerprint);names[1]="child";
    parents[1]=-1;CHECK(SightRigFingerprint(names,parents,bind)!=fingerprint);parents[1]=0;
    bind[1].values[3][0]=.1f;CHECK(SightRigFingerprint(names,parents,bind)!=fingerprint);
    parents.pop_back();CHECK(!SightRigFingerprint(names,parents,bind));
    // A different visible mechanism wrist/anchor must never change the accepted
    // activation, contact distance, hinge point or signed gesture input.
    s=Sample();s.frontValid=true;s.nativeFront=s.nativeSight;
    const auto before=MeasureWeaponSightContact(s);
    s.visualHandValid=true;s.visualLeftTarget=interaction::Multiply(turn,s.rawLeftTarget);
    s.visualPointWristMeters={.012f,.023f,.09f};
    out=MeasureWeaponSightContact(s);
    CHECK(out.valid&&out.previewValid&&before.previewValid);
    CHECK(out.contactDistanceMeters==before.contactDistanceMeters);
    CHECK(out.handLocalMeters.x==before.handLocalMeters.x&&out.handLocalMeters.y==before.handLocalMeters.y&&out.handLocalMeters.z==before.handLocalMeters.z);
    CHECK(out.graspPointMeters.x==before.graspPointMeters.x&&out.graspPointMeters.y==before.graspPointMeters.y&&out.graspPointMeters.z==before.graspPointMeters.z);
    CHECK(out.handLocalFrameMeters.values!=before.handLocalFrameMeters.values);
    CHECK(out.rawHandLocalFrameMeters.values==before.rawHandLocalFrameMeters.values);
    CHECK(out.rawPalmPointWristMeters.x==before.rawPalmPointWristMeters.x&&
        out.rawPalmPointWristMeters.y==before.rawPalmPointWristMeters.y&&out.rawPalmPointWristMeters.z==before.rawPalmPointWristMeters.z);
    CHECK(Near(out.palmPointWristMeters.z,.09f));
    s.visualPointWristMeters.x=std::numeric_limits<float>::quiet_NaN();
    out=MeasureWeaponSightContact(s);CHECK(out.valid&&!out.previewValid);
    return 0;
}
