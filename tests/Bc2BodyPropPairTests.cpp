#include "Bc2BodyPropPair.h"
#include "Bc2MagazineConsumerFixture.h"
#include "Test.h"
#include <iostream>
using namespace fvr;using namespace fvr::bc2;using namespace fvr::graphics;
namespace {
auto Eye(unsigned side=0){BodyPropEye e;e.view=interaction::reload_insertion_detail::Identity();
    e.view.values[3][0]=side?.03f:-.03f;e.projection=*math::MakeLhProjectionFromFovTangents({-1,1,1,-1},.05f,100);
    e.ammo.sequence=100+side;e.ammo.loaded=27-int(side);return e;}
BodyAmmoRenderSource Ammo(magazine_consumer_fixture::Fixture& f,float x=0){
    auto a=std::make_shared<BodyAmmoTracking>();a->magazine=f.result.tracking;
    a->magazine->selected=std::make_shared<const SelectedMeshesSnapshot>(*f.result.tracking.selected);
    auto& v=a->visual;v.enabled=true;const auto map=BindMagazineOwners(f.s.input.owner,f.s.weapon,f.reserve,0,f.now,f.s.family);
    v.source=*MagazineBodySupply(*map,f.reserve,f.s.trackingEpoch,f.now);v.input=f.s.input;v.gun=*f.gun;
    v.contact=*interaction::ChestAmmoSupply().alternateContact;v.frame=interaction::SupplyAnchorFrame::RecenteredBody;
    const auto& g=*Xm8MagazineEquipment().geometry;BodyAmmoRenderSource s;s.asset=g.asset;s.mesh=g.mesh;s.part=g.bones.magazine;s.rigFingerprint=g.rigFingerprint;
    s.geometry=MakeBodyAmmoGeometryKey(s.asset,s.mesh,s.part,s.rigFingerprint);auto pose=interaction::reload_insertion_detail::Identity();pose.values[3][0]=x;
    s.prop={v,pose};s.authority=a;return s;
}
struct PairFixture {
    magazine_consumer_fixture::Fixture native;BodyPropSources a,b;BodyCarriedPairKey key{1,2,3,4,5,79,7};
    PairFixture(int beforeLoaded=27,int currentLoaded=27,int reserveUnits=83){
        native.reserve.loaded=beforeLoaded;native.reserve.reserve=reserveUnits;native.reserve.allThreeIdle=true;
        native.Send();a.ammo=Ammo(native);native.reserve.loaded=currentLoaded;native.Send();b.ammo=Ammo(native,.2f);
    }
    auto Now()const{return native.now;}
};
int TypedAmmoPoseAndHud(){PairFixture f;BodyPropPair pair;const auto left=pair.Begin(f.key,0,f.a);CHECK(left);
    CHECK(!pair.End(left,f.key,0,f.a,f.a,Eye(),f.Now()));const auto right=pair.Begin(f.key,1,f.b);CHECK(left==right);
    const auto result=pair.End(right,f.key,1,f.b,f.b,Eye(1),f.Now());CHECK(result);
    CHECK((*result)[0].count==1&&(*result)[1].count==1);const auto& a=(*result)[0].instances[0];const auto& b=(*result)[1].instances[0];
    CHECK(SameBodyPropSource(a,b)&&a.world.values==b.world.values);CHECK(a.world.values[3][0]==0);
    CHECK(a.deadlineNs<=f.a.ammo->prop.source.input.deadlineNs);CHECK((*result)[0].view.values!=(*result)[1].view.values);
    CHECK((*result)[0].ammo==Eye().ammo&&(*result)[1].ammo==Eye(1).ammo);return 0;
}
int CoherentLoadedCountTransitionPair(){
    // Fixture snapshots only: no native firing or headset acceptance is implied.
    for(const auto counts:{std::array{30,29,83},std::array{29,28,83},
                           std::array{1,0,83},std::array{30,29,1}})
    for(unsigned fault=0;fault<7;++fault){
        PairFixture f(counts[0],counts[1],counts[2]);BodyPropPair pair;
        CHECK(BodyAmmoFresh(*f.a.ammo->authority,f.Now())&&BodyAmmoFresh(*f.b.ammo->authority,f.Now()));
        const auto original=pair.Begin(f.key,0,f.a);CHECK(original);
        CHECK(!pair.End(original,f.key,0,f.a,f.a,Eye(),f.Now()));
        auto current=f.b;auto now=f.Now();
        if(fault==1){ // A different positive reserve is independently valid, but is not the retained source.
            ++f.native.reserve.reserve;f.native.Send();current.ammo=Ammo(f.native,.2f);now=f.Now();
            CHECK(BodyAmmoFresh(*current.ammo->authority,now));
        }
        if(fault==2){auto authority=std::make_shared<BodyAmmoTracking>(*current.ammo->authority);
            authority->visual.gun.token.id=0;current.ammo->authority=authority;current.ammo->prop.source=authority->visual;}
        if(fault==3)current.ammo.reset();
        if(fault==4)now=f.a.ammo->prop.source.source.deadlineNs;
        if(fault==5)++current.ammo->prop.source.source.objectUnits; // Prop contradicts its own typed publication.
        if(fault==6){auto authority=std::make_shared<BodyAmmoTracking>(*current.ammo->authority);
            auto& m=*authority->magazine;const auto& old=f.a.ammo->authority->magazine->reserve;
            m.reserve.sequence=old.sequence;m.reserve.observedNs=old.observedNs;m.reserve.deadlineNs=old.deadlineNs;
            const auto owners=BindMagazineOwners(authority->visual.input.owner,m.family.binding.weapon,m.reserve,m.cycle,now,m.family);CHECK(owners);
            const auto source=MagazineBodySupply(*owners,m.reserve,authority->visual.input.owner.space,now);CHECK(source);
            authority->visual.source=*source;current.ammo->authority=authority;current.ammo->prop.source=authority->visual;
            CHECK(BodyAmmoFresh(*authority,now)); // Same reserve ID cannot carry a different loaded count.
        }
        const auto second=pair.Begin(f.key,1,current);CHECK(second==original);
        const auto result=pair.End(second,f.key,1,current,current,Eye(1),now);CHECK(result);
        for(unsigned eye=0;eye<2;++eye){const auto& delivered=(*result)[eye];CHECK(delivered.count==(fault?0u:1u));
            CHECK(delivered.ammo==Eye(eye).ammo);
            if(!fault){CHECK(delivered.instances[0].world.values==f.a.ammo->prop.partWorld.values);
                CHECK(delivered.instances[0].deadlineNs<=f.a.ammo->prop.source.source.deadlineNs);
                CHECK(delivered.instances[0].deadlineNs<=f.a.ammo->prop.source.input.deadlineNs);}
        }
        if(!fault)CHECK(SameBodyPropSource((*result)[0].instances[0],(*result)[1].instances[0]));
        CHECK(f.native.starts==0&&f.native.submits==0&&f.native.cancels==0&&f.native.reserve.loaded==counts[1]);
    }
    return 0;
}
int MissingAndRevokedSources(){
    for(unsigned variant=0;variant<6;++variant){PairFixture f;BodyPropPair pair;BodyPropSources none;
        const auto first=pair.Begin(f.key,0,variant==0?none:f.a);CHECK(first);
        CHECK(!pair.End(first,f.key,0,variant==0?none:f.a,variant==1?none:f.a,Eye(),f.Now()));
        auto current=f.b;
        if(variant==4){auto authority=std::make_shared<BodyAmmoTracking>(*current.ammo->authority);authority->visual.input.focused=false;current.ammo->authority=authority;}
        if(variant==5){auto authority=std::make_shared<BodyAmmoTracking>(*current.ammo->authority);++authority->magazine->family.binding.weapon.id;current.ammo->authority=authority;}
        const auto second=pair.Begin(f.key,1,current);CHECK(second);
        const auto result=pair.End(second,f.key,1,variant==2?none:current,variant==3?none:current,Eye(1),f.Now());CHECK(result);
        CHECK(!(*result)[0].count&&!(*result)[1].count);CHECK((*result)[0].ammo.sequence==100&&(*result)[1].ammo.sequence==101);
    }return 0;
}
BodyPropInstance Item(BodyPropSourceKind kind,unsigned ordinal=0){return {{1,2,3},interaction::reload_insertion_detail::Identity(),1000000000,1100000000,
    1,BodyPropWireEpoch(kind,2,ordinal),7};}
int DenseSlotIntersection(){auto left=Eye(),right=Eye(1);left.count=3;right.count=2;
    left.instances[0]=Item(BodyPropSourceKind::Ammo);left.instances[1]=Item(BodyPropSourceKind::Carried,0);left.instances[2]=Item(BodyPropSourceKind::Carried,1);
    right.instances[0]=left.instances[2];right.instances[1]=left.instances[1];right.instances[1].deadlineNs-=10000000;
    const auto result=IntersectBodyPropPair(left,right,7,1050000000);CHECK(result[0].count==2&&result[1].count==2);
    for(unsigned n=0;n<2;++n){CHECK(SameBodyPropSource(result[0].instances[n],result[1].instances[n]));CHECK(result[0].instances[n].equipmentGeneration==left.instances[n+1].equipmentGeneration);}
    CHECK(result[0].instances[0].deadlineNs==1090000000&&result[1].instances[0].deadlineNs==1090000000);
    // Equal geometry cannot turn a carried model into the missing ammo source.
    CHECK(result[0].instances[0].equipmentGeneration!=left.instances[0].equipmentGeneration);
    left.instances[2]=left.instances[1];CHECK(!IntersectBodyPropPair(left,right,7,1050000000)[0].count);return 0;
}
int ExpiryAndWorldMismatch(){PairFixture f;BodyPropPair pair;const auto original=pair.Begin(f.key,0,f.a);CHECK(original);
    CHECK(!pair.End(original,f.key,0,f.a,f.a,Eye(),f.Now()));
    const auto expiry=f.a.ammo->prop.source.input.deadlineNs;
    const auto expired=pair.End(original,f.key,1,f.b,f.b,Eye(1),expiry);CHECK(expired&&!(*expired)[0].count&&!(*expired)[1].count);
    auto left=Eye(),right=Eye(1);left.count=right.count=1;left.instances[0]=right.instances[0]=Item(BodyPropSourceKind::Carried);
    right.instances[0].world.values[3][0]=.1f;CHECK(!IntersectBodyPropPair(left,right,7,1050000000)[0].count);
    right.instances[0]=left.instances[0];CHECK(!IntersectBodyPropPair(left,right,8,1050000000)[0].count);
    CHECK(!IntersectBodyPropPair(left,right,7,1100000000)[0].count);return 0;
}
int KeyResetAndFirstEyeRequired(){PairFixture f;BodyPropPair pair;CHECK(!pair.Begin(f.key,1,f.b));const auto original=pair.Begin(f.key,0,f.a);CHECK(original);
    CHECK(!pair.End(original,f.key,1,f.b,f.b,Eye(1),f.Now()));
    CHECK(!pair.End(original,f.key,0,f.a,f.a,Eye(),f.Now()));
    for(unsigned n=0;n<7;++n){auto key=f.key;switch(n){case 0:++key.channel;break;case 1:++key.request;break;case 2:++key.owner;break;
        case 3:++key.frame;break;case 4:++key.device;break;case 5:++key.tracking;break;case 6:++key.space;break;}
        CHECK(!pair.Begin(key,1,f.b));CHECK(!pair.End(original,key,1,f.b,f.b,Eye(1),f.Now()));}
    pair.Reset();CHECK(!pair.End(original,f.key,1,f.b,f.b,Eye(1),f.Now()));auto next=f.key;++next.request;
    const auto replacement=pair.Begin(next,0,f.b);CHECK(replacement&&replacement!=original);
    CHECK(!pair.End(replacement,next,1,f.b,f.b,Eye(1),f.Now()));return 0;
}
int BeforeLeaseAndInvalidView(){PairFixture f;auto middle=f.b;middle.ammo->authority=std::make_shared<BodyAmmoTracking>(*middle.ammo->authority);
    auto authority=std::make_shared<BodyAmmoTracking>(*middle.ammo->authority);auto selected=std::make_shared<SelectedMeshesSnapshot>(*authority->magazine->selected);
    selected->deadlineNs=f.Now()+1000000;authority->magazine->selected=selected;middle.ammo->authority=authority;
    BodyPropPairSource original{f.key,f.a};const auto packet=BuildBodyPropPairEye(original,middle,f.b,Eye(),f.Now());
    CHECK(packet.count==1&&packet.instances[0].deadlineNs==selected->deadlineNs);
    CHECK(!BuildBodyPropPairEye(original,middle,f.b,Eye(),selected->deadlineNs).count);
    CHECK(!BuildBodyPropPairEye(original,f.a,f.b,{},f.Now()).count);return 0;
}
int VehicleRayFrozenAndRevoked(){
    BodyPropSources source;VehicleReticleSample s;s.owner={1,2,3,4,5,6,7,8,9};s.presentationEpoch=1;s.inputSequence=10;
    s.sightWorld=interaction::reload_insertion_detail::Identity();s.observedNs=1000000000;s.deadlineNs=1100000000;
    s.proof=VehicleReticleProof::NativeSightLine;s.armedSeat=s.focused=s.playing=true;source.vehicleReticle=s;
    BodyCarriedPairKey key{1,2,3,4,5,10,9};BodyPropPair pair;const auto first=pair.Begin(key,0,source);CHECK(first);
    CHECK(!pair.End(first,key,0,source,source,Eye(),1000000001));
    auto newer=source;newer.vehicleReticle->sightWorld.values[3][0]=.5f;++newer.vehicleReticle->inputSequence;
    const auto both=pair.End(first,key,1,newer,newer,Eye(1),1000000002);CHECK(both);
    CHECK((*both)[0].count==1&&(*both)[1].count==1&&(*both)[0].instances[0].geometry==VehicleReticleGeometry);
    CHECK((*both)[0].instances[0].world.values==(*both)[1].instances[0].world.values&&Near((*both)[0].instances[0].world.values[3][0],0));
    newer.vehicleReticle.reset();const auto missing=pair.End(first,key,1,newer,newer,Eye(1),1000000003);
    CHECK(missing&&!(*missing)[0].count&&!(*missing)[1].count);return 0;
}
}
int main(){for(auto test:{TypedAmmoPoseAndHud,CoherentLoadedCountTransitionPair,MissingAndRevokedSources,DenseSlotIntersection,ExpiryAndWorldMismatch,KeyResetAndFirstEyeRequired,BeforeLeaseAndInvalidView,VehicleRayFrozenAndRevoked})if(test())return 1;
    std::cout<<"BodyPropPair: 8 typed-source/stereo-slot/lifetime groups passed; no native/GPU run\n";}
