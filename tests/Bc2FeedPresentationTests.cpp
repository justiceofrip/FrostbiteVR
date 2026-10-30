#include "Bc2FeedPresentation.h"
#include "Test.h"
#include <limits>
using namespace fvr;using namespace fvr::bc2;using namespace fvr::interaction;
namespace {
math::Matrix4 Identity(){math::Matrix4 m{};for(unsigned i=0;i<4;++i)m.values[i][i]=1;return m;}
bool Same(const math::Matrix4& a,const math::Matrix4& b,float tolerance=.00001f){
    for(unsigned i=0;i<4;++i)for(unsigned j=0;j<4;++j)if(!Near(a.values[i][j],b.values[i][j],tolerance))return false;
    return true;
}
const auto& Geometry(){return bc2_feed_profile_detail::M60Geometry;}
Bc2M60FeedPresentation Rest(){return *ComposeBc2M60FeedPresentation({{1,2},{3,4}},Geometry().poses[0]);}
std::array<math::Matrix4,6> Bases(){std::array<math::Matrix4,6> a;for(auto& m:a)m=Identity();return a;}
RigSnapshot Rig(){
    RigSnapshot r;r.identity.soldier=0x10000;r.identity.weak=0x11000;r.identity.count=170;
    r.names.resize(170);r.parents.resize(170,0);r.parents[0]=-1;r.weaponBone=10;
    r.evaluatedWorld.assign(170,Identity());r.inverseBind.assign(170,Identity());
    for(unsigned i=0;i<170;++i)r.names[i]="fixture"+std::to_string(i);
    r.names[10]="jntWpn_1";r.names[41]="jntWpn_6";r.names[34]="jntWpn_9";
    r.names[38]="jntWpn_12";r.names[100]="jntWpn_13";r.names[114]="jntWpnwpnJnt_14";r.names[116]="jntWpn_15";
    for(auto i:{41,34,38})r.parents[i]=10;
    r.parents[100]=38;r.parents[114]=100;r.parents[116]=114;r.parents[117]=116;
    r.evaluatedWorld[117].values[3][1]=.03f;return r;
}
template<std::size_t N> void Text(std::array<char,N>& out,std::string_view value){
    out={};std::copy(value.begin(),value.end(),out.begin());
}
Bc2PhysicalFeedPresentation Physical(){
    Bc2PhysicalFeedPresentation p;p.owner={0x12000,0x10000,0x11000,0x13000,5,6,7};p.rig=Rig().identity;
    p.input.owner={(std::uint64_t(p.owner.weak)<<32)|p.owner.soldier,5,11,7};
    p.input.sequence=20;p.input.observedNs=1000000000;p.input.deadlineNs=1100000000;p.input.nowNs=1020000000;
    p.input.focused=true;p.input.tracked={true,true};p.weapon={p.owner.weapon,11};
    p.supply=Rest().supply;p.lifetime=30;p.revision=1;p.rigFingerprint=Geometry().rigFingerprint;
    p.weaponFromPouch=Rest().weaponFromPouch;p.weaponFromChain=Rest().weaponFromChain;
    p.weaponFromCoverHinge=Geometry().weaponFromHinge;p.originalWeaponWorldMeters=Identity();return p;
}
WeaponEquipmentIdentity Equipment(){
    WeaponEquipmentIdentity e;e.weapon=0x13000;e.data=0x14000;e.persistence=0x15000;Text(e.asset,Geometry().asset);return e;
}
SelectedMeshesSnapshot Selected(){
    SelectedMeshesSnapshot s;s.owner=Physical().owner;s.sequence=20;s.observedNs=1000000000;s.deadlineNs=1200000000;
    s.weaponData=Equipment().data;s.inventory=0x16000;s.stateTypeInfo=0x17000;s.meshTypeInfo=0x18000;
    s.configurationPathVerified=true;s.configurationPathPointer=0x19000;s.stateCount=1;s.soleConfiguredArray=0x1a000;
    Text(s.weaponName,Geometry().asset);Text(s.configurationPath,Geometry().configurationPath);
    auto& state=s.states[0];state.array=s.soleConfiguredArray;state.state=0x1b000;state.count=1;
    state.meshes[0].address=0x1c000;state.meshes[0].typeInfo=s.meshTypeInfo;state.meshes[0].namePointer=0x1d000;
    Text(state.meshes[0].assetPath,Geometry().meshPath);return s;
}
void Claim(Bc2PhysicalFeedPresentation& p){
    p.mechanismClaim=HandClaim{{40,p.input.owner,InteractionHand::Left,HandClaimKind::Mechanism,
        p.weapon,{902,p.lifetime},39},p.input.deadlineNs,p.input.sequence};
    p.mechanismWristWorldMeters=Identity();p.operation=FeedPhysicalOperation::DetachContainer;
    p.targetKind=FeedInteractionTargetKind::Object;
}
int MeasuredCoverFrameIsNotTheHingeFrame(){
    const auto rest=Bc2FeedPartWorldFrames(Rest(),Geometry().weaponFromHinge,Bases(),1);CHECK(rest);
    CHECK(Same((*rest)[1],feed_presentation_detail::CoverReference));
    CHECK(!Same((*rest)[1],Geometry().weaponFromHinge));
    const auto opened=feed_interaction_detail::Multiply(
        feed_interaction_detail::Along(Identity(),FeedMotionKind::Hinge,Geometry().coverTravelRadians),Geometry().weaponFromHinge);
    const auto parts=Bc2FeedPartWorldFrames(Rest(),opened,Bases(),1);CHECK(parts);
    CHECK(Near((*parts)[1].values[3][1],.0745346174f));
    CHECK(Near((*parts)[1].values[3][2],-.4183450341f));
    CHECK(std::abs((*parts)[1].values[1][2])>.99f);return 0;
}
int CarriedWorldBasisIsIndependentOfNewWeapon(){
    auto basis=Bases();for(auto& b:basis)b.values[3][0]=2;
    basis[0].values[3][0]=1;for(unsigned i=2;i<6;++i)basis[i].values[3][0]=1;
    const auto a=Bc2FeedPartWorldFrames(Rest(),Geometry().weaponFromHinge,basis,100);CHECK(a);
    basis[1].values[3][0]=5;
    const auto b=Bc2FeedPartWorldFrames(Rest(),Geometry().weaponFromHinge,basis,100);CHECK(b);
    CHECK(Same((*a)[0],(*b)[0]));for(unsigned i=2;i<6;++i)CHECK(Same((*a)[i],(*b)[i]));
    CHECK(Near((*b)[1].values[3][0]-(*a)[1].values[3][0],300));
    CHECK(Near((*a)[0].values[3][0],(1+Rest().weaponFromPouch.values[3][0])*100));return 0;
}
int ArticulatedSubtreesPreserveUnownedAnimation(){
    auto rig=Rig();const auto binding=feed_presentation_detail::Derive(rig);CHECK(binding);
    const auto pose=*ComposeBc2M60FeedPresentation({{1,2},{3,4}},Geometry().poses[2]);
    const auto world=Bc2FeedPartWorldFrames(pose,Geometry().weaponFromHinge,Bases(),1);CHECK(world);
    const auto writes=BuildBc2FeedPartWrites(*binding,rig,*world);CHECK(writes&&writes->size()==7);
    for(unsigned i=0;i<6;++i){
        const auto w=std::find_if(writes->begin(),writes->end(),[&](const auto& x){return x.index==binding->parts[i];});
        CHECK(w!=writes->end()&&Same(w->transform,(*world)[i]));
    }
    CHECK(std::none_of(writes->begin(),writes->end(),[](const auto& w){return w.index==10||w.index==1;}));
    const auto child=std::find_if(writes->begin(),writes->end(),[](const auto& w){return w.index==117;});
    CHECK(child!=writes->end());
    const auto local=feed_interaction_detail::Multiply(child->transform,feed_interaction_detail::Inverse((*world)[5]));
    CHECK(Same(local,rig.evaluatedWorld[117]));return 0;
}
int StructuralFixturesNeverGrantProductionBinding(){
    auto r=Rig();CHECK(feed_presentation_detail::Derive(r));
    CHECK(!BindBc2FeedParts(r,Geometry().asset,Geometry().configurationPath,Geometry().meshPath));
    r.parents[114]=10;CHECK(!feed_presentation_detail::Derive(r));r=Rig();
    r.names[150]="jntWpn_9";CHECK(!feed_presentation_detail::Derive(r));r=Rig();
    r.nativeHiddenLeaves={41};CHECK(!feed_presentation_detail::Derive(r));return 0;
}
int InvalidGeometryAndChangedRigFailClosed(){
    const auto basis=Bases();CHECK(!Bc2FeedPartWorldFrames(Rest(),Geometry().weaponFromHinge,basis,0));
    auto bad=Rest();bad.weaponFromChain[3].values[0][0]=std::numeric_limits<float>::quiet_NaN();
    CHECK(!Bc2FeedPartWorldFrames(bad,Geometry().weaponFromHinge,basis,1));
    auto r=Rig();const auto binding=*feed_presentation_detail::Derive(r);
    const auto world=*Bc2FeedPartWorldFrames(Rest(),Geometry().weaponFromHinge,basis,1);
    ++r.identity.pose;CHECK(!BuildBc2FeedPartWrites(binding,r,world));return 0;
}
int OriginalRawLeaseAndActualHandOwnerRemainRequired(){
    const auto p=Physical();auto current=p.input;current.sequence++;current.observedNs+=10000000;current.deadlineNs+=10000000;
    CHECK(Bc2FeedSourceFresh(p,p.owner,current,Equipment(),p.rig,1020000000));
    // The native selection epoch is deliberately distinct from the actual hand epoch.
    CHECK(p.owner.equipGeneration!=current.owner.equipGeneration);
    auto bad=current;bad.owner.equipGeneration=p.owner.equipGeneration;
    CHECK(!Bc2FeedSourceFresh(p,p.owner,bad,Equipment(),p.rig,1020000000));
    bad=current;bad.sequence=p.input.sequence-1;CHECK(!Bc2FeedSourceFresh(p,p.owner,bad,Equipment(),p.rig,1020000000));
    CHECK(!Bc2FeedSourceFresh(p,p.owner,current,Equipment(),p.rig,p.input.deadlineNs));
    auto owner=p.owner;++owner.equipGeneration;CHECK(!Bc2FeedSourceFresh(p,owner,current,Equipment(),p.rig,1020000000));
    auto rig=p.rig;++rig.pose;CHECK(!Bc2FeedSourceFresh(p,p.owner,current,Equipment(),rig,1020000000));
    auto stale=p;stale.input.deadlineNs=stale.input.observedNs+100000001;
    CHECK(!Bc2FeedSourceFresh(stale,p.owner,current,Equipment(),p.rig,1020000000));return 0;
}
int ExactSelectedConfigurationAndOriginalLeaseAreRequired(){
    const auto p=Physical();const auto old=Selected();auto current=old;
    current.sequence++;current.observedNs+=10000000;current.deadlineNs+=10000000;
    CHECK(Bc2FeedSelectedRetained(old,current,p,Equipment(),21,1020000000));
    auto bad=current;bad.configurationPathVerified=false;CHECK(!Bc2FeedSelected(bad,p,Equipment(),21,1020000000));
    bad=current;bad.configurationPath[0]='X';CHECK(!Bc2FeedSelected(bad,p,Equipment(),21,1020000000));
    bad=current;bad.states[0].count=2;bad.states[0].meshes[1]=bad.states[0].meshes[0];
    CHECK(!Bc2FeedSelected(bad,p,Equipment(),21,1020000000));
    bad=current;++bad.states[0].meshes[0].address;CHECK(!Bc2FeedSelectedRetained(old,bad,p,Equipment(),21,1020000000));
    bad=current;++bad.configurationPathPointer;CHECK(!Bc2FeedSelectedRetained(old,bad,p,Equipment(),21,1020000000));
    CHECK(!Bc2FeedSelectedRetained(old,current,p,Equipment(),21,old.deadlineNs));
    CHECK(!Bc2FeedSelected(current,p,Equipment(),20,1020000000));return 0;
}
int RealMechanismClaimAndPairRetentionAreRequired(){
    auto p=Physical();p.mechanismWristWorldMeters=Identity();CHECK(!Bc2FeedMechanismClaimFresh(p,1020000000));
    Claim(p);CHECK(Bc2FeedMechanismClaimFresh(p,1020000000));
    auto current=p;++current.revision;++current.input.sequence;current.input.observedNs+=10000000;current.input.deadlineNs+=10000000;
    current.mechanismClaim->inputSequence=current.input.sequence;current.mechanismClaim->deadlineNs=current.input.deadlineNs;
    CHECK(Bc2FeedPresentationRetained(p,current,true,1020000000));
    auto bad=current;bad.mechanismClaim.reset();CHECK(!Bc2FeedPresentationRetained(p,bad,true,1020000000));
    CHECK(Bc2FeedPresentationRetained(p,bad,false,1020000000)); // Rest geometry needs no hand claim.
    bad=current;++bad.mechanismClaim->token.id;CHECK(!Bc2FeedPresentationRetained(p,bad,true,1020000000));
    bad=current;bad.mechanismClaim->token.hand=InteractionHand::Right;CHECK(!Bc2FeedMechanismClaimFresh(bad,1020000000));
    bad=current;++bad.mechanismClaim->token.item.generation;CHECK(!Bc2FeedMechanismClaimFresh(bad,1020000000));
    bad=current;bad.input.released[0]=true;CHECK(!Bc2FeedMechanismClaimFresh(bad,1020000000));
    bad=current;++bad.supply.container.generation;CHECK(!Bc2FeedPresentationRetained(p,bad,false,1020000000));
    bad=current;bad.carriedPartsMask=1;CHECK(!Bc2FeedPresentationRetained(p,bad,false,1020000000));
    CHECK(!Bc2FeedPresentationRetained(p,current,true,p.input.deadlineNs));return 0;
}
int CarryMaskSelectsSeparateRetainedWorldBases(){
    auto p=Physical();p.carriedPartsMask=61;p.pouchCarryWeaponWorldMeters=Identity();p.chainCarryWeaponWorldMeters=Identity();
    p.pouchCarryWeaponWorldMeters.values[3][0]=3;p.chainCarryWeaponWorldMeters.values[3][0]=4;
    auto placed=Identity();placed.values[3][0]=9;
    const auto bases=Bc2FeedWorldBases(p,placed);CHECK(bases);
    CHECK((*bases)[0].values[3][0]==3&&(*bases)[1].values[3][0]==9);
    for(unsigned i=2;i<6;++i)CHECK((*bases)[i].values[3][0]==4);
    p.carriedPartsMask=60;const auto seatedPouch=Bc2FeedWorldBases(p,placed);CHECK(seatedPouch);
    CHECK((*seatedPouch)[0].values[3][0]==9&&(*seatedPouch)[2].values[3][0]==4);
    p.carriedPartsMask=2;CHECK(!Bc2FeedWorldBases(p,placed));p.carriedPartsMask=128;CHECK(!Bc2FeedWorldBases(p,placed));
    p.carriedPartsMask=1;p.pouchCarryWeaponWorldMeters={};CHECK(!Bc2FeedWorldBases(p,placed));return 0;
}
}
int main(){
    CHECK(MeasuredCoverFrameIsNotTheHingeFrame()==0);
    CHECK(CarriedWorldBasisIsIndependentOfNewWeapon()==0);
    CHECK(ArticulatedSubtreesPreserveUnownedAnimation()==0);
    CHECK(StructuralFixturesNeverGrantProductionBinding()==0);
    CHECK(InvalidGeometryAndChangedRigFailClosed()==0);
    CHECK(OriginalRawLeaseAndActualHandOwnerRemainRequired()==0);
    CHECK(ExactSelectedConfigurationAndOriginalLeaseAreRequired()==0);
    CHECK(RealMechanismClaimAndPairRetentionAreRequired()==0);
    CHECK(CarryMaskSelectsSeparateRetainedWorldBases()==0);
    std::puts("Bc2FeedPresentationTests: 9 geometry/lease/selector/claim/carry groups; no native/headset acceptance");
}
