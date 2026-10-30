#include "Bc2PistolSlideProfile.h"
#include "Test.h"
#include <limits>
using namespace fvr;using namespace fvr::bc2;using namespace fvr::interaction;
namespace {
const auto& Geometry(){return bc2_pistol_slide_profile_detail::Mp443Geometry;}
// Deliberately artificial: this is a pure registry fixture, not another gun.
inline constexpr Bc2PistolSlideGeometry SyntheticGeometry{
    {"SyntheticPistol2","fixture/pistol/second","22222222-2222-2222-2222-222222222222",
     "2222222222222222222222222222222222222222222222222222222222222222","fixture/mesh/second",0x202},
    "fixture/geometry","3333333333333333333333333333333333333333333333333333333333333333",
    "fixture/roles","4444444444444444444444444444444444444444444444444444444444444444",
    "fixtureRoot","fixtureSlide",2,4,
    bc2_pistol_slide_profile_detail::Frame({1,0,0,0,0,1,0,0,0,0,1,0,.2f,.1f,-.3f,1}),
    bc2_pistol_slide_profile_detail::Frame({1,0,0,0,0,1,0,0,0,0,1,0,.01f,.02f,.03f,1}),
    {1,0,0},{-.1f,-.1f,-.1f},{.1f,.1f,.1f},{.01f,0,0},{0,0,0},-.005,.055,1.5,.01,false,false,false};
inline constexpr Bc2PistolSlideInteractionPolicy SyntheticPolicy{
    "Synthetic second policy","fixture/contact",0x53594e5448455432ull,2,
    .002f,.04f,.001f,.002f,.0005f,.02f,.015f,.25f,.02f,20000000,5000000000ll};
inline constexpr std::array<ReloadTimingWord,6> SyntheticTiming{{
    {0x10,std::bit_cast<std::uint32_t>(.4f)},{0x14,0},{0x18,std::bit_cast<std::uint32_t>(2.f)},
    {0x20,1},{0x24,0},{0x2c,0}}};
inline constexpr Bc2PistolSlideRegistration SyntheticRow{
    0x2222222222222222ull,"2222222222222222222222222222222222222222222222222222222222222222",
    &SyntheticGeometry,&SyntheticPolicy,
    {"SyntheticPistol2","fixture/pistol/second",{0,1,8,29,9,2,0,2,.4f,0,0,0,false,false},
        SyntheticTiming,ReloadDescriptorAdmission::ReviewedNative},
    Bc2PistolSlideNativeClass::SingleFireMagazineZeroBolt,true,true};
ReloadObservedConfig Observed(const ReloadConfigDescriptor& d){
    ReloadObservedConfig c;c.weaponData=0x10000;c.firingData=0x11000;c.primaryFire=0x12000;c.ammoAddress=c.primaryFire+0x170;
    std::copy(d.assetName.begin(),d.assetName.end(),c.assetName.begin());
    std::copy(d.assetPath.begin(),d.assetPath.end(),c.assetPath.begin());
    const auto& v=d.values;c.fireLogicType=v.fireLogicType;c.reloadType=v.reloadType;c.fireInputAction=v.fireInputAction;
    c.reloadInputAction=v.reloadInputAction;c.baseCapacity=v.baseCapacity;c.numberOfMagazines=v.numberOfMagazines;
    c.reloadDelay=v.reloadDelay;c.reloadTime=v.reloadTime;c.reloadThreshold=v.reloadThreshold;c.postReloadTime=v.postReloadTime;
    c.boltDelay=v.boltDelay;c.boltTime=v.boltTime;c.holdBoltUntilFireRelease=v.holdBoltUntilFireRelease;
    c.holdBoltUntilZoomRelease=v.holdBoltUntilZoomRelease;return c;
}
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
int DistinctRowsKeepTheirOwnGeometryPolicyAndConfiguration(){
    const std::array rows{bc2_pistol_slide_profile_detail::Mp443Registration,SyntheticRow};
    const auto* first=FindBc2PistolSlideRegistration(rows,Geometry().selection);
    const auto* second=FindBc2PistolSlideRegistration(rows,SyntheticGeometry.selection);
    CHECK(first==&rows[0]&&second==&rows[1]&&first->key!=second->key);
    CHECK(ResolveBc2PistolSlideRegistration(rows,second->key)==second);
    CHECK(FindBc2PistolSlideRegistration(rows,second->configuration)==second);
    CHECK(FindBc2PistolSlideRegistration(rows,Observed(second->configuration))==second);
    CHECK(first->configuration.values.baseCapacity==17&&second->configuration.values.baseCapacity==9);
    CHECK(first->configuration.assetPath!=second->configuration.assetPath);
    const auto a=BuildBc2PistolSlideProfile(*first),b=BuildBc2PistolSlideProfile(*second);CHECK(a&&b);
    CHECK(a->id!=b->id&&Near(a->stroke,.065f)&&Near(b->stroke,.04f));
    CHECK(a->axis[2]==1&&b->axis[0]==1&&b->axis[2]==0);
    CHECK(Near(b->closedContact.values[3][0],.212f)&&Near(b->closedContact.values[3][1],.12f)&&
        Near(b->closedContact.values[3][2],-.27f));
    const auto frame=Bc2PistolSlideLocalFrame(*second,.042f);CHECK(frame);
    CHECK(Near(frame->values[3][0],.242f)&&Near(frame->values[3][1],.1f)&&Near(frame->values[3][2],-.3f));
    CHECK(!Bc2PistolSlideLocalFrame(*second,.06f));
    auto wrong=SyntheticGeometry.selection;wrong.configurationPath=Geometry().selection.configurationPath;
    CHECK(!FindBc2PistolSlideRegistration(rows,wrong));
    wrong=SyntheticGeometry.selection;wrong.asset="SyntheticPistol";CHECK(!FindBc2PistolSlideRegistration(rows,wrong));
    wrong=SyntheticGeometry.selection;wrong.rigFingerprint=Geometry().selection.rigFingerprint;
    CHECK(!FindBc2PistolSlideRegistration(rows,wrong));
    auto c=Observed(second->configuration);c.baseCapacity=17;CHECK(!FindBc2PistolSlideRegistration(rows,c));
    c=Observed(second->configuration);++c.ammoAddress;CHECK(!FindBc2PistolSlideRegistration(rows,c));
    auto descriptor=second->configuration;descriptor.admission=ReloadDescriptorAdmission::Candidate;
    CHECK(!FindBc2PistolSlideRegistration(rows,descriptor));
    auto timing=SyntheticTiming;++timing[0].expected;descriptor=second->configuration;descriptor.timing=timing;
    CHECK(!FindBc2PistolSlideRegistration(rows,descriptor));
    // A fixture span does not enroll anything in the real registry.
    CHECK(!FindBc2PistolSlideRegistration(SyntheticGeometry.selection));return 0;
}
int DuplicatesCollisionsAndAmbiguityFailClosed(){
    std::array rows{bc2_pistol_slide_profile_detail::Mp443Registration,SyntheticRow,SyntheticRow};
    CHECK(!ResolveBc2PistolSlideRegistration(rows,SyntheticRow.key));
    CHECK(!FindBc2PistolSlideRegistration(rows,SyntheticGeometry.selection));
    CHECK(!FindBc2PistolSlideRegistration(rows,Observed(SyntheticRow.configuration)));
    CHECK(ResolveBc2PistolSlideRegistration(rows,rows[0].key));
    rows[2]=rows[0];rows[2].key=SyntheticRow.key; // Even an invalid collision poisons this key.
    CHECK(!ResolveBc2PistolSlideRegistration(rows,SyntheticRow.key));
    rows[2]=rows[0];rows[2].profileDigest=SyntheticRow.profileDigest;
    CHECK(!ResolveBc2PistolSlideRegistration(rows,SyntheticRow.key));
    rows[2]=rows[0];rows[2].configuration=SyntheticRow.configuration;
    CHECK(!ResolveBc2PistolSlideRegistration(rows,SyntheticRow.key));
    rows[2]=rows[0];rows[2].geometry=&SyntheticGeometry;
    CHECK(!ResolveBc2PistolSlideRegistration(rows,SyntheticRow.key));
    rows[2]=SyntheticRow;rows[2].enabled=false;
    CHECK(!ResolveBc2PistolSlideRegistration(rows,SyntheticRow.key));
    const std::array builtinDuplicate{rows[0],rows[0]};
    CHECK(!ResolveBc2PistolSlideRegistration(builtinDuplicate,rows[0].key));
    std::array<Bc2PistolSlideRegistration,MaxGeneratedBc2PistolSlideProfiles+2> oversized{};
    oversized[0]=SyntheticRow;CHECK(!ResolveBc2PistolSlideRegistration(oversized,SyntheticRow.key));return 0;
}
int InvalidReviewIdentityGeometryAndPolicyCannotEnter(){
    CHECK(Bc2PistolSlideRegistrationValid(SyntheticRow));
    auto bad=SyntheticRow;bad.enabled=false;CHECK(!Bc2PistolSlideRegistrationValid(bad));
    bad=SyntheticRow;bad.reviewed=false;CHECK(!Bc2PistolSlideRegistrationValid(bad));
    bad=SyntheticRow;bad.nativeClass=Bc2PistolSlideNativeClass::Unsupported;CHECK(!Bc2PistolSlideRegistrationValid(bad));
    bad=SyntheticRow;bad.nativeClass=static_cast<Bc2PistolSlideNativeClass>(255);CHECK(!Bc2PistolSlideRegistrationValid(bad));
    bad=SyntheticRow;++bad.key;CHECK(!Bc2PistolSlideRegistrationValid(bad));
    bad=SyntheticRow;bad.profileDigest="ABC";CHECK(!Bc2PistolSlideRegistrationValid(bad));
    CHECK(!Bc2PistolSlideProfileKey("0000000000000000222222222222222222222222222222222222222222222222"));
    bad=SyntheticRow;bad.configuration.assetPath="fixture/pistol/wrong";CHECK(!Bc2PistolSlideRegistrationValid(bad));
    bad=SyntheticRow;bad.configuration.values.fireLogicType=2;CHECK(!Bc2PistolSlideRegistrationValid(bad));
    bad=SyntheticRow;bad.configuration.values.boltTime=.1f;CHECK(!Bc2PistolSlideRegistrationValid(bad));
    bad=SyntheticRow;bad.configuration.values.holdBoltUntilFireRelease=true;CHECK(!Bc2PistolSlideRegistrationValid(bad));
    bad=SyntheticRow;bad.configuration.values.baseCapacity=0;CHECK(!Bc2PistolSlideRegistrationValid(bad));
    bad=SyntheticRow;bad.configuration.admission=ReloadDescriptorAdmission::Candidate;CHECK(!Bc2PistolSlideRegistrationValid(bad));
    bad=SyntheticRow;bad.configuration.timing={};CHECK(!Bc2PistolSlideRegistrationValid(bad));
    auto g=SyntheticGeometry;bad=SyntheticRow;bad.geometry=&g;g.rearward={0,0,0};CHECK(!BuildBc2PistolSlideProfile(bad));
    g=SyntheticGeometry;g.rootIndex=g.slideIndex;CHECK(!BuildBc2PistolSlideProfile(bad));
    g=SyntheticGeometry;g.selection.configurationSha256="invalid";CHECK(!BuildBc2PistolSlideProfile(bad));
    g=SyntheticGeometry;g.slideFromAuthoredWrist.values[0][0]=2;CHECK(!BuildBc2PistolSlideProfile(bad));
    g=SyntheticGeometry;g.partBoundsMin[0]=g.partBoundsMax[0];CHECK(!BuildBc2PistolSlideProfile(bad));
    g=SyntheticGeometry;g.maximumDisplacementMeters=std::numeric_limits<double>::infinity();CHECK(!BuildBc2PistolSlideProfile(bad));
    auto policy=SyntheticPolicy;bad=SyntheticRow;bad.interaction=&policy;policy.sourceDisplacementMeters=.04f;
    CHECK(!BuildBc2PistolSlideProfile(bad));
    policy=SyntheticPolicy;policy.strokeMeters=std::numeric_limits<float>::quiet_NaN();CHECK(!BuildBc2PistolSlideProfile(bad));
    policy=SyntheticPolicy;policy.endpointDwellNs=0;CHECK(!BuildBc2PistolSlideProfile(bad));return 0;
}
}
int main(){
    CHECK(ExactConfiguredAssetOnly()==0);
    CHECK(SignedAuthoredRailDoesNotInventNativeStops()==0);
    CHECK(TransientWristAndRingLandmarkStayMeasuredGeometry()==0);
    CHECK(ModPolicyIncludingHysteresisFitsMeasuredRange()==0);
    CHECK(DistinctRowsKeepTheirOwnGeometryPolicyAndConfiguration()==0);
    CHECK(DuplicatesCollisionsAndAmbiguityFailClosed()==0);
    CHECK(InvalidReviewIdentityGeometryAndPolicyCannotEnter()==0);
    std::puts("Bc2PistolSlideProfileTests: 7 registry/selector/measured-geometry/mod-policy groups; synthetic fixture is not enrollment; no native/headset acceptance");
}
