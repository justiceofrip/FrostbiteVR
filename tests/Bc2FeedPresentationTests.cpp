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
Bc2FeedPartFrames Bases(){Bc2FeedPartFrames a;for(auto& m:a)m=Identity();return a;}
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
    p.registrationKey=bc2_feed_profile_detail::M60Registration.key;p.chainCount=Geometry().chainCount;
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
struct IndependentPresentationFixture {
    Bc2FeedGeometry geometry{};RigSnapshot rig{};
    IndependentPresentationFixture(){
    auto& g=geometry;g.asset="SyntheticPresentation";g.configurationPath="Fixture/Presentation/Config";
    g.meshPath="Fixture/Presentation/Mesh";g.rigFingerprint=123;g.chainCount=6;
    g.rootBone={"ownRoot","",3,0};g.pouchBone={"ownBox","ownRoot",9,3};g.coverBone={"ownCover","ownRoot",14,3};
    g.chain={{{"first","ownRoot",2,3},{"second","first",6,2},{"third","second",11,6},
        {"fourth","third",17,11},{"fifth","fourth",21,17},{"sixth","fifth",27,21}}};
    g.weaponFromHinge=g.hingeFromOpeningContact=g.hingeFromClosingContact=g.weaponFromCoverReference=Identity();
    g.weaponFromHinge.values[3][0]=.05f;g.weaponFromCoverReference.values[3][0]=.16f;g.coverTravelRadians=.75f;
    for(auto& pose:g.poses){pose.chainCount=g.chainCount;pose.weaponFromPouch=pose.weaponFromFeedRoot=pose.sourceFromWrist=Identity();
        pose.weaponFromPouch.values[3][1]=-.12f;pose.weaponFromFeedRoot.values[3][1]=.03f;
        for(unsigned i=0;i+1<g.chainCount;++i){pose.parentFromChainChild[i]=Identity();pose.parentFromChainChild[i].values[3][0]=.02f;}
    }
    rig.identity.count=35;rig.names.resize(35);rig.parents.assign(35,3);
    rig.parents[3]=-1;rig.weaponBone=3;rig.evaluatedWorld.assign(35,Identity());rig.inverseBind.assign(35,Identity());
    for(unsigned i=0;i<35;++i)rig.names[i]="other"+std::to_string(i);
    rig.names[3]="ownRoot";
    const auto assign=[&](const Bc2FeedChainBone& b){rig.names[b.index]=b.bone;rig.parents[b.index]=std::int32_t(b.parentIndex);};
    assign(g.pouchBone);assign(g.coverBone);for(unsigned i=0;i<g.chainCount;++i)assign(g.chain[i]);
    rig.parents[30]=27;rig.evaluatedWorld[30].values[3][2]=.04f;
    }
};
int IndependentSixBoneChainUsesOwnRolesAndCoverFrame(){
    IndependentPresentationFixture fixture;const auto& g=fixture.geometry;const auto& rig=fixture.rig;
    const auto binding=feed_presentation_detail::Derive(rig,g);CHECK(binding&&binding->partCount==8);
    CHECK(binding->parts[0]==9&&binding->parts[1]==14&&binding->parts[7]==27);
    CHECK(!BindBc2FeedParts(rig,g.asset,g.configurationPath,g.meshPath)); // Structural fixture is never enrollment.
    const auto pose=ComposeBc2FeedPresentation({{1,2},{3,4}},g.poses[0]);CHECK(pose&&pose->chainCount==6);
    const auto frames=Bc2FeedPartWorldFrames(g,*pose,g.weaponFromHinge,Bases(),100);CHECK(frames);
    CHECK(Near((*frames)[1].values[3][0],16.f));CHECK(Near((*frames)[7].values[3][0],10.f));
    const auto writes=BuildBc2FeedPartWrites(*binding,rig,*frames);CHECK(writes&&writes->size()==9);
    for(unsigned i=0;i<binding->partCount;++i){
        const auto w=std::find_if(writes->begin(),writes->end(),[&](const auto& x){return x.index==binding->parts[i];});
        CHECK(w!=writes->end()&&Same(w->transform,(*frames)[i]));
    }
    CHECK(std::none_of(writes->begin(),writes->end(),[](const auto& w){return w.index==3||w.index==34;}));
    const auto child=std::find_if(writes->begin(),writes->end(),[](const auto& w){return w.index==30;});CHECK(child!=writes->end());
    CHECK(Near(child->transform.values[3][2],.04f));
    auto changed=*pose;changed.chainCount=4;CHECK(!Bc2FeedPartWorldFrames(g,changed,g.weaponFromHinge,Bases(),1));
    auto badRig=rig;badRig.parents[21]=3;CHECK(!BuildBc2FeedPartWrites(*binding,badRig,*frames));
    badRig=rig;badRig.names[34]="sixth";CHECK(!feed_presentation_detail::Derive(badRig,g));
    CHECK(feed_presentation_detail::CarryMask(6)==253&&feed_presentation_detail::CarryMask(8)==1021);
    CHECK(feed_presentation_detail::CarryMask(0)==0&&feed_presentation_detail::CarryMask(9)==0);return 0;
}
int RigidCoverSiblingsKeepOwnTargetsAndNativeDescendants(){
    IndependentPresentationFixture fixture;auto& g=fixture.geometry;auto& rig=fixture.rig;
    g.coverCompanionCount=4;
    g.coverCompanions={{{{"sideA","ownRoot",4,3},Identity()},{{"sideB","ownRoot",13,3},Identity()},
        {{"sideC","ownRoot",23,3},Identity()},{{"sideD","ownRoot",31,3},Identity()}}};
    for(unsigned i=0;i<g.coverCompanionCount;++i){
        auto& companion=g.coverCompanions[i];companion.weaponFromReference=feed_interaction_detail::Along(Identity(),FeedMotionKind::Hinge,.37);
        companion.weaponFromReference.values[3]={.25f+.01f*i,.2f,.07f,1};
        const auto& b=companion.bone;rig.names[b.index]=b.bone;rig.parents[b.index]=std::int32_t(b.parentIndex);
        // Native animation is intentionally different from the measured pose.
        // Retargeting only the primary cover delta would leave this drift.
        rig.evaluatedWorld[b.index]=feed_interaction_detail::Along(Identity(),FeedMotionKind::Hinge,-.6);
        rig.evaluatedWorld[b.index].values[3]={-.4f,.5f,-.2f,1};
    }
    auto childLocal=feed_interaction_detail::Along(Identity(),FeedMotionKind::Hinge,.52);
    childLocal.values[3][2]=.03f;
    rig.parents[5]=4;rig.evaluatedWorld[5]=feed_interaction_detail::Multiply(childLocal,rig.evaluatedWorld[4]);
    rig.parents[16]=14;rig.evaluatedWorld[16]=feed_interaction_detail::Multiply(childLocal,rig.evaluatedWorld[14]);
    rig.parents[32]=34;rig.evaluatedWorld[32]=childLocal;
    const auto originalWorld=rig.evaluatedWorld;
    const auto binding=feed_presentation_detail::Derive(rig,g);CHECK(binding&&binding->partCount==12);
    CHECK(binding->parts[7]==27&&binding->parts[8]==4&&binding->parts[11]==31);
    auto p=Physical();p.chainCount=g.chainCount;p.carriedPartsMask=feed_presentation_detail::CarryMask(g.chainCount);
    p.pouchCarryWeaponWorldMeters=p.chainCarryWeaponWorldMeters=Identity();
    p.pouchCarryWeaponWorldMeters.values[3][0]=5;p.chainCarryWeaponWorldMeters.values[3][0]=6;
    auto placed=Identity();placed.values[3][0]=2;placed.values[3][1]=3;
    const auto bases=feed_presentation_detail::WorldBases(g,p,placed);CHECK(bases);
    CHECK((*bases)[0].values[3][0]==5&&(*bases)[2].values[3][0]==6);
    for(unsigned i=8;i<12;++i)CHECK(Same((*bases)[i],placed));
    auto bad=p;bad.carriedPartsMask|=1u<<8;CHECK(!feed_presentation_detail::WorldBases(g,bad,placed));
    const auto pose=ComposeBc2FeedPresentation(p.supply,g.poses[0]);CHECK(pose);
    const auto closed=Bc2FeedPartWorldFrames(g,*pose,g.weaponFromHinge,*bases,100);CHECK(closed);
    CHECK(Near((*closed)[8].values[3][0],225.f)&&Near((*closed)[8].values[3][1],320.f));
    const auto opened=feed_interaction_detail::Multiply(
        feed_interaction_detail::Along(Identity(),FeedMotionKind::Hinge,1.5707963267948966),g.weaponFromHinge);
    const auto frames=Bc2FeedPartWorldFrames(g,*pose,opened,*bases,100);CHECK(frames);
    // Quarter turn about (.05,0,0): (.25,.2,.07) becomes (-.15,.2,.07).
    CHECK(Near((*frames)[8].values[3][0],185.f)&&Near((*frames)[8].values[3][1],320.f));
    CHECK(Near((*frames)[8].values[3][2],7.f));
    CHECK(Near((*frames)[8].values[0][0],-std::sin(.37f))&&Near((*frames)[8].values[0][1],std::cos(.37f)));
    const auto writes=BuildBc2FeedPartWrites(*binding,rig,*frames);CHECK(writes&&writes->size()==15);
    for(unsigned i=0;i<binding->partCount;++i){
        const auto w=std::find_if(writes->begin(),writes->end(),[&](const auto& x){return x.index==binding->parts[i];});
        CHECK(w!=writes->end()&&Same(w->transform,(*frames)[i]));
    }
    for(const auto indices:{std::array<unsigned,2>{5,8},std::array<unsigned,2>{16,1}}){
        const auto w=std::find_if(writes->begin(),writes->end(),[&](const auto& x){return x.index==indices[0];});
        CHECK(w!=writes->end());
        const auto relative=feed_interaction_detail::Multiply(w->transform,feed_interaction_detail::Inverse((*frames)[indices[1]]));
        // Inverting float world coordinates near300 native units loses about
        // 1.1e-5 units here; retain sub-micrometre precision in metres.
        CHECK(Same(relative,childLocal,.00002f));
    }
    CHECK(std::none_of(writes->begin(),writes->end(),[](const auto& w){return w.index==3||w.index==32||w.index==34;}));
    for(unsigned i=0;i<rig.evaluatedWorld.size();++i)CHECK(rig.evaluatedWorld[i].values==originalWorld[i].values);
    auto badRig=rig;badRig.parents[4]=9;CHECK(!BuildBc2FeedPartWrites(*binding,badRig,*frames));
    badRig=rig;badRig.names[34]="sideA";CHECK(!feed_presentation_detail::Derive(badRig,g));
    badRig=rig;badRig.nativeHiddenLeaves={4};CHECK(!feed_presentation_detail::Derive(badRig,g));
    auto badTargets=*frames;badTargets[11]={};CHECK(!BuildBc2FeedPartWrites(*binding,rig,badTargets));
    auto nested=g;nested.coverBone.parent="sideA";nested.coverBone.parentIndex=4;
    auto nestedRig=rig;nestedRig.parents[14]=4;
    const auto nestedBinding=feed_presentation_detail::Derive(nestedRig,nested);CHECK(nestedBinding);
    const auto nestedWrites=BuildBc2FeedPartWrites(*nestedBinding,nestedRig,*frames);CHECK(nestedWrites);
    const auto nestedCover=std::find_if(nestedWrites->begin(),nestedWrites->end(),[](const auto& w){return w.index==14;});
    CHECK(nestedCover!=nestedWrites->end()&&Same(nestedCover->transform,(*frames)[1]));
    // A later companion in part order is applied before its earlier owned
    // descendant; the primary cover still gets its own exact target.
    auto disconnected=g;disconnected.pouchBone.parent="other34";disconnected.pouchBone.parentIndex=34;
    badRig=rig;badRig.parents[9]=34;badRig.parents[34]=-1;
    CHECK(!feed_presentation_detail::Derive(badRig,disconnected));
    badRig.parents[34]=9;CHECK(!feed_presentation_detail::Derive(badRig,disconnected));
    return 0;
}
int RegistrationAndChainIdentitySurvivePairRetention(){
    auto p=Physical();auto current=p.input;current.sequence++;
    auto bad=p;bad.registrationKey++;CHECK(!Bc2FeedSourceFresh(bad,p.owner,current,Equipment(),p.rig,1020000000));
    CHECK(!Bc2FeedSelected(Selected(),bad,Equipment(),21,1020000000));
    CHECK(!Bc2FeedWorldBases(bad,Identity()));CHECK(!Bc2FeedPresentationRetained(p,bad,false,1020000000));
    bad=p;bad.chainCount=6;CHECK(!Bc2FeedSourceFresh(bad,p.owner,current,Equipment(),p.rig,1020000000));
    CHECK(!Bc2FeedWorldBases(bad,Identity()));CHECK(!Bc2FeedSelected(Selected(),bad,Equipment(),21,1020000000));
    CHECK(!Bc2FeedPresentationRetained(p,bad,false,1020000000));
    bad=p;bad.carriedPartsMask=1u<<8;CHECK(!Bc2FeedWorldBases(bad,Identity()));return 0;
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
    CHECK(IndependentSixBoneChainUsesOwnRolesAndCoverFrame()==0);
    CHECK(RigidCoverSiblingsKeepOwnTargetsAndNativeDescendants()==0);
    CHECK(RegistrationAndChainIdentitySurvivePairRetention()==0);
    std::puts("Bc2FeedPresentationTests: 12 geometry/lease/selector/claim/carry groups; no native/headset acceptance");
}
