#include "Bc2PistolSlidePresentation.h"
#include "Test.h"
#include <limits>
using namespace fvr;using namespace fvr::bc2;using namespace fvr::interaction;
namespace {
math::Matrix4 Identity(){math::Matrix4 m{};for(unsigned i=0;i<4;++i)m.values[i][i]=1;return m;}
bool Same(const math::Matrix4& a,const math::Matrix4& b,float tolerance=.00001f){
    for(unsigned i=0;i<4;++i)for(unsigned j=0;j<4;++j)if(!Near(a.values[i][j],b.values[i][j],tolerance))return false;
    return true;
}
const auto& Geometry(){return bc2_pistol_slide_profile_detail::Mp443Geometry;}
RigSnapshot Rig(){
    RigSnapshot r;r.identity.soldier=0x10000;r.identity.weak=0x11000;r.identity.count=170;
    r.weaponBone=10;r.names.resize(170);r.parents.assign(170,0);r.parents[0]=-1;
    for(unsigned i=0;i<170;++i)r.names[i]="fixture"+std::to_string(i);
    r.names[10]="jntWpn_1";r.names[30]="jntWpn_3";r.parents[30]=10;
    r.evaluatedWorld.assign(170,Identity());r.inverseBind.assign(170,Identity());return r;
}
Bc2PistolSlideSource Source(){
    Bc2PistolSlideSource s;s.rig=Rig().identity;
    s.input.owner={(std::uint64_t(s.rig.weak)<<32)|s.rig.soldier,5,11,7};
    s.input.sequence=20;s.input.observedNs=1000000000;s.input.deadlineNs=1100000000;s.input.nowNs=1020000000;
    s.input.focused=true;s.input.tracked={true,true};s.weapon={0x13000,11};s.mechanism={900,30};
    s.gunClaim={{40,s.input.owner,InteractionHand::Right,HandClaimKind::GunHold,s.weapon,{901,30},0},
        s.input.deadlineNs,s.input.sequence};
    s.mechanismClaim={{41,s.input.owner,InteractionHand::Left,HandClaimKind::Mechanism,s.weapon,s.mechanism,40},
        s.input.deadlineNs,s.input.sequence};
    s.rawSourceWorldMeters=BuildBc2Mp443ModSlideProfile(Geometry().selection)->closedContact;
    s.sourceFromWristMeters=Identity();s.originalWeaponWorldMeters=Identity();return s;
}
bool Fresh(const Bc2PistolSlideSource& s,std::int64_t now=1020000000){
    return Bc2PistolSlideSourceFresh(s,s.input,s.gunClaim,s.mechanismClaim,s.rig,now);
}
int OriginalInputAndClaimsCannotBeRenewedByNewPackets(){
    const auto s=Source();CHECK(Fresh(s));auto current=s;
    ++current.input.sequence;current.input.observedNs+=10000000;current.input.deadlineNs+=10000000;
    current.gunClaim.inputSequence=current.input.sequence;current.mechanismClaim.inputSequence=current.input.sequence;
    current.gunClaim.deadlineNs=current.input.deadlineNs;current.mechanismClaim.deadlineNs=current.input.deadlineNs;
    CHECK(Bc2PistolSlideSourceFresh(s,current.input,current.gunClaim,current.mechanismClaim,s.rig,1020000000));
    // RenewFrom keeps the mechanism's raw source sequence/deadline while the
    // actual GunHold is renewed by the current safety packet.
    CHECK(Bc2PistolSlideSourceFresh(s,current.input,current.gunClaim,s.mechanismClaim,s.rig,1020000000));
    CHECK(!Bc2PistolSlideSourceFresh(s,current.input,current.gunClaim,current.mechanismClaim,s.rig,s.input.deadlineNs));
    auto bad=current;--bad.input.sequence;CHECK(!Bc2PistolSlideSourceFresh(s,bad.input,bad.gunClaim,bad.mechanismClaim,s.rig,1020000000));
    bad=current;bad.gunClaim.inputSequence=s.input.sequence;
    CHECK(!Bc2PistolSlideSourceFresh(s,bad.input,bad.gunClaim,bad.mechanismClaim,s.rig,1020000000));
    bad=current;bad.input.observedNs=s.input.observedNs;
    CHECK(!Bc2PistolSlideSourceFresh(s,bad.input,bad.gunClaim,bad.mechanismClaim,s.rig,1020000000));
    bad=s;bad.input.deadlineNs++;CHECK(!Fresh(bad)); // Original claims alone cannot disguise an invalid >100ms input.
    bad=s;bad.input.nowNs=999999999;CHECK(!Fresh(bad));
    bad=s;bad.input.focused=false;CHECK(!Fresh(bad));
    bad=s;bad.input.tracked[0]=false;CHECK(!Fresh(bad));
    bad=s;bad.input.released[0]=true;CHECK(!Fresh(bad));
    bad=s;bad.input.released[1]=true;CHECK(!Fresh(bad));return 0;
}
int ActualGunAndMechanismOwnershipAreRequired(){
    const auto s=Source();auto bad=s;
    bad.mechanismClaim={};CHECK(!Fresh(bad));
    bad=s;bad.mechanismClaim.token.kind=HandClaimKind::WeaponSupport;CHECK(!Fresh(bad));
    bad=s;bad.mechanismClaim.token.hand=InteractionHand::Right;CHECK(!Fresh(bad));
    bad=s;++bad.mechanismClaim.token.contact.generation;CHECK(!Fresh(bad));
    bad=s;++bad.mechanismClaim.token.item.generation;CHECK(!Fresh(bad));
    bad=s;++bad.mechanismClaim.token.prerequisiteClaim;CHECK(!Fresh(bad));
    bad=s;bad.mechanismClaim.deadlineNs=s.input.deadlineNs+1;CHECK(!Fresh(bad));
    bad=s;bad.gunClaim.deadlineNs=s.input.nowNs;CHECK(!Fresh(bad));
    bad=s;bad.mechanismClaim.token.id=bad.gunClaim.token.id;CHECK(!Fresh(bad));
    bad=s;++bad.input.owner.space;CHECK(!Fresh(bad));
    bad=s;++bad.rig.soldier;CHECK(!Fresh(bad));
    auto rig=s.rig;++rig.pose;
    CHECK(!Bc2PistolSlideSourceFresh(s,s.input,s.gunClaim,s.mechanismClaim,rig,s.input.nowNs));
    auto newClaim=s.mechanismClaim;++newClaim.token.id;
    CHECK(!Bc2PistolSlideSourceFresh(s,s.input,s.gunClaim,newClaim,s.rig,s.input.nowNs));return 0;
}
int RawControllerBasisAndOffAxisMotionArePreserved(){
    auto s=Source();const auto expected=s.rawSourceWorldMeters;
    s.sourceFromWristMeters.values[3][0]=.02f;
    s.sourceFromWristMeters.values[3][2]=-.03f;
    // Hand-authored 90-degree Y rotation and translation in the weapon basis.
    s.originalWeaponWorldMeters=bc2_pistol_slide_profile_detail::Frame({0,0,-1,0, 0,1,0,0, 1,0,0,0, 2,3,4,1});
    s.rawSourceWorldMeters=feed_interaction_detail::Multiply(
        feed_interaction_detail::Multiply(feed_interaction_detail::Inverse(s.sourceFromWristMeters),expected),
        s.originalWeaponWorldMeters);
    const auto local=Bc2PistolSlideRawWristInWeapon(s);CHECK(local&&Same(*local,expected));
    const auto rawBefore=s.rawSourceWorldMeters;const auto weaponBefore=s.originalWeaponWorldMeters;
    const auto offsetBefore=s.sourceFromWristMeters;
    const auto frames=ComposeBc2PistolSlideWorldFrames(Geometry().selection,s,s.originalWeaponWorldMeters,.04f,100,s.input.nowNs);CHECK(frames);
    CHECK(Near(frames->slide.values[3][0],195.264680028f,.0001f));
    CHECK(Near(frames->slide.values[3][1],308.023967594f,.0001f));
    CHECK(Near(frames->slide.values[3][2],400.f,.0001f));
    CHECK(Near(frames->authoredWrist.values[3][0],207.325760413f,.0001f));
    CHECK(Near(frames->authoredWrist.values[3][1],305.538167368f,.0001f));
    CHECK(Near(frames->authoredWrist.values[3][2],390.170163934f,.0001f));
    CHECK(frames->slide.values[0][2]==-1&&frames->slide.values[2][0]==1);
    CHECK(Same(rawBefore,s.rawSourceWorldMeters)&&Same(weaponBefore,s.originalWeaponWorldMeters)&&
        Same(offsetBefore,s.sourceFromWristMeters));
    // +world X is +weapon Z; +world Y remains off-axis. Neither is clamped or
    // projected away before the actual recognizer receives the raw contact.
    s.rawSourceWorldMeters.values[3][0]+=.04f;s.rawSourceWorldMeters.values[3][1]+=.08f;
    const auto offAxis=Bc2PistolSlideRawWristInWeapon(s);CHECK(offAxis);
    CHECK(Near(offAxis->values[3][2]-expected.values[3][2],.04f));
    CHECK(Near(offAxis->values[3][1]-expected.values[3][1],.08f));return 0;
}
int NonRigidSourcesAndInvalidNativeScaleFailClosed(){
    const auto s=Source();
    for(unsigned i=0;i<3;++i){
        auto bad=s;auto* matrix=i==0?&bad.rawSourceWorldMeters:i==1?&bad.sourceFromWristMeters:&bad.originalWeaponWorldMeters;
        for(unsigned j=0;j<3;++j)matrix->values[0][j]*=2;
        CHECK(!Fresh(bad)&&!Bc2PistolSlideRawWristInWeapon(bad));
        CHECK(!ComposeBc2PistolSlideWorldFrames(Geometry().selection,bad,bad.originalWeaponWorldMeters,0,1,s.input.nowNs));
    }
    auto bad=s;bad.originalWeaponWorldMeters.values[0][0]=-1;CHECK(!Fresh(bad));
    bad=s;bad.rawSourceWorldMeters.values[3][0]=std::numeric_limits<float>::quiet_NaN();CHECK(!Fresh(bad));
    for(float scale:{0.f,-1.f,std::numeric_limits<float>::infinity()})
        CHECK(!ComposeBc2PistolSlideWorldFrames(Geometry().selection,s,s.originalWeaponWorldMeters,0,scale,s.input.nowNs));
    bad=s;bad.originalWeaponWorldMeters.values[3][0]=100;
    CHECK(!ComposeBc2PistolSlideWorldFrames(Geometry().selection,bad,bad.originalWeaponWorldMeters,0,std::numeric_limits<float>::max(),s.input.nowNs));
    CHECK(!ComposeBc2PistolSlideWorldFrames(Geometry().selection,s,s.originalWeaponWorldMeters,.073f,1,s.input.nowNs));
    CHECK(!ComposeBc2PistolSlideWorldFrames(Geometry().selection,s,s.originalWeaponWorldMeters,0,1,s.input.deadlineNs));
    auto wrong=Geometry().selection;wrong.configurationPath={};
    CHECK(!ComposeBc2PistolSlideWorldFrames(wrong,s,s.originalWeaponWorldMeters,0,1,s.input.nowNs));return 0;
}
int StructuralFixtureNeverGrantsProductionBinding(){
    using namespace bc2_pistol_slide_presentation_detail;
    auto r=Rig();CHECK(Topology(r,Geometry()));CHECK(!BindBc2PistolSlide(r,Geometry().selection));
    const Bc2PistolSlideBinding forged{r.identity,Geometry().selection.rigFingerprint,30};
    CHECK(!BuildBc2PistolSlideWrite(forged,r,Identity()));
    r.parents[30]=0;CHECK(!Topology(r,Geometry()));r=Rig();
    r.parents[31]=30;CHECK(!Topology(r,Geometry()));r=Rig();
    r.names[31]="jntWpn_3";CHECK(!Topology(r,Geometry()));r=Rig();
    r.names[31]="jntWpn_1";CHECK(!Topology(r,Geometry()));r=Rig();
    r.nativeHiddenLeaves={30};CHECK(!Topology(r,Geometry()));r=Rig();
    r.parents[50]=51;r.parents[51]=50;CHECK(!Topology(r,Geometry()));r=Rig();
    r.parents[50]=171;CHECK(!Topology(r,Geometry()));r=Rig();
    r.inverseBind.pop_back();CHECK(!Topology(r,Geometry()));return 0;
}
int LeafTargetIsIndependentOfPreviousOverrides(){
    using namespace bc2_pistol_slide_presentation_detail;
    auto r=Rig();r.evaluatedWorld[10].values[3][0]=8;r.evaluatedWorld[42].values[3][2]=.7f;
    const auto source=Source();const auto frames=ComposeBc2PistolSlideWorldFrames(Geometry().selection,source,source.originalWeaponWorldMeters,.04f,1,source.input.nowNs);
    CHECK(frames);const auto before=r.evaluatedWorld;
    const auto first=LeafWrite(r,Geometry(),frames->slide);CHECK(first&&first->index==30&&Same(first->transform,frames->slide));
    for(unsigned i=0;i<before.size();++i)CHECK(Same(before[i],r.evaluatedWorld[i]));
    // A dirty prior leaf, changed native recoil and unrelated animation do not
    // feed back into the absolute accepted target. This is not a restore test.
    r.evaluatedWorld[30]={};r.evaluatedWorld[10].values[3][0]=20;r.evaluatedWorld[42].values[3][2]=-.4f;
    const auto second=LeafWrite(r,Geometry(),frames->slide);CHECK(second&&Same(first->transform,second->transform));
    CHECK(r.evaluatedWorld[10].values[3][0]==20&&r.evaluatedWorld[42].values[3][2]==-.4f);
    auto nonrigid=frames->slide;nonrigid.values[1][1]=2;CHECK(!LeafWrite(r,Geometry(),nonrigid));return 0;
}
int PlacedWeaponMovesAttachedSlideWithoutChangingGestureSource(){
    const auto source=Source();const auto raw=Bc2PistolSlideRawWristInWeapon(source);CHECK(raw);
    auto placed=bc2_pistol_slide_profile_detail::Frame({0,0,-1,0, 0,1,0,0, 1,0,0,0, 2,3,4,1});
    const auto frames=ComposeBc2PistolSlideWorldFrames(Geometry().selection,source,placed,.04f,100,source.input.nowNs);
    CHECK(frames);
    CHECK(Near(frames->slide.values[3][0],195.264680028f,.0001f));
    CHECK(Near(frames->slide.values[3][1],308.023967594f,.0001f));
    CHECK(Near(frames->slide.values[3][2],400.f,.0001f));
    CHECK(Near(frames->authoredWrist.values[3][0],207.325760413f,.0001f));
    CHECK(Same(*Bc2PistolSlideRawWristInWeapon(source),*raw));
    CHECK(Same(source.originalWeaponWorldMeters,Identity()));
    CHECK(placed.values[3][0]==2&&frames->slide.values[0][2]==-1);
    placed.values[0][2]=-2;
    CHECK(!ComposeBc2PistolSlideWorldFrames(Geometry().selection,source,placed,.04f,100,source.input.nowNs));
    placed=Identity();placed.values[3][1]=std::numeric_limits<float>::quiet_NaN();
    CHECK(!ComposeBc2PistolSlideWorldFrames(Geometry().selection,source,placed,0,1,source.input.nowNs));return 0;
}
int NewSourceConfirmsOriginalLeaseWithoutRestampingIt(){
    const auto original=Source();auto current=original;
    ++current.input.sequence;current.input.observedNs+=10000000;current.input.deadlineNs+=10000000;
    current.input.nowNs+=10000000;current.gunClaim.inputSequence=current.input.sequence;
    current.mechanismClaim.inputSequence=current.input.sequence;
    current.gunClaim.deadlineNs=current.mechanismClaim.deadlineNs=current.input.deadlineNs;
    current.rawSourceWorldMeters.values[3][2]+=.01f;
    const auto retained=[&](const Bc2PistolSlideSource& next,std::int64_t now){
        return Bc2PistolSlideSourceRetained(original,next,next.input,next.gunClaim,next.mechanismClaim,next.rig,now);};
    CHECK(retained(current,current.input.nowNs));
    CHECK(!retained(current,original.input.deadlineNs));
    auto bad=current;++bad.mechanismClaim.token.id;CHECK(!retained(bad,current.input.nowNs));
    bad=current;bad.input.released[0]=true;CHECK(!retained(bad,current.input.nowNs));
    bad=current;++bad.rig.pose;CHECK(!retained(bad,current.input.nowNs));
    bad=current;bad.input.observedNs=original.input.observedNs;CHECK(!retained(bad,current.input.nowNs));
    bad=original;bad.input.nowNs+=1000000;CHECK(retained(bad,bad.input.nowNs));
    bad.rawSourceWorldMeters.values[3][2]+=.01f;CHECK(!retained(bad,bad.input.nowNs));
    bad=original;--bad.input.deadlineNs;bad.gunClaim.deadlineNs=bad.mechanismClaim.deadlineNs=bad.input.deadlineNs;
    CHECK(!retained(bad,bad.input.nowNs));return 0;
}
ReloadStateOwner Owner(const Bc2PistolSlideSource& s){
    return {0x12000,s.rig.soldier,s.rig.weak,unsigned(s.weapon.id),s.input.owner.actorGeneration,11,s.input.owner.space};
}
int TypedTargetCannotBorrowAnotherFeedClaimOrGeometry(){
    const auto s=Source();const auto owner=Owner(s);const auto profile=BuildBc2Mp443ModSlideProfile(Geometry().selection);
    PistolAfterFeedTarget target;target.context={{owner.soldier,owner.actorGeneration,owner.weapon,s.mechanism.generation},owner.equipGeneration,owner.space};
    target.feedRequest=50;target.inputSequence=s.input.sequence;target.mechanism=s.mechanismClaim.token;target.gun=s.gunClaim.token;
    target.contact=profile->closedContact;target.travel=.04f;target.contact.values[3][2]+=.04f;
    target.observedNs=s.input.observedNs;target.deadlineNs=s.input.deadlineNs;
    const auto valid=[&](const PistolAfterFeedTarget& t){return Bc2PistolSlideTargetFresh(s,owner,Geometry().selection,t,.04f,s.input.nowNs);};
    CHECK(valid(target));auto bad=target;++bad.context.resource.weaponGeneration;CHECK(!valid(bad));
    bad=target;bad.feedRequest=0;CHECK(!valid(bad));bad=target;++bad.inputSequence;CHECK(!valid(bad));
    bad=target;++bad.gun.id;CHECK(!valid(bad));bad=target;++bad.mechanism.id;CHECK(!valid(bad));
    bad=target;++bad.deadlineNs;CHECK(!valid(bad));bad=target;++bad.observedNs;CHECK(!valid(bad));
    bad=target;bad.contact.values[3][0]+=.01f;CHECK(!valid(bad));bad=target;bad.travel=.05f;CHECK(!valid(bad));
    auto current=target;++current.inputSequence;current.observedNs+=1000000;current.deadlineNs+=1000000;
    current.travel=.05f;current.contact.values[3][2]+=.01f;
    CHECK(Bc2PistolSlideTargetRetained(target,current));
    ++current.feedRequest;CHECK(!Bc2PistolSlideTargetRetained(target,current));
    current=target;++current.context.resource.weaponGeneration;CHECK(!Bc2PistolSlideTargetRetained(target,current));
    current=target;++current.mechanism.id;CHECK(!Bc2PistolSlideTargetRetained(target,current));
    current=target;--current.inputSequence;CHECK(!Bc2PistolSlideTargetRetained(target,current));
    CHECK(!Bc2PistolSlideTargetFresh(s,owner,Geometry().selection,target,.04f,s.input.deadlineNs));return 0;
}
template<std::size_t N> void Text(std::array<char,N>& out,std::string_view text){std::copy(text.begin(),text.end(),out.begin());}
int SelectedConfigurationAndOriginalLeaseMustSurvive(){
    const auto source=Source();const auto owner=Owner(source);const auto& selection=Geometry().selection;
    WeaponEquipmentIdentity equipment;equipment.weapon=owner.weapon;equipment.data=0x14000;Text(equipment.asset,selection.asset);
    SelectedMeshesSnapshot selected;selected.owner=owner;selected.sequence=source.input.sequence;
    selected.observedNs=source.input.observedNs;selected.deadlineNs=source.input.deadlineNs;
    selected.weaponData=equipment.data;selected.inventory=0x15000;selected.stateTypeInfo=0x16000;selected.meshTypeInfo=0x17000;
    selected.configurationPathVerified=true;selected.configurationPathPointer=0x18000;
    Text(selected.configurationPath,selection.configurationPath);Text(selected.weaponName,selection.asset);
    selected.stateCount=1;selected.soleConfiguredArray=0x19000;auto& state=selected.states[0];
    state.state=0x20000;state.array=selected.soleConfiguredArray;state.count=1;
    state.meshes[0].address=0x21000;state.meshes[0].typeInfo=selected.meshTypeInfo;state.meshes[0].namePointer=0x22000;
    Text(state.meshes[0].assetPath,selection.meshPath);
    const auto valid=[&](const SelectedMeshesSnapshot& s){return Bc2PistolSlideSelected(s,owner,selection,equipment,21,source.input.nowNs);};
    CHECK(valid(selected));auto bad=selected;bad.configurationPathVerified=false;CHECK(!valid(bad));
    bad=selected;bad.configurationPath[0]='X';CHECK(!valid(bad));bad=selected;bad.stateCount=2;CHECK(!valid(bad));
    bad=selected;bad.states[0].count=2;bad.states[0].meshes[1]=bad.states[0].meshes[0];CHECK(!valid(bad));
    bad=selected;bad.states[0].meshes[0].namePointer=0;CHECK(!valid(bad));
    auto current=selected;++current.sequence;current.observedNs+=1000000;current.deadlineNs+=1000000;
    CHECK(Bc2PistolSlideSelectedRetained(selected,current,owner,selection,equipment,21,source.input.nowNs));
    CHECK(!Bc2PistolSlideSelectedRetained(selected,current,owner,selection,equipment,21,selected.deadlineNs));
    ++current.configurationPathPointer;
    CHECK(!Bc2PistolSlideSelectedRetained(selected,current,owner,selection,equipment,21,source.input.nowNs));return 0;
}
int SyntheticRowOwnsAxisWristTopologyAndBindingKey(){
    // Artificial geometry proves the shared code consumes the selected row.
    // None of these names, positions or native values enroll a real weapon.
    auto rig=Rig();rig.identity.count=6;rig.weaponBone=2;
    rig.names={"fixture0","fixture1","fixtureRoot","fixture3","fixtureSlide","fixture5"};
    rig.parents={-1,0,0,0,2,0};rig.evaluatedWorld.assign(6,Identity());rig.inverseBind.assign(6,Identity());
    Bc2PistolSlideGeometry geometry{
        {"SyntheticPistol2","fixture/pistol/second","22222222-2222-2222-2222-222222222222",
         "2222222222222222222222222222222222222222222222222222222222222222","fixture/mesh/second",
         SightRigFingerprint(rig.names,rig.parents,rig.inverseBind)},
        "fixture/geometry","3333333333333333333333333333333333333333333333333333333333333333",
        "fixture/roles","4444444444444444444444444444444444444444444444444444444444444444",
        "fixtureRoot","fixtureSlide",2,4,
        bc2_pistol_slide_profile_detail::Frame({1,0,0,0,0,1,0,0,0,0,1,0,.2f,.1f,-.3f,1}),
        bc2_pistol_slide_profile_detail::Frame({1,0,0,0,0,1,0,0,0,0,1,0,.01f,.02f,.03f,1}),
        {1,0,0},{-.1f,-.1f,-.1f},{.1f,.1f,.1f},{.01f,0,0},{0,0,0},-.005,.055,1.5,.01,false,false,false};
    const Bc2PistolSlideInteractionPolicy policy{
        "Synthetic second policy","fixture/contact",0x53594e5448455432ull,2,
        .002f,.04f,.001f,.002f,.0005f,.02f,.015f,.25f,.02f,20000000,5000000000ll};
    const std::array<ReloadTimingWord,6> timing{{{0x10,std::bit_cast<std::uint32_t>(.4f)},{0x14,0},
        {0x18,std::bit_cast<std::uint32_t>(2.f)},{0x20,1},{0x24,0},{0x2c,0}}};
    const Bc2PistolSlideRegistration row{
        0x2222222222222222ull,"2222222222222222222222222222222222222222222222222222222222222222",
        &geometry,&policy,{"SyntheticPistol2","fixture/pistol/second",{0,1,8,29,9,2,0,2,.4f,0,0,0,false,false},
            timing,ReloadDescriptorAdmission::ReviewedNative},Bc2PistolSlideNativeClass::SingleFireMagazineZeroBolt,true,true};
    const std::array rows{bc2_pistol_slide_profile_detail::Mp443Registration,row};
    const auto* selected=FindBc2PistolSlideRegistration(rows,geometry.selection);CHECK(selected==&rows[1]);
    const auto profile=BuildBc2PistolSlideProfile(*selected);CHECK(profile);
    auto source=Source();source.rig=rig.identity;source.mechanism.id=profile->id;
    source.mechanismClaim.token.contact=source.mechanism;source.rawSourceWorldMeters=profile->closedContact;
    const auto originalRaw=source.rawSourceWorldMeters;
    CHECK(Fresh(source));const auto binding=BindBc2PistolSlide(rig,*selected);CHECK(binding);
    CHECK(binding->leaf==4&&binding->profileKey==row.key&&binding->fingerprint==geometry.selection.rigFingerprint);
    CHECK(!BindBc2PistolSlide(rig,Geometry().selection));
    auto placed=Identity();placed.values[3][0]=2;placed.values[3][1]=3;placed.values[3][2]=4;
    const auto frames=ComposeBc2PistolSlideWorldFrames(*selected,source,placed,.032f,100,source.input.nowNs);CHECK(frames);
    CHECK(Near(frames->slide.values[3][0],223.2f,.0001f)&&Near(frames->slide.values[3][1],310.f,.0001f)&&
        Near(frames->slide.values[3][2],370.f,.0001f));
    CHECK(Near(frames->authoredWrist.values[3][0],224.2f,.0001f)&&Near(frames->authoredWrist.values[3][1],312.f,.0001f)&&
        Near(frames->authoredWrist.values[3][2],373.f,.0001f));
    CHECK(Same(source.rawSourceWorldMeters,originalRaw));
    const auto before=rig.evaluatedWorld;const auto write=BuildBc2PistolSlideWrite(*selected,*binding,rig,frames->slide);
    CHECK(write&&write->index==4&&Same(write->transform,frames->slide));
    for(std::size_t i=0;i<before.size();++i)CHECK(Same(before[i],rig.evaluatedWorld[i]));
    CHECK(!BuildBc2PistolSlideWrite(rows[0],*binding,rig,frames->slide));
    // The production resolver cannot silently enroll this fixture's key.
    CHECK(!BuildBc2PistolSlideWrite(*binding,rig,frames->slide));
    auto changed=*binding;++changed.profileKey;CHECK(!BuildBc2PistolSlideWrite(*selected,changed,rig,frames->slide));
    const auto owner=Owner(source);PistolAfterFeedTarget target;
    target.context={{owner.soldier,owner.actorGeneration,owner.weapon,source.mechanism.generation},owner.equipGeneration,owner.space};
    target.feedRequest=50;target.inputSequence=source.input.sequence;target.mechanism=source.mechanismClaim.token;
    target.gun=source.gunClaim.token;target.contact=profile->closedContact;target.travel=.03f;target.contact.values[3][0]+=.03f;
    target.observedNs=source.input.observedNs;target.deadlineNs=source.input.deadlineNs;
    const float displacement=policy.sourceDisplacementMeters+target.travel;
    CHECK(Bc2PistolSlideTargetFresh(source,owner,*selected,target,displacement,source.input.nowNs));
    CHECK(!Bc2PistolSlideTargetFresh(source,owner,rows[0],target,displacement,source.input.nowNs));
    CHECK(!Bc2PistolSlideTargetFresh(source,owner,*selected,target,target.travel,source.input.nowNs));
    rig.parents[4]=0;CHECK(!BindBc2PistolSlide(rig,*selected));
    CHECK(!BuildBc2PistolSlideWrite(*selected,*binding,rig,frames->slide));return 0;
}
}
int main(){
    CHECK(OriginalInputAndClaimsCannotBeRenewedByNewPackets()==0);
    CHECK(ActualGunAndMechanismOwnershipAreRequired()==0);
    CHECK(RawControllerBasisAndOffAxisMotionArePreserved()==0);
    CHECK(NonRigidSourcesAndInvalidNativeScaleFailClosed()==0);
    CHECK(StructuralFixtureNeverGrantsProductionBinding()==0);
    CHECK(LeafTargetIsIndependentOfPreviousOverrides()==0);
    CHECK(PlacedWeaponMovesAttachedSlideWithoutChangingGestureSource()==0);
    CHECK(NewSourceConfirmsOriginalLeaseWithoutRestampingIt()==0);
    CHECK(TypedTargetCannotBorrowAnotherFeedClaimOrGeometry()==0);
    CHECK(SelectedConfigurationAndOriginalLeaseMustSurvive()==0);
    CHECK(SyntheticRowOwnsAxisWristTopologyAndBindingKey()==0);
    std::puts("Bc2PistolSlidePresentationTests: 11 source/custody/selection/geometry/scale/leaf groups; synthetic fixture is not enrollment; no native/headset acceptance");
}
