#include "Bc2PistolSlideProfile.h"
#include "Test.h"
#include <limits>
using namespace fvr;using namespace fvr::bc2;using namespace fvr::interaction;
namespace {
const auto& Geometry(){return bc2_pistol_slide_profile_detail::Mp443Geometry;}
int ExactConfiguredAssetOnly(){
    const auto selected=Geometry().selection;CHECK(FindBc2Mp443SlideGeometry(selected));
    auto wrong=selected;wrong.asset="MP443";CHECK(!BuildBc2Mp443ModSlideProfile(wrong));
    wrong=selected;wrong.asset="mp443_sp";CHECK(!FindBc2Mp443SlideGeometry(wrong));
    wrong=selected;wrong.configurationPath="Objects/Weapons/Handheld/RU_hg_MP443Grach/RU_hg_MP443Grach";
    CHECK(!FindBc2Mp443SlideGeometry(wrong));
    wrong=selected;wrong.configurationGuid="58098858-821b-4406-a19c-ae62e5d38bde";
    CHECK(!FindBc2Mp443SlideGeometry(wrong));
    wrong=selected;wrong.configurationSha256={};CHECK(!FindBc2Mp443SlideGeometry(wrong));
    wrong=selected;wrong.meshPath="Objects/Weapons/Handheld/RU_hg_MP443Grach/RU_hg_MP443Grach_Mesh.dbx";
    CHECK(!Bc2Mp443SlideLocalFrame(wrong,0));
    wrong=selected;++wrong.rigFingerprint;CHECK(!FindBc2Mp443SlideGeometry(wrong));
    CHECK(!BuildBc2Mp443ModSlideProfile({}));return 0;
}
int SignedAuthoredRailDoesNotInventNativeStops(){
    const auto& g=Geometry();
    CHECK(g.root=="jntWpn_1"&&g.slide=="jntWpn_3"&&g.rootIndex==10&&g.slideIndex==30);
    CHECK(!g.nativeClosedVerified&&!g.nativeRetractedVerified&&!g.stableControllerGraspVerified);
    CHECK(std::abs(g.minimumDisplacementMeters-(-.0019974632828737467))<1e-15);
    CHECK(std::abs(g.maximumDisplacementMeters-.07247133458104844)<1e-15);
    CHECK(std::abs(g.maximumDisplacementMeters-g.minimumDisplacementMeters-.07446879786392219)<1e-15);
    const auto reference=Bc2Mp443SlideLocalFrame(g.selection,0);
    const auto forward=Bc2Mp443SlideLocalFrame(g.selection,float(g.minimumDisplacementMeters));
    const auto rearward=Bc2Mp443SlideLocalFrame(g.selection,float(g.maximumDisplacementMeters));
    CHECK(reference&&forward&&rearward);
    CHECK(Near(reference->values[3][0],-2.4863219039161777e-8f,1e-12f));
    CHECK(Near(reference->values[3][1],.0802396759390831f,1e-8f));
    CHECK(Near(reference->values[3][2],-.08735319972038269f,1e-8f));
    CHECK(forward->values[3][2]<reference->values[3][2]);
    CHECK(Near(rearward->values[3][2],-.01488186513933425f,1e-8f));
    for(unsigned i=0;i<3;++i)for(unsigned j=0;j<3;++j)
        CHECK(reference->values[i][j]==(i==j?1.f:0.f)&&rearward->values[i][j]==reference->values[i][j]);
    CHECK(!Bc2Mp443SlideLocalFrame(g.selection,-.002f));
    CHECK(!Bc2Mp443SlideLocalFrame(g.selection,.073f));
    CHECK(!Bc2Mp443SlideLocalFrame(g.selection,std::numeric_limits<float>::quiet_NaN()));return 0;
}
int TransientWristAndRingLandmarkStayMeasuredGeometry(){
    const auto& g=Geometry();const auto profile=BuildBc2Mp443ModSlideProfile(g.selection);CHECK(profile);
    CHECK(g.authoredLandmarkSeconds==3.0&&!g.stableControllerGraspVerified);
    double distance2=0;
    for(unsigned i=0;i<3;++i){
        const double delta=double(g.ring3Part[i])-g.nearestVertexPart[i];distance2+=delta*delta;
        CHECK(g.nearestVertexPart[i]>=g.partBoundsMin[i]&&g.nearestVertexPart[i]<=g.partBoundsMax[i]);
    }
    CHECK(std::abs(std::sqrt(distance2)-.0018849795476781619)<1e-8);
    CHECK(feed_mechanism_detail::Pose(g.slideFromAuthoredWrist));
    // Independent composed coordinates: authored wrist translation plus static
    // slide origin. Ring3's nearest vertex is not the controller/wrist origin.
    CHECK(Near(profile->closedContact.values[3][0],.09829836066438154f,1e-8f));
    CHECK(Near(profile->closedContact.values[3][1],.05538167367927758f,1e-8f));
    CHECK(Near(profile->closedContact.values[3][2],.03325760413274309f,1e-8f));
    CHECK(std::abs(profile->closedContact.values[3][0]-g.nearestVertexPart[0])>.1f);
    CHECK(profile->axis[0]==0&&profile->axis[1]==0&&profile->axis[2]==1);return 0;
}
int ModPolicyIncludingHysteresisFitsMeasuredRange(){
    const auto& g=Geometry();const auto& policy=bc2_pistol_slide_profile_detail::Mp443ModPolicy;
    const auto p=BuildBc2Mp443ModSlideProfile(g.selection);CHECK(p&&pistol_slide_detail::Profile(*p));
    CHECK(!policy.name.empty()&&!policy.sourceReference.empty());
    CHECK(policy.sourceDisplacementMeters==0&&Near(p->stroke,.065f));
    // The entire accepted input envelope, not only nominal endpoints, must fit
    // the signed measured rail. The full 74.47mm authored span is not a stroke.
    CHECK(policy.sourceDisplacementMeters-p->frontTolerance-p->hysteresis>=g.minimumDisplacementMeters);
    CHECK(policy.sourceDisplacementMeters+p->stroke+p->rearTolerance+p->hysteresis<=g.maximumDisplacementMeters);
    CHECK(Near(p->frontTolerance,.001f)&&Near(p->rearTolerance,.003f)&&Near(p->hysteresis,.00075f));
    CHECK(Near(p->contactRadius,.035f)&&Near(p->lateralTolerance,.025f)&&Near(p->rotationTolerance,.35f));
    CHECK(Near(p->maxStepMeters,.025f)&&p->endpointDwellNs==30000000&&p->maximumStrokeNs==8000000000ll);
    CHECK(p->hands.mechanism==InteractionHand::Left&&p->hands.gun==InteractionHand::Right);return 0;
}
}
int main(){
    CHECK(ExactConfiguredAssetOnly()==0);
    CHECK(SignedAuthoredRailDoesNotInventNativeStops()==0);
    CHECK(TransientWristAndRingLandmarkStayMeasuredGeometry()==0);
    CHECK(ModPolicyIncludingHysteresisFitsMeasuredRange()==0);
    std::puts("Bc2PistolSlideProfileTests: 4 selector/measured-geometry/mod-policy groups; no native/headset acceptance");
}
