#include "Bc2PhysicalPistolSlide.h"
#include "Test.h"
#include <cstring>
#include <sstream>
using namespace fvr::bc2;
using namespace fvr::interaction;
namespace {
constexpr std::int64_t Ms=1000000;
constexpr auto ProfileKey=bc2_pistol_slide_profile_detail::Mp443Registration.key;
template<std::size_t N>void Text(std::array<char,N>& dst,std::string_view value){
    dst={};std::memcpy(dst.data(),value.data(),std::min(value.size(),N-1));
}
fvr::math::Matrix4 Identity(){fvr::math::Matrix4 m{};for(unsigned i=0;i<4;++i)m.values[i][i]=1;return m;}
struct Fixture {
    // Multiple independent native journals in one scenario exceed the small
    // default executable stack; production stores this journal persistently.
    std::unique_ptr<PistolAfterFeedEvidence> backendStorage=std::make_unique<PistolAfterFeedEvidence>();
    PistolAfterFeedEvidence& backend=*backendStorage;
    std::unique_ptr<AmmunitionLedger> ledger=std::make_unique<AmmunitionLedger>();
    ReloadHoldIdentity identity{{0x10000,0x20000,0x30000,0x40000,1,2,3},{0x50000,0x60000,0x70000},0x80000,0x90000,0xa0000};
    AmmoResourceBinding binding{identity.owner,{{0x20000,1,0x40000,19},2,3},0xb0000,0xc0000,0xd0000,0xe0000,0,0};
    AmmunitionCounts counts{0,34,17};AmmunitionSnapshot admission{};
    AmmoResourceRequest request{};AmmunitionCommand command{};AmmunitionReceipt receipt{};
    std::shared_ptr<SelectedMeshesSnapshot> selected=std::make_shared<SelectedMeshesSnapshot>();
    HandInteraction hands;std::uint64_t intent=0,sequence=0,snapshot=0,requestId=0,invocation=100;
    std::int64_t now=1000*Ms;bool initialReady=true,releaseAllowed=true,skipEvents=false;
    std::optional<std::uint64_t> eventProfileKey,nativeProfileKey,readyProfileKey;
    bool wrongNativeCapacity=false;
    unsigned releases=0;std::optional<PistolAfterFeedRelease> cleared;
    Bc2PhysicalPistolSlideSample sample{};Bc2PhysicalPistolSlideResult result{};
    ReloadRawContact previousRaw{};bool delayedRaw=false;
    Bc2PhysicalPistolSlide physical;
    Bc2PhysicalPistolSlideApi Api(){return {this,
        [](void* p,std::uint64_t after)noexcept{auto& f=*static_cast<Fixture*>(p);
            auto event=f.skipEvents?std::optional<PistolAfterFeedEvent>{}:f.backend.Event(after);
            if(event&&f.eventProfileKey)event->profileKey=*f.eventProfileKey;return event;},
        [](void* p,const AmmoResourceIdentity& r,std::int64_t at)noexcept{auto& f=*static_cast<Fixture*>(p);
            auto state=f.backend.Read(r,at);if(state){
                if(f.nativeProfileKey)state->profileKey=*f.nativeProfileKey;
                if(f.readyProfileKey&&state->ready)state->ready->branches[1].profileKey=*f.readyProfileKey;
                if(f.wrongNativeCapacity)++state->admission.counts.capacity;
            }return state;},
        [](void* p,const PistolAfterFeedRelease& release)noexcept{auto& f=*static_cast<Fixture*>(p);
            if(!f.releaseAllowed||!f.backend.Release(release,f.sample.input.nowNs))return false;
            ++f.releases;f.cleared=release;return true;},
        [](void* p,std::uint64_t after)noexcept{return static_cast<Fixture*>(p)->backend.Retirement(after);}};}
    explicit Fixture(int loaded=0):physical(Api()){
        counts.loaded=loaded;const auto& g=bc2_pistol_slide_profile_detail::Mp443Geometry.selection;
        sample.asset=g.asset;sample.configurationPath=g.configurationPath;sample.meshPath=g.meshPath;
        sample.rigFingerprint=g.rigFingerprint;sample.selectedRigVerified=true;sample.selected=selected;
        Text(selected->weaponName,g.asset);Text(selected->configurationPath,g.configurationPath);
        selected->configurationPathVerified=true;selected->configurationPathPointer=0xf1000;selected->stateCount=1;
        selected->stateTypeInfo=0xf4000;selected->meshTypeInfo=0xf5000;
        selected->soleConfiguredArray=0xf2000;selected->states[0].array=0xf2000;selected->states[0].count=1;
        selected->states[0].state=0xf6000;selected->states[0].meshes[0].address=0xf3000;
        selected->states[0].meshes[0].typeInfo=0xf5000;selected->states[0].meshes[0].namePointer=0xf7000;
        Text(selected->states[0].meshes[0].assetPath,g.meshPath);
        sample.raw.valid=true;sample.raw.rig.count=170;sample.raw.rigFingerprint=g.rigFingerprint;
        sample.raw.weaponWorldMeters=Identity();Sync();
    }
    void Sync(){
        sample.owner=identity.owner;sample.weapon={identity.owner.weapon,identity.owner.equipGeneration+100};
        sample.equipment=WeaponEquipmentIdentity{identity.owner.weapon,binding.data,binding.persistence,{}};
        Text(sample.equipment->asset,bc2_pistol_slide_profile_detail::Mp443Geometry.selection.asset);
        sample.input.owner={(std::uint64_t(identity.owner.weak)<<32)|identity.owner.soldier,identity.owner.actorGeneration,
            identity.owner.equipGeneration+100,identity.owner.space};
        selected->owner=identity.owner;selected->weaponData=binding.data;selected->inventory=binding.inventory;
        sample.raw.owner=identity.owner;sample.raw.rig.soldier=identity.owner.soldier;sample.raw.rig.weak=identity.owner.weak;
    }
    bool Queue(AmmunitionOperation operation){
        now+=Ms;binding.observedNs=now-1;binding.deadlineNs=now+99*Ms;
        admission={binding.context,++snapshot,now-1,now+99*Ms,counts,true};if(!ledger->Bind(admission,now))return false;
        const auto original=operation==AmmunitionOperation::ReturnMagazine?ledger->Original():std::optional<RemovedMagazineResource>{};
        request={binding,{binding.context,++requestId,now-1,now+99*Ms,operation,original},false,{}};
        const auto queued=ledger->Queue(operation,admission,now,original);if(!queued)return false;command=*queued;
        return backend.Admit(request,command,identity,admission,initialReady,ProfileKey);
    }
    bool Complete(){
        if(!ledger->Dispatch(admission,now))return false;
        receipt={command,++invocation,now+1,now+2,command.before,command.after,true,true};
        now+=3;if(!ledger->Complete(receipt,now))return false;
        if(!backend.Outcome({request,AmmoResourceRequestState::Completed,receipt,now,true,{}}))return false;
        counts=receipt.after;return true;
    }
    bool Transfer(AmmunitionOperation operation){return Queue(operation)&&Complete();}
    bool Feed(bool replacement=true){
        if(!Transfer(AmmunitionOperation::RemoveMagazine))return false;
        if(replacement){if(!ledger->Original()||!ledger->Discard(*ledger->Original()))return false;
            return Transfer(AmmunitionOperation::RefillMagazine);}
        return Transfer(AmmunitionOperation::ReturnMagazine);
    }
    bool Ready(){
        for(unsigned branch=0;branch<3;++branch){
            fvr::bc2::PistolAfterFeedOwnUpdate r;r.context=binding.context;r.identity=identity;
            r.profileKey=ProfileKey;
            r.before.verified=r.after.verified=true;r.effectiveContextVerified=r.finiteAmmoVerified=true;
            auto& u=r.own;u.beforeOwner=u.afterOwner=identity.owner;u.branch=branch;u.depth=1;u.invocation=++invocation;
            r.nativeInvocation=r.nativeUpdate=u.invocation;
            u.beginNs=now+1;u.endNs=now+2;u.firing=r.beforeFiring=identity.firing[branch];
            u.serverPlayer=r.beforeServerPlayer=identity.serverPlayer;u.serverSoldier=r.beforeServerSoldier=identity.serverSoldier;
            u.serverItem=r.beforeServerItem=identity.serverItem;u.finished=u.identityRetained=u.neutralContext=true;
            u.current=u.next=2;u.loadedBefore=u.loadedAfter=counts.loaded;u.reserveBefore=u.reserveAfter=counts.reserve;
            if(!backend.Observe(r))return false;now+=Ms;
        }
        return backend.Read(binding.context.resource,now)->ready.has_value();
    }
    fvr::math::Matrix4 Contact(float travel=0)const {
        const auto p=BuildBc2Mp443ModSlideProfile(bc2_pistol_slide_profile_detail::Mp443Geometry.selection);
        auto m=p->closedContact;for(unsigned i=0;i<3;++i)m.values[3][i]+=p->axis[i]*travel;return m;
    }
    Bc2PhysicalPistolSlideResult Tick(float travel=0,bool grip=false,bool cancel=false){
        // Actual inventory proof is taken during Gather, after XR observation.
        now+=10*Ms;++sequence;binding.observedNs=now+1;binding.deadlineNs=now+99*Ms;sample.binding=binding;
        selected->sequence=sequence;selected->observedNs=now-1;selected->deadlineNs=now+99*Ms;
        sample.input.sequence=sequence;sample.input.observedNs=now;sample.input.deadlineNs=now+100*Ms;sample.input.nowNs=now+1;
        sample.input.focused=true;sample.input.tracked={true,true};sample.input.released={!grip,false};sample.grip=grip;sample.cancel=cancel;
        ReloadRawContact next=sample.raw;next.valid=true;next.inputEvidence=sample.input;next.rawLeftWristWorldMeters=Contact(travel);
        sample.raw=delayedRaw&&previousRaw.valid?previousRaw:next;previousRaw=next;sample.frameSequence=sample.raw.inputEvidence.sequence;
        sample.previousLeft=hands.Current(InteractionHand::Left);
        const HandContactProof proof{{1,2},sequence,sample.input.deadlineNs,true};const auto gun=hands.Current(InteractionHand::Right);
        if(gun)sample.lifecycle=hands.UpdateAndRenewGunHold(sample.input,gun->token,proof).lifecycle;
        else{sample.lifecycle=hands.Update(sample.input);(void)hands.Acquire(sample.input,{sample.input.owner,InteractionHand::Right,
            HandClaimKind::GunHold,sample.weapon,proof,++intent,0});}
        result=physical.Tick(sample,hands,intent);return result;
    }
    bool Grip(){Tick();Tick(0,true);if(delayedRaw)Tick(0,true);return result.ownsHand;}
    void Stroke(){
        for(float travel:{.01f,.02f,.03f,.04f,.05f,.06f,.065f,.065f,.065f,.065f,.065f})Tick(travel,true);
        for(float travel:{.055f,.045f,.035f,.025f,.015f,.005f,0.f,0.f,0.f,0.f,0.f})Tick(travel,true);
        Tick(0,false);if(delayedRaw)Tick(0,false);
    }
};
int EmptyRequiresMeasuredStrokeReleaseAndLaterOwnCohort(){Fixture f;CHECK(f.Feed());CHECK(f.Ready());
    CHECK(f.Tick().selected&&f.result.blocksFire&&!f.result.pending&&!f.result.blocksEquipment);
    CHECK(f.Grip()&&f.result.presentation&&f.result.blocksEquipment);
    const auto p=f.result.presentation;CHECK(p->source.rawSourceWorldMeters.values[3][0]==f.sample.raw.rawLeftWristWorldMeters.values[3][0]);
    CHECK(p->registrationKey==ProfileKey&&p->selection==bc2_pistol_slide_profile_detail::Mp443Registration.geometry->selection);
    CHECK(p->source.weapon.generation==f.sample.input.owner.equipGeneration);
    CHECK(p->source.weapon.generation!=f.binding.context.resource.weaponGeneration);
    f.Stroke();CHECK(f.result.blocksFire&&!f.result.ownsHand&&!f.result.blocksEquipment&&f.releases==0);
    CHECK(f.Ready());f.Tick();CHECK(!f.result.blocksFire&&f.cleared&&f.cleared->clearance.stroke);
    CHECK(f.cleared->clearance.stroke->feed==f.cleared->clearance.feed);CHECK(f.counts==AmmunitionCounts(17,17,17));return 0;
}
int TacticalReturnAndRefillNeedOnlyFreshNativeReadiness(){for(bool replacement:{false,true}){Fixture f(8);CHECK(f.Feed(replacement));
    CHECK(f.Tick().blocksFire&&!f.result.blocksEquipment);CHECK(f.Ready());f.Tick();
    CHECK(!f.result.blocksFire&&f.cleared&&!f.cleared->clearance.stroke&&f.cleared->clearance.retainedPolicyReady);
    CHECK(f.counts.loaded==(replacement?17:8));}return 0;
}
int DelayedTransactionsKeepOriginalClocksAndStowDebt(){Fixture f;CHECK(f.Feed());const auto original=f.receipt;
    f.now+=5000*Ms;f.sample.selectedRigVerified=false;f.Tick();CHECK(f.result.blocksFire&&!f.result.blocksEquipment);
    f.sample.selectedRigVerified=true;f.identity.owner.equipGeneration+=2;f.identity.owner.space+=1;
    f.binding.owner=f.identity.owner;f.binding.context.equipGeneration=f.identity.owner.equipGeneration;f.binding.context.space=f.identity.owner.space;
    f.Sync();f.Tick();CHECK(f.result.selected&&f.result.blocksFire&&!f.result.evidenceGap);CHECK(f.Grip());f.Stroke();
    CHECK(f.Ready());f.Tick();CHECK(!f.result.blocksFire&&f.cleared&&f.cleared->clearance.feed.receipt==original);return 0;
}
int ExistingDebtSurvivesPositiveCountRemoval(){Fixture f(6);f.initialReady=false;CHECK(f.Feed(false));CHECK(f.Ready());
    f.Tick();CHECK(f.result.blocksFire&&!f.cleared);CHECK(f.Grip());f.Stroke();CHECK(f.Ready());f.Tick();
    CHECK(!f.result.blocksFire&&f.cleared->clearance.stroke);
    Fixture g;CHECK(g.Feed());g.Tick();CHECK(g.result.blocksFire&&g.counts.loaded==17);
    CHECK(g.Feed(false));CHECK(g.Ready());g.Tick();CHECK(g.result.blocksFire&&!g.cleared);return 0;
}
int ReplacementAtSameAddressCannotInheritOldPolicy(){Fixture f;CHECK(f.Feed());f.Tick();const auto oldBinding=f.binding;
    const auto oldEquipment=*f.sample.equipment;f.binding.context.resource.weaponGeneration+=1;f.binding.persistence+=1;f.Sync();
    f.Tick();CHECK(!f.result.blocksFire&&!f.result.ownsHand);CHECK(!f.physical.BlocksFire(f.sample.owner,*f.sample.equipment,f.binding));
    CHECK(f.physical.BlocksFire(f.sample.owner,oldEquipment,oldBinding));return 0;
}
int OccupiedLeftHandAndWrongRigNeverBecomeSlideClaims(){for(unsigned role=0;role<4;++role){Fixture f;CHECK(f.Feed());f.Tick(0,true);
    const auto gun=f.hands.Current(InteractionHand::Right);CHECK(gun);
    const auto kind=role==0?HandClaimKind::WeaponSupport:role==1?HandClaimKind::AmmoObject:HandClaimKind::BodyInventory;
    const HandInteractionKey item=role==0?f.sample.weapon:HandInteractionKey{200+role,1};
    const auto claim=f.hands.Acquire(f.sample.input,{f.sample.input.owner,InteractionHand::Left,kind,item,
        {{300+role,1},f.sequence,f.sample.input.deadlineNs,true},++f.intent,role==0?gun->token.id:0});CHECK(claim.claim);
    f.Tick(0,true);CHECK(f.hands.Current(InteractionHand::Left)->token==claim.claim->token&&!f.result.ownsHand&&!f.result.presentation);
    }Fixture f;CHECK(f.Feed());f.sample.rigFingerprint^=1;f.Tick();CHECK(!f.result.selected&&f.result.blocksFire);return 0;
}
int UnknownNativeOutcomeRemainsPendingAcrossStow(){Fixture f(7);CHECK(f.Queue(AmmunitionOperation::RemoveMagazine));
    f.Tick();CHECK(f.result.pending&&f.result.blocksFire&&f.result.blocksEquipment);
    f.now+=10000*Ms;f.sample.selectedRigVerified=false;f.Tick();CHECK(f.result.pending&&f.result.blocksFire);
    CHECK(!f.backend.Outcome({f.request,AmmoResourceRequestState::Uncertain,{},f.now,true,{}}));f.Tick();CHECK(f.result.pending);
    return 0;
}
int ExactNoEntryRejectionDoesNotInventTransfer(){Fixture f(7);CHECK(f.Queue(AmmunitionOperation::RemoveMagazine));
    const auto request=f.request;f.now+=Ms;CHECK(f.backend.Outcome({request,AmmoResourceRequestState::Rejected,{},f.now,false,{}}));
    f.Tick();CHECK(!f.result.pending&&!f.result.blocksFire&&!f.cleared&&f.counts.loaded==7);return 0;
}
int RefusedNativeReleaseKeepsFireVeto(){Fixture f(7);CHECK(f.Feed(false));CHECK(f.Ready());f.releaseAllowed=false;
    f.Tick();CHECK(f.result.blocksFire&&f.result.releaseRefused&&!f.result.blocksEquipment);
    f.releaseAllowed=true;f.Tick();CHECK(!f.result.blocksFire&&f.cleared);return 0;
}
int DelayedRawContactRetainsItsOriginalPacket(){Fixture f;f.delayedRaw=true;CHECK(f.Feed());CHECK(f.Grip());
    CHECK(f.result.presentation&&f.result.presentation->source.input.sequence+1==f.sample.input.sequence);
    const auto frozen=f.result.presentation;f.Stroke();CHECK(f.result.blocksFire&&!f.result.ownsHand);
    CHECK(f.Ready());f.Tick();CHECK(!f.result.blocksFire&&f.cleared&&f.cleared->clearance.stroke);
    CHECK(frozen->source.input.observedNs<f.sample.input.observedNs);return 0;
}
int DrainedActorRetirementReclaimsPendingSlots(){Fixture f(7);
    for(unsigned n=0;n<36;++n){CHECK(f.Queue(AmmunitionOperation::RemoveMagazine));f.Tick();CHECK(f.result.pending);
        const auto old=f.identity;auto replacement=old;replacement.owner.soldier+=0x100;replacement.owner.weak+=0x100;
        ++replacement.owner.actorGeneration;replacement.serverSoldier+=0x100;
        Bc2AmmoReserveLease lease;lease.identity=replacement;lease.sequence=n+1;lease.observedNs=f.now+2;lease.deadlineNs=f.now+100*Ms;
        lease.loaded=f.counts.loaded;lease.reserve=f.counts.reserve;lease.capacity=17;lease.verified=lease.allThreeIdle=true;
        AmmoResourceActorDrain drain{old,{},n+1,n+1,f.now+2,f.now+100*Ms,true};
        CHECK(f.backend.Retire(lease,drain,f.now+3));f.identity=replacement;f.binding.owner=replacement.owner;
        f.binding.context.resource.actor=replacement.owner.soldier;f.binding.context.resource.actorGeneration=replacement.owner.actorGeneration;
        f.ledger=std::make_unique<AmmunitionLedger>();f.Sync();f.Tick();CHECK(!f.result.pending&&!f.result.blocksFire);
    }return 0;
}
int UnknownOrChangedEventProfileCannotCreateOrReplacePolicy(){
    for(const auto key:{std::uint64_t(0),ProfileKey^1}){Fixture f;CHECK(f.Feed());f.eventProfileKey=key;
        f.Tick();CHECK(f.result.selected&&f.result.blocksFire&&!f.result.ownsHand&&!f.result.presentation);
        CHECK(!f.Grip()&&!f.cleared);
    }
    Fixture f;CHECK(f.Feed());CHECK(f.Grip());const auto original=f.result.presentation;
    CHECK(original&&original->registrationKey==ProfileKey);CHECK(f.Feed(false));f.eventProfileKey=ProfileKey^1;
    f.Tick(0,true);CHECK(f.result.evidenceGap&&f.result.blocksFire&&!f.result.ownsHand&&!f.result.presentation&&!f.cleared);
    f.eventProfileKey.reset();f.Tick();CHECK(f.result.evidenceGap&&f.result.blocksFire);
    CHECK(original->registrationKey==ProfileKey&&original->selection==bc2_pistol_slide_profile_detail::Mp443Registration.geometry->selection);
    return 0;
}
int NativeProfileAndCapacityMismatchCannotClearDebt(){
    for(unsigned mismatch=0;mismatch<4;++mismatch){Fixture f(7);CHECK(f.Feed(false));CHECK(f.Ready());
        if(mismatch==0)f.nativeProfileKey=0;
        else if(mismatch==1)f.nativeProfileKey=ProfileKey^1;
        else if(mismatch==2)f.readyProfileKey=ProfileKey^1;
        else f.wrongNativeCapacity=true;
        f.Tick();CHECK(f.result.blocksFire&&f.result.evidenceGap&&!f.result.ownsHand&&!f.result.presentation&&!f.cleared);
        f.nativeProfileKey.reset();f.readyProfileKey.reset();f.wrongNativeCapacity=false;
        f.Tick();CHECK(f.result.evidenceGap&&f.result.blocksFire&&!f.cleared);
    }return 0;
}
int ConfigurationAndGeometrySelectionAreIndependent(){
    for(unsigned mismatch=0;mismatch<5;++mismatch){Fixture f;CHECK(f.Feed());
        if(mismatch==0)Text(f.sample.equipment->asset,"unregistered_fixture");
        else if(mismatch==1)Text(f.selected->configurationPath,"unregistered/configuration");
        else if(mismatch==2)f.sample.configurationPath="unregistered/configuration";
        else if(mismatch==3)f.sample.meshPath="unregistered/mesh";
        else f.sample.rigFingerprint^=1;
        f.Tick();CHECK(!f.result.selected&&f.result.blocksFire&&!f.result.ownsHand&&!f.result.presentation&&!f.cleared);
    }return 0;
}
int DiagnosticRegistrationSurvivesStowWithoutReadingEquipment(){Fixture f;CHECK(f.Feed());CHECK(f.Grip());
    std::ostringstream selected;f.physical.Report(selected);
    CHECK(selected.str().find("\"asset_class\":\"registered_slide\"")!=std::string::npos);
    const auto key=std::to_string(ProfileKey);
    CHECK(selected.str().find("\"registration_key\":"+key)!=std::string::npos);
    f.sample.asset="unregistered_fixture";f.sample.selectedRigVerified=false;f.Tick();
    std::ostringstream stowed;f.physical.Report(stowed);
    CHECK(stowed.str().find("\"asset_class\":\"other\"")!=std::string::npos);
    CHECK(stowed.str().find("\"last_selected_registration_key\":"+key)!=std::string::npos);
    return 0;
}
}
int main(){
    CHECK(EmptyRequiresMeasuredStrokeReleaseAndLaterOwnCohort()==0);
    CHECK(TacticalReturnAndRefillNeedOnlyFreshNativeReadiness()==0);
    CHECK(DelayedTransactionsKeepOriginalClocksAndStowDebt()==0);
    CHECK(ExistingDebtSurvivesPositiveCountRemoval()==0);
    CHECK(ReplacementAtSameAddressCannotInheritOldPolicy()==0);
    CHECK(OccupiedLeftHandAndWrongRigNeverBecomeSlideClaims()==0);
    CHECK(UnknownNativeOutcomeRemainsPendingAcrossStow()==0);
    CHECK(ExactNoEntryRejectionDoesNotInventTransfer()==0);
    CHECK(RefusedNativeReleaseKeepsFireVeto()==0);
    CHECK(DelayedRawContactRetainsItsOriginalPacket()==0);
    CHECK(DrainedActorRetirementReclaimsPendingSlots()==0);
    CHECK(UnknownOrChangedEventProfileCannotCreateOrReplacePolicy()==0);
    CHECK(NativeProfileAndCapacityMismatchCannotClearDebt()==0);
    CHECK(ConfigurationAndGeometrySelectionAreIndependent()==0);
    CHECK(DiagnosticRegistrationSurvivesStowWithoutReadingEquipment()==0);
    std::puts("Bc2PhysicalPistolSlideTests: 15 deterministic event/policy/arbiter/profile/gate compositions; no native or headset acceptance");
}
