#include "Bc2PumpPart.h"
#include "Test.h"
#include "CapturedSpasPumpMotion.h"
#include <cstring>
using namespace fvr;using namespace fvr::bc2;using namespace fvr::interaction;
namespace {
math::Matrix4 Pose(float z=0){math::Matrix4 m{};for(unsigned k=0;k<4;++k)m.values[k][k]=1;m.values[3][2]=z;return m;}
struct Fixture {
    RigSnapshot rig;Bc2PumpPartBinding binding;Bc2PumpPartSource source;HandInteractionSample input;
    Fixture(){rig.names={"root","jntWpn_1","jntWpn_4","other"};rig.parents={-1,0,1,0};rig.weaponBone=1;
        rig.identity={0x10000,0x20000,0x30000,0x40000,0x50000,0x60000,0x70000,0x80000,0x90000,4,false};
        rig.world.assign(4,Pose());rig.inverseBind.assign(4,Pose());rig.evaluatedWorld.assign(4,Pose());rig.nativeEvaluated.resize(4);
        for(auto& b:rig.nativeEvaluated)b.fill(std::byte{0x5a});binding=*bc2_pump_detail::DerivePart(rig);
        input={{(std::uint64_t(rig.identity.weak)<<32)|rig.identity.soldier,2,3,4},10,1000000000,1100000000,1000000000,true,{true,true},{false,false}};
        source.lease={input.owner,{7,8},{9,10},11,0,12,input.observedNs,input.deadlineNs,true};source.rig=rig.identity;
        source.closedPartFromWeapon=Pose(-.8469f);source.rearDirection=-1;source.inputSequence=input.sequence;source.observedNs=input.observedNs;source.deadlineNs=input.deadlineNs;
        source.gun={2,input.owner,InteractionHand::Right,HandClaimKind::GunHold,source.lease.item,{1,1},0};
        source.mechanism={3,input.owner,InteractionHand::Left,HandClaimKind::Mechanism,source.lease.item,source.lease.mechanism,2};source.nativeBoundaryVerified=true;
    }
    auto Run(float travel){return BuildSpasPumpPart(rig,binding,source,source.lease,input,
        {source.mechanism,input.deadlineNs,input.sequence},{source.gun,input.deadlineNs,input.sequence},Pose(),1,travel);}
};
int SingleLeafPrivateCopy(){Fixture f;const auto old=f.rig.nativeEvaluated;auto p=f.Run(SpasObservedForeEndStroke);CHECK(p&&p->edits.size()==1&&p->edits[0].index==2);
    CHECK(f.rig.nativeEvaluated==old);for(unsigned r=0;r<4;++r)for(unsigned k=12;k<16;++k)CHECK(p->edits[0].after[r*16+k]==std::byte{0x5a});
    float z=0;std::memcpy(&z,p->edits[0].after.data()+56,4);CHECK(Near(z,.8469f+SpasObservedForeEndStroke,1e-5f));
    CHECK(!BindSpasPumpPart(f.rig,SpasReloadAsset,SpasReloadMesh));return 0;}
int CurrentAnimationDoesNotDoubleMovePart(){Fixture f;auto a=f.Run(.04f);f.rig.evaluatedWorld[2]=Pose(-.06f);auto b=f.Run(.04f);
    CHECK(a&&b&&a->edits[0].after==b->edits[0].after);return 0;}
int ExactLifetimeAndClaims(){for(unsigned n=0;n<15;++n){Fixture f;switch(n){case 0:f.source.nativeBoundaryVerified=false;break;
    case 1:++f.source.rig.pose;break;case 2:f.source.lease.held=false;break;case 3:f.source.deadlineNs=f.input.nowNs;break;
    case 4:++f.input.owner.space;break;case 5:f.source.mechanism.prerequisiteClaim=3;break;case 6:f.source.mechanism.kind=HandClaimKind::WeaponSupport;break;
    case 7:f.input.focused=false;break;case 8:f.rig.nativeHiddenLeaves={2};break;case 9:f.rig.parents[3]=2;break;
    case 10:f.rig.names[3]="jntWpn_4";break;case 11:++f.source.inputSequence;break;
    case 12:f.source.rearDirection=0;break;case 13:++f.source.mechanism.contact.id;break;
    case 14:++f.rig.identity.soldier;f.source.rig=f.rig.identity;break;}CHECK(!f.Run(.04f));}return 0;}
int CapturedChannelTravelReplay(){Fixture f;f.source.closedPartFromWeapon=captured_spas_pump::HighZ;
    auto plan=f.Run(SpasObservedForeEndStroke);CHECK(plan&&plan->edits.size()==1);
    math::Matrix4 posed{};for(unsigned row=0;row<4;++row)std::memcpy(posed.values[row].data(),plan->edits[0].after.data()+row*16,12);
    for(unsigned n=0;n<4;++n){posed.values[2][n]=-posed.values[2][n];posed.values[n][2]=-posed.values[n][2];}
    for(unsigned row=0;row<4;++row)for(unsigned col=0;col<3;++col)
        CHECK(Near(posed.values[row][col],captured_spas_pump::LowZ.values[row][col],row==3?.00015f:.00002f));
    return 0;
}
int PhysicalConsumerToPrivatePalette(){
    Fixture f;HandInteraction hands;PhysicalWeaponCycle physical;PhysicalWeaponCycleSample sample;
    sample.source=f.input;sample.lease=f.source.lease;sample.rawContact=Pose();
    WeaponCycleProfile profile;profile.id=3;profile.revision=1;profile.closedContact=Pose();profile.axis={0,0,-1};
    profile.stroke=SpasObservedForeEndStroke;profile.rearTolerance=.003f;profile.frontTolerance=.003f;
    profile.contactRadius=.06f;profile.lateralTolerance=.03f;profile.maxStepMeters=.03f;profile.rotationTolerance=.15f;
    profile.endpointDwellNs=10000000;profile.maximumCycleNs=2000000000;
    std::uint64_t intent=1;hands.Update(sample.source);
    const auto gun=hands.Acquire(sample.source,{sample.source.owner,InteractionHand::Right,HandClaimKind::GunHold,sample.lease.item,
        {{1,1},sample.source.sequence,sample.source.deadlineNs,true},intent++,0});CHECK(gun.claim);sample.gun=gun.claim->token;
    CHECK(physical.Begin(profile,sample.lease,sample.source.nowNs));
    const auto original=f.rig.nativeEvaluated;unsigned palettes=0,submissions=0;
    for(float travel:{-1.f,0.f,.02f,.04f,.06f,.08f,SpasObservedForeEndStroke,SpasObservedForeEndStroke,.075f,.055f,.035f,.015f,0.f,0.f}){
        ++sample.source.sequence;sample.source.nowNs+=10000000;sample.source.observedNs=sample.source.nowNs;sample.source.deadlineNs=sample.source.nowNs+100000000;
        ++sample.lease.sequence;sample.lease.observedNs=sample.source.nowNs;sample.lease.deadlineNs=sample.source.deadlineNs;
        sample.grip=travel>=0;sample.source.released[0]=!sample.grip;
        sample.rawContact=weapon_cycle_detail::Target(profile,std::max(0.f,travel),0);
        sample.contact={sample.lease.mechanism,sample.source.sequence,sample.source.deadlineNs,true};sample.acquireIntent=intent++;
        hands.Update(sample.source);CHECK(hands.Renew(sample.source,sample.gun,{{1,1},sample.source.sequence,sample.source.deadlineNs,true}).accepted);
        const auto result=physical.Update(sample,hands);if(result.release)++submissions;
        if(!result.target)continue;
        const auto mechanism=hands.Current(InteractionHand::Left),currentGun=hands.Current(InteractionHand::Right);CHECK(mechanism&&currentGun);
        // Native callback and boundary calibration are mocked. The unchanged
        // captured closed transform is retained; only current receipts renew.
        auto source=f.source;source.lease=result.target->lease;source.mechanism=result.target->mechanism;source.gun=result.target->gun;
        source.inputSequence=result.target->inputSequence;source.observedNs=result.target->observedNs;source.deadlineNs=result.target->deadlineNs;
        const auto plan=BuildSpasPumpCyclePart(f.rig,f.binding,source,profile,*result.target,sample.lease,sample.source,*mechanism,*currentGun,Pose(),1);
        CHECK(plan&&plan->edits.size()==1&&plan->edits[0].index==f.binding.part);++palettes;
        float z=0;std::memcpy(&z,plan->edits[0].after.data()+56,4);CHECK(Near(z,.8469f+travel,1e-5f));
        for(unsigned mutation=0;mutation<6;++mutation){auto badSource=source;auto badTarget=*result.target;auto badProfile=profile;
            if(mutation==0)++badTarget.revision;
            if(mutation==1)badTarget.contact.values[3][2]+=.01f;
            if(mutation==2)badSource.rearDirection=1;
            if(mutation==3)badSource.nativeBoundaryVerified=false;
            if(mutation==4)--badSource.inputSequence;
            if(mutation==5)badProfile.stroke=.1f;
            CHECK(!BuildSpasPumpCyclePart(f.rig,f.binding,badSource,badProfile,badTarget,sample.lease,sample.source,*mechanism,*currentGun,Pose(),1));
        }
    }
    CHECK(palettes==12&&submissions==1&&!hands.Current(InteractionHand::Left));CHECK(f.rig.nativeEvaluated==original);
    CHECK(physical.Phase()==WeaponCyclePhase::AwaitingNative);return 0;
}
}
int main(){if(SingleLeafPrivateCopy()||CurrentAnimationDoesNotDoubleMovePart()||ExactLifetimeAndClaims()||CapturedChannelTravelReplay()||PhysicalConsumerToPrivatePalette())return 1;
    std::puts("5 SPAS private part adapter groups passed; real physical consumer/private palette, mocked native boundaries, no runtime admission.");}
