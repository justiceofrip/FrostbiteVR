#include "Bc2PhysicalFeed.h"
#include "Bc2ReloadOwnerLoss.h"
#include "Bc2FeedPresentation.h"
#include "Bc2HandPose.h"
#include "Bc2SupportControllerMapping.h"
#include "Test.h"
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <functional>
#include <memory>
#include <sstream>
#include <vector>
using namespace fvr::bc2;
using namespace fvr::interaction;
namespace {
constexpr std::int64_t Ms=1000000;
template<std::size_t N>void Text(std::array<char,N>& out,std::string_view text){
    out={};std::memcpy(out.data(),text.data(),(std::min)(text.size(),N-1));
}
fvr::math::Matrix4 Identity(){fvr::math::Matrix4 m{};for(unsigned i=0;i<4;++i)m.values[i][i]=1;return m;}
fvr::math::Matrix4 Turn(float angle){auto m=Identity();const auto c=std::cos(angle),s=std::sin(angle);
    m.values[0][0]=m.values[1][1]=c;m.values[0][1]=s;m.values[1][0]=-s;return m;}
bool NearPose(const fvr::math::Matrix4& a,const fvr::math::Matrix4& b,float tolerance=.00005f){
    for(unsigned r=0;r<4;++r)for(unsigned c=0;c<4;++c)if(!Near(a.values[r][c],b.values[r][c],tolerance))return false;
    return true;
}
bool DisplayOnlyFeed(const Bc2PhysicalFeedResult& result){
    const auto& p=result.presentation;
    return p&&p->displayInput&&!p->mechanismClaim&&!p->mechanismInput&&!p->mechanismWristWorldMeters&&!p->supplyObject&&
        p->operation==FeedPhysicalOperation::None&&p->targetKind==FeedInteractionTargetKind::None;
}
ReloadRawContact RepublishFeedContact(const ReloadRawContact&,const fvr::math::Matrix4&,std::int64_t);
struct Fixture {
    const Bc2FeedRegistration* registration=FindBc2FeedRegistration(bc2_feed_profile_detail::M60Registration.key);
    // Production keeps these native histories in static storage. Keep each
    // fixture's independent backend off the stack even when tests are inlined.
    std::unique_ptr<FeedEquipmentAdapter> backendStorage=std::make_unique<FeedEquipmentAdapter>();
    FeedEquipmentAdapter& backend=*backendStorage;
    FeedEquipmentNativeView native{};
    std::shared_ptr<SelectedMeshesSnapshot> selected=std::make_shared<SelectedMeshesSnapshot>();
    HandInteraction hands;
    std::uint64_t intent=0,sequence=0,invocation=10;
    std::int64_t now=1000*Ms;
    Bc2PhysicalFeedSample sample{};
    std::optional<fvr::math::Matrix4> wristCalibration;
    std::optional<fvr::math::Matrix4> placedWeapon;
    fvr::math::Matrix4 lastWrist=Identity();
    Bc2PhysicalFeedResult result{};
    std::optional<FeedEquipmentSubmission> submitted;
    bool refuse=false,occupyLeft=false,refuseGate=false;unsigned submits=0,nativeReads=0,nativeReadAts=0;
    bool busyGate=false;
    // Native simulation, controller input and render publication have separate
    // clocks. Defaults preserve the original one-publication-per-input fixture.
    std::vector<unsigned> nativeCadence{1};
    std::size_t nativeTick=0;
    unsigned nativePublications=0,delayedRenders=0,repeatedRenders=0;
    std::uint64_t firstRepeatedSource=0;
    bool delayedRender=false,repeatRender=false,mutateNativeObserved=false,missingCurrentView=false,missingPacketNative=false;
    // Selected-mesh observation can stop while controller/native clocks run.
    // A retained publication keeps its original sequence and deadline.
    bool publishSelected=true;
    std::optional<ReloadOwnerLoss> reloadOwnerLoss;
    ReloadOwnerLossOrigin reloadOwnerLossOrigin=ReloadOwnerLossOrigin::Lifecycle;
    bool resetHandsOnOwnerLoss=false;
    bool hideReceipt=false;
    std::optional<ReloadRawContact> awaitingRender,lastRendered;
    std::optional<FeedEquipmentNativeView> lastAcceptedNative;
    std::uint64_t lastAcceptedInput=0;
    std::array<unsigned,10> reusedNativeByOperation{};
    bool reusedNativeUnchanged=true;
    bool captureCadenceTrace=false;
    std::array<std::string,64> cadenceTrace{};
    unsigned cadenceTraceCount=0;
    // Replay a publication interruption at a named point of the same complete
    // physical trajectory; no separate policy or synthetic completion path.
    std::function<bool(Fixture&,FeedPhysicalOperation,unsigned)> placementStep;
    // Gameplay runs support after feed. Replay that real arbiter interleaving
    // while current input and the delivered raw pose are different packets.
    std::function<int(Fixture&,const ReloadRawContact&)> afterDelayedConsume;
    int afterDelayedConsumeResult=0;
    Bc2PhysicalFeed physical;
    void SelectWeaponData(std::uint32_t weaponData){
        selected=std::make_shared<SelectedMeshesSnapshot>(*selected);selected->weaponData=weaponData;sample.selected=selected;
    }
    std::optional<FeedEquipmentNativeView> NativeRead(std::optional<FeedEquipmentNativeView> view)const {
        // Fault injection only: the real backend never issues mutated copies
        // of one native sequence. Exercise refusal at the physical/core join.
        if(view&&mutateNativeObserved)++view->observedNs;
        return view;
    }
    Bc2PhysicalFeedApi Api(bool observedGate=false){Bc2PhysicalFeedApi api{this,
        [](void* p,const ReloadStateOwner& o,std::int64_t now)noexcept{auto& f=*static_cast<Fixture*>(p);
            ++f.nativeReads;
            return f.missingCurrentView?std::optional<FeedEquipmentNativeView>{}:f.NativeRead(f.backend.Read(o,now));},
        [](void* p,const FeedEquipmentSubmission& s)noexcept{auto& f=*static_cast<Fixture*>(p);++f.submits;f.submitted=s;
            return !f.refuse&&f.backend.Submit(s,f.now+1);},
        [](void* p,std::uint64_t life,std::uint64_t id)noexcept{auto& f=*static_cast<Fixture*>(p);
            return f.hideReceipt?decltype(f.backend.Receipt(life,id)){}:f.backend.Receipt(life,id);},
        [](void* p,const ReloadStateOwner& o,std::int64_t observed,std::int64_t now)noexcept{
            auto& f=*static_cast<Fixture*>(p);++f.nativeReadAts;
            return f.missingPacketNative?std::optional<FeedEquipmentNativeView>{}:f.NativeRead(f.backend.ReadAt(o,observed,now));},
        [](void* p,const FeedEquipmentSubmission& s,bool noEntryOnly)noexcept{auto& f=*static_cast<Fixture*>(p);
            if(!noEntryOnly){++f.submits;f.submitted=s;}
            return f.backend.SubmitResult(s,f.now+1,noEntryOnly||f.refuse);},
        [](void* p,const FeedEquipmentGate& gate)noexcept{auto& f=*static_cast<Fixture*>(p);return !f.refuseGate&&!f.busyGate&&f.backend.PublishGate(gate,f.now+1);},
        [](void* p,const AmmoResourceIdentity& resource)noexcept{return static_cast<Fixture*>(p)->backend.ReadGate(resource);},
        [](void* p,std::uint64_t after)noexcept{return static_cast<Fixture*>(p)->backend.Retirement(after);}};
        if(observedGate)api.publishGateObserved=[](void* p,const FeedEquipmentGate& gate)noexcept{
            auto& f=*static_cast<Fixture*>(p);
            if(f.refuseGate)return FeedEquipmentGateResult::Rejected;
            if(f.busyGate)return FeedEquipmentGateResult::Busy;
            return f.backend.PublishGate(gate,f.now+1)?FeedEquipmentGateResult::Published:FeedEquipmentGateResult::Rejected;
        };
        return api;
    }
    explicit Fixture(const Bc2FeedRegistration* row=nullptr,bool observedGate=false):physical(Api(observedGate)){
        if(row)registration=row;
        if(!registration)std::abort();
        native.identity={{0x10000,0x20000,0x30000,0x40000,1,2,3},{0x50000,0x60000,0x70000},0x80000,0x90000,0xa0000};
        native.binding={native.identity.owner,{{0x20000,1,0x40000,19},2,3},0xb0000,0xc0000,0xd0000,0xe0000,now,now+100*Ms};
        auto& c=native.config;c.weaponData=0xd0000;c.firingData=0xf0000;c.primaryFire=0x100000;c.ammoAddress=c.primaryFire+0x170;
        const auto& g=registration->geometry;Text(c.assetName,g.asset);Text(c.assetPath,g.configurationPath);
        const auto& v=registration->config.values;
        c.fireLogicType=v.fireLogicType;c.reloadType=v.reloadType;c.fireInputAction=v.fireInputAction;c.reloadInputAction=v.reloadInputAction;
        c.baseCapacity=v.baseCapacity;c.numberOfMagazines=v.numberOfMagazines;c.reloadDelay=v.reloadDelay;
        c.reloadTime=v.reloadTime;c.reloadThreshold=v.reloadThreshold;c.postReloadTime=v.postReloadTime;
        c.boltDelay=v.boltDelay;c.boltTime=v.boltTime;c.holdBoltUntilFireRelease=v.holdBoltUntilFireRelease;
        c.holdBoltUntilZoomRelease=v.holdBoltUntilZoomRelease;native.counts={20,130,v.baseCapacity};
        sample.owner=native.identity.owner;sample.weapon={sample.owner.weapon,7};sample.asset=g.asset;sample.configurationPath=g.configurationPath;
        sample.equipment=WeaponEquipmentIdentity{sample.owner.weapon,native.binding.data,native.binding.persistence,c.assetName};
        sample.meshPath=g.meshPath;sample.rigFingerprint=g.rigFingerprint;sample.selectedRigVerified=true;
        sample.input.owner={(std::uint64_t(sample.owner.weak)<<32)|sample.owner.soldier,1,7,3};
        sample.input.focused=true;sample.input.tracked={true,true};
        sample.selected=selected;selected->owner=sample.owner;selected->weaponData=c.weaponData;selected->inventory=native.binding.inventory;
        Text(selected->weaponName,g.asset);Text(selected->configurationPath,g.configurationPath);selected->configurationPathVerified=true;
        selected->configurationPathPointer=0xf1000;selected->stateCount=1;selected->soleConfiguredArray=0xf2000;
        selected->states[0].array=selected->soleConfiguredArray;selected->states[0].count=1;
        selected->states[0].meshes[0].address=0xf3000;Text(selected->states[0].meshes[0].assetPath,g.meshPath);
        sample.raw.valid=true;sample.raw.owner=sample.owner;sample.raw.rig.soldier=sample.owner.soldier;
        sample.raw.rig.weak=sample.owner.weak;sample.raw.rig.count=170;sample.raw.rigFingerprint=g.rigFingerprint;
        sample.raw.weaponWorldMeters=Identity();
    }
    void PrepareTick(const fvr::math::Matrix4& wrist,bool grip,bool cancel=false,std::int64_t inputAge=100*Ms){
        lastWrist=wrist;
        const auto age=(std::min)(100*Ms,registration->mechanism.maxAgeNs);
        now+=10*Ms;++sequence;
        const auto publications=nativeCadence[nativeTick++%nativeCadence.size()];
        for(unsigned n=0;n<publications;++n){
            // Observations occur at actual fixture clock instants before this
            // input. An idle native clock changes neither sequence nor lease.
            native.observedNs=now-1-(publications-1-n)*(10*Ms/publications);
            native.deadlineNs=native.observedNs+age;
            native.binding.observedNs=native.observedNs;native.binding.deadlineNs=native.deadlineNs;
            (void)backend.Publish(native,native.observedNs+1);++nativePublications;
        }
        sample.binding=native.binding;
        if(publishSelected){selected=std::make_shared<SelectedMeshesSnapshot>(*selected);
            selected->sequence=sequence;selected->observedNs=native.observedNs;selected->deadlineNs=native.deadlineNs;
            sample.selected=selected;}
        sample.input.sequence=sequence;sample.input.observedNs=now;sample.input.deadlineNs=now+(std::min)(age,inputAge);sample.input.nowNs=now+1;
        sample.input.released[0]=!grip;sample.grip=grip;sample.cancel=cancel;
        sample.raw.inputEvidence=sample.input;sample.raw.rawLeftWristWorldMeters=wrist;sample.frameSequence=sequence;
        if(wristCalibration){
            InputFrame input;input.generation=sequence;input.spaceGeneration=sample.owner.space;input.predictedNs=now;
            input.focused=input.headValid=true;for(auto& hand:input.hands)hand.gripTracked=hand.aimTracked=true;
            auto rig=sample.raw.rig;rig.pose=0x110000;rig.skeleton=0x120000;
            sample.raw=BuildSupportControllerMapping(sample.owner,sample.input,input,rig,sample.rigFingerprint,
                feed_interaction_detail::Multiply(*wristCalibration,wrist),Identity(),Identity(),1,sample.input.nowNs);
        }
        if(placedWeapon){
            sample.raw.weaponWorldMeters=*placedWeapon;
            sample.raw.rawLeftWristWorldMeters=feed_interaction_detail::Multiply(sample.raw.rawLeftWristWorldMeters,*placedWeapon);
        }
        // Run the gameplay decision before this packet can renew hand authority.
        // The callbacks use the same physical suspension and publication loss
        // as ClearReloadOwner; no fixture-local cancellation policy is copied.
        if(reloadOwnerLoss){
            (void)ApplyReloadOwnerFeedLoss(*reloadOwnerLoss,reloadOwnerLossOrigin,
                physical.HasCurrentOwnerEvidence(sample.owner,sample.input.nowNs),
                [&]{physical.Suspend(sample.input,hands,{Bc2PhysicalFeedSuspendSource::ReloadOwnerLoss,unsigned(*reloadOwnerLoss)});},
                [&]{sample.selected.reset();});
            if(resetHandsOnOwnerLoss)(void)hands.Reset();
        }
        const HandContactProof gunProof{{1,7},sequence,sample.input.deadlineNs,true};
        const auto gun=hands.Current(InteractionHand::Right);
        if(gun)(void)hands.UpdateAndRenewGunHold(sample.input,gun->token,gunProof);
        else {(void)hands.Update(sample.input);(void)hands.Acquire(sample.input,{sample.input.owner,InteractionHand::Right,
            HandClaimKind::GunHold,sample.weapon,gunProof,++intent,0});}
        if(occupyLeft)(void)hands.Acquire(sample.input,{sample.input.owner,InteractionHand::Left,
            HandClaimKind::BodyInventory,{999,1},{{998,1},sequence,sample.input.deadlineNs,true},++intent,0});
    }
    Bc2PhysicalFeedResult Consume(const ReloadRawContact* rendered=nullptr){
        if(rendered){sample.raw=*rendered;sample.frameSequence=rendered->inputEvidence.sequence;}
        result=physical.Tick(sample,hands,intent);
        if(result.diagnostic.policyEvaluated&&result.presentation&&result.ownsHand&&!result.pending&&
           (result.interactionStatus==FeedInteractionStatus::Acquired||result.interactionStatus==FeedInteractionStatus::Moving||
            result.interactionStatus==FeedInteractionStatus::Endpoint)){
            const auto source=backend.ReadAt(sample.owner,sample.raw.inputEvidence.observedNs,sample.input.nowNs);
            if(source){
                if(lastAcceptedNative&&source->sequence==lastAcceptedNative->sequence&&
                   sample.raw.inputEvidence.sequence>lastAcceptedInput){
                    ++reusedNativeByOperation[unsigned(result.presentation->operation)];
                    reusedNativeUnchanged=reusedNativeUnchanged&&*source==*lastAcceptedNative;
                }
                lastAcceptedNative=source;lastAcceptedInput=sample.raw.inputEvidence.sequence;
            }
        }
        if(std::getenv("FVR_FEED_TEST_TRACE"))std::fprintf(stderr,"profile=%llu tick=%llu grip=%d selected=%d blocked=%d owns=%d pending=%d status=%u interaction=%u reload=%u hand=%u angle=%.4f submits=%u\n",
            static_cast<unsigned long long>(registration->key),static_cast<unsigned long long>(sequence),sample.grip,result.selected,result.blocksFire,result.ownsHand,result.pending,
            unsigned(result.status),unsigned(result.interactionStatus),unsigned(result.reloadStatus),unsigned(result.handReason),result.presentation?result.presentation->coverRadians:-99.f,submits);
        if(captureCadenceTrace){
            char row[512];const auto left=hands.Current(InteractionHand::Left);
            std::snprintf(row,sizeof(row),"input=%llu source=%llu native=%llu grip=%d/%d op=%u owns=%d left=%u status=%u interaction=%u reload=%u hand=%u exit=%u cover=%.4f submits=%u",
                static_cast<unsigned long long>(sample.input.sequence),static_cast<unsigned long long>(sample.raw.inputEvidence.sequence),
                static_cast<unsigned long long>(result.diagnostic.sourceNativeSequence),sample.grip,!sample.raw.inputEvidence.released[0],
                result.presentation?unsigned(result.presentation->operation):0,result.ownsHand,left?unsigned(left->token.kind):0,
                unsigned(result.status),unsigned(result.interactionStatus),unsigned(result.reloadStatus),unsigned(result.handReason),
                unsigned(result.diagnostic.exit),result.presentation?result.presentation->coverRadians:-99.f,submits);
            cadenceTrace[cadenceTraceCount++%cadenceTrace.size()]=row;
        }
        return result;
    }
    void PrintCadenceFailure(unsigned cadence,FeedPhysicalOperation operation)const {
        std::fprintf(stderr,"Independent feed failure: profile=%llu asset=%.*s cadence=%u delayed=%d operation=%u\n",
            static_cast<unsigned long long>(registration->key),int(registration->geometry.asset.size()),registration->geometry.asset.data(),
            cadence,delayedRender,unsigned(operation));
        const auto count=(std::min)(cadenceTraceCount,unsigned(cadenceTrace.size()));
        for(unsigned n=cadenceTraceCount-count;n<cadenceTraceCount;++n)std::fprintf(stderr,"  %s\n",cadenceTrace[n%cadenceTrace.size()].c_str());
    }
    Bc2PhysicalFeedResult Tick(const fvr::math::Matrix4& wrist,bool grip,bool cancel=false){
        PrepareTick(wrist,grip,cancel);
        if(!delayedRender)return Consume();
        const auto next=sample.raw;
        if(repeatRender&&lastRendered&&grip&&result.ownsHand&&!result.pending&&!lastRendered->inputEvidence.released[0]){
            // A render pass can map the previous source again while current
            // controller evidence has advanced. It cannot contribute motion.
            const auto repeated=RepublishFeedContact(*lastRendered,lastRendered->weaponWorldMeters,sample.input.nowNs);
            if(!firstRepeatedSource)firstRepeatedSource=repeated.inputEvidence.sequence;
            Consume(&repeated);++repeatedRenders;
        }
        if(awaitingRender){Consume(&*awaitingRender);++delayedRenders;lastRendered=awaitingRender;}
        else {Consume(&next);lastRendered=next;}
        if(afterDelayedConsume&&!afterDelayedConsumeResult)afterDelayedConsumeResult=afterDelayedConsume(*this,next);
        awaitingRender=next;return result;
    }
    FeedMotionProfile Open()const {return *BuildBc2FeedMotion(registration->geometry,
        FeedPhysicalOperation::OpenCover,registration->interactionPolicy);}
    unsigned DwellPackets()const {
        return unsigned(registration->interactionPolicy.endpointDwellNs/(10*Ms))+2;
    }
    bool OpenCover(){
        using namespace feed_interaction_detail;const auto m=Open();
        const auto start=feed_interaction_detail::Multiply(m.motionFromContact,m.weaponFromMotion);
        Tick(start,false);Tick(start,true);
        for(unsigned step=1;step<=12;++step)Tick(feed_interaction_detail::Multiply(Along(m.motionFromContact,m.kind,m.travel*step/12),m.weaponFromMotion),true);
        for(unsigned step=0;step<DwellPackets()&&!submitted;++step)Tick(feed_interaction_detail::Multiply(Along(m.motionFromContact,m.kind,m.travel),m.weaponFromMotion),true);
        return submitted.has_value();
    }
    bool Callback(unsigned mask=7){
        if(!submitted)return false;const auto& c=submitted->command;
        FeedEquipmentNativeCall call{c,native.identity,++invocation,now+2,now+3,true,c.mutationRequired,true,true};
        if(!backend.Call(call))return false;
        for(unsigned branch:{2u,0u,1u})if(mask&(1u<<branch)){
            AmmoResourceOwnUpdate row;row.beforeOwner=row.afterOwner=call.identity.owner;row.firing=call.identity.firing[branch];
            row.serverPlayer=call.identity.serverPlayer;row.serverSoldier=call.identity.serverSoldier;row.serverItem=call.identity.serverItem;
            row.invocation=branch==2?call.invocation:++invocation;row.beginNs=now+1;row.endNs=now+4+branch;row.branch=branch;row.depth=1;
            row.loadedBefore=c.before.counts.loaded;row.reserveBefore=c.before.counts.reserve;
            row.loadedAfter=c.after.loaded;row.reserveAfter=c.after.reserve;row.current=row.next=2;
            row.finished=row.identityRetained=row.neutralContext=true;(void)backend.Observe(row,now+10);
        }
        if(mask==7)native.counts=c.after;
        return backend.Receipt(c.request.lifetime,c.request.id).has_value();
    }
    bool Reconcile(){
        if(!Callback())return false;
        Tick(lastWrist,false);
        Tick(lastWrist,false);
        return !result.pending;
    }
    bool NoEntryCallbacks(){
        if(!submitted)return false;
        for(unsigned branch:{2u,0u,1u}){
            AmmoResourceOwnUpdate row;row.beforeOwner=row.afterOwner=native.identity.owner;row.firing=native.identity.firing[branch];
            row.serverPlayer=native.identity.serverPlayer;row.serverSoldier=native.identity.serverSoldier;row.serverItem=native.identity.serverItem;
            row.invocation=++invocation;row.beginNs=now+2;row.endNs=now+4+branch;row.branch=branch;row.depth=1;
            row.loadedBefore=row.loadedAfter=native.counts.loaded;row.reserveBefore=row.reserveAfter=native.counts.reserve;
            row.current=row.next=2;row.finished=row.identityRetained=row.neutralContext=true;
            (void)backend.Observe(row,now+10);
        }
        const auto receipt=backend.Receipt(submitted->command.request.lifetime,submitted->command.request.id);
        return receipt&&receipt->acknowledgement.status==FeedReloadAcknowledgement::Rejected&&receipt->noTransferEntry;
    }
    bool PickSupply(){
        auto chest=Identity();const auto config=Bc2FeedSupplyConfig();
        for(unsigned n=0;n<3;++n)chest.values[3][n]=config.pouchCenterMeters[n];
        sample.bodyFromHand=chest;
        // Chest rebasing follows a suspension and intentionally still needs
        // a newer native observation. Hold the physical neutral through one
        // scheduled native cycle and its render lag before the pickup edge.
        const auto neutralPackets=(std::max)(std::size_t{2},nativeCadence.size()+(delayedRender?1u:0u));
        for(std::size_t packet=0;packet<neutralPackets;++packet)Tick(chest,false);
        Tick(chest,true);
        const auto hand=hands.Current(InteractionHand::Left);
        return hand&&hand->token.kind==HandClaimKind::AmmoObject&&result.ownsHand;
    }
    bool PickSupplyFromHeldApproach(bool skipPickupRender=false){
        auto contact=Identity();const auto config=Bc2FeedSupplyConfig();
        for(unsigned axis=0;axis<3;++axis)contact.values[3][axis]=config.pouchCenterMeters[axis];
        auto outside=contact;outside.values[3][0]+=config.pouchRadiusMeters+.25f;
        // The only neutral is physically outside both supply contacts. The
        // player then squeezes while approaching and arrives already gripping.
        sample.bodyFromHand=outside;Tick(outside,false);Tick(outside,true);
        sample.bodyFromHand=contact;
        if(skipPickupRender){
            // The input really reaches the belt and acquires the box, but its
            // own raw render is skipped. The next rendered hand has already
            // left the belt; no held-at-belt catch-up packet is supplied.
            const auto previous=awaitingRender?*awaitingRender:sample.raw;
            PrepareTick(contact,true);Consume(&previous);
            const auto acquired=hands.Current(InteractionHand::Left);
            if(!acquired||acquired->token.kind!=HandClaimKind::AmmoObject)return false;
            awaitingRender.reset();lastRendered.reset();sample.bodyFromHand=outside;
            for(std::size_t n=0;n<nativeCadence.size()+3;++n)Tick(outside,true);
        }else{
            Tick(contact,true);
            for(std::size_t n=0;n<nativeCadence.size()+(delayedRender?1u:0u);++n)Tick(contact,true);
        }
        const auto hand=hands.Current(InteractionHand::Left);
        return hand&&hand->token.kind==HandClaimKind::AmmoObject&&result.ownsHand&&
            (result.interactionStatus==FeedInteractionStatus::Acquired||result.interactionStatus==FeedInteractionStatus::Moving);
    }
    std::optional<FeedMotionProfile> PlacementMotion(FeedPhysicalOperation op)const {
        if(!result.presentation)return {};
        const auto retained=result.presentation;auto pose=registration->geometry.poses[unsigned(Bc2FeedPoseId::Resting)];
        pose.chainCount=retained->chainCount;
        if(retained->registrationKey!=registration->key||pose.chainCount!=registration->geometry.chainCount)return {};
        pose.weaponFromPouch=retained->weaponFromPouch;pose.weaponFromFeedRoot=retained->weaponFromChain[0];
        for(unsigned i=0;i+1<pose.chainCount;++i)pose.parentFromChainChild[i]=feed_interaction_detail::Multiply(
            retained->weaponFromChain[i+1],feed_interaction_detail::Inverse(retained->weaponFromChain[i]));
        return BuildBc2FeedMotionFromRetainedPose(registration->geometry,op,registration->interactionPolicy,pose);
    }
    bool Place(FeedPhysicalOperation op,bool acquireSupply=true,bool release=true,bool supplyAlreadyHeld=false){
        const bool picked=op==FeedPhysicalOperation::AttachContainer&&(acquireSupply||supplyAlreadyHeld);
        if(picked&&!supplyAlreadyHeld&&!PickSupply())return false;
        const auto motion=PlacementMotion(op);
        if(!motion)return false;submitted.reset();
        auto sourceFromWrist=motion->placement.sourceFromGrip;
        if(picked){
            // The retained source already follows the calibrated pickup hand.
            // Preserve that grasp, then undo calibration for PrepareTick's input.
            const auto localWrist=feed_interaction_detail::Multiply(sample.raw.rawLeftWristWorldMeters,
                feed_interaction_detail::Inverse(sample.raw.weaponWorldMeters));
            sourceFromWrist=feed_interaction_detail::Multiply(localWrist,
                feed_interaction_detail::Inverse(motion->placement.weaponFromSource));
            if(wristCalibration)sourceFromWrist=feed_interaction_detail::Multiply(
                feed_interaction_detail::Inverse(*wristCalibration),sourceFromWrist);
        }
        const auto contact=[&](const fvr::math::Matrix4& object){return feed_interaction_detail::Multiply(sourceFromWrist,object);};
        const auto start=contact(motion->placement.weaponFromSource);
        if(!picked){Tick(start,false);Tick(start,false);}Tick(start,true);
        for(unsigned step=1;step<=30;++step){
            if(placementStep&&!placementStep(*this,op,step))return false;
            Tick(contact(reload_insertion_detail::BlendRigid(
                motion->placement.weaponFromSource,motion->placement.weaponFromDestination,double(step)/30)),true);
        }
        const auto end=contact(motion->placement.weaponFromDestination);
        for(unsigned step=0;step<DwellPackets();++step)Tick(end,true);
        if(!release)return result.interactionStatus==FeedInteractionStatus::Endpoint&&result.ownsHand&&!submitted;
        Tick(end,false);
        if(delayedRender)Tick(end,false); // Deliver the actual delayed release packet.
        return submitted&&submitted->command.authority.operation==op;
    }
    bool ResumePlacementEndpoint(FeedPhysicalOperation op,const fvr::math::Matrix4& endpoint,const HandClaimToken& lostClaim){
        // The caller supplies fresh neutral packets first. Recover the actual
        // retained endpoint through the recognizer's proof; constructing a new
        // full motion here would incorrectly require a second source-to-end path.
        if(!result.presentation||result.interactionStatus!=FeedInteractionStatus::Armed||
           hands.Current(InteractionHand::Left))return false;
        const auto retained=*result.presentation;const auto counts=native.counts;const auto priorSubmits=submits;
        const auto unchanged=[&]{return !submitted&&!backend.Pending()&&!result.pending&&submits==priorSubmits&&
            native.counts==counts&&result.presentation&&result.presentation->lifetime==retained.lifetime&&
            result.presentation->supply==retained.supply&&result.presentation->registrationKey==retained.registrationKey;};
        if(!unchanged())return false;
        Tick(endpoint,true);if(!unchanged())return false;
        if(delayedRender){Tick(endpoint,true);if(!unchanged())return false;}
        const auto claim=hands.Current(InteractionHand::Left);
        if(!claim||claim->token==lostClaim||claim->token.kind!=lostClaim.kind||!result.ownsHand||
           result.interactionStatus!=FeedInteractionStatus::Acquired)return false;
        for(unsigned packet=0;packet<DwellPackets();++packet){
            Tick(endpoint,true);const auto current=hands.Current(InteractionHand::Left);
            if(!unchanged()||!result.ownsHand||!current||current->token!=claim->token)return false;
        }
        if(result.interactionStatus!=FeedInteractionStatus::Endpoint)return false;
        Tick(endpoint,false);
        if(delayedRender)Tick(endpoint,false);
        return submitted&&submits==priorSubmits+1&&native.counts==counts&&
            submitted->command.request.lifetime==retained.lifetime&&submitted->command.authority.operation==op;
    }
    bool Close(){
        const auto motion=BuildBc2FeedMotion(registration->geometry,FeedPhysicalOperation::CloseCover,registration->interactionPolicy);
        if(!motion)return false;submitted.reset();using namespace feed_interaction_detail;
        const auto start=feed_interaction_detail::Multiply(motion->motionFromContact,motion->weaponFromMotion);
        Tick(start,false);Tick(start,false);Tick(start,true);
        for(unsigned step=1;step<=12;++step)Tick(feed_interaction_detail::Multiply(Along(motion->motionFromContact,motion->kind,motion->travel*step/12),motion->weaponFromMotion),true);
        for(unsigned step=0;step<DwellPackets()&&!submitted;++step)Tick(feed_interaction_detail::Multiply(Along(motion->motionFromContact,motion->kind,motion->travel),motion->weaponFromMotion),true);
        return submitted&&submitted->command.authority.operation==FeedPhysicalOperation::CloseCover;
    }
    bool BlocksFire()const {return sample.equipment&&physical.BlocksFire(sample.owner,*sample.equipment,sample.binding);}
};
int FeedPresentationReachesArmAndHand(const Fixture& f,const fvr::math::Matrix4& expected,
    const std::optional<fvr::math::Matrix4>& currentWeapon=std::nullopt){
    const auto& p=*f.result.presentation;const auto now=f.sample.input.nowNs;const auto& g=f.registration->geometry;
    CHECK(Bc2FeedSourceFresh(p,f.sample.owner,f.sample.input,*f.sample.equipment,p.rig,now));
    CHECK(Bc2FeedMechanismClaimFresh(p,now)&&p.mechanismWristWorldMeters);
    const auto placed=currentWeapon?*currentWeapon:f.sample.raw.weaponWorldMeters;
    const auto targetWrist=Bc2FeedMechanismWristWorld(p,placed);CHECK(targetWrist&&NearPose(*targetWrist,expected));
    // Structural anatomy/parts fixture drives the same pure palette, IK and
    // MechanismGrip consumers as RigPublication. It grants no production
    // fingerprint binding, native callback execution or headset acceptance.
    RigSnapshot rig;rig.identity=p.rig;rig.weaponBone=g.rootBone.index;
    const auto count=rig.identity.count;rig.names.resize(count);rig.parents.assign(count,std::int32_t(rig.weaponBone));
    CHECK(rig.weaponBone<count);
    rig.evaluatedWorld.assign(count,Identity());rig.inverseBind.assign(count,Identity());
    std::vector<bool> used(count,false);
    for(unsigned i=0;i<count;++i)rig.names[i]="synthetic"+std::to_string(i);
    rig.parents[rig.weaponBone]=-1;rig.names[rig.weaponBone]=g.rootBone.bone;used[rig.weaponBone]=true;
    const auto part=[&](const Bc2FeedChainBone& bone){
        if(bone.index>=count||bone.parentIndex>=count)return false;
        rig.names[bone.index]=bone.bone;rig.parents[bone.index]=std::int32_t(bone.parentIndex);used[bone.index]=true;
        return true;
    };
    CHECK(part(g.pouchBone)&&part(g.coverBone));for(unsigned i=0;i<g.chainCount;++i)CHECK(part(g.chain[i]));
    for(unsigned i=0;i<g.coverCompanionCount;++i)CHECK(part(g.coverCompanions[i].bone));
    std::vector<unsigned> free;for(unsigned i=0;i<count;++i)if(!used[i])free.push_back(i);CHECK(free.size()>=21);
    rig.left={free[0],free[1],free[2]};rig.right={free[3],free[4],free[5]};
    const auto at=[&](float x,float y,float z){auto m=Identity();m.values[3]={x,y,z,1};return m;};
    const auto x=expected.values[3][0],y=expected.values[3][1],z=expected.values[3][2];
    for(const auto arm:{rig.left,rig.right}){
        rig.parents[arm.elbow]=std::int32_t(arm.shoulder);rig.parents[arm.wrist]=std::int32_t(arm.elbow);
        rig.evaluatedWorld[arm.shoulder]=at(x-.4f,y+.3f,z);rig.evaluatedWorld[arm.elbow]=at(x-.2f,y+.1f,z);
        rig.evaluatedWorld[arm.wrist]=at(x+.03f,y,z);
    }
    rig.names[rig.left.wrist]="LeftHand";
    const std::array<const char*,5> fingers{"Thumb","Index","Middle","Ring","Pinky"};
    for(unsigned digit=0;digit<5;++digit)for(unsigned joint=0;joint<3;++joint){
        const auto index=free[6+digit*3+joint];const auto parent=joint?free[6+digit*3+joint-1]:rig.left.wrist;
        rig.names[index]=std::string("LeftHand")+fingers[digit]+std::to_string(joint+1);rig.parents[index]=std::int32_t(parent);
        const auto local=joint?(digit==0?at(.016f,0,.023f):at(0,0,joint==1?.03f:.022f)):
            (digit==0?at(.03f,-.018f,.022f):at((2.5f-float(digit))*.02f,0,.065f));
        rig.evaluatedWorld[index]=feed_interaction_detail::Multiply(local,rig.evaluatedWorld[parent]);
    }
    for(unsigned i=0;i<count;++i)rig.inverseBind[i]=feed_interaction_detail::Inverse(rig.evaluatedWorld[i]);
    const auto binding=feed_presentation_detail::Derive(rig,g);CHECK(binding);
    const auto bases=Bc2FeedWorldBases(p,placed);CHECK(bases);
    const Bc2FeedPresentation parts{p.supply,p.weaponFromPouch,p.weaponFromChain,p.chainCount};
    const auto frames=Bc2FeedPartWorldFrames(g,parts,p.weaponFromCoverHinge,*bases,1);CHECK(frames);
    const auto writes=BuildBc2FeedPartWrites(*binding,rig,*frames);CHECK(writes);
    CHECK(std::any_of(writes->begin(),writes->end(),[&](const auto& w){return w.index==g.coverBone.index&&NearPose(w.transform,(*frames)[1]);}));
    const auto wrist=feed_presentation_detail::ScaleTranslation(*targetWrist,1);
    const auto solved=SolveTrackedArms(rig.parents,rig.evaluatedWorld,rig.left,rig.right,
        {wrist,{0,0,1},{},true,ArmReachPolicy::ExtendToAdmittedWrist},{{},{},{},false});
    CHECK(solved&&!solved->reachClamped[0]&&solved->targetError[0]<=.0001f);
    CHECK(std::any_of(solved->writes.begin(),solved->writes.end(),[&](const auto& w){return w.index==rig.left.wrist&&NearPose(w.transform,expected);}));
    const auto hand=bc2_hand_detail::Derive(rig.names,rig.parents,rig.inverseBind,1);CHECK(hand);
    const auto fingersPosed=GenerateHandPose(rig.parents,hand->referenceWorld,rig.evaluatedWorld,hand->pose,wrist,
        LeftHandTargets(HandPoseRole::MechanismGrip,0,0));CHECK(fingersPosed&&fingersPosed->writes.size()==16);
    CHECK(std::any_of(fingersPosed->writes.begin(),fingersPosed->writes.end(),[&](const auto& w){return w.index==rig.left.wrist&&NearPose(w.transform,expected);}));
    return 0;
}
int ExactSelectedJoinAndPhysicalOwner(){
    Fixture f;CHECK(f.OpenCover());CHECK(f.submits==1);CHECK(f.result.blocksFire&&f.result.pending);
    CHECK(FindBc2FeedRegistration(f.submitted->source.config)==f.registration);
    CHECK(f.submitted->geometry.registrationKey==f.registration->key);
    CHECK(f.submitted->geometry.profile==f.registration->mechanism);
    CHECK(f.submitted->geometry.rigFingerprint==f.registration->geometry.rigFingerprint);
    CHECK(f.submitted->command.authority.effect==FeedAmmunitionEffect::PhysicalOnly);
    CHECK(f.submitted->command.before.counts.loaded==20&&f.submitted->command.after.loaded==20);
    CHECK(f.submitted->command.before.loadedSupply==f.submitted->command.loadedSupplyAfter);
    CHECK(f.submitted->command.request.completion.candidate.refill.state==FeedRefillState::Required);
    CHECK(f.Callback());const auto supply=f.submitted->command.loadedSupplyAfter;
    auto m=f.Open();auto wrist=feed_interaction_detail::Multiply(feed_interaction_detail::Along(m.motionFromContact,m.kind,m.travel),m.weaponFromMotion);
    f.Tick(wrist,false);CHECK(f.result.blocksFire&&!f.result.pending);CHECK(f.result.presentation);
    CHECK(f.result.presentation->registrationKey==f.registration->key);
    CHECK(f.result.presentation->chainCount==f.registration->geometry.chainCount);
    CHECK(f.result.presentation->supply==supply);CHECK(f.result.presentation->coverRadians>1.4f);
    CHECK(f.native.counts.loaded==20&&f.native.counts.reserve==130);return 0;
}
int WrongRegistrationCannotBorrowValidAuthority(){
    Fixture f;CHECK(f.OpenCover());const auto valid=*f.submitted;
    CHECK(FeedEquipmentSubmissionValid(valid,f.now+1));
    auto wrong=valid;wrong.geometry.registrationKey^=1;
    CHECK(!FeedEquipmentSubmissionValid(wrong,f.now+1));
    CHECK(f.backend.SubmitResult(wrong,f.now+1)==FeedEquipmentSubmitResult::Invalid);
    wrong=valid;wrong.geometry.registrationKey=0;
    CHECK(!FeedEquipmentSubmissionValid(wrong,f.now+1));
    wrong=valid;++wrong.geometry.profile.revision;
    CHECK(!FeedEquipmentSubmissionValid(wrong,f.now+1));
    wrong=valid;++wrong.geometry.rigFingerprint;
    CHECK(!FeedEquipmentSubmissionValid(wrong,f.now+1));
    CHECK(f.backend.Pending()&&f.backend.Pending()->command==valid.command);
    CHECK(f.result.blocksFire&&f.result.pending&&f.BlocksFire());
    CHECK(f.submits==1);CHECK(f.Reconcile());return 0;
}
int ExactNativeConfigurationAndGeometryAreBothRequired(){
    Fixture config;config.native.config.reloadTime=std::nextafter(config.native.config.reloadTime,100.f);
    CHECK(!FindBc2FeedRegistration(config.native.config));CHECK(!config.OpenCover());
    CHECK(!config.result.selected&&!config.result.presentation&&config.submits==0);
    Fixture geometry;CHECK(FindBc2FeedRegistration(geometry.native.config)==geometry.registration);
    geometry.sample.meshPath="fixture/unregistered_feed_mesh";
    CHECK(!geometry.OpenCover());CHECK(!geometry.result.selected&&!geometry.result.presentation&&geometry.submits==0);
    // Loss of the exact selector suspends geometry, retaining the same item's
    // completed cover state and inventory debt for its eventual return.
    Fixture retained;CHECK(retained.OpenCover());CHECK(retained.Reconcile());
    const auto original=retained.result.presentation;const auto path=retained.sample.configurationPath;
    retained.sample.configurationPath="fixture/unregistered_feed_configuration";retained.Tick(Identity(),false);
    CHECK(!retained.result.selected&&!retained.result.presentation&&retained.BlocksFire());
    CHECK(!retained.result.blocksEquipment);
    retained.sample.configurationPath=path;retained.Tick(Identity(),false);
    CHECK(retained.result.selected&&retained.result.presentation&&retained.BlocksFire());
    CHECK(retained.result.presentation->registrationKey==original->registrationKey);
    CHECK(retained.result.presentation->lifetime==original->lifetime&&retained.result.presentation->supply==original->supply);
    CHECK(retained.result.presentation->coverRadians==original->coverRadians);return 0;
}
int DiagnosticsIdentifyRegisteredProfileAndRetainSelection(){
    Fixture f;CHECK(f.OpenCover());std::ostringstream first;f.physical.Report(first);
    const auto key=std::to_string(f.registration->key);
    CHECK(first.str().find("\"asset_class\":\"registered_feed\"")!=std::string::npos);
    CHECK(first.str().find("\"registration_key\":"+key)!=std::string::npos);
    CHECK(first.str().find("\"last_selected_registration_key\":"+key)!=std::string::npos);
    f.sample.asset="fixture/unregistered_feed_asset";f.Tick(Identity(),false,true);
    std::ostringstream stowed;f.physical.Report(stowed);
    CHECK(stowed.str().find("\"asset_class\":\"other\"")!=std::string::npos);
    CHECK(stowed.str().find("\"last_selected_registration_key\":"+key)!=std::string::npos);
    CHECK(f.BlocksFire());return 0;
}
int MissingCallbackAndTrackingPreserveDebt(){
    Fixture f;CHECK(f.OpenCover());CHECK(!f.Callback(5));CHECK(f.backend.Pending());
    const auto life=f.submitted->command.request.lifetime;
    f.Tick(Identity(),false,true);CHECK(f.BlocksFire());CHECK(!f.result.presentation);
    CHECK(f.backend.Pending()->command.request.lifetime==life);CHECK(f.submits==1);return 0;
}
int RefusedSubmitIsNeverAnAcknowledgement(){
    Fixture f;f.refuse=true;CHECK(f.OpenCover());CHECK(f.result.submitRefused);CHECK(!f.backend.Pending());
    CHECK(f.BlocksFire());
    for(unsigned n=0;n<4;++n)f.Tick(Identity(),false,true);
    CHECK(f.submits==1&&f.BlocksFire());
    CHECK(!f.backend.Receipt(f.submitted->command.request.lifetime,f.submitted->command.request.id));return 0;
}
int OccupiedHandAndExactRigAreRequired(){
    Fixture f;const auto motion=f.Open();const auto wrist=feed_interaction_detail::Multiply(motion.motionFromContact,motion.weaponFromMotion);
    f.Tick(wrist,false);f.Tick(wrist,true);
    // Another consumer cannot steal the established mechanism claim.
    const auto occupied=f.hands.Current(InteractionHand::Left);CHECK(occupied&&occupied->token.kind==HandClaimKind::Mechanism);
    const auto knife=f.hands.Acquire(f.sample.input,{f.sample.input.owner,InteractionHand::Left,HandClaimKind::BodyInventory,
        {999,1},{{998,1},f.sequence,f.sample.input.deadlineNs,true},++f.intent,0});CHECK(!knife.accepted);
    CHECK(f.submits==0);f.sample.selectedRigVerified=false;f.Tick(wrist,true);CHECK(f.submits==0);
    CHECK(f.BlocksFire());CHECK(!f.result.presentation);
    Fixture wrong;wrong.sample.configurationPath="Objects/Weapons/Handheld/BU_lmg_M60/other";
    CHECK(!wrong.OpenCover());CHECK(wrong.submits==0);return 0;
}
int AdmissionObservationRetainsFirstSeatPressWithoutGrantingAuthority(){
    const auto observe=[](){std::ostringstream out;out<<std::boolalpha;WriteBc2PhysicalFeedAdmissionObservation(out);return out.str();};
    const auto original=observe();
    Fixture f;CHECK(f.OpenCover()&&f.Reconcile());
    CHECK(f.Place(FeedPhysicalOperation::DetachContainer)&&f.Reconcile());
    CHECK(f.Place(FeedPhysicalOperation::AttachContainer)&&f.Reconcile());
    CHECK(f.submits==3&&f.result.presentation&&!f.result.ownsHand);
    CHECK(observe()==original); // Normal gameplay does not opt in diagnostics.
    const auto& p=*f.result.presentation;
    auto pose=f.registration->geometry.poses[unsigned(Bc2FeedPoseId::Resting)];
    pose.chainCount=p.chainCount;pose.weaponFromPouch=p.weaponFromPouch;pose.weaponFromFeedRoot=p.weaponFromChain[0];
    for(unsigned i=0;i+1<pose.chainCount;++i)pose.parentFromChainChild[i]=feed_interaction_detail::Multiply(
        p.weaponFromChain[i+1],feed_interaction_detail::Inverse(p.weaponFromChain[i]));
    const auto motion=BuildBc2FeedMotionFromRetainedPose(f.registration->geometry,FeedPhysicalOperation::SeatFeed,f.registration->interactionPolicy,pose);
    CHECK(motion);const auto wrist=feed_interaction_detail::Multiply(motion->placement.sourceFromGrip,motion->placement.weaponFromSource);
    f.Tick(wrist,false);f.Tick(wrist,false);const auto neutral=f.sample.raw;
    RequestBc2PhysicalFeedAdmissionObservation(f.now);
    f.PrepareTick(wrist,true);const auto pressed=f.sample.raw;f.Consume(&neutral);
    CHECK(!f.result.ownsHand&&f.submits==3);
    const auto first=observe();const auto split=first.find(",\"latestChange\":");CHECK(split!=std::string::npos);
    const auto firstPrefix=first.substr(0,split);
    const auto stamp=[](const HandInteractionSample& s){return "\"sequence\":"+std::to_string(s.sequence)+",\"observedNs\":"+
        std::to_string(s.observedNs)+",\"deadlineNs\":"+std::to_string(s.deadlineNs)+",\"nowNs\":"+std::to_string(s.nowNs);};
    CHECK(firstPrefix.find(stamp(pressed.inputEvidence))!=std::string::npos);
    CHECK(firstPrefix.find(stamp(neutral.inputEvidence))!=std::string::npos);
    CHECK(firstPrefix.find("\"currentGrip\":true,\"rawPresent\":true")!=std::string::npos);
    CHECK(firstPrefix.find("\"originalPacket\":{\"grip\":false")!=std::string::npos);
    CHECK(firstPrefix.find("\"contact\":null")!=std::string::npos);
    CHECK(firstPrefix.find("\"ownership\":{\"action\":\"not_attempted\"")!=std::string::npos);
    // The separately recorded held source can now acquire; the observer must
    // preserve the missed current squeeze, and the actual claim's old lease.
    f.PrepareTick(wrist,true);f.Consume(&pressed);
    CHECK(f.result.ownsHand&&f.result.presentation&&f.result.presentation->mechanismClaim&&f.submits==3);
    CHECK(f.result.presentation->mechanismClaim->inputSequence==pressed.inputEvidence.sequence);
    CHECK(f.result.presentation->mechanismClaim->deadlineNs==pressed.inputEvidence.deadlineNs);
    const auto acquired=observe();CHECK(acquired.substr(0,acquired.find(",\"latestChange\":"))==firstPrefix);
    const auto latest=acquired.substr(acquired.find(",\"latestChange\":"));
    CHECK(latest.find("\"originalPacket\":{\"grip\":true")!=std::string::npos);
    CHECK(latest.find("\"contact\":{\"kind\":\"candidate\",\"status\":"+std::to_string(unsigned(FeedMechanismStatus::ContactMatch)))!=std::string::npos);
    CHECK(latest.find("\"geometryEvaluated\":true,\"distanceMeters\":")!=std::string::npos);
    CHECK(latest.find("\"ownership\":{\"action\":\"acquire\",\"inputValid\":true,\"accepted\":true")!=std::string::npos);
    CHECK(latest.find("\"recognitionEnabled\":true")!=std::string::npos);
    CHECK(acquired.size()<8192); // Leave room for the existing 32 KiB snapshot.
    f.Consume(&pressed);const auto duplicate=observe();f.Consume(&pressed);CHECK(observe()==duplicate);
    // A raw-less failure with contradictory selection has no Feed pose, but still carries its coherent
    // current packet and explicit unreached source/contact/ownership fields.
    f.PrepareTick(wrist,true);f.sample.raw.valid=false;f.SelectWeaponData(f.selected->weaponData+1);f.Consume();
    CHECK(!f.result.presentation&&!f.result.ownsHand&&f.submits==3);
    const auto failed=observe();CHECK(failed.substr(0,failed.find(",\"latestChange\":"))==firstPrefix);
    const auto failure=failed.substr(failed.find(",\"latestChange\":"));
    CHECK(failure.find("\"rawPresent\":false")!=std::string::npos);
    CHECK(failure.find("\"sourceLookedUp\":false,\"originalPacket\":null")!=std::string::npos);
    CHECK(failure.find("\"selectionFailure\":8")!=std::string::npos);
    CHECK(failure.find("\"contact\":null")!=std::string::npos);
    f.now+=1000*Ms;f.Tick(wrist,true);CHECK(observe()==failed); // No renewal after the opt-in window.
    return 0;
}
int AdmissionObservationExplainsMissingViewRetentionBeforeSuspension(){
    const auto observe=[](){std::ostringstream out;out<<std::boolalpha;WriteBc2PhysicalFeedAdmissionObservation(out);return out.str();};
    const auto field=[](const std::string& text,const char* begin,const char* end){
        const auto at=text.find(begin),until=text.find(end,at);
        return at==std::string::npos||until==std::string::npos?std::string{}:text.substr(at,until-at);
    };
    const auto* registration=FindBc2FeedRegistration(5358977986686110024ull);
    CHECK(registration&&registration->geometry.asset=="QJU88_sp");
    for(unsigned fault=0;fault<4;++fault){
        Fixture f(registration);CHECK(f.OpenCover()&&f.Reconcile());f.submitted.reset();
        const auto motion=BuildBc2FeedMotionFromRetainedPose(registration->geometry,FeedPhysicalOperation::DetachContainer,
            registration->interactionPolicy,registration->geometry.poses[unsigned(Bc2FeedPoseId::Resting)]);
        CHECK(motion);const auto wrist=feed_interaction_detail::Multiply(motion->placement.sourceFromGrip,motion->placement.weaponFromSource);
        f.Tick(wrist,false);f.Tick(wrist,false);RequestBc2PhysicalFeedAdmissionObservation(f.now);f.Tick(wrist,true);
        CHECK(f.result.ownsHand&&f.result.interactionStatus==FeedInteractionStatus::Acquired);
        const auto held=f.hands.Current(InteractionHand::Left);CHECK(held);
        const auto native=f.backend.ReadAt(f.sample.owner,f.sample.input.observedNs,f.sample.input.nowNs);CHECK(native);
        const auto original=f.result.presentation;CHECK(original);
        const auto first=observe();const auto firstPress=field(first,"\"firstPress\":",",\"latestChange\":");CHECK(!firstPress.empty());
        CHECK(first.find("\"failedRetention\":null")!=std::string::npos);
        const auto counts=f.native.counts;const auto reads=f.nativeReads,readAts=f.nativeReadAts;
        // Actual feedback80/82 shape: repeat the admitted input while its
        // original native and hand leases are both live, then lose current view.
        f.missingCurrentView=true;
        if(fault==1)f.refuseGate=true;
        if(fault==2){f.sample.raw.valid=false;f.SelectWeaponData(f.selected->weaponData+1);}
        if(fault==3)++f.sample.equipment->data;
        CHECK(f.sample.input.nowNs<held->deadlineNs&&f.sample.input.nowNs<native->deadlineNs);
        f.Consume();
        CHECK(f.nativeReads==reads+1&&f.nativeReadAts==readAts); // Observer cannot re-read native evidence.
        CHECK(f.result.diagnostic.exit==Bc2PhysicalFeedExit::ViewUnavailable&&!f.result.diagnostic.policyEvaluated);
        CHECK(!f.result.presentation&&!f.result.pending&&!f.backend.Pending()&&!f.submitted&&f.submits==1&&f.native.counts==counts);
        const auto observed=observe();CHECK(field(observed,"\"firstPress\":",",\"latestChange\":")==firstPress);
        const auto latest=field(observed,"\"latestChange\":",",\"failedRetention\":");CHECK(!latest.empty());
        CHECK(latest.find("\"retention\":{\"checked\":63,\"passed\":63,\"firstFailure\":null")!=std::string::npos);
        CHECK(latest.find("\"registrationKey\":"+std::to_string(registration->key)+",\"lifetime\":"+
            std::to_string(original->lifetime)+",\"claim\":"+std::to_string(held->token.id))!=std::string::npos);
        const auto failed=field(observed,"\"failedRetention\":",",\"retentionEntryPredicates\":");CHECK(!failed.empty());
        if(fault<=1){
            CHECK(f.result.ownsHand&&f.result.blocksEquipment&&f.result.blocksFire);
            CHECK(bool(f.result.diagnostic.flags&(1u<<5))==(fault==1));
            const auto retained=f.hands.Current(InteractionHand::Left);CHECK(retained&&retained->token==held->token);
            CHECK(retained->inputSequence==held->inputSequence&&retained->deadlineNs==held->deadlineNs);
            CHECK(latest.find("\"checked\":17179869183,\"passed\":17179869183,\"firstFailure\":null,\"selectionFailure\":0")!=std::string::npos);
            CHECK(failed=="\"failedRetention\":null");
        }else{
            CHECK(!f.result.ownsHand&&!f.hands.Current(InteractionHand::Left));
            CHECK(failed.find("\"leftFeedClaim\":{\"slot\":0,\"registrationKey\":"+std::to_string(registration->key)+
                ",\"lifetime\":"+std::to_string(original->lifetime)+",\"lost\":true}")!=std::string::npos);
            const char* expected=fault==2?"\"checked\":8589934591,\"passed\":4294967295,\"firstFailure\":\"selected\",\"selectionFailure\":24":
                "\"checked\":2047,\"passed\":1023,\"firstFailure\":\"current_equipment\",\"selectionFailure\":null";
            CHECK(latest.find(expected)!=std::string::npos&&failed.find(expected)!=std::string::npos);
            // Later held/default observations and a neutral release must not
            // erase the original failed transition or its preceding first press.
            f.refuseGate=false;f.sample.raw.valid=true;if(fault==3)--f.sample.equipment->data;
            if(fault==2)f.SelectWeaponData(f.native.config.weaponData);
            f.Consume();const auto after=observe();
            CHECK(field(after,"\"failedRetention\":",",\"retentionEntryPredicates\":")==failed);
            CHECK(field(after,"\"firstPress\":",",\"latestChange\":")==firstPress);
            f.Tick(wrist,false);const auto neutral=observe();
            CHECK(field(neutral,"\"failedRetention\":",",\"retentionEntryPredicates\":")==failed);
            CHECK(field(neutral,"\"firstPress\":",",\"latestChange\":")==firstPress);
        }
        if(fault<=2){
            CHECK(latest.find("\"heldInput\":{\"sequence\":"+std::to_string(held->inputSequence))!=std::string::npos);
            CHECK(latest.find("\"heldNative\":{\"sequence\":"+std::to_string(native->sequence)+",\"observedNs\":"+
                std::to_string(native->observedNs)+",\"deadlineNs\":"+std::to_string(native->deadlineNs))!=std::string::npos);
        }
        std::ostringstream full,compact;full<<std::boolalpha;compact<<std::boolalpha;
        WriteBc2PhysicalFeedAdmissionObservation(full,&compact);
        CHECK(field(compact.str(),"\"failedRetention\":",",\"retentionEntryPredicates\":")==failed);
        CHECK(compact.str().find("\"latestChange\":{\"omitted\":\"payload_limit\"}")!=std::string::npos);
        const auto fullPress=field(full.str(),"\"firstPress\":",",\"latestChange\":");
        const auto compactPress=field(compact.str(),"\"firstPress\":",",\"latestChange\":");
        CHECK(field(compactPress,"\"firstPress\":",",\"leftBefore\":")==field(fullPress,"\"firstPress\":",",\"admissionChecked\":"));
        CHECK(field(compactPress,"\"leftBefore\":",",\"omitted\":")==field(fullPress,"\"leftBefore\":",",\"policy\":"));
        // Reserve the independently conservative fixed-gate/header bound and
        // 4 KiB for all three populated native Feed-gate rows plus metadata.
        CHECK(compact.str().size()+11702+4096<=32768);
    }
    return 0;
}
int AdmissionObservationIgnoresUnrelatedMechanismClaim(){
    Fixture f;CHECK(f.OpenCover()&&f.Reconcile());
    f.PrepareTick(Identity(),true);
    const auto unrelated=f.hands.Acquire(f.sample.input,{f.sample.input.owner,InteractionHand::Left,
        HandClaimKind::Mechanism,{999,1},{{998,1},f.sequence,f.sample.input.deadlineNs,true},++f.intent,0});
    CHECK(unrelated.accepted&&unrelated.claim);
    RequestBc2PhysicalFeedAdmissionObservation(f.now);f.missingCurrentView=true;
    const auto reads=f.nativeReads,readAts=f.nativeReadAts,submits=f.submits;const auto counts=f.native.counts;
    f.Consume();
    CHECK(f.result.diagnostic.exit==Bc2PhysicalFeedExit::ViewUnavailable&&!f.result.ownsHand);
    const auto retained=f.hands.Current(InteractionHand::Left);CHECK(retained);
    CHECK(retained->token==unrelated.claim->token&&retained->inputSequence==unrelated.claim->inputSequence&&
        retained->deadlineNs==unrelated.claim->deadlineNs);
    CHECK(f.nativeReads==reads+1&&f.nativeReadAts==readAts+1&&f.submits==submits&&f.native.counts==counts);
    std::ostringstream full,compact;full<<std::boolalpha;compact<<std::boolalpha;
    WriteBc2PhysicalFeedAdmissionObservation(full,&compact);
    for(const auto* text:{&full,&compact}){
        CHECK(text->str().find("\"failedRetention\":null")!=std::string::npos);
        CHECK(text->str().find("\"leftFeedClaim\":null")!=std::string::npos);
        CHECK(text->str().find("\"leftBefore\":{\"id\":"+std::to_string(retained->token.id)+",")!=std::string::npos);
    }
    return 0;
}
// Replace the diagnostic publication deterministically during full output. A
// second publication load for compact output would mix two different presses.
class FeedObservationInterleavingBuffer final:public std::stringbuf {
    std::function<void()> onWrite;
    void Interleave(){if(onWrite){auto callback=std::move(onWrite);onWrite={};callback();}}
    std::streamsize xsputn(const char* text,std::streamsize count)override{
        Interleave();return std::stringbuf::xsputn(text,count);
    }
    int_type overflow(int_type value)override{Interleave();return std::stringbuf::overflow(value);}
public:
    explicit FeedObservationInterleavingBuffer(std::function<void()> callback):onWrite(std::move(callback)){}
};
int CompactAdmissionUsesSameImmutableFailureAndPress(){
    Fixture f;const auto motion=f.Open();
    const auto wrist=feed_interaction_detail::Multiply(motion.motionFromContact,motion.weaponFromMotion);
    f.Tick(wrist,false);RequestBc2PhysicalFeedAdmissionObservation(f.now);f.Tick(wrist,true);
    CHECK(f.result.ownsHand);const auto held=f.hands.Current(InteractionHand::Left);CHECK(held);
    f.missingCurrentView=true;f.sample.raw.valid=false;f.SelectWeaponData(f.selected->weaponData+1);f.Consume();CHECK(!f.result.ownsHand);
    std::ostringstream before,beforeCompact;before<<std::boolalpha;beforeCompact<<std::boolalpha;
    WriteBc2PhysicalFeedAdmissionObservation(before,&beforeCompact);
    CHECK(before.str().find("\"failedRetention\":null")==std::string::npos);
    bool replaced=false;
    FeedObservationInterleavingBuffer buffer([&]{
        f.sample.grip=false;f.Consume();f.sample.grip=true;f.Consume();replaced=true;
    });
    std::ostream full(&buffer);full<<std::boolalpha;std::ostringstream compact;compact<<std::boolalpha;
    WriteBc2PhysicalFeedAdmissionObservation(full,&compact);
    CHECK(replaced&&buffer.str()==before.str()&&compact.str()==beforeCompact.str());
    std::ostringstream after;after<<std::boolalpha;WriteBc2PhysicalFeedAdmissionObservation(after);
    CHECK(after.str()!=before.str()&&after.str().find("\"failedRetention\":null")!=std::string::npos);
    CHECK(compact.str().find("\"leftBefore\":{\"id\":"+std::to_string(held->token.id)+",")!=std::string::npos);
    CHECK(compact.str().size()+11702+4096<=32768);
    return 0;
}
int HeldSupplyLossRetainsPreclearReasonAndCustody(){
    const auto observe=[](bool compact=false){std::ostringstream full,small;full<<std::boolalpha;small<<std::boolalpha;
        WriteBc2PhysicalFeedAdmissionObservation(full,&small);return compact?small.str():full.str();};
    const auto loss=[](const std::string& text){const auto at=text.find("\"firstSupplyLoss\":");
        return at==std::string::npos?std::string{}:text.substr(at);};
    const auto first=[](const std::string& text){return text.substr(0,text.find(",\"latestChange\":"));};
    const auto field=[](const std::string& text,const char* key,const char* next){const auto at=text.find(key);
        return at==std::string::npos?std::string{}:text.substr(at,text.find(next,at)-at);};
    const auto* registration=FindBc2FeedRegistration(5358977986686110024ull);CHECK(registration);
    for(unsigned fault=0;fault<5;++fault){
        Fixture f(registration,true);f.now=(30+fault)*1000*Ms;
        CHECK(f.OpenCover()&&f.Reconcile());CHECK(f.Place(FeedPhysicalOperation::DetachContainer)&&f.Reconcile());
        RequestBc2PhysicalFeedAdmissionObservation(f.now);CHECK(f.PickSupply());
        CHECK(f.result.presentation&&f.result.presentation->supplyObject);
        const auto held=*f.result.presentation->supplyObject;
        CHECK(held.claim.token.kind==HandClaimKind::AmmoObject&&held.units>0);
        const auto before=observe(),press=first(before),compactPress=first(observe(true));
        CHECK(loss(before)=="\"firstSupplyLoss\":null}");
        std::string failed,mechanismLoss;
        const auto counts=f.native.counts;const auto submits=f.submits;
        f.PrepareTick(f.lastWrist,true);
        if(fault==0){f.missingCurrentView=true;f.SelectWeaponData(f.selected->weaponData+1);}
        if(fault==1)++f.sample.raw.inputEvidence.observedNs; // No exact source packet.
        if(fault==2){const auto right=f.hands.Current(InteractionHand::Right);CHECK(right);
            CHECK(f.hands.Release(f.sample.input,right->token).accepted);}
        if(fault==3){f.sample.raw.valid=false;f.SelectWeaponData(f.selected->weaponData+1);}
        const auto reads=f.nativeReads,readAts=f.nativeReadAts;
        if(fault==4)f.physical.Suspend(f.sample.input,f.hands);else f.Consume();
        CHECK(f.nativeReads==reads+(fault==4?0:1)&&f.nativeReadAts==readAts+(fault==4?0:1));
        CHECK(!f.hands.Current(InteractionHand::Left)&&!f.backend.Pending()&&f.submits==submits&&f.native.counts==counts);
        CHECK(f.BlocksFire());
        const auto observed=observe(),retained=loss(observed);CHECK(retained!="\"firstSupplyLoss\":null}");
        CHECK(first(observed)==press&&observed.find("\"failedRetention\":null")!=std::string::npos);
        const char* site=fault==4?"external_suspend":"tick_suspend";
        CHECK(retained.find(std::string("\"site\":\"")+site+'"')!=std::string::npos);
        const char* exit=fault==0?"view_unavailable":fault==3?"selection_or_input_unverified":
            fault==4?"none":"gun_or_raw_unverified";
        CHECK(retained.find(std::string("\"exit\":\"")+exit+'"')!=std::string::npos);
        CHECK(retained.find("\"nowNs\":"+std::to_string(f.sample.input.nowNs))!=std::string::npos);
        CHECK(retained.find("\"item\":["+std::to_string(held.item.id)+','+std::to_string(held.item.generation)+']')!=std::string::npos);
        CHECK(retained.find("\"claim\":{\"id\":"+std::to_string(held.claim.token.id)+',')!=std::string::npos);
        CHECK(retained.find("\"units\":"+std::to_string(held.units))!=std::string::npos);
        CHECK(retained.find("\"custody\":{\"object\":")!=std::string::npos);
        CHECK(retained.find("\"counts\":["+std::to_string(counts.loaded)+','+std::to_string(counts.reserve)+','+
            std::to_string(counts.capacity)+']')!=std::string::npos);
        CHECK(retained.find("\"nativePending\":false")!=std::string::npos&&retained.find("\"pending\":false")!=std::string::npos);
        if(fault==0)CHECK(retained.find("\"admissionChecked\":1,\"admissionPassed\":0")!=std::string::npos);
        if(fault==1)CHECK(retained.find("\"rawChecked\":3,\"rawPassed\":1")!=std::string::npos);
        if(fault==2)CHECK(retained.find("\"gun\":{\"checked\":1,\"passed\":0,\"firstFailure\":\"right_claim\"}")!=std::string::npos);
        if(fault==3)CHECK(retained.find("\"selectionFailure\":8")!=std::string::npos);
        if(fault==4)CHECK(retained.find("\"rawChecked\":0,\"rawPassed\":0")!=std::string::npos);
        CHECK(loss(observe(true))==retained);
        CHECK(observe(true).size()+11702+4096<=32768);
        if(fault==0){
            // Actual06's claim6 -> claim7 shape: continue the same physical
            // squeeze after supply loss, recover the retained feed contact,
            // then acquire a Mechanism claim while recognition needs neutral.
            f.missingCurrentView=false;f.SelectWeaponData(f.native.config.weaponData);
            std::optional<HandClaim> mechanism;
            for(unsigned step=0;step<6&&!mechanism;++step){
                f.Tick(f.lastWrist,true);const auto current=f.hands.Current(InteractionHand::Left);
                if(current&&current->token.kind==HandClaimKind::Mechanism)mechanism=current;
                CHECK(loss(observe())==retained&&!f.sample.input.released[0]);
            }
            CHECK(mechanism&&mechanism->token.id!=held.claim.token.id);
            CHECK(f.result.interactionStatus==FeedInteractionStatus::NeedNeutral);
            CHECK(f.result.presentation&&f.result.presentation->lifetime==f.submitted->command.request.lifetime);
            f.busyGate=true;f.Tick(f.lastWrist,true);f.busyGate=false;
            CHECK(loss(observe())==retained&&f.submits==submits&&!f.result.pending);
            // Retain an independent missing-view Mechanism failure as well.
            // Later default updates must preserve both original transitions.
            f.missingCurrentView=true;f.sample.raw.valid=false;f.SelectWeaponData(f.selected->weaponData+1);f.Consume();
            const auto both=observe();const auto at=both.find("\"failedRetention\":");
            failed=both.substr(at,both.find(",\"retentionEntryPredicates\":",at)-at);
            CHECK(failed!="\"failedRetention\":null"&&loss(both)==retained);
            const auto mechanismAt=both.find("\"firstMechanismLoss\":");CHECK(mechanismAt!=std::string::npos);
            mechanismLoss=both.substr(mechanismAt,both.find(",\"firstSupplyLoss\":",mechanismAt)-mechanismAt);
            CHECK(mechanismLoss!="\"firstMechanismLoss\":null");
            f.physical.Suspend(f.sample.input,f.hands);f.Consume();
            const auto unchanged=observe();const auto after=unchanged.find("\"failedRetention\":");
            CHECK(unchanged.substr(after,unchanged.find(",\"retentionEntryPredicates\":",after)-after)==failed);
            CHECK(loss(unchanged)==retained&&loss(observe(true))==retained);
            const auto compact=observe(true);const auto compactAt=compact.find("\"firstMechanismLoss\":");
            CHECK(compact.substr(compactAt,compact.find(",\"firstSupplyLoss\":",compactAt)-compactAt)==mechanismLoss);
            CHECK(observe(true).size()+11702+4096<=32768);
        }
        // Supply-only and all-three-failure payloads retain the same first press
        // and complete first failures through later held/Suspend/neutral changes.
        f.missingCurrentView=false;f.sample.raw.valid=true;f.SelectWeaponData(f.native.config.weaponData);
        f.Tick(f.lastWrist,true);f.physical.Suspend(f.sample.input,f.hands);
        const auto heldCompact=observe(true); // Suspend may add the first mechanism loss.
        CHECK(heldCompact.find("\"latestChange\":{\"omitted\":\"payload_limit\"}")!=std::string::npos);
        f.Tick(f.lastWrist,false);
        const auto neutral=observe(),compactNeutral=observe(true);
        CHECK(loss(neutral)==retained&&loss(compactNeutral)==retained&&first(neutral)==press);
        if(first(compactNeutral)!=compactPress||compactNeutral!=heldCompact)
            std::fprintf(stderr,"Neutral feed admission fault %u: firstPressEqual=%d, compactEqual=%d, compact=%zu, held=%zu\n",
                fault,int(first(compactNeutral)==compactPress),int(compactNeutral==heldCompact),compactNeutral.size(),heldCompact.size());
        CHECK(first(compactNeutral)==compactPress&&compactNeutral==heldCompact);
        const auto neutralLatest=field(neutral,"\"latestChange\":",",\"failedRetention\":");
        CHECK(neutralLatest.find("\"currentGrip\":false")!=std::string::npos);
        CHECK(neutralLatest.find("\"ownership\":{")!=std::string::npos&&neutralLatest.find("\"retention\":{")!=std::string::npos);
        CHECK(neutralLatest.find("\"omitted\":")==std::string::npos);
        if(fault==0){
            for(const auto* text:{&neutral,&compactNeutral}){
                CHECK(field(*text,"\"failedRetention\":",",\"retentionEntryPredicates\":")==failed);
                CHECK(field(*text,"\"firstMechanismLoss\":",",\"firstSupplyLoss\":")==mechanismLoss);
            }
        }
        if(compactNeutral.size()+11702+4096>32768)
            std::fprintf(stderr,"Neutral feed admission fault %u: compact=%zu, held=%zu, total=%zu, limit=32768; firstPress=%zu, failedRetention=%zu, firstMechanismLoss=%zu, firstSupplyLoss=%zu, fullLatest=%zu\n",
                fault,compactNeutral.size(),heldCompact.size(),compactNeutral.size()+11702+4096,
                compactPress.size(),failed.size(),mechanismLoss.size(),retained.size(),neutralLatest.size());
        CHECK(compactNeutral.size()+11702+4096<=32768);
        CHECK(f.native.counts==counts&&f.submits==submits&&!f.backend.Pending()&&f.BlocksFire());
        CHECK(f.result.presentation&&!f.result.presentation->supplyObject&&!f.result.bodyVisual);
    }
    // With the request expired, the same real held-to-unheld transition leaves
    // the immutable diagnostic publication untouched.
    Fixture quiet(registration);quiet.now=100*1000*Ms;
    CHECK(quiet.OpenCover()&&quiet.Reconcile());CHECK(quiet.Place(FeedPhysicalOperation::DetachContainer)&&quiet.Reconcile());
    CHECK(quiet.PickSupply());const auto unchanged=observe();const auto counts=quiet.native.counts;const auto submits=quiet.submits;
    quiet.missingCurrentView=true;quiet.SelectWeaponData(quiet.selected->weaponData+1);quiet.Consume();
    CHECK(!quiet.hands.Current(InteractionHand::Left)&&observe()==unchanged);
    CHECK(quiet.native.counts==counts&&quiet.submits==submits&&quiet.BlocksFire());
    return 0;
}
int CompactAdmissionUsesSameImmutableSupplyLoss(){
    Fixture f;f.now=110*1000*Ms;
    CHECK(f.OpenCover()&&f.Reconcile());CHECK(f.Place(FeedPhysicalOperation::DetachContainer)&&f.Reconcile());
    RequestBc2PhysicalFeedAdmissionObservation(f.now);CHECK(f.PickSupply());
    f.missingCurrentView=true;f.SelectWeaponData(f.selected->weaponData+1);f.Consume();CHECK(!f.hands.Current(InteractionHand::Left));
    // A publication-side grip change with the same unreleased input packet
    // may change firstPress under the existing contract, never supply loss.
    f.sample.grip=false;f.Consume();f.sample.grip=true;f.Consume();CHECK(!f.sample.input.released[0]);
    std::ostringstream before,beforeCompact;before<<std::boolalpha;beforeCompact<<std::boolalpha;
    WriteBc2PhysicalFeedAdmissionObservation(before,&beforeCompact);
    CHECK(before.str().find("\"firstSupplyLoss\":null")==std::string::npos);
    bool replaced=false;
    FeedObservationInterleavingBuffer buffer([&]{
        f.Tick(f.lastWrist,false);f.Tick(f.lastWrist,true);replaced=true;
    });
    std::ostream full(&buffer);full<<std::boolalpha;std::ostringstream compact;compact<<std::boolalpha;
    WriteBc2PhysicalFeedAdmissionObservation(full,&compact);
    CHECK(replaced&&buffer.str()==before.str()&&compact.str()==beforeCompact.str());
    std::ostringstream after;after<<std::boolalpha;WriteBc2PhysicalFeedAdmissionObservation(after);
    CHECK(after.str()!=before.str()&&after.str().find("\"firstSupplyLoss\":null")!=std::string::npos);
    CHECK(compact.str().size()+11702+4096<=32768);
    return 0;
}
std::string MechanismLoss(const std::string& text){
    const auto at=text.find("\"firstMechanismLoss\":");
    return at==std::string::npos?std::string{}:text.substr(at,text.find(",\"firstSupplyLoss\":",at)-at);
}
int HeldMechanismLossRetainsFirstCauseAcrossSameGripReacquisition(){
    const auto observe=[](bool compact=false){std::ostringstream full,small;full<<std::boolalpha;small<<std::boolalpha;
        WriteBc2PhysicalFeedAdmissionObservation(full,&small);return compact?small.str():full.str();};
    for(unsigned fault=0;fault<5;++fault){
        Fixture f;f.now=(120+fault)*1000*Ms;CHECK(f.OpenCover()&&f.Reconcile());
        RequestBc2PhysicalFeedAdmissionObservation(f.now);
        CHECK(f.Place(FeedPhysicalOperation::DetachContainer,true,false));
        const auto held=f.hands.Current(InteractionHand::Left);CHECK(held&&held->token.kind==HandClaimKind::Mechanism);
        const auto heldInput=f.sample.input;const auto native=f.backend.Read(f.sample.owner,heldInput.nowNs);CHECK(native);
        const auto before=observe(),press=before.substr(0,before.find(",\"latestChange\":"));
        CHECK(MechanismLoss(before)=="\"firstMechanismLoss\":null");
        const auto counts=f.native.counts;const auto submits=f.submits;
        f.PrepareTick(f.lastWrist,true);
        if(fault==0)f.refuseGate=true;
        if(fault==2)CHECK(f.hands.Release(f.sample.input,held->token).accepted);
        if(fault==3||fault==4){f.sample.raw.valid=false;f.SelectWeaponData(f.selected->weaponData+1);}
        if(fault==4)f.missingCurrentView=true;
        const auto reads=f.nativeReads,readAts=f.nativeReadAts;
        const Bc2PhysicalFeedSuspendContext external{Bc2PhysicalFeedSuspendSource::InputApplyRejected,5,37,true};
        if(fault==1)f.physical.Suspend(f.sample.input,f.hands,external);else f.Consume();
        CHECK(f.nativeReads==reads+(fault==1?0:1)&&f.nativeReadAts==readAts+(fault==1?0:1));
        const auto observed=observe(),loss=MechanismLoss(observed);
        CHECK(loss!="\"firstMechanismLoss\":null"&&loss==MechanismLoss(observe(true)));
        CHECK(observed.substr(0,observed.find(",\"latestChange\":"))==press);
        // Explicit claim release fails raw continuation before the later claim-unavailable branch.
        const char* site=fault==0?"policy_clear":fault==1?"external_suspend":fault==2?"release_continuation_unverified":"tick_suspend";
        if(loss.find(std::string("\"site\":\"")+site+'"')==std::string::npos)
            std::fprintf(stderr,"Mechanism loss fault %u: expected=%s, actualPrefix=%.100s\n",fault,site,loss.c_str());
        CHECK(loss.find(std::string("\"site\":\"")+site+'"')!=std::string::npos);
        CHECK(loss.find("\"heldToken\":{\"id\":"+std::to_string(held->token.id)+',')!=std::string::npos);
        CHECK(loss.find("\"heldClaim\":{\"id\":"+std::to_string(held->token.id)+',')!=std::string::npos);
        CHECK(loss.find("\"inputSequence\":"+std::to_string(held->inputSequence)+",\"deadlineNs\":"+
            std::to_string(held->deadlineNs))!=std::string::npos);
        CHECK(loss.find("\"heldNative\":{\"sequence\":"+std::to_string(native->sequence)+",\"observedNs\":"+
            std::to_string(native->observedNs)+",\"deadlineNs\":"+std::to_string(native->deadlineNs))!=std::string::npos);
        CHECK(loss.find("\"nowNs\":"+std::to_string(f.sample.input.nowNs))!=std::string::npos);
        CHECK(loss.find("\"operationBefore\":"+std::to_string(unsigned(FeedPhysicalOperation::DetachContainer)))!=std::string::npos);
        if(fault==0){CHECK(loss.find("\"gateFailed\":true")!=std::string::npos);
            CHECK(loss.find("\"rawChecked\":511,\"rawPassed\":511")!=std::string::npos);
            CHECK(loss.find("\"outcome\":{\"evaluated\":true,\"completion\":false,\"dispatch\":false")!=std::string::npos);}
        if(fault==1)CHECK(loss.find("\"external\":{\"source\":\"input_apply_rejected\",\"detail\":5,\"sourceFlags\":37,\"preserveMagazine\":true}")!=std::string::npos);
        if(fault==3)CHECK(loss.find("\"selectionFailure\":8")!=std::string::npos);
        if(fault==4)CHECK(loss.find("\"admissionChecked\":1,\"admissionPassed\":0")!=std::string::npos);
        CHECK(f.native.counts==counts&&f.submits==submits&&!f.backend.Pending());
        f.refuseGate=f.missingCurrentView=false;f.sample.raw.valid=true;f.SelectWeaponData(f.native.config.weaponData);
        if(fault==0){
            // Same continuously held input, new mechanism token. Recovery and
            // NeedNeutral must not replace the reason the original token died.
            std::optional<HandClaim> reacquired;
            for(unsigned step=0;step<8&&!reacquired;++step){f.Tick(f.lastWrist,true);
                const auto left=f.hands.Current(InteractionHand::Left);
                if(left&&left->token.id!=held->token.id)reacquired=left;
                CHECK(!f.sample.input.released[0]&&MechanismLoss(observe())==loss);}
            CHECK(reacquired&&reacquired->token.id==held->token.id+1);
            CHECK(f.result.interactionStatus==FeedInteractionStatus::NeedNeutral);
            f.PrepareTick(f.lastWrist,true);++f.sample.raw.inputEvidence.observedNs;f.Consume();
            CHECK(f.result.diagnostic.rawChecked==3&&f.result.diagnostic.rawPassed==1);
            CHECK(MechanismLoss(observe())==loss); // The later raw failure is not first cause.
            // Changing the selected owner/space while this squeeze continues
            // can start a new firstPress, but cannot erase the earlier loss.
            ++f.native.identity.owner.equipGeneration;++f.native.identity.owner.space;
            f.native.binding.owner=f.native.identity.owner;f.native.binding.context.equipGeneration=f.native.identity.owner.equipGeneration;
            f.native.binding.context.space=f.native.identity.owner.space;
            f.sample.owner=f.native.identity.owner;f.selected->owner=f.sample.owner;f.sample.raw.owner=f.sample.owner;
            ++f.sample.weapon.generation;++f.sample.input.owner.equipGeneration;++f.sample.input.owner.space;
            (void)f.hands.Reset();f.Tick(f.lastWrist,true);
            CHECK(MechanismLoss(observe())==loss);
        }
        f.physical.Suspend(f.sample.input,f.hands,{Bc2PhysicalFeedSuspendSource::HandOwnershipReset,
            unsigned(Bc2PhysicalFeedHandResetReason::SessionStop)});
        f.sample.grip=false;f.Consume();f.sample.grip=true;f.Consume();
        CHECK(!f.sample.input.released[0]&&MechanismLoss(observe())==loss);
        const auto heldCompact=observe(true);
        CHECK(heldCompact.find("\"latestChange\":{\"omitted\":\"payload_limit\"}")!=std::string::npos);
        f.Tick(f.lastWrist,false);CHECK(MechanismLoss(observe())==loss&&observe(true)==heldCompact);
        f.Tick(f.lastWrist,true);CHECK(MechanismLoss(observe())=="\"firstMechanismLoss\":null");
        CHECK(f.native.counts==counts&&f.submits==submits&&!f.backend.Pending());
        CHECK(observe(true).size()+11702+4096<=32768);
    }
    // The expired request cannot publish a diagnostic on an actual claim loss.
    Fixture quiet;quiet.now=140*1000*Ms;const auto motion=quiet.Open();
    const auto wrist=feed_interaction_detail::Multiply(motion.motionFromContact,motion.weaponFromMotion);
    quiet.Tick(wrist,false);quiet.Tick(wrist,true);CHECK(quiet.result.ownsHand);
    const auto unchanged=observe();quiet.physical.Suspend(quiet.sample.input,quiet.hands);
    CHECK(!quiet.hands.Current(InteractionHand::Left)&&observe()==unchanged);
    return 0;
}
int MechanismReleaseWaitIsDistinctFromSuccessfulExplicitRelease(){
    const auto observe=[](){std::ostringstream out;out<<std::boolalpha;WriteBc2PhysicalFeedAdmissionObservation(out);return out.str();};
    for(bool delayed:{false,true}){
        Fixture f;f.now=(150+unsigned(delayed))*1000*Ms;CHECK(f.OpenCover()&&f.Reconcile());
        RequestBc2PhysicalFeedAdmissionObservation(f.now);CHECK(f.Place(FeedPhysicalOperation::DetachContainer,true,false));
        const auto held=f.sample.raw;const auto counts=f.native.counts;
        const auto native=f.backend.Read(f.sample.owner,f.sample.input.nowNs);CHECK(native);
        f.PrepareTick(f.lastWrist,false);const auto released=f.sample.raw;
        if(!delayed){f.Consume();CHECK(f.result.pending&&f.submits==2);
            CHECK(MechanismLoss(observe())=="\"firstMechanismLoss\":null");}
        else {f.Consume(&held);CHECK(!f.result.ownsHand&&!f.result.pending&&f.submits==1);
            const auto loss=MechanismLoss(observe());CHECK(loss.find("\"site\":\"release_wait\"")!=std::string::npos);
            CHECK(loss.find("\"released\":[true,false]")!=std::string::npos);
            f.now=native->deadlineNs-10*Ms;f.PrepareTick(f.lastWrist,false);f.Consume(&released);
            CHECK(!f.result.pending&&f.submits==1&&f.native.counts==counts);
            CHECK(MechanismLoss(observe())==loss);}
    }
    return 0;
}
int CompactAdmissionUsesSameImmutableMechanismLoss(){
    Fixture f;f.now=160*1000*Ms;const auto motion=f.Open();
    const auto wrist=feed_interaction_detail::Multiply(motion.motionFromContact,motion.weaponFromMotion);
    f.Tick(wrist,false);RequestBc2PhysicalFeedAdmissionObservation(f.now);f.Tick(wrist,true);CHECK(f.result.ownsHand);
    f.physical.Suspend(f.sample.input,f.hands,{Bc2PhysicalFeedSuspendSource::HandOwnershipReset,
        unsigned(Bc2PhysicalFeedHandResetReason::InputUnavailable),11,true});
    std::ostringstream before,beforeCompact;before<<std::boolalpha;beforeCompact<<std::boolalpha;
    WriteBc2PhysicalFeedAdmissionObservation(before,&beforeCompact);
    CHECK(MechanismLoss(before.str())!="\"firstMechanismLoss\":null");
    CHECK(MechanismLoss(before.str())==MechanismLoss(beforeCompact.str()));
    bool replaced=false;FeedObservationInterleavingBuffer buffer([&]{f.Tick(wrist,false);f.Tick(wrist,true);replaced=true;});
    std::ostream full(&buffer);full<<std::boolalpha;std::ostringstream compact;compact<<std::boolalpha;
    WriteBc2PhysicalFeedAdmissionObservation(full,&compact);
    CHECK(replaced&&buffer.str()==before.str()&&compact.str()==beforeCompact.str());
    std::ostringstream after;after<<std::boolalpha;WriteBc2PhysicalFeedAdmissionObservation(after);
    CHECK(MechanismLoss(after.str())=="\"firstMechanismLoss\":null");
    CHECK(compact.str().size()+11702+4096<=32768);
    return 0;
}
int NeutralBlockedAdmissionObservesExistingRefusalsWithoutAuthority(){
    const auto observe=[](bool compact=false){std::ostringstream full,small;full<<std::boolalpha;small<<std::boolalpha;
        WriteBc2PhysicalFeedAdmissionObservation(full,&small);return compact?small.str():full.str();};
    const auto press=[](const std::string& text){return text.substr(0,text.find(",\"latestChange\":"));};
    const auto latest=[](const std::string& text){const auto at=text.find("\"latestChange\":");
        return text.substr(at,text.find(",\"failedRetention\":",at)-at);};
    const auto* registration=FindBc2FeedRegistration(5358977986686110024ull);CHECK(registration);
    for(unsigned fault=0;fault<5;++fault){
        Fixture f(registration);f.now=(10+fault)*1000*Ms;
        CHECK(f.OpenCover()&&f.Reconcile());
        RequestBc2PhysicalFeedAdmissionObservation(f.now);
        CHECK(f.Place(FeedPhysicalOperation::DetachContainer)&&f.Reconcile());
        CHECK(f.result.blocksFire&&f.result.presentation&&!f.result.ownsHand&&!f.result.presentation->supplyObject);
        const auto first=press(observe());CHECK(first.find("\"currentGrip\":true")!=std::string::npos);
        const auto previous=f.sample.raw;
        if(fault==1)++f.native.counts.reserve;
        f.PrepareTick(f.lastWrist,false);
        if(fault==0)++f.sample.raw.inputEvidence.observedNs; // No exact recorded packet.
        if(fault==2){auto loss=f.sample.input;loss.tracked[0]=false;(void)f.hands.Update(loss);}
        if(fault==3){const auto gun=f.hands.Current(InteractionHand::Right);CHECK(gun);
            CHECK(f.hands.Release(f.sample.input,gun->token).accepted);}
        if(fault==4)f.sample.input.tracked[0]=false;
        const auto counts=f.native.counts;const auto reads=f.nativeReads,readAts=f.nativeReadAts,submits=f.submits;
        f.Consume(fault==1?&previous:nullptr);
        CHECK(f.nativeReads==reads+1&&f.nativeReadAts==readAts+1&&f.submits==submits&&f.native.counts==counts);
        CHECK(f.result.blocksFire&&!f.result.blocksEquipment&&!f.result.ownsHand&&!f.result.pending&&
            (fault==4?!f.result.presentation:DisplayOnlyFeed(f.result)));
        CHECK(!f.result.diagnostic.policyEvaluated&&!f.hands.Current(InteractionHand::Left)&&!f.backend.Pending());
        CHECK(f.result.diagnostic.exit==(fault==4?Bc2PhysicalFeedExit::SelectionOrInput:Bc2PhysicalFeedExit::GunOrRawUnverified));
        const auto full=observe(),compact=observe(true),row=latest(full),small=latest(compact);
        CHECK(press(full)==first&&row.find("\"currentGrip\":false")!=std::string::npos);
        CHECK(full.find("\"failedRetention\":null")!=std::string::npos&&MechanismLoss(full)=="\"firstMechanismLoss\":null");
        CHECK(full.find("\"firstSupplyLoss\":null")!=std::string::npos);
        CHECK(small.substr(0,small.find(",\"leftBefore\":"))==row.substr(0,row.find(",\"admissionChecked\":")));
        const auto smallClaims=small.find(",\"leftBefore\":"),fullClaims=row.find(",\"leftBefore\":");
        CHECK(smallClaims!=std::string::npos&&fullClaims!=std::string::npos);
        CHECK(small.substr(smallClaims,small.find(",\"omitted\":")-smallClaims)==row.substr(fullClaims,row.find(",\"policy\":")-fullClaims));
        CHECK(row.find("\"sequence\":"+std::to_string(f.sequence)+",")!=std::string::npos);
        const char* masks=fault==0?"\"rawChecked\":3,\"rawPassed\":1":
            fault==1?"\"rawChecked\":255,\"rawPassed\":127":
            fault==2?"\"rawChecked\":511,\"rawPassed\":255":
            fault==3?"\"rawChecked\":511,\"rawPassed\":510":"\"rawChecked\":0,\"rawPassed\":0";
        CHECK(row.find(masks)!=std::string::npos&&small.find(masks)!=std::string::npos);
        const char* gun=fault==3?"\"gun\":{\"checked\":1,\"passed\":0,\"firstFailure\":\"right_claim\"}":
            fault==4?"\"gun\":{\"checked\":0,\"passed\":0,\"firstFailure\":null}":
            "\"gun\":{\"checked\":63,\"passed\":63,\"firstFailure\":null}";
        CHECK(row.find(gun)!=std::string::npos&&small.find(gun)!=std::string::npos);
        if(fault==4)CHECK(row.find("\"admissionChecked\":63,\"admissionPassed\":31")!=std::string::npos);
        CHECK(full.size()<8192&&compact.size()+11702+4096<=32768);
        f.Consume();const auto stable=observe();
        for(unsigned i=0;i<4;++i){++f.sample.input.nowNs;f.Consume();CHECK(observe()==stable);}
        if(fault==0){
            // New timestamps/sequences with the same missing-packet predicate
            // do not replace the retained change on every neutral frame.
            for(unsigned i=0;i<4;++i){f.PrepareTick(f.lastWrist,false);++f.sample.raw.inputEvidence.observedNs;
                f.Consume();CHECK(observe()==stable);}
        }
        f.now+=1000*Ms;f.Tick(f.lastWrist,false);CHECK(observe()==stable);
    }
    // A request first observed during neutral debt has no synthetic firstPress.
    Fixture f(registration);f.now=20*1000*Ms;CHECK(f.OpenCover()&&f.Reconcile());
    CHECK(f.Place(FeedPhysicalOperation::DetachContainer)&&f.Reconcile());const auto before=observe();
    f.PrepareTick(f.lastWrist,false);++f.sample.raw.inputEvidence.observedNs;f.Consume();CHECK(observe()==before);
    RequestBc2PhysicalFeedAdmissionObservation(f.now);f.Consume();
    CHECK(observe().find("\"retainedPress\":false")!=std::string::npos);
    CHECK(observe().find("\"firstPress\":null")!=std::string::npos);
    CHECK(latest(observe(true)).find("\"rawChecked\":3,\"rawPassed\":1")!=std::string::npos);
    return 0;
}
int BackendLifetimesIncreaseAcrossOwners(){
    Fixture first;CHECK(first.OpenCover());Fixture second;CHECK(second.OpenCover());
    CHECK(second.submitted->command.request.lifetime>first.submitted->command.request.lifetime);
    CHECK(first.submitted->command.request.id==1&&second.submitted->command.request.id==1);return 0;
}
int FiveStagesRetainPartialAmmoAndRefillOnlyAtAttachment(){
    Fixture f;CHECK(f.OpenCover());const auto supply=f.submitted->command.before.loadedSupply;
    const auto lifetime=f.submitted->command.request.lifetime;CHECK(f.Reconcile());
    CHECK(f.Place(FeedPhysicalOperation::DetachContainer));CHECK((f.submitted->command.after==FeedAmmunitionCounts{20,130,100}));
    CHECK(f.result.presentation&&f.result.presentation->carriedPartsMask==61);
    CHECK(f.submitted->command.loadedSupplyAfter==supply);CHECK(f.Reconcile());CHECK(f.BlocksFire());
    CHECK(f.Place(FeedPhysicalOperation::AttachContainer));CHECK(f.submitted->command.authority.effect==FeedAmmunitionEffect::RefillLoaded);
    CHECK((f.submitted->command.after==FeedAmmunitionCounts{100,50,100}));CHECK(f.submitted->command.loadedSupplyAfter==supply);
    CHECK(f.Reconcile());CHECK(f.BlocksFire());CHECK(f.result.presentation->carriedPartsMask==0);
    CHECK(f.Place(FeedPhysicalOperation::SeatFeed));CHECK(f.submitted->command.authority.effect==FeedAmmunitionEffect::PhysicalOnly);
    CHECK(f.result.presentation->carriedPartsMask==60);
    CHECK(f.submitted->command.request.completion.candidate.refill.requestId==3);CHECK(f.Reconcile());
    CHECK(f.BlocksFire());CHECK(f.Close());CHECK(f.submitted->command.request.id==5);
    CHECK(f.submitted->command.request.lifetime==lifetime);CHECK(f.Reconcile());CHECK(!f.BlocksFire());
    CHECK((f.native.counts==FeedAmmunitionCounts{100,50,100}));CHECK(f.submits==5);return 0;
}
int ReattachingWithoutChestSupplyCannotRefill(){
    Fixture f;CHECK(f.OpenCover());CHECK(f.Reconcile());CHECK(f.Place(FeedPhysicalOperation::DetachContainer));CHECK(f.Reconcile());
    const auto before=f.native.counts;CHECK(!f.Place(FeedPhysicalOperation::AttachContainer,false));
    CHECK(f.native.counts==before&&f.BlocksFire());CHECK(!f.result.pending);
    CHECK(f.Place(FeedPhysicalOperation::AttachContainer));CHECK(f.submitted);
    CHECK(f.submitted->command.authority.rounds==80);CHECK(f.Reconcile());
    CHECK((f.native.counts==FeedAmmunitionCounts{100,50,100}));return 0;
}
int DetachedDwellKeepsOriginalCarryAndRequiresFreshSupply(){
    // feedback67 Detach snapshot020 and the later refused Attach snapshot001:
    // same exact equipment, cover open, loaded/reserve 0/687, released hand,
    // and native weapon transforms with ordinary float orthogonality error.
    const auto* registration=FindBc2FeedRegistration(5358977986686110024ull);
    CHECK(registration&&registration->geometry.asset=="QJU88_sp");
    Fixture f(registration);f.native.counts={0,687,200};
    auto world=Identity();
    world.values={{{-.896846652f,3.16649675e-8f,-.442341506f,0},
        {6.70552254e-8f,1,1.49011612e-7f,0},{.442341536f,-1.49011612e-8f,-.896846712f,0},
        {164.812531f,170.831558f,378.542969f,1}}};
    CHECK(feed_mechanism_detail::Pose(world));f.placedWeapon=world;
    CHECK(f.OpenCover()&&f.Reconcile());
    CHECK(f.Place(FeedPhysicalOperation::DetachContainer)&&f.Reconcile());
    CHECK(f.submits==2&&f.result.presentation&&f.BlocksFire());
    const auto detached=f.result.presentation;const auto counts=f.native.counts;
    const auto released=f.lastWrist;f.submitted.reset();
    for(unsigned packet=0;packet<600;++packet){
        if(packet==300){
            world.values={{{-.896846771f,-2.32830644e-9f,-.442341566f,0},
                {3.7252903e-8f,1,6.70552254e-8f,0},{.442341536f,7.4505806e-9f,-.896846831f,0},
                {164.812561f,170.833389f,378.542938f,1}}};
            CHECK(feed_mechanism_detail::Pose(world));f.placedWeapon=world;
        }
        f.PrepareTick(released,false);
        // Native/gameplay can consume several publications of one real input.
        // The visual fall grants no hand claim, motion or ammunition authority.
        for(unsigned publication=0;publication<3;++publication){
            f.Consume();
            CHECK(f.result.diagnostic.exit==Bc2PhysicalFeedExit::Updated&&f.result.selected);
            CHECK(f.result.status==FeedEquipmentStatus::PreflightRejected);
            CHECK(f.result.interactionStatus==FeedInteractionStatus::Interrupted&&f.result.reloadStatus==FeedReloadStatus::Suspended);
            CHECK(f.result.presentation&&f.result.blocksFire&&!f.result.pending&&!f.result.ownsHand&&!f.result.blocksEquipment);
            CHECK(f.result.presentation->input.sequence==f.sample.input.sequence);
            CHECK(f.result.presentation->input.deadlineNs==f.sample.input.deadlineNs);
            CHECK(!f.result.presentation->mechanismClaim&&!f.result.presentation->mechanismInput&&!f.result.presentation->supplyObject);
            CHECK(!f.hands.Current(InteractionHand::Left)&&f.submits==2&&f.native.counts==counts);
        }
    }
    const auto retained=f.result.presentation;
    CHECK(retained->lifetime==detached->lifetime&&retained->supply==detached->supply&&retained->owner==detached->owner);
    CHECK(retained->coverRadians==detached->coverRadians&&retained->carriedPartsMask==detached->carriedPartsMask);
    CHECK(retained->weaponFromPouch.values==detached->weaponFromPouch.values);
    CHECK(detached->droppedOriginalAtNs>0&&retained->droppedOriginalAtNs==detached->droppedOriginalAtNs);
    CHECK(!detached->hiddenPartsMask&&retained->hiddenPartsMask==retained->carriedPartsMask);
    CHECK(retained->pouchCarryWeaponWorldMeters.values[3][1]<detached->pouchCarryWeaponWorldMeters.values[3][1]);
    CHECK(retained->chainCarryWeaponWorldMeters.values[3][1]<detached->chainCarryWeaponWorldMeters.values[3][1]);
    for(unsigned row=0;row<4;++row)for(unsigned column=0;column<4;++column)if(row!=3||column!=1){
        CHECK(retained->pouchCarryWeaponWorldMeters.values[row][column]==detached->pouchCarryWeaponWorldMeters.values[row][column]);
        CHECK(retained->chainCarryWeaponWorldMeters.values[row][column]==detached->chainCarryWeaponWorldMeters.values[row][column]);
    }
    for(unsigned bone=0;bone<retained->chainCount;++bone)
        CHECK(retained->weaponFromChain[bone].values==detached->weaponFromChain[bone].values);
    CHECK(!ReadBc2PhysicalFeedPresentation(f.sample.owner,retained->input.deadlineNs));
    // Pickup is still a new neutral/contact/grip claim. Refill remains exactly
    // one receipt-bound operation after the long detached interval.
    CHECK(f.Place(FeedPhysicalOperation::AttachContainer));
    CHECK(!f.result.presentation->hiddenPartsMask&&!f.result.presentation->droppedOriginalAtNs);
    CHECK(f.submitted&&f.submitted->command.request.lifetime==detached->lifetime);
    CHECK(f.submitted->command.request.id==3&&f.submitted->command.authority.rounds==200);
    CHECK(f.native.counts==counts&&f.Reconcile());
    CHECK((f.native.counts==FeedAmmunitionCounts{200,487,200}));
    CHECK(f.Place(FeedPhysicalOperation::SeatFeed)&&f.Reconcile());
    CHECK(f.Close()&&f.Reconcile());CHECK(f.submits==5&&!f.BlocksFire());return 0;
}
int EveryEnrolledFeedUsesTheSharedChestRefillPath(){
    unsigned configurations=0;
    for(const auto& registration:RegisteredBc2FeedProfiles()){
        Fixture f(&registration);const auto before=f.native.counts;
        // The player does not reproduce the stock animation's wrist rotation.
        // Keep a non-identity grasp through all five operations and receipts.
        f.wristCalibration=Turn(2.29598f);
        const auto units=(std::min)(before.capacity-before.loaded,before.reserve);
        CHECK(f.OpenCover());CHECK(f.Reconcile());CHECK(f.Place(FeedPhysicalOperation::DetachContainer));CHECK(f.Reconcile());
        CHECK(!f.Place(FeedPhysicalOperation::AttachContainer,false));CHECK(f.native.counts==before);
        CHECK(f.Place(FeedPhysicalOperation::AttachContainer));CHECK(f.submitted->geometry.registrationKey==registration.key);
        CHECK(f.submitted->command.authority.rounds==units);CHECK(f.Reconcile());
        CHECK(f.native.counts.loaded==before.loaded+units&&f.native.counts.reserve==before.reserve-units);
        CHECK(f.Place(FeedPhysicalOperation::SeatFeed));CHECK(f.Reconcile());CHECK(f.Close());CHECK(f.Reconcile());
        CHECK(!f.BlocksFire());++configurations;
    }
    CHECK(configurations>0);std::printf("Physical feed chest/refill configurations: %u\n",configurations);return 0;
}
int EveryEnrolledFeedUsesAnatomicalControllerMapping(){
    for(const auto& registration:RegisteredBc2FeedProfiles()){
        Fixture anatomical(&registration),rotated(&registration);
        anatomical.wristCalibration=Identity();rotated.wristCalibration=Turn(2.29598f);
        CHECK(registration.mechanism.contacts[unsigned(FeedPart::Cover)].orientation==FeedContactOrientation::Acquired);
        // Both are independently tracked anatomical grasps, including one
        // with the actual failed attempt's angular difference from authored.
        CHECK(anatomical.OpenCover());CHECK(rotated.OpenCover());
        CHECK(anatomical.sample.raw.valid&&rotated.sample.raw.valid);
        const auto& a=anatomical.sample.raw;const auto& s=rotated.sample.raw;
        CHECK(feed_interaction_detail::Distance(a.rawLeftWristWorldMeters,s.rawLeftWristWorldMeters)<.00001f);
        CHECK(feed_interaction_detail::Angle(a.rawLeftWristWorldMeters,s.rawLeftWristWorldMeters)>
            registration.mechanism.contacts[unsigned(FeedPart::Cover)].maxAngleRadians);
        CHECK(a.owner==anatomical.sample.owner&&a.rigFingerprint==registration.geometry.rigFingerprint);
        CHECK(a.inputEvidence.sequence==anatomical.sample.input.sequence);
        CHECK(a.inputEvidence.observedNs==anatomical.sample.input.observedNs&&
            a.inputEvidence.deadlineNs==anatomical.sample.input.deadlineNs);
        CHECK(anatomical.result.pending&&anatomical.submits==1);
        CHECK(anatomical.submitted->command.authority.operation==FeedPhysicalOperation::OpenCover);
        CHECK(rotated.result.selected&&rotated.result.pending&&rotated.result.blocksFire&&rotated.submits==1);
    }
    return 0;
}
int EveryEnrolledFeedAcceptsObservedGraspWithDelayedPublication(){
    for(const auto& registration:RegisteredBc2FeedProfiles()){
        Fixture f(&registration);const auto m=f.Open();
        const auto contact=feed_interaction_detail::Multiply(m.motionFromContact,m.weaponFromMotion);
        auto wrist=feed_interaction_detail::Multiply(Turn(2.29598f),contact);
        const bool observedConfiguration=registration.geometry.asset=="QJU88_sp"&&
            registration.geometry.configurationPath=="Objects/Weapons/Handheld/MEC_lmg_QJY88/SP_lmg_QJY88";
        const auto contactOffset=observedConfiguration?.0453684f:
            .75f*registration.mechanism.contacts[unsigned(FeedPart::Cover)].radiusMeters;
        // Serial 1640 logs distance/angle, not the full wrist matrix. Keep its
        // exact distance for QJU88_sp; other weapons have different contact
        // radii. Test the same wrist angle and publication lag inside each
        // one's own contact, with a radial offset away from the hinge axis.
        const auto x=m.motionFromContact.values[3][0],y=m.motionFromContact.values[3][1],radius=std::hypot(x,y);
        for(unsigned axis=0;axis<3;++axis)wrist.values[3][axis]+=contactOffset*
            (x*m.weaponFromMotion.values[0][axis]+y*m.weaponFromMotion.values[1][axis])/radius;
        f.sequence=7049;f.Tick(wrist,false);const auto neutral=f.sample.raw;
        f.now+=3880000;f.PrepareTick(wrist,true);const auto pressed=f.sample.raw;f.Consume(&neutral);
        f.PrepareTick(wrist,true);f.Consume(&pressed);
        CHECK(f.sample.input.sequence==7052&&f.sample.raw.inputEvidence.sequence==7051);
        CHECK(f.result.diagnostic.rawPassed==511&&f.result.diagnostic.admissionPassed==63);
        CHECK(f.result.diagnostic.idlePassed==2045); // All acquisition gates; release is false.
        CHECK(f.result.diagnostic.coverDistanceMeters&&std::abs(*f.result.diagnostic.coverDistanceMeters-contactOffset)<.00001f);
        CHECK(f.result.diagnostic.coverAngleRadians&&std::abs(*f.result.diagnostic.coverAngleRadians-2.29598f)<.00001f);
        CHECK(f.result.diagnostic.neutralAgeNs==23880001);
        CHECK(f.result.ownsHand&&f.result.interactionStatus==FeedInteractionStatus::Acquired&&f.submits==0);
        CHECK(f.result.presentation&&f.result.presentation->mechanismWristWorldMeters);
        auto constrained=wrist;constrained.values[3]=contact.values[3];
        CHECK(feed_interaction_detail::Distance(*f.result.presentation->mechanismWristWorldMeters,wrist)>contactOffset*.99f);
        CHECK(FeedPresentationReachesArmAndHand(f,constrained)==0);
        CHECK(f.result.presentation->mechanismInput->sequence==pressed.inputEvidence.sequence);
        CHECK(f.result.presentation->mechanismClaim->deadlineNs==pressed.inputEvidence.deadlineNs);
        f.Consume(&pressed); // Duplicate render publication cannot add motion.
        CHECK(f.result.ownsHand&&!f.result.pending&&f.submits==0);
        CHECK(FeedPresentationReachesArmAndHand(f,constrained)==0);
        const auto moved=feed_interaction_detail::Multiply(feed_interaction_detail::Along(
            feed_interaction_detail::Multiply(wrist,feed_interaction_detail::Inverse(m.weaponFromMotion)),m.kind,m.travel/12),m.weaponFromMotion);
        const auto constrainedMoved=feed_interaction_detail::Multiply(feed_interaction_detail::Along(
            feed_interaction_detail::Multiply(constrained,feed_interaction_detail::Inverse(m.weaponFromMotion)),m.kind,m.travel/12),m.weaponFromMotion);
        f.Tick(moved,true);CHECK(f.result.interactionStatus==FeedInteractionStatus::Moving);
        CHECK(FeedPresentationReachesArmAndHand(f,constrainedMoved)==0);
        const auto movingRaw=f.sample.raw;const auto angle=f.result.presentation->coverRadians;
        CHECK(angle!=0&&NearPose(movingRaw.rawLeftWristWorldMeters,moved)); // Guided wrist never becomes recognition input.
        f.PrepareTick(moved,true);
        auto world=Turn(.37f);world.values[3]={1.2f,-.3f,.2f,1};
        // Palette construction can place the weapon after this publication.
        // The cover and its attached wrist must move together without republishing input.
        CHECK(FeedPresentationReachesArmAndHand(f,feed_interaction_detail::Multiply(constrainedMoved,world),world)==0);
        // This fixture's original raw has structural rig metadata only; keep
        // the same exact source evidence while moving its display transforms.
        auto repeated=movingRaw;repeated.weaponWorldMeters=world;
        repeated.rawLeftWristWorldMeters=feed_interaction_detail::Multiply(moved,world);
        f.Consume(&repeated);CHECK(f.result.interactionStatus==FeedInteractionStatus::Duplicate);
        CHECK(FeedPresentationReachesArmAndHand(f,feed_interaction_detail::Multiply(constrainedMoved,world))==0);
        CHECK(f.result.presentation->coverRadians==angle&&f.result.presentation->input.deadlineNs==movingRaw.inputEvidence.deadlineNs);
        CHECK(!Bc2FeedMechanismClaimFresh(*f.result.presentation,movingRaw.inputEvidence.deadlineNs));
        f.sample.raw.weaponWorldMeters=Identity();f.Tick(moved,false);
        CHECK(!f.result.ownsHand&&!f.result.pending&&f.submits==0);
        CHECK(f.result.presentation&&Near(f.result.presentation->coverRadians,angle)&&!f.result.presentation->mechanismWristWorldMeters);
    }
    return 0;
}
int EveryEnrolledFeedStartsFromReleasedApproach(){
    using namespace feed_interaction_detail;
    for(const auto& registration:RegisteredBc2FeedProfiles()){
        Fixture f(&registration);const auto m=f.Open();
        const auto start=feed_interaction_detail::Multiply(m.motionFromContact,m.weaponFromMotion);
        auto away=start;away.values[3][0]+=.3f;
        f.Tick(away,false);
        CHECK(f.result.diagnostic.idleChecked==2047&&!(f.result.diagnostic.idlePassed&(1u<<4)));
        CHECK(f.result.diagnostic.idlePassed&(1u<<6));
        CHECK(f.result.diagnostic.coverDistanceMeters&&*f.result.diagnostic.coverDistanceMeters>.29f);
        CHECK(f.result.diagnostic.coverAngleRadians&&*f.result.diagnostic.coverAngleRadians<.001f);
        f.Tick(start,true);
        CHECK(f.result.ownsHand&&f.result.blocksFire&&f.result.diagnostic.policyEvaluated);
        CHECK(f.result.interactionStatus==FeedInteractionStatus::Acquired);
        for(unsigned step=1;step<=12;++step)f.Tick(feed_interaction_detail::Multiply(Along(m.motionFromContact,m.kind,m.travel*step/12),m.weaponFromMotion),true);
        for(unsigned step=0;step<f.DwellPackets()&&!f.submitted;++step)
            f.Tick(feed_interaction_detail::Multiply(Along(m.motionFromContact,m.kind,m.travel),m.weaponFromMotion),true);
        CHECK(f.submitted&&f.submits==1&&f.submitted->command.authority.operation==FeedPhysicalOperation::OpenCover);
        CHECK(f.Reconcile());
    }
    return 0;
}
int EveryEnrolledSupplyPresentsDelayedPickupOffsetAndDropsLostHand(){
    using namespace feed_interaction_detail;unsigned configurations=0;
    for(const auto& registration:RegisteredBc2FeedProfiles())for(bool trackingLoss:{false,true}){
        Fixture f(&registration);f.wristCalibration=Identity();
        CHECK(f.OpenCover()&&f.Reconcile());
        CHECK(f.Place(FeedPhysicalOperation::DetachContainer)&&f.Reconcile());f.submitted.reset();
        auto chest=Identity();const auto config=Bc2FeedSupplyConfig();
        for(unsigned axis=0;axis<3;++axis)chest.values[3][axis]=config.pouchCenterMeters[axis];
        f.sample.bodyFromHand=chest;f.Tick(chest,false);f.Tick(chest,false);const auto neutral=f.sample.raw;
        auto pickup=feed_interaction_detail::Multiply(Turn(.4f),chest);pickup.values[3][0]+=.008f;
        f.PrepareTick(pickup,true);const auto pressed=f.sample.raw;f.Consume(&neutral);
        CHECK(f.result.presentation&&f.result.presentation->supplyObject&&f.result.ownsHand);
        CHECK(f.result.presentation->input.sequence==neutral.inputEvidence.sequence);
        CHECK(f.result.presentation->mechanismInput->sequence==pressed.inputEvidence.sequence);
        CHECK(f.result.presentation->mechanismClaim->inputSequence==pressed.inputEvidence.sequence);
        CHECK(FeedPresentationReachesArmAndHand(f,chest)==0);
        const auto firstPouch=f.result.presentation->weaponFromPouch;
        f.PrepareTick(pickup,true);f.Consume(&pressed);
        CHECK(f.result.interactionStatus==FeedInteractionStatus::Acquired&&f.result.presentation->supplyObject);
        CHECK(FeedPresentationReachesArmAndHand(f,pickup)==0);
        CHECK(NearPose(f.result.presentation->weaponFromPouch,firstPouch)); // Pickup keeps the real loose object.
        const auto acquired=f.result.presentation;
        const auto objectWorld=feed_interaction_detail::Multiply(acquired->weaponFromPouch,acquired->pouchCarryWeaponWorldMeters);
        const auto objectFromWrist=feed_interaction_detail::Multiply(pickup,Inverse(objectWorld));
        CHECK(Distance(objectFromWrist,registration.geometry.poses[unsigned(Bc2FeedPoseId::PouchWithdrawn)].sourceFromWrist)>.004f);
        auto movedWrist=pickup;movedWrist.values[3][0]+=.012f;
        f.PrepareTick(movedWrist,true);const auto moved=f.sample.raw;
        auto weapon=Turn(.37f);weapon.values[3]={1.2f,-.3f,.2f,1};
        CHECK(FeedPresentationReachesArmAndHand(f,pickup,weapon)==0); // Loose objects do not follow later weapon placement.
        const auto repeated=RepublishFeedContact(pressed,weapon,f.sample.input.nowNs);CHECK(repeated.valid);
        f.Consume(&repeated);
        CHECK(f.result.interactionStatus==FeedInteractionStatus::Duplicate&&f.result.presentation->supplyObject);
        CHECK(FeedPresentationReachesArmAndHand(f,pickup)==0); // Carried object stays in its retained world basis.
        CHECK(NearPose(f.result.presentation->weaponFromPouch,firstPouch));
        CHECK(f.result.presentation->input.deadlineNs==pressed.inputEvidence.deadlineNs);
        CHECK(f.result.presentation->mechanismClaim->inputSequence==f.sample.input.sequence);
        CHECK(Bc2FeedPresentationRetained(*acquired,*f.result.presentation,true,f.sample.input.nowNs));
        f.Consume(&moved);
        CHECK(f.result.interactionStatus==FeedInteractionStatus::Moving&&f.result.presentation->supplyObject);
        CHECK(FeedPresentationReachesArmAndHand(f,movedWrist)==0);
        const auto carried=f.result.presentation;
        const auto carriedWorld=feed_interaction_detail::Multiply(carried->weaponFromPouch,carried->pouchCarryWeaponWorldMeters);
        CHECK(NearPose(feed_interaction_detail::Multiply(objectFromWrist,carriedWorld),movedWrist));
        CHECK(!Bc2FeedMechanismClaimFresh(*carried,carried->input.deadlineNs));
        const auto cover=carried->coverRadians;const auto counts=f.native.counts;
        if(trackingLoss){
            f.sample.input.tracked[0]=false;f.Tick(movedWrist,true);
            CHECK(!f.result.presentation&&!f.result.ownsHand&&f.BlocksFire());
            // Untracked mapping returns an empty raw packet. Restore the
            // independently observed rig before constructing the recovery packet.
            f.sample.raw.rig=moved.rig;
            f.sample.input.tracked[0]=true;f.Tick(movedWrist,false);
        }else f.Tick(movedWrist,false);
        if(!f.result.presentation||f.result.presentation->mechanismWristWorldMeters||f.result.presentation->mechanismClaim)
            std::fprintf(stderr,"Supply release failure: profile=%llu trackingLoss=%d input=%llu source=%llu raw=%d exit=%u selection=%u presentation=%d wrist=%d claim=%d\n",
                static_cast<unsigned long long>(registration.key),trackingLoss,static_cast<unsigned long long>(f.sample.input.sequence),
                static_cast<unsigned long long>(f.sample.raw.inputEvidence.sequence),f.sample.raw.valid,unsigned(f.result.diagnostic.exit),
                f.result.diagnostic.selectionFailure,bool(f.result.presentation),
                bool(f.result.presentation&&f.result.presentation->mechanismWristWorldMeters),
                bool(f.result.presentation&&f.result.presentation->mechanismClaim));
        CHECK(f.result.presentation&&!f.result.presentation->mechanismWristWorldMeters&&!f.result.presentation->mechanismClaim);
        CHECK(!f.result.ownsHand&&Near(f.result.presentation->coverRadians,cover)&&f.native.counts==counts);
        CHECK(!f.result.presentation->hiddenPartsMask&&!f.result.presentation->droppedOriginalAtNs);
        CHECK(NearPose(feed_interaction_detail::Multiply(f.result.presentation->weaponFromPouch,
            f.result.presentation->pouchCarryWeaponWorldMeters),carriedWorld));
        CHECK(f.submits==2&&!f.submitted&&f.BlocksFire());
        if(!trackingLoss)++configurations;
    }
    CHECK(configurations==24);return 0;
}
int EveryEnrolledSupplyPinsPickupToOriginalNativeObservation(){
    unsigned configurations=0,cycles=0,receipts=0;
    for(const auto& registration:RegisteredBc2FeedProfiles()){
        ++configurations;
        for(bool delayed:{false,true}){
            Fixture f(&registration);f.wristCalibration=Identity();const auto before=f.native.counts;
            CHECK(f.OpenCover()&&f.Reconcile());
            CHECK(f.Place(FeedPhysicalOperation::DetachContainer)&&f.Reconcile());f.submitted.reset();
            auto chest=Identity();const auto config=Bc2FeedSupplyConfig();
            for(unsigned axis=0;axis<3;++axis)chest.values[3][axis]=config.pouchCenterMeters[axis];
            f.sample.bodyFromHand=chest;f.Tick(chest,false);f.Tick(chest,false);const auto neutral=f.sample.raw;
            CHECK(f.result.interactionStatus==FeedInteractionStatus::Armed);
            std::array<ReloadRawContact,2> queuedNeutral{};
            if(delayed)for(auto& packet:queuedNeutral){
                f.PrepareTick(chest,false);packet=f.sample.raw;f.Consume(&neutral);
                CHECK(f.result.interactionStatus==FeedInteractionStatus::Duplicate);
            }
            auto pickup=feed_interaction_detail::Multiply(Turn(.4f),chest);pickup.values[3][0]+=.008f;
            f.sample.bodyFromHand=pickup;f.PrepareTick(pickup,true);const auto pressed=f.sample.raw;
            const auto paired=f.backend.ReadAt(f.sample.owner,pressed.inputEvidence.observedNs,f.sample.input.nowNs);CHECK(paired);
            // Native simulation publishes after this input, before consumption.
            // Custody from that newer view would reject the original pickup's
            // paired native observation and suspend an otherwise valid grasp.
            f.native.observedNs=f.now+Ms;f.native.deadlineNs=f.native.observedNs+(std::min)(100*Ms,registration.mechanism.maxAgeNs);
            f.native.binding.observedNs=f.native.observedNs;f.native.binding.deadlineNs=f.native.deadlineNs;
            f.now=f.native.observedNs+1;f.sample.input.nowNs=f.now;f.sample.binding=f.native.binding;
            CHECK(f.backend.Publish(f.native,f.now));++f.nativePublications;
            const auto latest=f.backend.Read(f.sample.owner,f.now);CHECK(latest);
            CHECK(latest->sequence>paired->sequence&&latest->observedNs>pressed.inputEvidence.observedNs);
            CHECK(f.backend.ReadAt(f.sample.owner,pressed.inputEvidence.observedNs,f.now)==paired);
            f.Consume(delayed?&neutral:nullptr);
            const auto claim=f.hands.Current(InteractionHand::Left);CHECK(claim&&claim->token.kind==HandClaimKind::AmmoObject);
            CHECK(f.result.ownsHand&&f.result.presentation&&f.result.presentation->supplyObject);
            CHECK(f.result.presentation->supplyObject->claim.token==claim->token);
            CHECK(claim->inputSequence==pressed.inputEvidence.sequence&&claim->deadlineNs==pressed.inputEvidence.deadlineNs);
            CHECK(FeedPresentationReachesArmAndHand(f,delayed?chest:pickup)==0);
            const auto firstPouch=f.result.presentation->weaponFromPouch;
            if(delayed){
                CHECK(f.result.interactionStatus==FeedInteractionStatus::Duplicate);
                for(const auto& packet:queuedNeutral){
                    f.PrepareTick(pickup,true);f.Consume(&packet);
                    // A second real neutral can arrive after acquisition. It
                    // arms policy without a target; it cannot erase the grasp.
                    CHECK(f.result.interactionStatus==FeedInteractionStatus::Armed);
                    CHECK(f.result.presentation&&f.result.presentation->supplyObject&&f.result.ownsHand);
                    CHECK(f.result.presentation->input.sequence==packet.inputEvidence.sequence);
                    CHECK(f.result.presentation->input.deadlineNs==packet.inputEvidence.deadlineNs);
                    CHECK(f.hands.Current(InteractionHand::Left)->token==claim->token);
                    CHECK(NearPose(f.result.presentation->weaponFromPouch,firstPouch));
                    CHECK(FeedPresentationReachesArmAndHand(f,chest)==0);
                    f.Consume(&packet);CHECK(f.result.interactionStatus==FeedInteractionStatus::Duplicate);
                }
                f.PrepareTick(pickup,true);f.Consume(&pressed);
            }
            CHECK(f.result.interactionStatus==FeedInteractionStatus::Acquired);
            CHECK(f.result.reloadStatus==FeedReloadStatus::AwaitingPhysicalCompletion);
            CHECK(f.result.diagnostic.sourceNativeSequence==paired->sequence);
            CHECK(f.result.diagnostic.sourceNativeObservedNs==paired->observedNs);
            CHECK(f.result.diagnostic.sourceNativeDeadlineNs==paired->deadlineNs);
            CHECK(f.result.presentation->input.deadlineNs==pressed.inputEvidence.deadlineNs);
            CHECK(f.hands.Current(InteractionHand::Left)->token==claim->token);
            CHECK(FeedPresentationReachesArmAndHand(f,pickup)==0);
            CHECK(f.native.counts==before&&f.submits==2&&!f.result.pending&&!f.backend.Pending());
            f.Consume(&pressed);CHECK(f.result.interactionStatus==FeedInteractionStatus::Duplicate);
            CHECK(FeedPresentationReachesArmAndHand(f,pickup)==0);
            f.Tick(pickup,true);CHECK(f.result.ownsHand&&f.result.presentation->supplyObject);
            CHECK(f.Place(FeedPhysicalOperation::AttachContainer,false,true,true));
            CHECK(f.submitted&&f.submitted->command.authority.operation==FeedPhysicalOperation::AttachContainer);
            CHECK(f.submitted->command.before.counts==before&&f.native.counts==before&&f.submits==3);
            const auto after=f.submitted->command.after;
            CHECK(f.Reconcile()&&f.native.counts==after);
            CHECK(f.Place(FeedPhysicalOperation::SeatFeed)&&f.Reconcile());
            CHECK(f.Close()&&f.Reconcile());CHECK(f.submits==5&&!f.BlocksFire());
            ++cycles;receipts+=f.submits;
        }
    }
    CHECK(configurations==24&&cycles==48&&receipts==240);
    std::printf("Feed supply paired native: %u profiles, %u immediate/delayed full cycles, %u offline mocked adapter receipts\n",configurations,cycles,receipts);
    return 0;
}
int EveryEnrolledSupplyCompletesAcrossSplitClockRenewalGaps(){
    // Actual245: current input7074 had no readAt native publication while the
    // renderer still delivered valid7073. Renewing supply onto7074 erased the
    // last valid hand proof before raw7074 arrived. The old current==raw gap
    // fixture returned early and never exercised that ordering.
    const std::array<std::vector<unsigned>,2> cadences{{{1,0},{2}}};
    unsigned profiles=0,cycles=0,receipts=0;
    for(const auto& registration:RegisteredBc2FeedProfiles()){
        ++profiles;
        for(const auto& cadence:cadences)for(unsigned gapStep:{4u,24u})for(unsigned gapPackets:{1u,2u}){
            Fixture f(&registration);f.nativeCadence=cadence;f.wristCalibration=Turn(2.29598f);
            const auto before=f.native.counts;bool injected=false;int injectionResult=0;
            const auto gap=[&](Fixture& f)->int{
                const auto held=f.hands.Current(InteractionHand::Left);CHECK(held&&held->token.kind==HandClaimKind::AmmoObject);
                const auto admitted=f.sample.raw;const auto selection=f.sample.selected;CHECK(selection);
                const auto originalSelectionSequence=selection->sequence;
                const auto originalSelectionObserved=selection->observedNs,originalSelectionDeadline=selection->deadlineNs;
                const auto counts=f.native.counts;const auto submits=f.submits;
                const auto unchangedHand=[&](){const auto current=f.hands.Current(InteractionHand::Left);
                    return current&&current->token==held->token&&current->inputSequence==held->inputSequence&&current->deadlineNs==held->deadlineNs;};
                CHECK(!f.result.pending&&!f.backend.Pending());
                ReloadRawContact missing;
                f.publishSelected=false;
                for(unsigned packet=0;packet<gapPackets;++packet){
                    f.PrepareTick(f.lastWrist,true);missing=f.sample.raw;f.missingPacketNative=true;
                    // Current N is missing, rendered N-1 still has exact native
                    // proof. This must not renew the carried object onto N.
                    f.Consume(&admitted);
                    CHECK(unchangedHand());
                    CHECK(f.result.ownsHand&&DisplayOnlyFeed(f.result)&&!f.result.diagnostic.policyEvaluated);
                    CHECK(f.submits==submits&&f.native.counts==counts);
                    f.Consume(&missing);
                    CHECK(unchangedHand());
                    CHECK(f.result.ownsHand&&DisplayOnlyFeed(f.result)&&!f.result.pending&&!f.backend.Pending());
                    // Repeated geometry and later availability cannot backfill
                    // N's absent native or change any original expiry time.
                    const auto readAts=f.nativeReadAts;f.missingPacketNative=false;f.Consume(&missing);
                    CHECK(f.nativeReadAts==readAts&&unchangedHand());
                    CHECK(f.sample.selected==selection&&selection->sequence==originalSelectionSequence&&
                        selection->observedNs==originalSelectionObserved&&selection->deadlineNs==originalSelectionDeadline);
                    CHECK(f.submits==submits&&f.native.counts==counts);
                }
                f.publishSelected=true;f.PrepareTick(f.lastWrist,true);const auto fresh=f.sample.raw;
                f.Consume(&missing);CHECK(unchangedHand());
                CHECK(DisplayOnlyFeed(f.result)&&!f.result.pending);
                // Only a fresh independently paired packet resumes movement.
                f.Consume(&fresh);const auto resumed=f.hands.Current(InteractionHand::Left);
                CHECK(resumed&&resumed->token==held->token&&resumed->inputSequence==fresh.inputEvidence.sequence);
                CHECK(f.result.ownsHand&&f.result.presentation&&f.result.presentation->supplyObject);
                CHECK(f.submits==submits&&f.native.counts==counts&&!f.result.pending);
                return 0;
            };
            f.placementStep=[&](Fixture& fixture,FeedPhysicalOperation operation,unsigned step){
                if(operation==FeedPhysicalOperation::AttachContainer&&step==gapStep&&!injected){
                    injected=true;injectionResult=gap(fixture);return injectionResult==0;}
                return true;
            };
            CHECK(f.OpenCover()&&f.Reconcile());CHECK(f.submits==1&&f.native.counts==before);
            CHECK(f.Place(FeedPhysicalOperation::DetachContainer)&&f.Reconcile());CHECK(f.submits==2&&f.native.counts==before);
            CHECK(f.Place(FeedPhysicalOperation::AttachContainer));CHECK(injected&&injectionResult==0);
            CHECK(f.submitted&&f.submits==3&&f.submitted->command.before.counts==before);
            const auto after=f.submitted->command.after;
            const auto life=f.submitted->command.request.lifetime;
            CHECK(f.Reconcile()&&f.native.counts==after);
            CHECK(f.Place(FeedPhysicalOperation::SeatFeed)&&f.Reconcile());
            CHECK(f.Close()&&f.Reconcile());CHECK(f.submits==5&&!f.BlocksFire()&&f.native.counts==after);
            for(std::uint64_t id=1;id<=5;++id){
                const auto receipt=f.backend.Receipt(life,id);CHECK(receipt);
                CHECK(receipt->acknowledgement.status==FeedReloadAcknowledgement::Applied);
                CHECK(receipt->command.authority.effect==FeedAmmunitionEffect::RefillLoaded? id==3:id!=3);
                ++receipts;
            }
            ++cycles;
        }
    }
    CHECK(profiles==24&&cycles==192&&receipts==960);
    std::printf("Feed supply split clocks: %u profiles, %u full cycles, %u offline mocked adapter receipts\n",profiles,cycles,receipts);
    return 0;
}
int EveryEnrolledSupplySplitClockRetentionRejectsContradictions(){
    unsigned cases=0;
    for(const auto& registration:RegisteredBc2FeedProfiles())for(unsigned fault=0;fault<8;++fault){
        Fixture f(&registration);CHECK(f.OpenCover()&&f.Reconcile());
        CHECK(f.Place(FeedPhysicalOperation::DetachContainer)&&f.Reconcile());f.submitted.reset();
        CHECK(f.PickSupply());const auto held=f.hands.Current(InteractionHand::Left);CHECK(held);
        const auto admitted=f.sample.raw;
        const auto native=f.backend.ReadAt(f.sample.owner,admitted.inputEvidence.observedNs,f.sample.input.nowNs);CHECK(native);
        auto counts=f.native.counts;
        // Each contradiction is introduced during the same split-clock join,
        // while the renderer still supplies the previous genuine held packet.
        if(fault==1)f.sample.input.tracked[0]=false;
        if(fault==2)f.native.identity.firing[0]+=4;
        if(fault==3){++f.native.counts.reserve;++counts.reserve;}
        if(fault==4)f.now=(std::min)(held->deadlineNs,native->deadlineNs)-10*Ms;
        f.PrepareTick(f.lastWrist,fault!=0,fault==6);
        if(fault==5)f.SelectWeaponData(f.selected->weaponData+1);
        if(fault==7)CHECK(f.hands.Release(f.sample.input,held->token).accepted);
        const auto missing=f.sample.raw;f.missingPacketNative=true;f.Consume(&admitted);
        CHECK(!f.hands.Current(InteractionHand::Left)&&!f.result.ownsHand);
        CHECK(!f.result.pending&&!f.backend.Pending()&&f.submits==2&&f.native.counts==counts);
        CHECK(!f.result.presentation||!f.result.presentation->mechanismWristWorldMeters);
        f.missingPacketNative=false;const auto reads=f.nativeReadAts;f.Consume(&missing);
        CHECK(f.nativeReadAts==reads&&!f.hands.Current(InteractionHand::Left));
        CHECK(!f.result.pending&&!f.backend.Pending()&&f.submits==2&&f.native.counts==counts);
        ++cases;
    }
    CHECK(cases==192);
    std::printf("Feed supply split-clock contradictions: 24 profiles, %u release/tracking/identity/count/expiry/selection/cancel/claim cases\n",cases);
    return 0;
}
int EveryEnrolledFeedTransfersDelayedSupportAtEachAcquisition(){
    // Actual253 completed Open/Detach/Attach, then current held10984 consumed
    // neutral raw10983. Ordinary support claimed10983 before held raw10984
    // arrived. Replay that arbitration at every mechanism acquisition in the
    // same complete cycle, including CloseCover beyond the observed failure.
    const std::array<std::vector<unsigned>,2> cadences{{{1,0},{2}}};
    unsigned profiles=0,cycles=0,receipts=0,transfers=0;
    for(const auto& registration:RegisteredBc2FeedProfiles()){
        ++profiles;
        for(const auto& cadence:cadences)for(bool sameCurrent:{false,true}){
            Fixture f(&registration);f.nativeCadence=cadence;f.delayedRender=true;f.wristCalibration=Turn(2.29598f);
            f.captureCadenceTrace=true;
            const auto stage=[&](bool completed,FeedPhysicalOperation operation){
                if(!completed)f.PrintCadenceFailure(unsigned(&cadence-&cadences[0])*2+unsigned(sameCurrent),operation);
                return completed;
            };
            // Reuse the scheduled corroboration from the independent-clock
            // family fixture. Two controller ticks can still render the
            // pre-refill native observation; no publication is forced here.
            const auto reconcile=[&]()->bool{
                if(!f.Callback())return false;
                for(std::size_t packet=0;packet<f.nativeCadence.size()+3;++packet)f.Tick(f.lastWrist,false);
                return !f.backend.Pending()&&!f.result.pending;
            };
            const auto before=f.native.counts;std::optional<HandClaim> support;
            unsigned acquisitions=0,completedTransfers=0;
            const auto transferred=[&](Fixture& f)->int{
                CHECK(support);const auto hand=f.hands.Current(InteractionHand::Left),gun=f.hands.Current(InteractionHand::Right);
                CHECK(hand&&gun&&hand->token.kind==HandClaimKind::Mechanism&&hand->token.id!=support->token.id);
                CHECK(hand->token.owner==support->token.owner&&hand->token.item==support->token.item&&
                    hand->token.prerequisiteClaim==support->token.prerequisiteClaim&&hand->token.prerequisiteClaim==gun->token.id);
                CHECK(hand->inputSequence==f.sample.raw.inputEvidence.sequence&&hand->deadlineNs==f.sample.raw.inputEvidence.deadlineNs);
                CHECK(f.result.ownsHand&&f.result.interactionStatus==FeedInteractionStatus::Acquired&&f.result.presentation);
                CHECK(f.result.presentation->mechanismClaim&&f.result.presentation->mechanismClaim->token==hand->token&&
                    f.result.presentation->mechanismWristWorldMeters);
                support.reset();++completedTransfers;return 0;
            };
            f.afterDelayedConsume=[&](Fixture& f,const ReloadRawContact& current)->int{
                if(support&&!f.sample.raw.inputEvidence.released[0])CHECK(transferred(f)==0);
                if(!f.sample.grip||!f.sample.raw.inputEvidence.released[0]||f.result.ownsHand||f.result.pending||
                    f.hands.Current(InteractionHand::Left))return 0;
                const auto gun=f.hands.Current(InteractionHand::Right);CHECK(gun);
                const auto original=f.sample.raw.inputEvidence;
                const auto acquired=f.hands.AcquireFrom(f.sample.input,original,{f.sample.input.owner,InteractionHand::Left,
                    HandClaimKind::WeaponSupport,f.sample.weapon,{{2,f.sample.weapon.id},original.sequence,original.deadlineNs,true},
                    ++f.intent,gun->token.id});
                CHECK(acquired.accepted&&acquired.claim);support=acquired.claim;++acquisitions;
                CHECK(support->inputSequence==original.sequence&&support->deadlineNs==original.deadlineNs);
                const auto neutral=f.sample.raw;const auto counts=f.native.counts;const auto submits=f.submits;
                for(unsigned repeat=0;repeat<2;++repeat){
                    f.Consume(&neutral);const auto waiting=f.hands.Current(InteractionHand::Left);
                    CHECK(waiting&&waiting->token==support->token&&waiting->inputSequence==support->inputSequence&&
                        waiting->deadlineNs==support->deadlineNs&&!f.result.ownsHand);
                    CHECK(f.native.counts==counts&&f.submits==submits&&!f.result.pending);
                    CHECK(!f.result.presentation||!f.result.presentation->mechanismWristWorldMeters);
                }
                if(sameCurrent){f.Consume(&current);CHECK(transferred(f)==0);}
                return 0;
            };
            CHECK(f.OpenCover()&&reconcile());CHECK(f.submits==1&&f.native.counts==before);
            CHECK(f.Place(FeedPhysicalOperation::DetachContainer)&&reconcile());CHECK(f.submits==2&&f.native.counts==before);
            CHECK(f.Place(FeedPhysicalOperation::AttachContainer));CHECK(f.submitted&&f.submits==3);
            const auto after=f.submitted->command.after;const auto life=f.submitted->command.request.lifetime;
            CHECK(reconcile()&&f.native.counts==after);
            CHECK(stage(f.Place(FeedPhysicalOperation::SeatFeed)&&reconcile(),FeedPhysicalOperation::SeatFeed));
            CHECK(stage(f.Close()&&reconcile(),FeedPhysicalOperation::CloseCover));
            CHECK(!f.afterDelayedConsumeResult&&!support&&acquisitions==4&&completedTransfers==4);
            CHECK(f.submits==5&&!f.BlocksFire()&&f.native.counts==after);
            for(std::uint64_t id=1;id<=5;++id){const auto receipt=f.backend.Receipt(life,id);CHECK(receipt);
                CHECK(receipt->acknowledgement.status==FeedReloadAcknowledgement::Applied);
                CHECK(receipt->command.authority.effect==FeedAmmunitionEffect::RefillLoaded?id==3:id!=3);++receipts;}
            ++cycles;transfers+=completedTransfers;
        }
    }
    CHECK(profiles==24&&cycles==96&&receipts==480&&transfers==384);
    std::printf("Feed support arbitration: %u profiles, %u full cycles, %u guarded transfers, %u offline mocked adapter receipts\n",
        profiles,cycles,transfers,receipts);
    return 0;
}
int EveryEnrolledFeedRejectsUnarmedOrUnrelatedSupportTransfer(){
    unsigned cases=0;
    // Keep the full native-receipt prefix for each contradiction; rejection
    // must not undo the already completed refill or consume another claim.
    for(const auto& registration:RegisteredBc2FeedProfiles())for(unsigned fault=0;fault<6;++fault){
        Fixture f(&registration);CHECK(f.OpenCover()&&f.Reconcile());
        CHECK(f.Place(FeedPhysicalOperation::DetachContainer)&&f.Reconcile());
        CHECK(f.Place(FeedPhysicalOperation::AttachContainer)&&f.Reconcile());f.submitted.reset();
        const auto counts=f.native.counts;const auto motion=f.PlacementMotion(FeedPhysicalOperation::SeatFeed);CHECK(motion);
        const auto start=feed_interaction_detail::Multiply(motion->placement.sourceFromGrip,motion->placement.weaponFromSource);
        f.Tick(start,false);f.Tick(start,false);const auto neutral=f.sample.raw;
        f.PrepareTick(start,true);const auto held=f.sample.raw;f.Consume(&neutral);
        const auto gun=f.hands.Current(InteractionHand::Right);CHECK(gun&&!f.hands.Current(InteractionHand::Left));
        const auto kind=fault==0?HandClaimKind::Sight:fault==1?HandClaimKind::BodyInventory:HandClaimKind::WeaponSupport;
        const auto item=fault==1?HandInteractionKey{999,1}:f.sample.weapon;
        const auto acquired=f.hands.AcquireFrom(f.sample.input,neutral.inputEvidence,{f.sample.input.owner,InteractionHand::Left,
            kind,item,{{2,f.sample.weapon.id},neutral.inputEvidence.sequence,neutral.inputEvidence.deadlineNs,true},
            ++f.intent,fault==1?0:gun->token.id});
        CHECK(acquired.accepted&&acquired.claim);const auto old=*acquired.claim;
        if(fault==2){
            // A true held miss consumes the edge. Entering valid belt contact
            // on a later held packet must not steal the still-live support.
            auto miss=held;miss.rawLeftWristWorldMeters.values[3][0]+=.5f;f.Consume(&miss);
            f.Tick(start,true);
        }else if(fault==3){
            // Cancellation explicitly invalidates the earlier neutral even
            // though the unrelated support claim remains within its lease.
            f.sample.cancel=true;f.Consume(&held);f.sample.cancel=false;f.Tick(start,true);
        }else if(fault==4){
            // No expired neutral can authorize acquisition using fresh held
            // evidence, even after ordinary gun/support claims are reacquired.
            f.now=neutral.inputEvidence.deadlineNs;
            f.PrepareTick(start,true);
            // The real gap expires the root GunHold too. Its failed renewal
            // cannot resurrect it: acquire a new root from this current,
            // recorded held packet before acquiring its dependent support.
            CHECK(!f.hands.Current(InteractionHand::Right)&&!f.hands.Current(InteractionHand::Left));
            const auto gunProof=HandContactProof{gun->token.contact,f.sample.input.sequence,f.sample.input.deadlineNs,true};
            const auto freshGun=f.hands.Acquire(f.sample.input,{f.sample.input.owner,InteractionHand::Right,
                HandClaimKind::GunHold,f.sample.weapon,gunProof,++f.intent,0});
            CHECK(freshGun.accepted&&freshGun.claim&&freshGun.claim->token.id!=gun->token.id);
            const auto proof=HandContactProof{old.token.contact,f.sample.input.sequence,f.sample.input.deadlineNs,true};
            // Reacquire ordinary support without creating another neutral.
            const auto renewed=f.hands.AcquireFrom(f.sample.input,f.sample.input,{f.sample.input.owner,InteractionHand::Left,
                HandClaimKind::WeaponSupport,f.sample.weapon,proof,++f.intent,freshGun.claim->token.id});
            CHECK(renewed.accepted&&renewed.claim&&renewed.claim->inputSequence==f.sample.input.sequence&&
                renewed.claim->deadlineNs==f.sample.input.deadlineNs);f.Consume();
        }else if(fault==5){
            // Missing original native evidence cannot be substituted by the
            // newer readable native view; it cannot authorize a transfer.
            f.missingPacketNative=true;
            // Held was already recorded by the delayed neutral consume, so
            // use a distinct, genuinely unpaired held packet.
            f.PrepareTick(start,true);f.Consume();
        }else f.Consume(&held);
        const auto hand=f.hands.Current(InteractionHand::Left);
        CHECK(hand&&hand->token.kind==kind&&!f.result.ownsHand);
        if(fault!=4)CHECK(hand->token==old.token&&hand->inputSequence==old.inputSequence&&hand->deadlineNs==old.deadlineNs);
        CHECK(f.submits==3&&!f.submitted&&!f.result.pending&&!f.backend.Pending()&&f.native.counts==counts);
        CHECK(!f.result.presentation||!f.result.presentation->mechanismWristWorldMeters);
        ++cases;
    }
    CHECK(cases==144);
    std::printf("Feed support arbitration contradictions: 24 profiles, %u unrelated/held-entry/cancel/expiry/native cases\n",cases);
    return 0;
}
int EveryEnrolledSupplyRejectsPickupWithoutItsOriginalNative(){
    unsigned cases=0;
    for(const auto& registration:RegisteredBc2FeedProfiles()){
        Fixture f(&registration);CHECK(f.OpenCover()&&f.Reconcile());
        CHECK(f.Place(FeedPhysicalOperation::DetachContainer)&&f.Reconcile());f.submitted.reset();
        auto chest=Identity();const auto config=Bc2FeedSupplyConfig();
        for(unsigned axis=0;axis<3;++axis)chest.values[3][axis]=config.pouchCenterMeters[axis];
        f.sample.bodyFromHand=chest;f.Tick(chest,false);f.Tick(chest,false);const auto neutral=f.sample.raw;
        const auto counts=f.native.counts;
        f.PrepareTick(chest,true);const auto pressed=f.sample.raw;
        f.missingPacketNative=true;f.Consume(&neutral);f.missingPacketNative=false;
        // The delayed neutral and current view remain valid. Neither may
        // substitute for the pickup packet's missing native observation.
        CHECK(f.result.selected&&f.result.diagnostic.rawPassed==511&&f.result.diagnostic.policyEvaluated);
        CHECK(!f.result.ownsHand&&!f.hands.Current(InteractionHand::Left));
        CHECK(f.result.presentation&&!f.result.presentation->supplyObject&&!f.result.presentation->mechanismWristWorldMeters);
        f.PrepareTick(chest,true);f.Consume(&pressed);
        CHECK(!f.result.ownsHand&&DisplayOnlyFeed(f.result)&&!f.hands.Current(InteractionHand::Left));
        f.Tick(chest,true);CHECK(!f.result.ownsHand&&!f.hands.Current(InteractionHand::Left));
        CHECK(f.native.counts==counts&&f.submits==2&&!f.result.pending&&!f.backend.Pending()&&f.BlocksFire());
        ++cases;
    }
    CHECK(cases==24);std::printf("Feed supply unpaired pickup: %u profiles rejected without native or pose substitution\n",cases);
    return 0;
}
int EveryEnrolledSupplyFreezesMotionJumpWithoutDroppingHeldGrasp(){
    unsigned cases=0;
    for(const auto& registration:RegisteredBc2FeedProfiles())for(bool trackingLoss:{false,true}){
        Fixture f(&registration);CHECK(f.OpenCover()&&f.Reconcile());
        CHECK(f.Place(FeedPhysicalOperation::DetachContainer)&&f.Reconcile());f.submitted.reset();
        CHECK(f.PickSupply());const auto claim=f.hands.Current(InteractionHand::Left);CHECK(claim);
        const auto counts=f.native.counts;const auto held=f.result.presentation;
        CHECK(held&&held->supplyObject&&held->mechanismWristWorldMeters);
        // Reject discontinuous motion without losing the valid grasp. The
        // carried object and guided wrist stay at their last accepted pose.
        auto jump=f.lastWrist;jump.values[3][0]+=2;f.Tick(jump,true);
        CHECK(f.result.interactionStatus==FeedInteractionStatus::Moving&&f.result.ownsHand);
        CHECK(f.result.presentation&&f.result.presentation->supplyObject);
        const auto& frozen=*f.result.presentation;
        CHECK(f.hands.Current(InteractionHand::Left)&&f.hands.Current(InteractionHand::Left)->token==claim->token);
        CHECK(frozen.supplyObject->claim.token==claim->token&&frozen.supplyObject->identity==held->supplyObject->identity);
        CHECK(frozen.supplyObject->item==held->supplyObject->item&&frozen.supplyObject->units==held->supplyObject->units);
        CHECK(frozen.lifetime==held->lifetime&&frozen.supply==held->supply&&Near(frozen.coverRadians,held->coverRadians));
        CHECK(NearPose(frozen.weaponFromPouch,held->weaponFromPouch)&&NearPose(frozen.pouchCarryWeaponWorldMeters,held->pouchCarryWeaponWorldMeters));
        CHECK(Bc2FeedMechanismClaimFresh(frozen,f.sample.input.nowNs)&&!frozen.displayInput);
        CHECK(frozen.mechanismClaim->token==claim->token&&frozen.mechanismInput->sequence==f.sample.input.sequence);
        CHECK(NearPose(*frozen.mechanismWristWorldMeters,*held->mechanismWristWorldMeters));
        CHECK(f.native.counts==counts&&f.submits==2&&!f.result.pending&&!f.backend.Pending()&&f.BlocksFire());
        if(trackingLoss)f.sample.input.tracked[0]=false;
        f.Tick(jump,trackingLoss);
        CHECK(!f.hands.Current(InteractionHand::Left)&&!f.result.ownsHand);
        CHECK(!f.result.presentation||(!f.result.presentation->supplyObject&&!f.result.presentation->mechanismWristWorldMeters&&
            !f.result.presentation->mechanismClaim&&!f.result.presentation->mechanismInput));
        CHECK(f.native.counts==counts&&f.submits==2&&!f.result.pending&&!f.backend.Pending()&&f.BlocksFire());
        ++cases;
    }
    CHECK(cases==48);
    std::printf("Feed supply held publication: 24 profiles, %u frozen-jump/release-or-tracking cases\n",cases);
    return 0;
}
ReloadRawContact RepublishFeedContact(const ReloadRawContact& source,const fvr::math::Matrix4& world,std::int64_t now){
    using namespace feed_interaction_detail;
    InputFrame input;input.generation=source.inputEvidence.sequence;input.spaceGeneration=source.owner.space;
    input.predictedNs=source.inputEvidence.observedNs;input.focused=input.headValid=true;
    for(auto& hand:input.hands)hand.gripTracked=hand.aimTracked=true;
    const auto local=feed_interaction_detail::Multiply(source.rawLeftWristWorldMeters,Inverse(source.weaponWorldMeters));
    return BuildSupportControllerMapping(source.owner,source.inputEvidence,input,source.rig,source.rigFingerprint,
        feed_interaction_detail::Multiply(local,world),world,world,1,now);
}
int EveryEnrolledFeedCompletesWithIndependentPublicationClocks(){
    // Counts are native publications per 10 ms controller packet. Include
    // controller:native 2:1, 3:1, irregular, and native:controller 2:1.
    const std::array<std::vector<unsigned>,4> cadences{{{1,0},{1,0,0},{1,0,1,0,0,0,1,0,0},{2}}};
    constexpr std::array operations{FeedPhysicalOperation::OpenCover,FeedPhysicalOperation::DetachContainer,
        FeedPhysicalOperation::AttachContainer,FeedPhysicalOperation::SeatFeed,FeedPhysicalOperation::CloseCover};
    unsigned configurations=0,cycles=0,receipts=0;
    for(const auto& registration:RegisteredBc2FeedProfiles()){
        ++configurations;
        for(unsigned cadence=0;cadence<cadences.size();++cadence)for(bool delayed:{false,true}){
            Fixture f(&registration);f.nativeCadence=cadences[cadence];f.sequence=9004;
            f.captureCadenceTrace=true;
            f.wristCalibration=Turn(2.29598f);f.delayedRender=f.repeatRender=delayed;
            const auto before=f.native.counts;
            const auto units=(std::min)(before.capacity-before.loaded,before.reserve);
            std::uint64_t lifetime=0;
            const auto attempted=[&](FeedPhysicalOperation operation,bool completed){
                if(!completed)f.PrintCadenceFailure(cadence,operation);
                return completed;
            };
            const auto reconcile=[&](unsigned stage)->int{
                CHECK(f.submitted&&f.submits==stage+1&&f.backend.Pending());
                const auto command=f.submitted->command;
                CHECK(command.authority.operation==operations[stage]&&command.request.id==stage+1);
                CHECK(f.submitted->geometry.registrationKey==registration.key&&!f.result.submitRefused);
                if(!lifetime)lifetime=command.request.lifetime;
                CHECK(command.request.lifetime==lifetime);
                CHECK(command.before.sequence==f.submitted->source.sequence);
                CHECK(command.before.observedNs==f.submitted->source.observedNs);
                CHECK(command.before.deadlineNs==f.submitted->source.deadlineNs);
                CHECK(command.mutationRequired==(stage==2));
                CHECK(command.authority.rounds==(stage==2?units:0));
                // Offline callback rows exercise the real adapter's three
                // firing-copy reconciliation. They are not game receipts.
                CHECK(f.Callback());
                const auto receipt=f.backend.Receipt(lifetime,command.request.id);CHECK(receipt);
                CHECK(receipt->command==command&&receipt->acknowledgement.status==FeedReloadAcknowledgement::Applied);
                CHECK(receipt->post.sequence>command.before.sequence&&receipt->post.observedNs>command.request.requestedNs);
                CHECK(receipt->post.counts==command.after);
                ++receipts;
                // Let scheduled native publications corroborate the receipt;
                // never publish an extra observation to force reconciliation.
                for(std::size_t packet=0;packet<f.nativeCadence.size()+3;++packet)f.Tick(f.lastWrist,false);
                CHECK(!f.backend.Pending()&&!f.result.pending&&f.submits==stage+1);
                CHECK(f.native.counts==command.after);
                CHECK(f.BlocksFire()==(stage!=4));
                return 0;
            };
            CHECK(attempted(FeedPhysicalOperation::OpenCover,f.OpenCover()));CHECK(attempted(operations[0],reconcile(0)==0));CHECK(f.native.counts==before);
            CHECK(attempted(FeedPhysicalOperation::DetachContainer,f.Place(FeedPhysicalOperation::DetachContainer)));CHECK(attempted(operations[1],reconcile(1)==0));CHECK(f.native.counts==before);
            CHECK(attempted(FeedPhysicalOperation::AttachContainer,f.Place(FeedPhysicalOperation::AttachContainer)));CHECK(attempted(operations[2],reconcile(2)==0));
            CHECK((f.native.counts==FeedAmmunitionCounts{before.loaded+units,before.reserve-units,before.capacity}));
            CHECK(attempted(FeedPhysicalOperation::SeatFeed,f.Place(FeedPhysicalOperation::SeatFeed)));CHECK(attempted(operations[3],reconcile(3)==0));
            CHECK(attempted(FeedPhysicalOperation::CloseCover,f.Close()));CHECK(attempted(operations[4],reconcile(4)==0));
            CHECK(f.submits==5&&!f.BlocksFire()&&f.reusedNativeUnchanged);
            std::ostringstream report;f.physical.Report(report);
            CHECK(report.str().find("\"receipts_accepted\":5")!=std::string::npos);
            if(cadence<3){
                CHECK(f.nativePublications<f.nativeTick);
                for(auto operation:operations)CHECK(f.reusedNativeByOperation[unsigned(operation)]>0);
            }else CHECK(f.nativePublications==2*f.nativeTick);
            if(delayed)CHECK(f.delayedRenders>0&&f.repeatedRenders>0&&f.firstRepeatedSource==9006);
            else CHECK(f.delayedRenders==0&&f.repeatedRenders==0);
            ++cycles;
        }
    }
    CHECK(configurations==24&&cycles==192&&receipts==960);
    std::printf("Independent feed clocks: %u profiles, %u full cycles, %u offline mocked adapter receipts\n",configurations,cycles,receipts);
    return 0;
}
int EveryEnrolledFeedCompletesAcrossGameplayPublicationLoss(){
    enum class Interruption {Observer,Render,Selected,Tracking,Focus,Release,Deadline,
        SelectedContradiction,NativeContradiction,Ownership,Weapon};
    constexpr std::array interruptions{Interruption::Observer,Interruption::Render,Interruption::Selected,
        Interruption::Tracking,Interruption::Focus,Interruption::Release,Interruption::Deadline,
        Interruption::SelectedContradiction,Interruption::NativeContradiction,Interruption::Ownership,Interruption::Weapon};
    constexpr std::array operations{FeedPhysicalOperation::OpenCover,FeedPhysicalOperation::DetachContainer,
        FeedPhysicalOperation::AttachContainer,FeedPhysicalOperation::SeatFeed,FeedPhysicalOperation::CloseCover};
    constexpr std::array interruptionNames{"observer","render","selected","tracking","focus","release","deadline",
        "selected-contradiction","native-contradiction","ownership","weapon"};
    std::array<bool,2*interruptions.size()> tracedFailure{};
    unsigned configurations=0,completed=0,cycles=0,failed=0,receipts=0;
    for(const auto& registration:RegisteredBc2FeedProfiles()){
        ++configurations;
        for(bool missingPacketNative:{false,true})for(const auto interruption:interruptions){
            const auto shape=unsigned(interruption)+unsigned(missingPacketNative)*unsigned(interruptions.size());
            Fixture f(&registration);f.captureCadenceTrace=!tracedFailure[shape];
            const char* failureStage="open cover";
            auto failureOperation=FeedPhysicalOperation::OpenCover;
            unsigned caseReceipts=0;
            const auto runCase=[&]()->int{
                const auto before=f.native.counts;const auto originalNative=f.native;
                const auto originalEquipment=*f.sample.equipment;
                const auto units=(std::min)(before.capacity-before.loaded,before.reserve);
                std::uint64_t lifetime=0;FeedSupplyKey supply{};
                const auto reached=[&](bool condition,const char* stage){
                    if(!condition)failureStage=stage;
                    return condition;
                };
                const auto reconcile=[&](unsigned stage)->int{
                    CHECK(f.submitted&&f.submits==stage+1&&f.backend.Pending());
                    const auto command=f.submitted->command;
                    CHECK(command.authority.operation==operations[stage]&&command.request.id==stage+1);
                    CHECK(f.submitted->geometry.registrationKey==registration.key);
                    if(!lifetime){lifetime=command.request.lifetime;supply=command.before.loadedSupply;}
                    CHECK(command.request.lifetime==lifetime&&command.loadedSupplyAfter==supply);
                    CHECK(command.mutationRequired==(stage==2)&&command.authority.rounds==(stage==2?units:0));
                    // Reuse the real adapter with the existing explicitly mocked
                    // three-firing-copy callback; this is never native acceptance.
                    const bool reconciled=f.Reconcile();
                    const auto receipt=f.backend.Receipt(lifetime,command.request.id);
                    // Count the actual mocked receipt even if a later check fails.
                    if(receipt)++caseReceipts;
                    CHECK(reconciled);CHECK(receipt);
                    CHECK(receipt->command==command&&receipt->acknowledgement.status==FeedReloadAcknowledgement::Applied);
                    CHECK(receipt->post.counts==command.after&&f.native.counts==command.after);
                    CHECK(!f.backend.Pending()&&!f.result.pending&&f.BlocksFire()==(stage!=4));
                    return 0;
                };
                CHECK(f.OpenCover());CHECK(reconcile(0)==0);
                failureStage="admit detach claim";failureOperation=FeedPhysicalOperation::DetachContainer;
                CHECK(f.Place(FeedPhysicalOperation::DetachContainer,true,false));
                const auto held=f.hands.Current(InteractionHand::Left);CHECK(held&&held->token.kind==HandClaimKind::Mechanism);
                const auto heldRaw=f.sample.raw;const auto end=f.lastWrist;
                const auto admittedSelected=f.sample.selected;CHECK(admittedSelected);
                const auto admittedNative=f.backend.Read(f.sample.owner,f.sample.input.nowNs);CHECK(admittedNative);
                const auto selectedSequence=f.selected->sequence;
                const auto selectedObserved=f.selected->observedNs,selectedDeadline=f.selected->deadlineNs;
                CHECK(f.result.presentation&&f.result.presentation->lifetime==lifetime&&f.submits==1);
                f.publishSelected=false;f.nativeCadence={0};f.missingPacketNative=missingPacketNative;
                f.reloadOwnerLoss=ReloadOwnerLoss::PresentationUnavailable;
                f.reloadOwnerLossOrigin=ReloadOwnerLossOrigin::Observation;
                const bool continuous=interruption==Interruption::Observer||interruption==Interruption::Render||
                    interruption==Interruption::Selected;
                if(interruption==Interruption::Tracking||interruption==Interruption::Focus){
                    f.reloadOwnerLossOrigin=ReloadOwnerLossOrigin::Lifecycle;
                    f.resetHandsOnOwnerLoss=true;
                    if(interruption==Interruption::Tracking)f.sample.input.tracked[0]=false;
                    else f.sample.input.focused=false;
                }
                if(interruption==Interruption::Deadline)f.now=held->deadlineNs-10*Ms;
                if(interruption==Interruption::SelectedContradiction){
                    f.SelectWeaponData(f.selected->weaponData+1);
                }
                if(interruption==Interruption::NativeContradiction)f.mutateNativeObserved=true;
                const auto selectNative=[&]{
                    f.selected=std::make_shared<SelectedMeshesSnapshot>(*f.selected);
                    f.sample.owner=f.native.identity.owner;f.sample.raw.owner=f.sample.owner;f.selected->owner=f.sample.owner;
                    f.sample.equipment=WeaponEquipmentIdentity{f.sample.owner.weapon,f.native.binding.data,
                        f.native.binding.persistence,f.native.config.assetName};
                    f.sample.asset=f.sample.equipment->Asset();
                    ++f.sample.weapon.generation;f.sample.input.owner.equipGeneration=f.sample.weapon.generation;
                    f.sample.input.owner.space=f.sample.owner.space;
                };
                if(interruption==Interruption::Ownership||interruption==Interruption::Weapon){
                    f.reloadOwnerLoss=ReloadOwnerLoss::LifetimeEnded;f.reloadOwnerLossOrigin=ReloadOwnerLossOrigin::Lifecycle;
                    f.resetHandsOnOwnerLoss=true;f.nativeCadence={1};
                    ++f.native.identity.owner.equipGeneration;++f.native.identity.owner.space;
                    f.native.binding.owner=f.native.identity.owner;
                    f.native.binding.context.equipGeneration=f.native.identity.owner.equipGeneration;
                    f.native.binding.context.space=f.native.identity.owner.space;
                    if(interruption==Interruption::Weapon){
                        ++f.native.binding.data;++f.native.binding.persistence;++f.native.binding.context.resource.weaponGeneration;
                        f.native.config.weaponData=f.native.binding.data;Text(f.native.config.assetName,"MP443_sp");
                    }
                    selectNative();
                }
                failureStage="interruption retains original evidence and leases";
                for(unsigned packet=0;packet<2;++packet){
                    f.PrepareTick(end,interruption!=Interruption::Release);
                    // No renderer/animation publication is represented by missing
                    // evidence, not a synthesized pose with a renewed source time.
                    if(interruption==Interruption::Render||interruption==Interruption::Selected||interruption==Interruption::Release||
                       interruption==Interruption::NativeContradiction){
                        f.sample.raw.valid=false;f.sample.selectedRigVerified=false;
                        if(interruption==Interruption::Selected)f.sample.selected.reset();
                    }
                    f.Consume();
                    CHECK(reached(f.submits==1&&!f.submitted&&!f.result.pending&&!f.backend.Pending(),"gap cannot dispatch"));
                    CHECK(f.native.counts==before);
                    CHECK(f.selected->sequence==selectedSequence&&f.selected->observedNs==selectedObserved&&
                        f.selected->deadlineNs==selectedDeadline);
                    CHECK(admittedSelected->sequence==selectedSequence&&admittedSelected->observedNs==selectedObserved&&
                        admittedSelected->deadlineNs==selectedDeadline);
                    const auto claim=f.hands.Current(InteractionHand::Left);
                    if(continuous){
                        // Legacy ClearReloadOwner unconditionally suspends here;
                        // its lost token fails this behavioral regression before
                        // any recovery or mocked receipt can hide the failure.
                        CHECK(reached(claim&&claim->token==held->token&&f.result.ownsHand,"bounded publication retains actual claim"));
                        CHECK(f.backend.Read(f.sample.owner,f.sample.input.nowNs)==admittedNative);
                        if(interruption!=Interruption::Observer||missingPacketNative){
                            CHECK(claim->inputSequence==held->inputSequence&&claim->deadlineNs==held->deadlineNs);
                            CHECK((interruption==Interruption::Selected?!f.result.presentation:DisplayOnlyFeed(f.result))&&
                                !f.result.diagnostic.policyEvaluated);
                            CHECK(ReadBc2PhysicalFeedPresentation(f.sample.owner,f.sample.input.nowNs)==f.result.presentation);
                        }
                        if(missingPacketNative){
                            // Actual221/run04 had a valid selected/render packet
                            // with no nested native view, despite a current gun
                            // and independently fresh feed view. Replaying it
                            // after readAt recovers must not backfill its proof.
                            const auto readAts=f.nativeReadAts;
                            f.missingPacketNative=false;f.Consume();f.missingPacketNative=true;
                            const auto repeated=f.hands.Current(InteractionHand::Left);
                            CHECK(f.nativeReadAts==readAts&&repeated&&repeated->token==held->token);
                            CHECK(repeated->inputSequence==held->inputSequence&&repeated->deadlineNs==held->deadlineNs);
                            CHECK(f.result.ownsHand&&(interruption==Interruption::Selected?!f.result.presentation:DisplayOnlyFeed(f.result))&&!f.result.diagnostic.policyEvaluated);
                            CHECK(f.submits==1&&!f.backend.Pending()&&f.native.counts==before);
                        }
                    }else CHECK(reached(!claim||claim->token!=held->token,"interruption cannot retain old authority"));
                }
                if(interruption==Interruption::Weapon){
                    failureStage="weapon replacement clears old gate";
                    CHECK(!f.physical.RetainedGate(f.sample.owner,*f.sample.equipment,f.sample.binding));
                    const auto generation=f.native.identity.owner.equipGeneration+1;
                    f.native=originalNative;f.native.identity.owner.equipGeneration=generation;
                    f.native.binding.owner=f.native.identity.owner;f.native.binding.context.equipGeneration=generation;
                    selectNative();
                }
                failureStage="recover fresh publication";
                f.reloadOwnerLoss.reset();f.resetHandsOnOwnerLoss=false;f.publishSelected=true;f.nativeCadence={1};f.missingPacketNative=false;
                f.mutateNativeObserved=false;f.sample.input.focused=true;f.sample.input.tracked={true,true};
                f.selected->weaponData=originalNative.config.weaponData;f.selected->owner=f.sample.owner;
                f.sample.raw=heldRaw;f.sample.raw.owner=f.sample.owner;f.sample.raw.valid=true;f.sample.selectedRigVerified=true;
                f.Tick(end,true);
                CHECK(f.sample.selected!=admittedSelected&&admittedSelected->sequence==selectedSequence&&
                    admittedSelected->observedNs==selectedObserved&&admittedSelected->deadlineNs==selectedDeadline);
                CHECK(reached(f.submits==1&&!f.backend.Pending()&&f.native.counts==before,"recovery cannot invent detach"));
                if(continuous){
                    failureStage="resume retained detach claim";
                    const auto resumed=f.hands.Current(InteractionHand::Left);
                    CHECK(resumed&&resumed->token==held->token);
                    f.Tick(end,false);
                }else{
                    failureStage="cancel lost detach gesture";
                    // A fresh native observation alone cannot complete the lost
                    // gesture. A new neutral, grasp and explicit release are needed.
                    f.Tick(end,false);f.Tick(end,false);
                    CHECK(reached(f.submits==1&&!f.backend.Pending(),"lost gesture stays cancelled"));
                    CHECK(f.result.presentation&&f.result.presentation->lifetime==lifetime&&f.BlocksFire());
                    CHECK(reached(f.ResumePlacementEndpoint(FeedPhysicalOperation::DetachContainer,end,held->token),"detach after interruption"));
                }
                failureStage="detach after recovery";
                CHECK(reached(reconcile(1)==0,"detach after recovery"));CHECK(f.native.counts==before);
                const bool carriedGap=interruption==Interruption::Render||(missingPacketNative&&interruption==Interruption::Observer);
                if(carriedGap){
                    failureStage="acquire carried supply";failureOperation=FeedPhysicalOperation::AttachContainer;
                    CHECK(f.PickSupply());const auto carry=f.hands.Current(InteractionHand::Left);
                    CHECK(carry&&carry->token.kind==HandClaimKind::AmmoObject);
                    const auto carriedRaw=f.sample.raw;
                    const auto carriedPresentation=f.result.presentation;CHECK(carriedPresentation);
                    const auto carriedSelection=f.sample.selected;CHECK(carriedSelection);
                    const auto carriedSelectedDeadline=carriedSelection->deadlineNs;
                    f.publishSelected=false;f.nativeCadence={0};f.missingPacketNative=missingPacketNative;
                    f.reloadOwnerLoss=ReloadOwnerLoss::PresentationUnavailable;
                    f.reloadOwnerLossOrigin=ReloadOwnerLossOrigin::Observation;
                    failureStage="carried supply retains original evidence and leases";
                    for(unsigned packet=0;packet<2;++packet){
                        f.PrepareTick(f.lastWrist,true);
                        if(interruption==Interruption::Render){f.sample.raw.valid=false;f.sample.selectedRigVerified=false;}
                        f.Consume();
                        const auto retained=f.hands.Current(InteractionHand::Left);
                        CHECK(reached(retained&&retained->token==carry->token&&f.result.ownsHand,"carried supply survives bounded render gap"));
                        CHECK(retained->inputSequence==carry->inputSequence&&retained->deadlineNs==carry->deadlineNs);
                        CHECK(f.sample.selected==carriedSelection&&carriedSelection->deadlineNs==carriedSelectedDeadline);
                        CHECK(DisplayOnlyFeed(f.result)&&!f.result.diagnostic.policyEvaluated&&!f.result.pending&&
                            !f.backend.Pending()&&f.submits==2&&f.native.counts==before);
                        const auto& shown=*f.result.presentation;
                        CHECK(shown.lifetime==carriedPresentation->lifetime&&shown.supply==carriedPresentation->supply);
                        CHECK(shown.input.sequence==carriedPresentation->input.sequence&&shown.input.deadlineNs==carriedPresentation->input.deadlineNs);
                        CHECK(Near(shown.coverRadians,carriedPresentation->coverRadians)&&NearPose(shown.weaponFromPouch,carriedPresentation->weaponFromPouch));
                        CHECK(NearPose(shown.pouchCarryWeaponWorldMeters,carriedPresentation->pouchCarryWeaponWorldMeters));
                    }
                    failureStage="recover carried supply";
                    f.reloadOwnerLoss.reset();f.publishSelected=true;f.nativeCadence={1};f.missingPacketNative=false;
                    f.sample.raw=carriedRaw;f.sample.raw.valid=true;f.sample.selectedRigVerified=true;f.Tick(f.lastWrist,true);
                    const auto resumed=f.hands.Current(InteractionHand::Left);CHECK(resumed&&resumed->token==carry->token);
                    CHECK(f.sample.selected!=carriedSelection&&carriedSelection->deadlineNs==carriedSelectedDeadline);
                }
                failureStage="attach container";failureOperation=FeedPhysicalOperation::AttachContainer;
                CHECK(f.Place(FeedPhysicalOperation::AttachContainer,!carriedGap,true,carriedGap));CHECK(reconcile(2)==0);
                const FeedAmmunitionCounts filled{before.loaded+units,before.reserve-units,before.capacity};
                CHECK(f.native.counts==filled);
                if(interruption==Interruption::Tracking){
                    failureStage="tracking loss preserves completed transfer";
                    // A real loss after the only ammunition transfer cannot undo
                    // that receipt while the unfinished feed still blocks firing.
                    f.reloadOwnerLoss=ReloadOwnerLoss::PresentationUnavailable;f.reloadOwnerLossOrigin=ReloadOwnerLossOrigin::Lifecycle;
                    f.resetHandsOnOwnerLoss=true;f.sample.input.tracked[0]=false;f.Tick(f.lastWrist,false);
                    CHECK(f.native.counts==filled&&f.submits==3&&!f.backend.Pending()&&f.BlocksFire());
                    f.reloadOwnerLoss.reset();f.resetHandsOnOwnerLoss=false;f.sample.input.tracked[0]=true;
                    f.Tick(f.lastWrist,false);f.Tick(f.lastWrist,false);
                    CHECK(f.native.counts==filled&&f.result.presentation&&f.result.presentation->lifetime==lifetime);
                }
                failureStage="seat feed";failureOperation=FeedPhysicalOperation::SeatFeed;
                CHECK(f.Place(FeedPhysicalOperation::SeatFeed));CHECK(reconcile(3)==0);
                failureStage="close cover";failureOperation=FeedPhysicalOperation::CloseCover;
                CHECK(f.Close());CHECK(reconcile(4)==0);
                failureStage="completed cycle remains closed";
                CHECK(f.submits==5&&f.native.counts==filled&&!f.BlocksFire());
                CHECK(f.sample.equipment&&*f.sample.equipment==originalEquipment);
                for(unsigned packet=0;packet<3;++packet)f.Tick(f.lastWrist,false);
                CHECK(f.submits==5&&f.native.counts==filled&&!f.backend.Pending());return 0;
            };
            const auto result=runCase();++completed;
            receipts+=caseReceipts;
            if(result==0)++cycles;
            else{
                ++failed;
                std::fprintf(stderr,"Gameplay feed recovery: profile=%llu interruption=%u(%s) missingPacketNative=%d stage=%s result=FAIL offline_mocked_receipts=%u\n",
                    static_cast<unsigned long long>(registration.key),shape,interruptionNames[unsigned(interruption)],missingPacketNative,failureStage,caseReceipts);
                if(!tracedFailure[shape]){
                    f.PrintCadenceFailure(shape,failureOperation);tracedFailure[shape]=true;
                }
            }
        }
    }
    std::printf("Gameplay feed boundary: %u profiles, cases complete=%u/%u pass=%u fail=%u, %u recovered full cycles, %u offline mocked adapter receipts\n",
        configurations,completed,configurations*2*unsigned(interruptions.size()),cycles,failed,cycles,receipts);
    CHECK(completed==configurations*2*interruptions.size()&&completed==cycles+failed&&failed==0);
    CHECK(configurations==24&&cycles==configurations*2*interruptions.size()&&receipts==cycles*5);return 0;
}
int ChestPreparationSurvivesNeutralJitterAndOutsideNeutral(){
    for(bool loseContact:{false,true}){
        Fixture f;f.wristCalibration=Turn(2.29598f);
        CHECK(f.OpenCover()&&f.Reconcile());
        CHECK(f.Place(FeedPhysicalOperation::DetachContainer)&&f.Reconcile());
        const auto before=f.native.counts;
        f.nativeCadence={1,0,0,0};f.nativeTick=0;
        auto chest=Identity();const auto config=Bc2FeedSupplyConfig();
        for(unsigned n=0;n<3;++n)chest.values[3][n]=config.pouchCenterMeters[n];
        f.sample.bodyFromHand=chest;f.Tick(chest,false);
        CHECK(f.result.interactionStatus==FeedInteractionStatus::Armed&&f.result.reloadStatus==FeedReloadStatus::Armed);
        const auto admitted=f.backend.Read(f.sample.owner,f.sample.input.nowNs);CHECK(admitted);
        for(float offset:{.004f,-.004f}){
            auto jitter=chest;jitter.values[3][0]+=offset;CHECK(Bc2FeedSupplyContact(jitter));
            f.sample.bodyFromHand=jitter;f.Tick(jitter,false);
            CHECK(f.result.interactionStatus==FeedInteractionStatus::Armed&&f.result.reloadStatus==FeedReloadStatus::Armed);
            CHECK(f.backend.Read(f.sample.owner,f.sample.input.nowNs)==admitted);
            CHECK(f.result.diagnostic.sourceNativeSequence==admitted->sequence);
            CHECK(f.result.diagnostic.sourceNativeObservedNs==admitted->observedNs);
            CHECK(f.result.diagnostic.sourceNativeDeadlineNs==admitted->deadlineNs);
            CHECK(f.submits==2&&!f.result.ownsHand&&f.native.counts==before);
        }
        if(loseContact){
            auto away=chest;away.values[3][0]+=1;CHECK(!Bc2FeedSupplyContact(away));
            f.sample.bodyFromHand=away;f.Tick(away,false);
        }
        f.sample.bodyFromHand=chest;f.Tick(chest,true);
        const auto left=f.hands.Current(InteractionHand::Left);
        CHECK(left&&left->token.kind==HandClaimKind::AmmoObject&&f.result.ownsHand);
        CHECK(f.result.interactionStatus==FeedInteractionStatus::Acquired);
        CHECK(f.result.reloadStatus==FeedReloadStatus::AwaitingPhysicalCompletion);
        // A real outside neutral may now precede arrival+press. The fresh
        // paired source rebases that pickup without replaying a belt neutral.
        if(!loseContact)CHECK(f.backend.Read(f.sample.owner,f.sample.input.nowNs)==admitted);
        CHECK(f.submits==2&&f.native.counts==before&&!f.result.pending&&f.BlocksFire());
    }
    return 0;
}
int EveryEnrolledSupplyRefreshesNeutralApproachBeforePickup(){
    using namespace feed_interaction_detail;
    for(const auto& registration:RegisteredBc2FeedProfiles())for(bool delayed:{false,true}){
        Fixture f(&registration);CHECK(f.OpenCover()&&f.Reconcile());
        CHECK(f.Place(FeedPhysicalOperation::DetachContainer)&&f.Reconcile());f.submitted.reset();
        const auto before=f.native.counts;const auto detached=f.result.presentation;CHECK(detached);
        auto chest=Identity();const auto config=Bc2FeedSupplyConfig();
        for(unsigned axis=0;axis<3;++axis)chest.values[3][axis]=config.pouchCenterMeters[axis];
        f.sample.bodyFromHand=chest;f.Tick(chest,false);
        CHECK(f.result.interactionStatus==FeedInteractionStatus::Armed&&f.result.reloadStatus==FeedReloadStatus::Armed);
        const auto admitted=f.backend.Read(f.sample.owner,f.sample.input.nowNs);CHECK(admitted);
        auto approach=chest;const auto radius=registration.mechanism.contacts[unsigned(FeedPart::Container)].radiusMeters;
        approach.values[3][0]+=(std::max)(.0773f,1.25f*radius);
        CHECK(Distance(chest,approach)>radius&&Bc2FeedSupplyContact(approach));
        // Moving within the pouch on the same native observation must retain
        // the old arm; it cannot rebase or manufacture a new native lease.
        f.nativeCadence={0};f.sample.bodyFromHand=approach;f.Tick(approach,false);
        CHECK(f.result.interactionStatus==FeedInteractionStatus::Armed&&f.result.reloadStatus==FeedReloadStatus::Armed);
        CHECK(f.backend.Read(f.sample.owner,f.sample.input.nowNs)==admitted);
        f.Consume();CHECK(f.result.interactionStatus==FeedInteractionStatus::Duplicate);
        CHECK(f.result.reloadStatus==FeedReloadStatus::DuplicatePacket&&!f.result.ownsHand);
        // A newer paired neutral may now move the prepared pickup source.
        // Preparation never moves the detached, unheld object itself.
        f.nativeCadence={1};f.Tick(approach,false);const auto neutral=f.sample.raw;
        CHECK(f.result.diagnostic.sourceNativeSequence>admitted->sequence);
        CHECK(f.result.diagnostic.sourceNativeObservedNs>admitted->observedNs);
        CHECK(f.result.interactionStatus==FeedInteractionStatus::Armed&&f.result.reloadStatus==FeedReloadStatus::Armed);
        CHECK(f.result.presentation&&NearPose(f.result.presentation->weaponFromPouch,detached->weaponFromPouch));
        f.Consume();CHECK(f.result.interactionStatus==FeedInteractionStatus::Duplicate);
        CHECK(f.result.reloadStatus==FeedReloadStatus::DuplicatePacket&&f.native.counts==before&&f.submits==2);
        auto pickup=approach;pickup.values[3][0]+=.008f;CHECK(Bc2FeedSupplyContact(pickup));
        f.sample.bodyFromHand=pickup;f.PrepareTick(pickup,true);const auto pressed=f.sample.raw;
        f.Consume(delayed?&neutral:nullptr);
        CHECK(f.result.ownsHand&&f.result.presentation&&f.result.presentation->supplyObject);
        const auto acquiredPouch=f.result.presentation->weaponFromPouch;
        if(delayed){
            CHECK(f.result.presentation->input.sequence==neutral.inputEvidence.sequence);
            CHECK(f.result.presentation->mechanismClaim->inputSequence==pressed.inputEvidence.sequence);
            f.PrepareTick(pickup,true);f.Consume(&pressed);
        }
        CHECK(f.result.interactionStatus==FeedInteractionStatus::Acquired);
        CHECK(f.result.reloadStatus==FeedReloadStatus::AwaitingPhysicalCompletion);
        CHECK(f.result.presentation->supplyObject&&NearPose(f.result.presentation->weaponFromPouch,acquiredPouch));
        CHECK(f.result.presentation->input.deadlineNs==pressed.inputEvidence.deadlineNs);
        const auto claim=f.hands.Current(InteractionHand::Left);CHECK(claim&&claim->token.kind==HandClaimKind::AmmoObject);
        f.Consume(&pressed);CHECK(f.result.interactionStatus==FeedInteractionStatus::Duplicate);
        CHECK(f.hands.Current(InteractionHand::Left)->token==claim->token&&f.result.ownsHand);
        CHECK(NearPose(f.result.presentation->weaponFromPouch,acquiredPouch));
        CHECK(f.result.presentation->input.deadlineNs==pressed.inputEvidence.deadlineNs);
        // Complete from the actual retained grasp, then keep the exact refill
        // pending through further neutral movement until its callback receipt.
        const auto retained=f.result.presentation;auto pose=registration.geometry.poses[unsigned(Bc2FeedPoseId::Resting)];
        pose.weaponFromPouch=retained->weaponFromPouch;pose.weaponFromFeedRoot=retained->weaponFromChain[0];
        for(unsigned i=0;i+1<pose.chainCount;++i)pose.parentFromChainChild[i]=feed_interaction_detail::Multiply(
            retained->weaponFromChain[i+1],Inverse(retained->weaponFromChain[i]));
        const auto motion=BuildBc2FeedMotionFromRetainedPose(registration.geometry,FeedPhysicalOperation::AttachContainer,
            registration.interactionPolicy,pose);CHECK(motion);
        const auto grasp=feed_interaction_detail::Multiply(pickup,Inverse(motion->placement.weaponFromSource));
        const auto wrist=[&](const fvr::math::Matrix4& object){return feed_interaction_detail::Multiply(grasp,object);};
        for(unsigned step=1;step<=30;++step)f.Tick(wrist(reload_insertion_detail::BlendRigid(
            motion->placement.weaponFromSource,motion->placement.weaponFromDestination,double(step)/30)),true);
        const auto end=wrist(motion->placement.weaponFromDestination);
        for(unsigned step=0;step<f.DwellPackets();++step)f.Tick(end,true);
        f.Tick(end,false);
        CHECK(f.submitted&&f.submits==3&&f.result.pending&&f.backend.Pending());
        const auto command=f.submitted->command;
        CHECK(command.authority.operation==FeedPhysicalOperation::AttachContainer&&command.before.counts==before);
        CHECK(command.mutationRequired&&!f.result.ownsHand&&f.native.counts==before);
        for(const auto& neutralWrist:{chest,approach,chest}){
            f.sample.bodyFromHand=neutralWrist;f.Tick(neutralWrist,false);
            CHECK(f.result.pending&&f.result.blocksEquipment&&f.BlocksFire()&&f.submits==3);
            CHECK(f.backend.Pending()&&f.backend.Pending()->command==command&&f.native.counts==before);
        }
        CHECK(f.Reconcile());CHECK(f.native.counts==command.after&&f.submits==3&&!f.backend.Pending());
    }
    return 0;
}
int DelayedReleasePreservesExactGestureWithinOriginalDeadlines(){
    // Normal native advancement, expiry, repress, owner, tracking, focus,
    // cancel, native counts, raw loss, skipped release, packet mutation,
    // native identity, same-sequence native mutation and safety changes that
    // the hand arbiter observes between physical-feed ticks.
    for(unsigned fault=0;fault<18;++fault){
        Fixture f;CHECK(f.OpenCover()&&f.Reconcile());
        CHECK(f.Place(FeedPhysicalOperation::DetachContainer,true,false));
        const auto held=f.sample.raw;const auto end=f.lastWrist;
        const auto original=f.backend.Read(f.sample.owner,f.sample.input.nowNs);CHECK(original);
        const auto counts=f.native.counts;
        if(fault==12){f.nativeCadence={0};f.mutateNativeObserved=true;}
        if(fault>=13){
            f.PrepareTick(end,true);
            if(fault==13){auto loss=f.sample.input;loss.tracked[0]=false;(void)f.hands.Update(loss);}
            if(fault==14){auto loss=f.sample.input;loss.focused=false;(void)f.hands.Update(loss);}
            if(fault==15){const auto left=f.hands.Current(InteractionHand::Left);CHECK(left);CHECK(f.hands.Release(f.sample.input,left->token).accepted);}
            if(fault==16)(void)f.hands.Reset();
            if(fault==17){f.PrepareTick(end,false);f.PrepareTick(end,true);}
        }
        f.PrepareTick(end,false);const auto released=f.sample.raw;
        const auto releaseNative=f.backend.Read(f.sample.owner,f.sample.input.nowNs);CHECK(releaseNative);
        f.Consume(&held);
        CHECK(!f.result.diagnostic.policyEvaluated&&!f.result.ownsHand&&!f.hands.Current(InteractionHand::Left));
        CHECK(f.result.diagnostic.rawPassed==255&&f.result.blocksFire&&!f.result.pending&&f.submits==1);
        if(!fault){
            f.PrepareTick(end,false);f.Consume(&held);
            CHECK(!f.result.diagnostic.policyEvaluated&&!f.result.ownsHand&&!f.result.pending&&f.submits==1);
        }
        if(fault==1)f.now=original->deadlineNs-10*Ms;
        if(fault==3)++f.sample.input.owner.actorGeneration;
        if(fault==7)++f.native.counts.reserve;
        if(fault==11)f.native.identity.firing[0]+=0x10;
        f.PrepareTick(end,fault==2,fault==6);
        auto raw=released;
        if(fault==4){f.sample.input.tracked[0]=false;(void)f.hands.Update(f.sample.input);}
        if(fault==5){f.sample.input.focused=false;(void)f.hands.Update(f.sample.input);}
        if(fault==8)raw.valid=false;
        if(fault==9)raw=f.sample.raw;
        if(fault==10)++raw.inputEvidence.observedNs;
        f.Consume(&raw);
        if(!fault){
            CHECK(f.submitted&&f.submits==2&&f.result.pending);
            const auto command=f.submitted->command;
            CHECK(command.authority.operation==FeedPhysicalOperation::DetachContainer&&command.request.id==2);
            CHECK(command.before.sequence==releaseNative->sequence&&command.before.observedNs==releaseNative->observedNs);
            CHECK(command.before.deadlineNs==releaseNative->deadlineNs&&command.before.counts==counts);
            CHECK(f.backend.Read(f.sample.owner,f.sample.input.nowNs)->sequence>command.before.sequence);
            CHECK(f.Reconcile());
            const auto receipt=f.backend.Receipt(command.request.lifetime,command.request.id);CHECK(receipt);
            CHECK(receipt->command==command&&receipt->post.counts==counts&&f.native.counts==counts);
        }else {
            CHECK(f.submits==1&&!f.result.pending&&!f.backend.Pending()&&f.BlocksFire());
            f.sample.input.owner=held.inputEvidence.owner;f.sample.input.focused=true;f.sample.input.tracked={true,true};
            f.mutateNativeObserved=false;
            f.PrepareTick(end,false);f.Consume(&released);
            CHECK(f.submits==1&&!f.result.pending&&!f.backend.Pending()&&f.BlocksFire());
            auto expected=counts;if(fault==7)++expected.reserve;
            CHECK(f.native.counts==expected);
        }
    }
    return 0;
}
int IndependentNativeClockExpiryAndMutationAreRejected(){
    using namespace feed_interaction_detail;
    for(bool mutate:{false,true}){
        Fixture f;f.nativeCadence={1,0,0};f.wristCalibration=Identity();
        const auto motion=f.Open();const auto start=feed_interaction_detail::Multiply(motion.motionFromContact,motion.weaponFromMotion);
        f.Tick(start,false);const auto native=f.backend.Read(f.sample.owner,f.sample.input.nowNs);CHECK(native);
        f.Tick(start,true);CHECK(f.result.interactionStatus==FeedInteractionStatus::Acquired&&f.result.ownsHand);
        const auto acquired=f.backend.Read(f.sample.owner,f.sample.input.nowNs);CHECK(acquired&&*acquired==*native);
        const auto cover=f.result.presentation->coverRadians;
        if(mutate)f.mutateNativeObserved=true;
        else {f.nativeCadence={0};f.now=native->deadlineNs-10*Ms;}
        f.Tick(feed_interaction_detail::Multiply(Along(motion.motionFromContact,motion.kind,motion.travel/12),motion.weaponFromMotion),true);
        CHECK(f.native.observedNs==native->observedNs&&f.native.deadlineNs==native->deadlineNs&&f.nativePublications==1);
        CHECK(f.submits==0&&!f.backend.Pending()&&!f.result.pending&&f.BlocksFire());
        if(mutate){
            // All adapter admission and raw joins pass. Only immutable native
            // observation identity changed under the reused sequence.
            CHECK(f.result.diagnostic.admissionPassed==63&&f.result.diagnostic.rawPassed==511);
            // The ammunition ledger detects this same-sequence mutation
            // before granting recognizer authority; reload retains its own
            // exact snapshot-mismatch rejection as well.
            CHECK(f.result.interactionStatus==FeedInteractionStatus::Interrupted);
            CHECK(f.result.reloadStatus==FeedReloadStatus::SnapshotMismatch);
            CHECK(f.result.presentation&&f.result.presentation->coverRadians==cover);
        }else {
            CHECK(f.sample.input.observedNs==native->deadlineNs&&f.sample.input.deadlineNs>f.sample.input.nowNs);
            CHECK(!f.backend.Read(f.sample.owner,f.sample.input.nowNs));
            CHECK(f.result.diagnostic.exit==Bc2PhysicalFeedExit::ViewUnavailable&&!f.result.diagnostic.policyEvaluated);
            CHECK(!f.result.ownsHand&&!f.result.presentation&&!f.hands.Current(InteractionHand::Left));
        }
    }
    return 0;
}
int DelayedDetachWaitsForPairedNativeWithoutRenewingOwnership(){
    // Reproduce feedback66's QJU88 transition: cached native authority expires
    // 4.4426 ms before consumption while the real input has 16.4496 ms left.
    // A newer native view alone cannot be paired with that older geometry.
    const auto* registration=FindBc2FeedRegistration(5358977986686110024ull);
    CHECK(registration&&registration->geometry.asset=="QJU88_sp");
    for(unsigned fault=0;fault<9;++fault){
        Fixture f(registration);CHECK(f.OpenCover()&&f.Reconcile());f.submitted.reset();
        const auto motion=BuildBc2FeedMotionFromRetainedPose(registration->geometry,FeedPhysicalOperation::DetachContainer,
            registration->interactionPolicy,registration->geometry.poses[unsigned(Bc2FeedPoseId::Resting)]);
        CHECK(motion);
        const auto pose=[&](unsigned step){return feed_interaction_detail::Multiply(motion->placement.sourceFromGrip,
            reload_insertion_detail::BlendRigid(motion->placement.weaponFromSource,motion->placement.weaponFromDestination,double(step)/30));};
        f.Tick(pose(0),false);f.Tick(pose(0),false);f.Tick(pose(0),true);
        CHECK(f.result.ownsHand&&f.result.interactionStatus==FeedInteractionStatus::Acquired);
        for(unsigned step=1;step<=12;++step)f.Tick(pose(step),true);
        const auto cached=f.backend.Read(f.sample.owner,f.sample.input.nowNs);CHECK(cached);
        f.nativeCadence={0};f.now=cached->observedNs+20892200-10*Ms;f.Tick(pose(13),true);
        CHECK(f.result.ownsHand&&f.result.interactionStatus==FeedInteractionStatus::Moving);
        const auto delayed=f.sample.raw;const auto held=f.hands.Current(InteractionHand::Left);CHECK(held);
        const auto retained=f.result.presentation;CHECK(retained);
        CHECK(f.result.diagnostic.sourceNativeSequence==cached->sequence&&cached->observedNs<delayed.inputEvidence.observedNs);
        const auto counts=f.native.counts;
        const auto gather=fault==2?delayed.inputEvidence.deadlineNs:cached->deadlineNs+4442600;
        f.nativeCadence=fault==1?std::vector<unsigned>{0}:std::vector<unsigned>{1};
        if(fault==3)f.native.identity.firing[0]+=0x10;
        if(fault==4)++f.native.counts.reserve;
        if(fault==8)++f.native.identity.owner.actorGeneration;
        f.now=gather-10*Ms-1;f.PrepareTick(pose(14),fault!=5);const auto freshRaw=f.sample.raw;
        if(fault==6)CHECK(f.hands.Release(f.sample.input,held->token).accepted);
        if(fault==7){f.sample.input.focused=false;(void)f.hands.Update(f.sample.input);}
        const auto current=f.backend.Read(f.sample.owner,f.sample.input.nowNs);
        f.Consume(&delayed);
        CHECK(!f.result.diagnostic.policyEvaluated&&!f.result.pending&&!f.backend.Pending()&&f.submits==1);
        CHECK(f.BlocksFire()&&((fault==0||fault==2||fault==4||fault==5||fault==6)?DisplayOnlyFeed(f.result):!f.result.presentation));
        if(fault){
            CHECK(!f.result.ownsHand&&!f.hands.Current(InteractionHand::Left));
            auto expected=counts;if(fault==4)++expected.reserve;
            CHECK(f.native.counts==expected);
            continue;
        }
        CHECK(current&&FeedEquipmentViewFresh(*current,gather)&&FeedEquipmentSameItem(*cached,*current));
        CHECK(!FeedEquipmentViewFresh(*cached,gather)&&current->observedNs>delayed.inputEvidence.observedNs);
        CHECK(gather-cached->deadlineNs==4442600&&delayed.inputEvidence.deadlineNs-gather==16449600);
        CHECK(f.result.diagnostic.rawChecked==63&&f.result.diagnostic.rawPassed==31);
        CHECK(f.result.diagnostic.sourceNativeSequence==cached->sequence&&f.result.diagnostic.sourceNativeDeadlineNs==cached->deadlineNs);
        CHECK(f.result.ownsHand&&f.result.blocksEquipment&&f.result.status==FeedEquipmentStatus::Ready);
        auto claim=f.hands.Current(InteractionHand::Left);CHECK(claim&&claim->token==held->token);
        CHECK(claim->inputSequence==held->inputSequence&&claim->deadlineNs==held->deadlineNs);
        // Repeating the waiting packet still cannot advance motion, change its
        // snapshot, extend ownership or produce native work.
        ++f.sample.input.nowNs;f.Consume(&delayed);
        claim=f.hands.Current(InteractionHand::Left);CHECK(claim&&claim->token==held->token);
        CHECK(claim->inputSequence==held->inputSequence&&claim->deadlineNs==held->deadlineNs);
        CHECK(!f.result.diagnostic.policyEvaluated&&DisplayOnlyFeed(f.result)&&f.submits==1);
        // Only the actual current raw packet resumes the original gesture.
        f.Consume(&freshRaw);
        CHECK(f.result.ownsHand&&f.result.interactionStatus==FeedInteractionStatus::Moving&&f.result.presentation);
        CHECK(f.hands.Current(InteractionHand::Left)->token==held->token);
        CHECK(f.result.presentation->lifetime==retained->lifetime&&f.result.presentation->coverRadians==retained->coverRadians);
        CHECK(f.result.diagnostic.sourceNativeSequence==current->sequence&&f.native.counts==counts);
        for(unsigned step=15;step<=30;++step)f.Tick(pose(step),true);
        for(unsigned step=0;step<f.DwellPackets();++step)f.Tick(pose(30),true);
        CHECK(!f.submitted&&f.result.interactionStatus==FeedInteractionStatus::Endpoint);
        f.Tick(pose(30),false);
        CHECK(f.submitted&&f.submits==2&&f.backend.Pending()&&!f.result.submitRefused);
        CHECK(f.submitted->command.authority.operation==FeedPhysicalOperation::DetachContainer);
        CHECK(f.submitted->command.request.lifetime==retained->lifetime&&f.submitted->command.request.id==2);
        CHECK(FeedEquipmentSubmissionValid(*f.submitted,f.sample.input.nowNs));
        CHECK(f.submitted->source==*f.backend.ReadAt(f.sample.owner,f.sample.input.observedNs,f.sample.input.nowNs));
        CHECK(f.submitted->source.sequence>current->sequence&&f.submitted->command.before.counts==counts);
        CHECK(f.submitted->geometry.observedNs==f.sample.input.observedNs&&f.native.counts==counts);
    }
    return 0;
}
int BusyGatePreservesOnlyTheExistingHeldFeedGesture(){
    // Actual106: an accepted blocking gate was followed by native lock
    // contention; a live held claim was explicitly released before dispatch.
    using namespace feed_interaction_detail;
    const auto* registration=FindBc2FeedRegistration(5358977986686110024ull);CHECK(registration);
    Fixture f(registration,true);const auto m=f.Open();
    const auto pose=[&](unsigned step){return feed_interaction_detail::Multiply(
        Along(m.motionFromContact,m.kind,m.travel*step/12),m.weaponFromMotion);};
    f.Tick(pose(0),false);f.Tick(pose(0),true);
    const auto acquired=f.hands.Current(InteractionHand::Left);CHECK(acquired&&f.result.ownsHand);
    for(unsigned step=1;step<=10;++step)f.Tick(pose(step),true);
    const auto held=f.hands.Current(InteractionHand::Left);CHECK(held&&held->token==acquired->token);
    const auto raw=f.sample.raw;
    const auto gate=f.backend.ReadGate(f.native.binding.context.resource);CHECK(gate&&gate->blocksFire&&!gate->closedVerified);
    f.busyGate=true;f.PrepareTick(pose(11),true);const auto next=f.sample.raw;
    f.Consume(&raw);
    const auto repeated=f.hands.Current(InteractionHand::Left);CHECK(repeated&&repeated->token==held->token);
    CHECK(repeated->inputSequence==held->inputSequence&&repeated->deadlineNs==held->deadlineNs);
    CHECK(f.result.interactionStatus==FeedInteractionStatus::Duplicate&&f.result.ownsHand&&f.result.blocksFire);
    CHECK(f.backend.ReadGate(f.native.binding.context.resource)==gate&&!f.submitted&&f.submits==0);
    f.Consume(&next);CHECK(f.result.interactionStatus==FeedInteractionStatus::Moving&&f.result.ownsHand);
    CHECK(f.hands.Current(InteractionHand::Left)->token==held->token);
    f.Tick(pose(12),true);
    for(unsigned step=0;step<f.DwellPackets()&&!f.submitted;++step){
        CHECK(f.hands.Current(InteractionHand::Left)&&f.hands.Current(InteractionHand::Left)->token==held->token);
        f.Tick(pose(12),true);
    }
    CHECK(f.submitted&&f.submits==1&&f.result.pending);
    CHECK(f.submitted->command.authority.operation==FeedPhysicalOperation::OpenCover);
    CHECK(f.backend.ReadGate(f.native.binding.context.resource)==gate);
    CHECK(f.hands.Current(InteractionHand::Left)&&f.hands.Current(InteractionHand::Left)->token==held->token);
    f.busyGate=false;CHECK(f.Reconcile());CHECK(f.submits==1&&f.BlocksFire());
    CHECK(f.backend.Receipt(f.submitted->command.request.lifetime,f.submitted->command.request.id));
    return 0;
}
int GateDenialAndUntypedFailureStillInterruptHeldFeed(){
    using namespace feed_interaction_detail;
    for(bool observed:{false,true}){
        Fixture f(nullptr,observed);const auto m=f.Open();
        const auto start=feed_interaction_detail::Multiply(m.motionFromContact,m.weaponFromMotion);
        f.Tick(start,false);f.Tick(start,true);CHECK(f.result.ownsHand);
        const auto gate=f.backend.ReadGate(f.native.binding.context.resource);CHECK(gate);
        if(observed)f.refuseGate=true;else f.busyGate=true;
        f.Tick(start,true);
        CHECK(!f.result.ownsHand&&!f.hands.Current(InteractionHand::Left)&&!f.submitted&&f.submits==0);
        CHECK(f.result.blocksFire&&(f.result.diagnostic.flags&(1u<<5)));
        CHECK(f.backend.ReadGate(f.native.binding.context.resource)==gate);
        // A later Busy cannot clear a previous real refusal or reacquire a claim.
        f.refuseGate=false;f.busyGate=true;f.Tick(start,true);
        CHECK(!f.result.ownsHand&&!f.hands.Current(InteractionHand::Left)&&f.submits==0);
    }
    return 0;
}
int BusyGateCannotEnrollOrClearACompletedFeedBlocker(){
    using namespace feed_interaction_detail;
    for(bool previouslyClosed:{false,true}){
        Fixture f(nullptr,true);const auto m=f.Open();
        const auto start=feed_interaction_detail::Multiply(m.motionFromContact,m.weaponFromMotion);
        if(previouslyClosed)f.Tick(start,false);
        const auto gate=f.backend.ReadGate(f.native.binding.context.resource);
        CHECK(bool(gate)==previouslyClosed);
        if(gate)CHECK(!gate->blocksFire&&gate->closedVerified);
        f.busyGate=true;f.Tick(start,false);f.Tick(start,true);f.Tick(start,true);
        CHECK(!f.result.ownsHand&&!f.hands.Current(InteractionHand::Left)&&!f.submitted&&f.submits==0);
        CHECK(f.result.blocksFire&&(f.result.diagnostic.flags&(1u<<5)));
        CHECK(f.backend.ReadGate(f.native.binding.context.resource)==gate);
    }
    Fixture f(nullptr,true);CHECK(f.OpenCover()&&f.Reconcile());
    CHECK(f.Place(FeedPhysicalOperation::DetachContainer)&&f.Reconcile());
    CHECK(f.Place(FeedPhysicalOperation::AttachContainer)&&f.Reconcile());
    CHECK(f.Place(FeedPhysicalOperation::SeatFeed)&&f.Reconcile());
    CHECK(f.Close());const auto gate=f.backend.ReadGate(f.native.binding.context.resource);CHECK(gate&&gate->blocksFire);
    f.busyGate=true;CHECK(f.Reconcile());
    CHECK(f.BlocksFire()&&f.result.blocksFire&&!f.hands.Current(InteractionHand::Left));
    CHECK(f.backend.ReadGate(f.native.binding.context.resource)==gate&&f.submits==5);
    f.busyGate=false;f.Tick(f.lastWrist,false);CHECK(!f.BlocksFire()&&f.submits==5);
    return 0;
}
int BusyGateCannotRetainExpiredOrChangedFeedOwnership(){
    using namespace feed_interaction_detail;
    for(unsigned fault=0;fault<5;++fault){
        Fixture f(nullptr,true);const auto m=f.Open();
        const auto start=feed_interaction_detail::Multiply(m.motionFromContact,m.weaponFromMotion);
        f.Tick(start,false);f.Tick(start,true);const auto held=f.hands.Current(InteractionHand::Left);CHECK(held);
        const auto gate=f.backend.ReadGate(f.native.binding.context.resource);CHECK(gate);
        const auto originalEquipment=*f.sample.equipment;
        f.busyGate=true;
        if(fault==0)f.now=held->deadlineNs-10*Ms; // New input cannot prolong the old claim.
        f.PrepareTick(start,fault!=1);
        if(fault==2)++f.sample.equipment->data;
        if(fault==3){f.sample.raw.valid=false;f.SelectWeaponData(f.selected->weaponData+1);}
        if(fault==4){f.sample.input.nowNs=f.sample.input.deadlineNs;}
        f.Consume();
        CHECK(!f.result.ownsHand&&!f.hands.Current(InteractionHand::Left)&&!f.submitted&&f.submits==0);
        // A mismatched current equipment identity cannot inherit the original
        // item's blocker. Its rejected interaction still retains that exact
        // original item's physical debt and native blocking gate.
        CHECK(f.result.blocksFire==(fault!=2));
        CHECK(f.physical.BlocksFire(f.sample.owner,originalEquipment,f.sample.binding));
        CHECK(f.backend.ReadGate(f.native.binding.context.resource)==gate);
    }
    return 0;
}
int FailedGatePublicationRetainsOnlyOriginalMissingViewLeases(){
    const auto* registration=FindBc2FeedRegistration(5358977986686110024ull);
    CHECK(registration&&registration->geometry.asset=="QJU88_sp");
    for(bool inputExpiresFirst:{false,true}){
        Fixture f(registration);CHECK(f.OpenCover()&&f.Reconcile());f.submitted.reset();
        const auto motion=BuildBc2FeedMotionFromRetainedPose(registration->geometry,FeedPhysicalOperation::DetachContainer,
            registration->interactionPolicy,registration->geometry.poses[unsigned(Bc2FeedPoseId::Resting)]);
        CHECK(motion);const auto wrist=feed_interaction_detail::Multiply(motion->placement.sourceFromGrip,motion->placement.weaponFromSource);
        f.Tick(wrist,false);f.Tick(wrist,false);
        f.PrepareTick(wrist,true,false,inputExpiresFirst?30*Ms:100*Ms);f.Consume();
        CHECK(f.result.ownsHand&&f.result.interactionStatus==FeedInteractionStatus::Acquired);
        const auto held=f.hands.Current(InteractionHand::Left);CHECK(held);
        const auto heldInput=f.sample.input;
        const auto native=f.backend.ReadAt(f.sample.owner,heldInput.observedNs,heldInput.nowNs);CHECK(native);
        CHECK((held->deadlineNs<native->deadlineNs)==inputExpiresFirst);
        const auto expires=(std::min)(held->deadlineNs,native->deadlineNs);
        const auto gate=f.backend.ReadGate(native->binding.context.resource);CHECK(gate);
        const auto counts=f.native.counts;
        f.missingCurrentView=true;f.refuseGate=true;
        // Cover both the repeated admitted packet and fresh controller packets.
        // Neither can renew the old claim/native lease or advance the gesture.
        for(unsigned step=0;step<3;++step){
            if(step==2)f.now=expires-10*Ms-2;
            if(step)f.PrepareTick(wrist,true);
            const auto reads=f.nativeReads,readAts=f.nativeReadAts;
            f.Consume();
            CHECK(f.nativeReads==reads+1&&f.nativeReadAts==readAts+(step?1:0));
            CHECK(f.result.diagnostic.exit==Bc2PhysicalFeedExit::ViewUnavailable&&(f.result.diagnostic.flags&(1u<<5)));
            CHECK(f.result.ownsHand&&f.result.blocksEquipment&&f.result.blocksFire);
            CHECK(!f.result.diagnostic.policyEvaluated&&!f.result.presentation&&!f.result.pending&&!f.backend.Pending());
            CHECK(!ReadBc2PhysicalFeedPresentation(f.sample.owner,f.sample.input.nowNs));
            const auto claim=f.hands.Current(InteractionHand::Left);CHECK(claim&&claim->token==held->token);
            CHECK(claim->inputSequence==held->inputSequence&&claim->deadlineNs==held->deadlineNs);
            CHECK(f.backend.ReadAt(f.sample.owner,heldInput.observedNs,f.sample.input.nowNs)==native);
            CHECK(f.backend.ReadGate(native->binding.context.resource)==gate);
            CHECK(f.submits==1&&!f.submitted&&f.native.counts==counts);
        }
        CHECK(f.sample.input.nowNs==expires-1&&f.sample.input.deadlineNs>expires);
        // At the original earliest deadline, a newer live input/native view
        // cannot extend the retained gesture, even by one nanosecond.
        const auto reads=f.nativeReads,readAts=f.nativeReadAts;
        f.sample.input.nowNs=expires;f.Consume();
        CHECK(f.nativeReads==reads+1&&f.nativeReadAts==readAts);
        CHECK(f.result.diagnostic.exit==Bc2PhysicalFeedExit::ViewUnavailable);
        CHECK(!f.result.ownsHand&&!f.hands.Current(InteractionHand::Left));
        CHECK(!f.result.diagnostic.policyEvaluated&&!f.result.presentation&&!f.result.pending&&!f.backend.Pending());
        CHECK(f.submits==1&&!f.submitted&&f.native.counts==counts);
        f.Tick(wrist,true);
        CHECK(!f.result.ownsHand&&!f.hands.Current(InteractionHand::Left)&&!f.result.presentation&&f.submits==1);
    }
    return 0;
}
int MissingPacketNativeRetainsOnlyOriginalEvidenceLeases(){
    // Input, native and selection proofs keep independent original deadlines.
    // A newer current feed view cannot substitute for any of those proofs.
    for(const auto& registration:RegisteredBc2FeedProfiles())for(unsigned expiresFirst=0;expiresFirst<3;++expiresFirst){
        Fixture f(&registration);CHECK(f.OpenCover()&&f.Reconcile());f.submitted.reset();
        const auto motion=BuildBc2FeedMotionFromRetainedPose(registration.geometry,FeedPhysicalOperation::DetachContainer,
            registration.interactionPolicy,registration.geometry.poses[unsigned(Bc2FeedPoseId::Resting)]);
        CHECK(motion);const auto wrist=feed_interaction_detail::Multiply(motion->placement.sourceFromGrip,motion->placement.weaponFromSource);
        f.Tick(wrist,false);f.Tick(wrist,false);
        f.PrepareTick(wrist,true,false,expiresFirst==0?30*Ms:100*Ms);
        if(expiresFirst==2){
            f.selected=std::make_shared<SelectedMeshesSnapshot>(*f.selected);
            f.selected->deadlineNs=f.now+30*Ms;f.sample.selected=f.selected;
        }
        f.Consume();CHECK(f.result.ownsHand&&f.result.interactionStatus==FeedInteractionStatus::Acquired);
        const auto held=f.hands.Current(InteractionHand::Left);CHECK(held);
        const auto admitted=f.backend.ReadAt(f.sample.owner,f.sample.input.observedNs,f.sample.input.nowNs);CHECK(admitted);
        const auto selection=f.sample.selected;CHECK(selection);
        const auto expires=(std::min)({held->deadlineNs,admitted->deadlineNs,selection->deadlineNs});
        CHECK(expires==(expiresFirst==0?held->deadlineNs:expiresFirst==1?admitted->deadlineNs:selection->deadlineNs));
        const auto counts=f.native.counts;
        f.publishSelected=false;f.missingPacketNative=true;f.now=expires-10*Ms-2;
        f.PrepareTick(wrist,true);f.Consume();
        CHECK(f.sample.input.nowNs==expires-1&&f.sample.raw.valid&&f.sample.selected==selection);
        const auto current=f.backend.Read(f.sample.owner,f.sample.input.nowNs);CHECK(current&&current->sequence>admitted->sequence);
        CHECK(FeedEquipmentSameItem(*admitted,*current)&&current->counts==counts);
        const auto retained=f.hands.Current(InteractionHand::Left);CHECK(retained&&retained->token==held->token);
        CHECK(retained->inputSequence==held->inputSequence&&retained->deadlineNs==held->deadlineNs);
        CHECK(f.result.ownsHand&&DisplayOnlyFeed(f.result)&&!f.result.diagnostic.policyEvaluated);
        CHECK(f.submits==1&&!f.submitted&&!f.result.pending&&!f.backend.Pending()&&f.native.counts==counts);
        CHECK(ReadBc2PhysicalFeedPresentation(f.sample.owner,f.sample.input.nowNs)==f.result.presentation);
        // Recovery of the readAt API cannot rewrite this already cached packet.
        const auto readAts=f.nativeReadAts;f.missingPacketNative=false;f.Consume();
        CHECK(f.nativeReadAts==readAts&&f.result.ownsHand&&!f.result.diagnostic.policyEvaluated&&DisplayOnlyFeed(f.result));
        CHECK(f.hands.Current(InteractionHand::Left)->token==held->token);
        f.sample.input.nowNs=expires;f.Consume();
        CHECK(f.nativeReadAts==readAts&&!f.result.ownsHand&&!f.hands.Current(InteractionHand::Left));
        CHECK((expiresFirst==0?DisplayOnlyFeed(f.result):!f.result.presentation)&&!f.result.diagnostic.policyEvaluated&&!f.result.pending&&!f.backend.Pending());
        CHECK(f.submits==1&&!f.submitted&&f.native.counts==counts);
        CHECK(f.backend.Read(f.sample.owner,expires)==current&&FeedEquipmentViewFresh(*current,expires));
        CHECK(selection->deadlineNs==(expiresFirst==2?expires:admitted->deadlineNs));
        f.publishSelected=true;f.Tick(wrist,true);
        CHECK(f.submits==1&&!f.submitted&&!f.result.pending&&!f.backend.Pending()&&f.native.counts==counts);
    }
    return 0;
}
int MissingCurrentViewPreservesOnlyLiveDetachClaims(){
    // Feedback68/source32: QJU88 input16633 at 26765943514100 ns lost
    // its current native publication during an admitted DetachContainer hold.
    // Exercise that publication drop through the real adapter/hand/session
    // composition, then require an actual three-branch detach acknowledgement.
    const auto exercise=[](const Bc2FeedRegistration& registration,unsigned fault,bool failedGate=false)->int {
        Fixture f(&registration);CHECK(f.OpenCover()&&f.Reconcile());f.submitted.reset();
        const auto motion=BuildBc2FeedMotionFromRetainedPose(registration.geometry,FeedPhysicalOperation::DetachContainer,
            registration.interactionPolicy,registration.geometry.poses[unsigned(Bc2FeedPoseId::Resting)]);
        CHECK(motion);
        const auto pose=[&](unsigned step){return feed_interaction_detail::Multiply(motion->placement.sourceFromGrip,
            reload_insertion_detail::BlendRigid(motion->placement.weaponFromSource,motion->placement.weaponFromDestination,double(step)/30));};
        f.Tick(pose(0),false);f.Tick(pose(0),false);f.Tick(pose(0),true);
        CHECK(f.result.ownsHand&&f.result.interactionStatus==FeedInteractionStatus::Acquired);
        for(unsigned step=1;step<=12;++step)f.Tick(pose(step),true);
        const auto native=f.backend.Read(f.sample.owner,f.sample.input.nowNs);CHECK(native);
        // An independently older native publication leaves separate native
        // and input expiry boundaries; do not refresh either cached lease.
        f.nativeCadence={0};f.now=native->observedNs+20*Ms-10*Ms;f.Tick(pose(13),true);
        const auto held=f.hands.Current(InteractionHand::Left);CHECK(held&&f.result.ownsHand);
        const auto retained=f.result.presentation;CHECK(retained);
        const auto heldInput=f.sample.input;const auto counts=f.native.counts;
        CHECK(f.result.interactionStatus==FeedInteractionStatus::Moving&&f.submits==1);
        f.nativeCadence={1};f.missingCurrentView=true;f.refuseGate=failedGate;
        if(fault==8)f.now=held->deadlineNs-10*Ms;
        if(fault==9)f.now=native->deadlineNs-10*Ms;
        f.PrepareTick(pose(14),fault!=1,fault==2);
        if(fault==3){f.sample.input.tracked[0]=false;(void)f.hands.Update(f.sample.input);}
        if(fault==4)++f.sample.owner.actorGeneration;
        if(fault==5)++f.sample.weapon.generation;
        if(fault==6){++f.sample.input.owner.equipGeneration;(void)f.hands.Update(f.sample.input);}
        if(fault==7){++f.sample.input.owner.space;(void)f.hands.Update(f.sample.input);}
        if(fault==10)CHECK(f.hands.Release(f.sample.input,held->token).accepted);
        if(fault==11){const auto gun=f.hands.Current(InteractionHand::Right);CHECK(gun);
            CHECK(f.hands.Release(f.sample.input,gun->token).accepted);}
        if(fault==12)f.sample.selectedRigVerified=false;
        if(fault==13)++f.sample.equipment->data;
        if(fault==14){f.sample.grip=false;f.sample.input.released[0]=false;}
        if(fault==15)++f.sample.binding->context.resource.weaponGeneration;
        if(fault==16){
            const auto previous=f.native.identity;auto replacement=previous;
            replacement.owner.soldier+=0x10000;replacement.owner.weak+=0x10000;
            ++replacement.owner.actorGeneration;replacement.serverSoldier+=0x10000;
            Bc2AmmoReserveLease lease;lease.identity=replacement;lease.sequence=f.sequence+1;
            lease.observedNs=f.now+1;lease.deadlineNs=f.now+100*Ms;
            lease.loaded=counts.loaded;lease.reserve=counts.reserve;lease.capacity=counts.capacity;lease.verified=lease.allThreeIdle=true;
            FeedEquipmentActorDrain drain{previous,++f.invocation,1,f.now+2,f.now+99*Ms,true};
            CHECK(f.backend.RetireForReplacementActor(lease,f.now+3,drain));f.sample.input.nowNs=f.now+4;
        }
        f.Consume();
        CHECK(f.result.diagnostic.exit==Bc2PhysicalFeedExit::ViewUnavailable);
        CHECK(!f.result.diagnostic.policyEvaluated&&!f.result.presentation&&!f.result.pending);
        CHECK(!f.backend.Pending()&&f.submits==1&&!f.submitted&&f.native.counts==counts);
        if(fault){
            CHECK(!f.result.ownsHand&&!f.hands.Current(InteractionHand::Left));
            if(fault==9)CHECK(f.sample.input.nowNs>=native->deadlineNs&&f.sample.input.nowNs<held->deadlineNs);
            return 0;
        }
        CHECK(f.result.ownsHand&&f.result.blocksEquipment&&f.result.blocksFire);
        CHECK(f.result.status==FeedEquipmentStatus::Ready);
        auto claim=f.hands.Current(InteractionHand::Left);CHECK(claim&&claim->token==held->token);
        CHECK(claim->inputSequence==held->inputSequence&&claim->deadlineNs==held->deadlineNs);
        CHECK(f.backend.ReadAt(f.sample.owner,heldInput.observedNs,f.sample.input.nowNs)==native);
        // Another missing publication, even with fresh controller input,
        // must retain the original claim without advancing or renewing it.
        f.Tick(pose(14),true);
        claim=f.hands.Current(InteractionHand::Left);CHECK(claim&&claim->token==held->token);
        CHECK(claim->inputSequence==held->inputSequence&&claim->deadlineNs==held->deadlineNs);
        CHECK(f.result.ownsHand&&!f.result.diagnostic.policyEvaluated&&!f.result.presentation&&f.submits==1);
        f.missingCurrentView=false;f.refuseGate=false;f.Tick(pose(14),true);
        CHECK(f.result.ownsHand&&f.result.interactionStatus==FeedInteractionStatus::Moving&&f.result.presentation);
        CHECK(f.hands.Current(InteractionHand::Left)->token==held->token);
        CHECK(f.result.presentation->lifetime==retained->lifetime&&f.result.presentation->coverRadians==retained->coverRadians);
        for(unsigned step=15;step<=30;++step)f.Tick(pose(step),true);
        for(unsigned step=0;step<f.DwellPackets();++step)f.Tick(pose(30),true);
        CHECK(!f.submitted&&f.result.interactionStatus==FeedInteractionStatus::Endpoint);
        f.Tick(pose(30),false);
        CHECK(f.submitted&&f.submits==2&&f.backend.Pending()&&!f.result.submitRefused);
        CHECK(f.submitted->command.authority.operation==FeedPhysicalOperation::DetachContainer);
        CHECK(f.submitted->command.request.lifetime==retained->lifetime&&f.submitted->command.request.id==2);
        CHECK(FeedEquipmentSubmissionValid(*f.submitted,f.sample.input.nowNs));
        CHECK(f.submitted->source==*f.backend.ReadAt(f.sample.owner,f.sample.input.observedNs,f.sample.input.nowNs));
        CHECK(f.submitted->command.before.counts==counts&&f.native.counts==counts);
        CHECK(f.Reconcile()&&!f.backend.Pending()&&f.native.counts==counts&&f.submits==2);
        return 0;
    };
    for(const auto& registration:RegisteredBc2FeedProfiles())CHECK(exercise(registration,0)==0);
    const auto* registration=FindBc2FeedRegistration(5358977986686110024ull);
    CHECK(registration&&registration->geometry.asset=="QJU88_sp");
    for(unsigned fault=0;fault<=16;++fault){
        if(fault)CHECK(exercise(*registration,fault)==0);
        CHECK(exercise(*registration,fault,true)==0);
    }
    return 0;
}
int EveryEnrolledFeedRetainsRepeatedPacketThroughOpenCover(){
    using namespace feed_interaction_detail;unsigned configurations=0;
    for(const auto& registration:RegisteredBc2FeedProfiles()){
        Fixture f(&registration);f.wristCalibration=Identity();const auto m=f.Open();
        const auto pose=[&](unsigned step){return feed_interaction_detail::Multiply(
            Along(m.motionFromContact,m.kind,m.travel*step/12),m.weaponFromMotion);};
        f.sequence=2644;f.Tick(pose(0),false);f.Tick(pose(0),true);
        CHECK(f.result.interactionStatus==FeedInteractionStatus::Acquired&&f.result.ownsHand);
        const auto pressed=f.sample.raw;const auto acquired=f.hands.Current(InteractionHand::Left);CHECK(acquired);
        const auto cover=f.result.presentation->coverRadians;
        f.PrepareTick(pose(1),true);const auto next=f.sample.raw;
        // Real render producer: the same controller evidence is mapped again
        // into current native world/placed-weapon transforms. Float inversion
        // need not reproduce the first local matrix bit for bit.
        auto world=Turn(.37f);world.values[3][0]=127.123f;world.values[3][1]=-32.567f;world.values[3][2]=6.891f;
        const auto repeated=RepublishFeedContact(pressed,world,f.sample.input.nowNs);CHECK(repeated.valid);
        CHECK(feed_interaction_detail::Multiply(repeated.rawLeftWristWorldMeters,Inverse(repeated.weaponWorldMeters)).values!=
            feed_interaction_detail::Multiply(pressed.rawLeftWristWorldMeters,Inverse(pressed.weaponWorldMeters)).values);
        f.Consume(&repeated);
        CHECK(f.sample.input.sequence==2647&&f.sample.raw.inputEvidence.sequence==2646);
        CHECK(f.result.diagnostic.rawPassed==511&&f.result.diagnostic.admissionPassed==63);
        CHECK(f.result.interactionStatus==FeedInteractionStatus::Duplicate&&f.result.reloadStatus==FeedReloadStatus::DuplicatePacket);
        CHECK(f.result.ownsHand&&!f.result.pending&&f.submits==0);
        const auto retained=f.hands.Current(InteractionHand::Left);CHECK(retained&&retained->token==acquired->token);
        CHECK(retained->deadlineNs==acquired->deadlineNs&&retained->inputSequence==pressed.inputEvidence.sequence);
        CHECK(f.result.presentation&&f.result.presentation->coverRadians==cover);
        CHECK(f.result.presentation->originalWeaponWorldMeters.values==world.values);
        CHECK(FeedPresentationReachesArmAndHand(f,feed_interaction_detail::Multiply(pose(0),world))==0);
        f.Consume(&next);CHECK(f.result.interactionStatus==FeedInteractionStatus::Moving&&f.result.ownsHand);
        for(unsigned step=2;step<=12;++step)f.Tick(pose(step),true);
        for(unsigned step=0;step<f.DwellPackets()&&!f.submitted;++step)f.Tick(pose(12),true);
        CHECK(f.submitted&&f.submits==1&&f.result.pending);
        CHECK(f.submitted->command.authority.operation==FeedPhysicalOperation::OpenCover);
        // Completion keeps the original held cover claim. A repeated endpoint
        // still cannot renew its lease, advance another stage, or redispatch.
        const auto endpoint=f.sample.raw;const auto command=f.submitted->command;
        const auto endpointClaim=f.hands.Current(InteractionHand::Left);CHECK(endpointClaim);
        CHECK(f.hands.HasRecordedEvidence(endpoint.inputEvidence,InteractionHand::Left));
        f.sample.input.nowNs=f.now+2;f.Consume(&endpoint);
        CHECK(!f.result.diagnostic.policyEvaluated&&f.result.blocksEquipment&&f.result.blocksFire&&f.submits==1);
        CHECK(f.hands.Current(InteractionHand::Left)->token==endpointClaim->token);
        CHECK(f.hands.Current(InteractionHand::Left)->deadlineNs==endpointClaim->deadlineNs);
        CHECK(f.backend.Pending()&&f.backend.Pending()->command==command);
        CHECK(f.Callback());f.sample.input.nowNs=f.now+20;f.Consume(&endpoint);
        CHECK(!f.backend.Pending()&&f.result.blocksEquipment&&!f.result.pending&&f.submits==1&&f.result.blocksFire);
        CHECK(f.result.ownsHand&&f.result.presentation->mechanismWristWorldMeters);
        f.Tick(pose(12),false);CHECK(!f.result.ownsHand&&!f.result.blocksEquipment);
        ++configurations;
    }
    CHECK(configurations==24);return 0;
}
int RepeatedFeedPacketRejectsChangedSourceAndExpiredAuthority(){
    using namespace feed_interaction_detail;
    for(unsigned defect=0;defect<12;++defect){
        Fixture f;f.wristCalibration=Identity();const auto m=f.Open();
        const auto start=feed_interaction_detail::Multiply(m.motionFromContact,m.weaponFromMotion);
        f.Tick(start,false);f.Tick(start,true);CHECK(f.result.interactionStatus==FeedInteractionStatus::Acquired);
        const auto original=f.result.presentation;CHECK(original);
        auto repeated=f.sample.raw;f.PrepareTick(start,true);
        switch(defect){
        case 0:++repeated.inputEvidence.observedNs;break;
        case 1:++repeated.inputEvidence.deadlineNs;break;
        case 2:repeated.inputEvidence.released[0]=true;break;
        case 3:++repeated.inputEvidence.owner.space;break;
        case 4:++repeated.rig.skeleton;break;
        case 5:f.sample.binding->deadlineNs=f.sample.input.nowNs;break;
        case 6:f.selected->deadlineNs=f.sample.input.nowNs;break;
        case 7:{const auto gun=f.hands.Current(InteractionHand::Right);CHECK(gun);
            CHECK(f.hands.Release(f.sample.input,gun->token).accepted);break;}
        case 8:f.sample.input.focused=false;break;
        case 9:f.sample.input.tracked[0]=false;break;
        case 10:f.sample.cancel=true;break;
        case 11:
            // Current input/native evidence remains fresh; the exact source
            // packet has expired and cannot borrow the newer packet's lease.
            f.sample.input.nowNs=repeated.inputEvidence.deadlineNs;break;
        }
        f.Consume(&repeated);
        CHECK(!f.result.ownsHand&&!f.result.pending&&!f.result.diagnostic.policyEvaluated&&f.submits==0);
        CHECK(f.result.blocksFire&&!f.hands.Current(InteractionHand::Left));
        // Invalid raw evidence or a released hand cannot authorize IK or
        // motion; independently current equipment may still display its parts.
        const bool displayOnly=defect<4||defect==7||defect==11;
        CHECK(displayOnly?DisplayOnlyFeed(f.result):!f.result.presentation);
        if(displayOnly){const auto& shown=*f.result.presentation;
            CHECK(shown.lifetime==original->lifetime&&shown.supply==original->supply&&Near(shown.coverRadians,original->coverRadians));
            CHECK(shown.input.sequence==original->input.sequence&&shown.input.deadlineNs==original->input.deadlineNs);
        }
        if(defect<5)CHECK(f.result.diagnostic.exit==Bc2PhysicalFeedExit::GunOrRawUnverified);
        if(defect==11)CHECK(f.result.diagnostic.rawChecked==15&&f.result.diagnostic.rawPassed==7);
    }
    return 0;
}
int EveryEnrolledFeedConsumesMissedStartupEdge(){
    using namespace feed_interaction_detail;
    for(const auto& registration:RegisteredBc2FeedProfiles()){
        Fixture f(&registration);const auto m=f.Open();const auto start=feed_interaction_detail::Multiply(m.motionFromContact,m.weaponFromMotion);
        auto away=start;away.values[3][0]+=.3f;
        f.Tick(start,true);CHECK(!f.result.ownsHand&&!f.result.blocksFire&&!f.result.diagnostic.policyEvaluated);
        f.Tick(start,false);f.Tick(away,true);
        CHECK(!f.result.ownsHand&&!f.result.blocksFire&&!f.result.diagnostic.policyEvaluated);
        CHECK((f.result.diagnostic.idlePassed&(1u<<7))&&!(f.result.diagnostic.idlePassed&(1u<<4)));
        f.result=f.physical.Tick(f.sample,f.hands,f.intent); // Repeat the missed press.
        CHECK(!(f.result.diagnostic.idlePassed&(1u<<6))&&!f.result.diagnostic.policyEvaluated);
        f.Tick(start,true); // Entering contact while held cannot reuse neutral.
        CHECK(!f.result.ownsHand&&!f.result.blocksFire&&!f.result.pending&&f.submits==0);
        CHECK(!(f.result.diagnostic.idlePassed&(1u<<6)));
        f.Tick(away,false);f.Tick(start,true);
        CHECK(f.result.ownsHand&&f.result.interactionStatus==FeedInteractionStatus::Acquired);
    }
    return 0;
}
int EveryEnrolledFeedStartupPreservesAdmissionGates(){
    using namespace feed_interaction_detail;
    for(const auto& registration:RegisteredBc2FeedProfiles()){
        for(unsigned defect=0;defect<6;++defect){
            Fixture f(&registration);const auto m=f.Open();const auto start=feed_interaction_detail::Multiply(m.motionFromContact,m.weaponFromMotion);
            auto away=start;away.values[3][0]+=.3f;f.Tick(away,false);
            switch(defect){
            case 0:f.now+=registration.mechanism.maxAgeNs;break;
            case 1:f.sample.selectedRigVerified=false;break;
            case 2:f.sample.raw.valid=false;break;
            case 3:f.native.counts.reserve=0;break;
            case 4:f.native.counts.loaded=f.native.counts.capacity;break;
            case 5:f.occupyLeft=true;break;
            }
            f.PrepareTick(start,true);
            if(defect==0){
                // The time jump also expires the right-hand lease. Acquire a
                // fresh current gun hold so this case reaches neutral expiry,
                // without renewing or replacing the stored left-hand neutral.
                CHECK(!f.hands.Current(InteractionHand::Right));
                const HandContactProof gunProof{{1,7},f.sample.input.sequence,f.sample.input.deadlineNs,true};
                CHECK(f.hands.Acquire(f.sample.input,{f.sample.input.owner,InteractionHand::Right,
                    HandClaimKind::GunHold,f.sample.weapon,gunProof,++f.intent,0}).accepted);
            }
            f.Consume();
            CHECK(!f.result.ownsHand&&!f.result.blocksFire&&!f.result.pending&&!f.result.diagnostic.policyEvaluated&&f.submits==0);
            if(defect==0){
                CHECK(f.result.diagnostic.rawChecked==511&&f.result.diagnostic.rawPassed==511);
                CHECK(f.result.diagnostic.idleChecked==2047&&(f.result.diagnostic.idlePassed&(1u<<6))&&
                    (f.result.diagnostic.idlePassed&(1u<<7)));
                CHECK(f.result.diagnostic.neutralAgeNs>=registration.mechanism.maxAgeNs&&!(f.result.diagnostic.idlePassed&(1u<<8)));
            }
            if(defect==1||defect==2)CHECK(f.result.diagnostic.idleChecked==0);
            if(defect==3)CHECK(!(f.result.diagnostic.idlePassed&(1u<<10)));
            if(defect==4)CHECK(!(f.result.diagnostic.idlePassed&(1u<<9)));
            if(defect==5){CHECK(!(f.result.diagnostic.idlePassed&1u));
                const auto occupied=f.hands.Current(InteractionHand::Left);CHECK(occupied);
                CHECK(f.hands.Release(f.sample.input,occupied->token).accepted);f.occupyLeft=false;}
            f.sample.selectedRigVerified=f.sample.raw.valid=true;
            f.native.counts.reserve=130;f.native.counts.loaded=20;
            f.Tick(start,true);CHECK(!f.result.ownsHand&&!f.result.blocksFire&&!f.result.pending);
            f.Tick(away,false);f.Tick(start,true);
            CHECK(f.result.ownsHand&&f.result.interactionStatus==FeedInteractionStatus::Acquired);
        }
    }
    return 0;
}
int EveryEnrolledFeedRejectsDelayedNeutralAfterRejectedPress(){
    using namespace feed_interaction_detail;
    for(const auto& registration:RegisteredBc2FeedProfiles()){
        for(unsigned defect=0;defect<3;++defect){
            Fixture f(&registration);const auto m=f.Open();const auto start=feed_interaction_detail::Multiply(m.motionFromContact,m.weaponFromMotion);
            auto away=start;away.values[3][0]+=.3f;
            f.sequence=9;f.Tick(away,false);const auto consumed=f.sample.raw;
            CHECK(f.sample.input.sequence==10&&(f.result.diagnostic.idlePassed&(1u<<6)));
            // Current release 11 is recorded, but the renderer still supplies
            // packet 10. Keep the exact unconsumed release for later delivery.
            f.PrepareTick(away,false);const auto delayedNeutral=f.sample.raw;f.Consume(&consumed);
            CHECK(f.sample.input.sequence==11&&f.sample.raw.inputEvidence.sequence==10);
            f.PrepareTick(away,true);
            if(defect==0)f.sample.selectedRigVerified=false;
            if(defect==1)f.sample.raw.valid=false;
            if(defect==2)f.sample.raw.inputEvidence.released[0]=true;
            f.Consume();
            CHECK(f.sample.input.sequence==12&&!f.result.ownsHand&&!f.result.blocksFire&&!f.result.pending);
            CHECK(!f.result.diagnostic.policyEvaluated);
            CHECK(f.result.diagnostic.exit==(defect==2?Bc2PhysicalFeedExit::GunOrRawUnverified:Bc2PhysicalFeedExit::SelectionOrInput));
            f.sample.selectedRigVerified=f.sample.raw.valid=true;
            // At current held packet 13, release 11 still has valid original
            // and current native leases, matching counts, and arbiter evidence.
            f.PrepareTick(away,true);f.Consume(&delayedNeutral);
            CHECK(f.sample.input.sequence==13&&f.sample.raw.inputEvidence.sequence==11);
            CHECK(!f.sample.input.released[0]&&f.sample.raw.inputEvidence.released[0]);
            CHECK(f.result.selected&&f.result.diagnostic.rawChecked==511&&f.result.diagnostic.rawPassed==511);
            CHECK((f.result.diagnostic.idlePassed&(1u<<1))&&!(f.result.diagnostic.idlePassed&(1u<<2)));
            CHECK(!(f.result.diagnostic.idlePassed&(1u<<6))&&!f.result.diagnostic.policyEvaluated);
            f.Tick(start,true); // Held packet 14 cannot acquire from release 11.
            CHECK(!f.result.ownsHand&&!f.result.blocksFire&&!f.result.pending&&!f.result.diagnostic.policyEvaluated&&f.submits==0);
            f.Tick(away,false);f.Tick(start,true);
            CHECK(f.result.ownsHand&&f.result.interactionStatus==FeedInteractionStatus::Acquired);
        }
    }
    return 0;
}
int StartupRejectionBarrierStaysWithinItsInputLifetime(){
    Fixture f;const auto m=f.Open();const auto start=feed_interaction_detail::Multiply(m.motionFromContact,m.weaponFromMotion);
    auto away=start;away.values[3][0]+=.3f;f.Tick(away,false);
    auto unrelated=f.sample.input;++unrelated.owner.actorGeneration;unrelated.sequence=1000;
    f.physical.Suspend(unrelated,f.hands);
    f.Tick(away,false);CHECK(f.result.diagnostic.idlePassed&(1u<<6));
    f.sample.selectedRigVerified=false;f.Tick(start,true);f.sample.selectedRigVerified=true;
    CHECK(!f.result.ownsHand&&!f.result.blocksFire);
    // A verified new equip/space lifetime may restart its packet sequence.
    ++f.native.identity.owner.equipGeneration;++f.native.identity.owner.space;
    f.native.binding.owner=f.native.identity.owner;f.native.binding.context.equipGeneration=f.native.identity.owner.equipGeneration;
    f.native.binding.context.space=f.native.identity.owner.space;
    f.sample.owner=f.native.identity.owner;f.selected->owner=f.sample.owner;f.sample.raw.owner=f.sample.owner;
    ++f.sample.weapon.generation;++f.sample.input.owner.equipGeneration;++f.sample.input.owner.space;
    (void)f.hands.Reset();f.sequence=0;
    f.Tick(away,false);CHECK(f.result.diagnostic.idlePassed&(1u<<6));
    f.Tick(start,true);CHECK(f.result.ownsHand&&f.result.interactionStatus==FeedInteractionStatus::Acquired);
    return 0;
}
int PartialCoverRequiresFreshRegraspAndPreservesProgress(){
    {
    Fixture f;using namespace feed_interaction_detail;const auto m=f.Open();
    const auto pose=[&](double t){return feed_interaction_detail::Multiply(Along(m.motionFromContact,m.kind,m.travel*t),m.weaponFromMotion);};
    f.Tick(pose(0),false);f.Tick(pose(0),true);
    for(unsigned step=1;step<=6;++step)f.Tick(pose(double(step)/12),true);
    CHECK(!f.submitted);CHECK(f.result.presentation&&f.result.presentation->coverRadians>.7f);
    f.Tick(pose(.5),false,true);CHECK(f.BlocksFire());
    f.Tick(pose(.5),false);f.Tick(pose(.5),false);f.Tick(pose(.5),true);
    for(unsigned step=7;step<=12;++step)f.Tick(pose(double(step)/12),true);
    for(unsigned step=0;step<8;++step)f.Tick(pose(1),true);
    CHECK(!f.submitted);f.Tick(pose(1),false);
    CHECK(f.submitted&&f.submitted->command.request.id==1);CHECK(f.submitted->command.authority.operation==FeedPhysicalOperation::OpenCover);
    CHECK(f.Reconcile());CHECK(f.result.presentation->coverRadians>1.4f);
    }
    for(bool delayed:{false,true})for(unsigned fault=0;fault<3;++fault){
        Fixture f;using namespace feed_interaction_detail;const auto m=f.Open();
        f.wristCalibration=Identity(); // Repeated rendering rebuilds the real mapping, including its verified rig identity.
        f.nativeCadence={1,0,0};f.delayedRender=f.repeatRender=delayed;f.captureCadenceTrace=true;
        const auto reached=[&](bool condition,const char* stage){
            if(!condition){std::fprintf(stderr,"Partial feed recovery failure: delayed=%d fault=%u stage=%s\n",delayed,fault,stage);
                f.PrintCadenceFailure(1,FeedPhysicalOperation::OpenCover);}
            return condition;
        };
        const auto pose=[&](double t){return feed_interaction_detail::Multiply(Along(m.motionFromContact,m.kind,m.travel*t),m.weaponFromMotion);};
        f.Tick(pose(0),false);f.Tick(pose(0),true);
        for(unsigned step=1;step<=6;++step)f.Tick(pose(double(step)/12),true);
        f.Tick(pose(.5),true); // Deliver the final partial-motion source under render lag.
        CHECK(reached(!f.submitted&&f.result.presentation&&f.result.presentation->coverRadians>.7f,"partial motion"));
        const auto partial=f.result.presentation->coverRadians;
        const auto lifetime=f.result.presentation->lifetime;
        // Publish at suspension, then leave two controller packets on that
        // exact native observation so recovery must wait before rearming.
        f.nativeTick=0;f.Tick(pose(.5),false,true);CHECK(f.BlocksFire());
        bool waited=false,armed=false;
        std::optional<FeedEquipmentNativeView> previousNative;
        for(unsigned packet=0;packet<8;++packet){
            f.Tick(pose(.5),false);
            CHECK(!f.submitted&&!f.result.pending&&f.BlocksFire());
            CHECK(reached(f.result.presentation&&Near(f.result.presentation->coverRadians,partial),"retained partial pose"));
            CHECK(f.result.presentation->lifetime==lifetime);
            const auto source=f.backend.ReadAt(f.sample.owner,f.sample.raw.inputEvidence.observedNs,f.sample.input.nowNs);
            CHECK(source);
            if(f.result.diagnostic.policyEvaluated&&f.result.interactionStatus==FeedInteractionStatus::NeedNeutral){
                CHECK(previousNative&&*source==*previousNative);
                CHECK(!f.result.ownsHand);waited=true;
                if(fault){
                    f.nativeCadence={0};
                    const auto nativeBeforeFault=f.backend.Read(f.sample.owner,f.sample.input.nowNs);CHECK(nativeBeforeFault);
                    if(fault==1)f.now=source->deadlineNs-10*Ms;
                    else f.mutateNativeObserved=true;
                    const auto changedInput=f.sequence+1;
                    f.Tick(pose(.5),false);
                    if(fault==2&&delayed){
                        // Native mutation is attached to the new real packet;
                        // this render still carries the earlier unchanged one.
                        CHECK(f.sample.raw.inputEvidence.sequence<changedInput);
                        CHECK(f.submits==0&&!f.result.pending&&!f.result.ownsHand);
                        f.Tick(pose(.5),false);
                    }
                    CHECK(f.submits==0&&!f.backend.Pending()&&!f.result.pending&&f.BlocksFire());
                    if(fault==1){
                        CHECK(!f.backend.Read(f.sample.owner,f.sample.input.nowNs));
                        CHECK(f.result.diagnostic.exit==Bc2PhysicalFeedExit::ViewUnavailable);
                        CHECK(!f.result.ownsHand&&!f.result.presentation);
                    }else {
                        CHECK(f.sample.raw.inputEvidence.sequence==changedInput);
                        CHECK(f.result.diagnostic.sourceNativeSequence==nativeBeforeFault->sequence);
                        CHECK(f.result.diagnostic.sourceNativeObservedNs==nativeBeforeFault->observedNs+1);
                        CHECK(f.result.diagnostic.sourceNativeDeadlineNs==nativeBeforeFault->deadlineNs);
                        CHECK(reached(f.result.interactionStatus==FeedInteractionStatus::Interrupted,"mutated source rejected"));
                        CHECK(reached(f.result.reloadStatus==FeedReloadStatus::SnapshotMismatch,"mutated snapshot rejected"));
                        CHECK(f.result.presentation&&Near(f.result.presentation->coverRadians,partial));
                    }
                    break;
                }
            }
            if(f.result.diagnostic.policyEvaluated&&f.result.interactionStatus==FeedInteractionStatus::Armed){armed=true;break;}
            previousNative=source;
        }
        CHECK(reached(waited,"same native recovery wait"));
        if(fault)continue;
        CHECK(reached(armed,"recovery armed"));
        f.Tick(pose(.5),true);if(delayed)f.Tick(pose(.5),true);
        CHECK(reached(f.result.ownsHand&&f.result.interactionStatus==FeedInteractionStatus::Acquired,"recovery acquired"));
        CHECK(f.result.presentation&&Near(f.result.presentation->coverRadians,partial));
        for(unsigned step=7;step<=12;++step)f.Tick(pose(double(step)/12),true);
        for(unsigned packet=0;packet<f.DwellPackets()+3;++packet)f.Tick(pose(1),true);
        CHECK(reached(!f.submitted&&f.result.presentation&&f.result.presentation->coverRadians>1.4f,"recovered endpoint"));
        f.Tick(pose(1),false);if(delayed)f.Tick(pose(1),false);
        CHECK(reached(f.submitted&&f.submits==1&&f.submitted->command.request.id==1,"released completion"));
        CHECK(f.submitted->command.request.lifetime==lifetime);
        CHECK(f.submitted->command.authority.operation==FeedPhysicalOperation::OpenCover);
        CHECK(f.submitted->command.before.observedNs==f.submitted->source.observedNs);
        CHECK(f.submitted->command.before.deadlineNs==f.submitted->source.deadlineNs);
        CHECK(f.Callback());
        for(unsigned packet=0;packet<6;++packet)f.Tick(pose(1),false);
        CHECK(!f.result.pending&&f.result.presentation&&f.result.presentation->coverRadians>1.4f);
        CHECK(f.native.counts.loaded==20&&f.native.counts.reserve==130);
        if(delayed)CHECK(f.delayedRenders>0&&f.repeatedRenders>0);
    }
    return 0;
}
int LongStowMigratesOnlySamePinnedResource(){
    Fixture f;CHECK(f.OpenCover());const auto lifetime=f.submitted->command.request.lifetime;
    CHECK(f.result.blocksFire&&f.result.blocksEquipment&&f.result.pending);
    const auto supply=f.submitted->command.before.loadedSupply;CHECK(f.Reconcile());
    CHECK(f.result.blocksFire&&!f.result.blocksEquipment&&!f.result.ownsHand&&!f.result.pending);
    f.Tick(f.sample.raw.rawLeftWristWorldMeters,false,true);f.now+=1000*Ms;
    ++f.native.identity.owner.equipGeneration;++f.native.identity.owner.space;
    f.native.binding.owner=f.native.identity.owner;f.native.binding.context.equipGeneration=f.native.identity.owner.equipGeneration;
    f.native.binding.context.space=f.native.identity.owner.space;
    f.sample.owner=f.native.identity.owner;f.selected->owner=f.sample.owner;f.sample.raw.owner=f.sample.owner;
    ++f.sample.weapon.generation;++f.sample.input.owner.equipGeneration;++f.sample.input.owner.space;
    (void)f.hands.Reset();f.Tick(f.sample.raw.rawLeftWristWorldMeters,false);f.Tick(f.sample.raw.rawLeftWristWorldMeters,false);
    CHECK(f.result.presentation&&f.result.presentation->supply==supply);CHECK(f.result.presentation->lifetime==lifetime);
    CHECK(f.Place(FeedPhysicalOperation::DetachContainer));CHECK(f.submitted->command.request.lifetime==lifetime);
    CHECK(f.submitted->command.request.id==2&&f.submitted->command.before.loadedSupply==supply);
    CHECK(f.result.blocksFire&&f.result.blocksEquipment&&f.result.pending);
    CHECK(f.submitted->source.identity.owner==f.sample.owner);return 0;
}
int SameAddressReplacementDoesNotInheritFeedDebt(){
    Fixture f;CHECK(f.OpenCover());CHECK(f.Reconcile());
    const auto original=f.native;const auto equipment=*f.sample.equipment;
    const auto lifetime=f.result.presentation->lifetime;const auto supply=f.result.presentation->supply;
    const auto selects=[&](){
        f.sample.owner=f.native.identity.owner;f.sample.raw.owner=f.sample.owner;f.selected->owner=f.sample.owner;
        f.sample.equipment=WeaponEquipmentIdentity{f.sample.owner.weapon,f.native.binding.data,f.native.binding.persistence,f.native.config.assetName};
        f.sample.asset=f.sample.equipment->Asset();
        ++f.sample.weapon.generation;++f.sample.input.owner.equipGeneration;(void)f.hands.Reset();
    };
    // A failed identity read cannot enroll or override a guessed item.
    f.sample.equipment.reset();f.Tick(Identity(),false);
    CHECK(!f.result.selected&&!f.result.blocksFire&&!f.result.presentation&&!f.result.ownsHand);
    CHECK(f.physical.BlocksFire(original.identity.owner,equipment,original.binding));
    // Ordinary ground pickup may replace the data at the same weapon address.
    ++f.native.identity.owner.equipGeneration;f.native.binding.owner=f.native.identity.owner;
    ++f.native.binding.context.equipGeneration;++f.native.binding.context.resource.weaponGeneration;
    ++f.native.binding.data;++f.native.binding.persistence;f.native.config.weaponData=f.native.binding.data;
    Text(f.native.config.assetName,"MP443_sp");selects();f.Tick(Identity(),false);
    CHECK(!f.result.selected&&!f.result.blocksFire&&!f.BlocksFire()&&!f.result.presentation);
    CHECK(!f.physical.BlocksFire(f.sample.owner,*f.sample.equipment));
    CHECK(!f.physical.RetainedGate(f.sample.owner,*f.sample.equipment));
    CHECK(!f.physical.RetainedGate(f.sample.owner,*f.sample.equipment,f.sample.binding));
    f.Tick(Identity(),true); // Support acquisition requires a held, fresh left-hand packet.
    const auto gun=f.hands.Current(InteractionHand::Right);CHECK(gun);
    const auto support=f.hands.Acquire(f.sample.input,{f.sample.input.owner,InteractionHand::Left,HandClaimKind::WeaponSupport,
        f.sample.weapon,{{2,f.sample.weapon.id},f.sequence,f.sample.input.deadlineNs,true},++f.intent,gun->token.id});
    CHECK(support.claim);f.Tick(Identity(),true);
    const auto held=f.hands.Current(InteractionHand::Left);CHECK(held&&held->token==support.claim->token);
    CHECK(!f.result.blocksFire&&!f.result.ownsHand&&f.submits==1);
    // Identical native metadata still cannot borrow another inventory lifetime.
    const auto nextEquip=f.native.identity.owner.equipGeneration+1;
    f.native=original;f.native.identity.owner.equipGeneration=nextEquip;f.native.binding.owner=f.native.identity.owner;
    f.native.binding.context.equipGeneration=nextEquip;++f.native.binding.context.resource.weaponGeneration;
    selects();f.Tick(Identity(),false);
    CHECK(f.result.selected&&!f.result.blocksFire&&!f.BlocksFire()&&f.result.presentation);
    CHECK(f.result.presentation->lifetime!=lifetime&&f.result.presentation->supply!=supply);
    const auto replacementGate=f.physical.RetainedGate(f.sample.owner,*f.sample.equipment,f.sample.binding);
    CHECK(replacementGate&&!replacementGate->blocksFire);
    // Returning with the original pinned resource resumes only its retained debt.
    f.native=original;f.native.identity.owner.equipGeneration=nextEquip+1;f.native.binding.owner=f.native.identity.owner;
    f.native.binding.context.equipGeneration=nextEquip+1;selects();
    f.Tick(f.sample.raw.rawLeftWristWorldMeters,false);f.Tick(f.sample.raw.rawLeftWristWorldMeters,false);
    CHECK(f.result.selected&&f.result.blocksFire&&f.BlocksFire()&&f.result.presentation);
    CHECK(f.result.presentation->lifetime==lifetime&&f.result.presentation->supply==supply);
    CHECK(f.Place(FeedPhysicalOperation::DetachContainer));
    CHECK(f.submitted->command.request.lifetime==lifetime&&f.submitted->command.request.id==2);return 0;
}
int RealNoEntryReceiptRecoversCompletedEndpointWithNewId(){
    Fixture f;f.refuse=true;CHECK(f.OpenCover());CHECK(f.submits==1&&!f.backend.Pending());
    CHECK(f.NoEntryCallbacks());const auto lifetime=f.submitted->command.request.lifetime;
    const auto endpoint=f.sample.raw.rawLeftWristWorldMeters;f.submitted.reset();f.refuse=false;
    f.Tick(endpoint,false);f.Tick(endpoint,false);f.Tick(endpoint,true);
    for(unsigned n=0;n<8;++n)f.Tick(endpoint,true);
    CHECK(!f.submitted);f.Tick(endpoint,false);
    CHECK(f.submitted&&f.submits==2);CHECK(f.submitted->command.request.lifetime==lifetime);
    CHECK(f.submitted->command.request.id==2&&f.backend.Pending());CHECK(f.Reconcile());
    CHECK(f.native.counts.loaded==20&&f.native.counts.reserve==130);return 0;
}
int DrainedActorReplacementReclaimsSlotsWithoutInventingAmmunitionSuccess(){
    Fixture f;
    for(unsigned replacementIndex=0;replacementIndex<12;++replacementIndex){
        CHECK(f.OpenCover());CHECK(f.backend.Pending());const auto oldCommand=f.submitted->command;
        const auto previous=f.native.identity;auto replacement=previous;
        replacement.owner.soldier+=0x10000;replacement.owner.weak+=0x10000;
        ++replacement.owner.actorGeneration;replacement.serverSoldier+=0x10000;
        Bc2AmmoReserveLease lease;lease.identity=replacement;lease.sequence=f.sequence+1;
        lease.observedNs=f.now+1;lease.deadlineNs=f.now+100*Ms;
        lease.loaded=f.native.counts.loaded;lease.reserve=f.native.counts.reserve;lease.capacity=f.native.counts.capacity;
        lease.verified=lease.allThreeIdle=true;
        FeedEquipmentActorDrain drain{previous,++f.invocation,replacementIndex+1,f.now+2,f.now+99*Ms,false};
        CHECK(!f.backend.RetireForReplacementActor(lease,f.now+3,drain));
        CHECK(f.BlocksFire()&&f.backend.Pending());
        drain.callbacksDrained=true;CHECK(f.backend.RetireForReplacementActor(lease,f.now+3,drain));
        const auto retired=f.backend.Retirement(replacementIndex);
        CHECK(retired&&retired->pending==oldCommand&&!retired->nativeDispatched);
        CHECK(!f.backend.Receipt(oldCommand.request.lifetime,oldCommand.request.id));
        f.native.identity=replacement;f.native.binding.owner=replacement.owner;
        f.native.binding.context.resource.actor=replacement.owner.soldier;
        f.native.binding.context.resource.actorGeneration=replacement.owner.actorGeneration;
        f.sample.owner=replacement.owner;f.selected->owner=replacement.owner;f.sample.raw.owner=replacement.owner;
        f.sample.raw.rig.soldier=replacement.owner.soldier;f.sample.raw.rig.weak=replacement.owner.weak;
        f.sample.input.owner.actor=(std::uint64_t(replacement.owner.weak)<<32)|replacement.owner.soldier;
        f.sample.input.owner.actorGeneration=replacement.owner.actorGeneration;
        (void)f.hands.Reset();f.submitted.reset();
        const auto start=feed_interaction_detail::Multiply(f.Open().motionFromContact,f.Open().weaponFromMotion);
        f.Tick(start,false);
        CHECK(!f.physical.BlocksFire(previous.owner,*f.sample.equipment));CHECK(f.result.selected&&!f.result.blocksFire&&f.result.presentation);
        CHECK((f.native.counts==FeedAmmunitionCounts{20,130,100}));
    }
    return 0;
}

int EveryEnrolledFeedKeepsNaturalHeldGraspsThroughCompletion(){
    using namespace feed_interaction_detail;unsigned cases=0;
    for(const auto& registration:RegisteredBc2FeedProfiles()){
        Fixture f(&registration);const auto motion=f.Open();const auto counts=f.native.counts;
        CHECK(motion.retainWhileGripped&&motion.acquisitionRadiusMeters>=.12f);
        auto grasp=feed_interaction_detail::Multiply(Turn(2.2f),motion.motionFromContact);
        const auto radius=std::hypot(grasp.values[3][0],grasp.values[3][1]);
        for(unsigned axis=0;axis<2;++axis)grasp.values[3][axis]*=1+.10f/radius;
        const auto wrist=[&](float t,float twist=0,float drift=0){
            auto local=feed_interaction_detail::Multiply(Turn(twist),Along(grasp,motion.kind,motion.travel*t));
            const auto r=std::hypot(local.values[3][0],local.values[3][1]);
            for(unsigned axis=0;axis<2;++axis)local.values[3][axis]*=1+drift/r;
            local.values[3][2]+=drift;
            return feed_interaction_detail::Multiply(local,motion.weaponFromMotion);
        };
        f.Tick(wrist(0),false);f.Tick(wrist(0),true);
        CHECK(f.result.ownsHand&&f.result.interactionStatus==FeedInteractionStatus::Acquired);
        CHECK(f.result.diagnostic.coverDistanceMeters&&*f.result.diagnostic.coverDistanceMeters>.099f);
        const auto acquired=f.hands.Current(InteractionHand::Left);CHECK(acquired);
        const auto held=[&](){const auto current=f.hands.Current(InteractionHand::Left);
            return current&&current->token==acquired->token&&f.result.ownsHand&&f.result.presentation&&
                f.result.presentation->mechanismClaim&&f.result.presentation->mechanismClaim->token==acquired->token&&
                f.result.presentation->mechanismWristWorldMeters&&f.native.counts==counts;
        };
        CHECK(held());
        for(unsigned step=1;step<=32;++step){const auto t=float(step)/32;
            f.Tick(wrist(.7f*t,.9f*t,.035f*std::sin(t*3.14159265f)),true);
            CHECK(held()&&!f.submitted);
        }
        // Pausing beyond the previous ten-second limit retains one token.
        for(unsigned step=0;step<1020;++step){f.Tick(wrist(.7f,.9f),true);CHECK(held()&&!f.submitted);}
        for(unsigned step=1;step<=16;++step){const auto t=.7f+.3f*float(step)/16;
            f.Tick(wrist(t,.9f),true);CHECK(held());}
        for(unsigned step=0;step<f.DwellPackets()&&!f.submitted;++step){f.Tick(wrist(1,.9f),true);CHECK(held());}
        CHECK(f.submitted&&f.submits==1&&f.submitted->command.authority.operation==FeedPhysicalOperation::OpenCover);
        const auto beforeGap=f.hands.Current(InteractionHand::Left);CHECK(beforeGap);
        f.PrepareTick(wrist(1,.9f),true);f.sample.raw.valid=false;f.Consume();
        const auto afterGap=f.hands.Current(InteractionHand::Left);CHECK(afterGap&&f.result.ownsHand);
        CHECK(afterGap->token==beforeGap->token&&afterGap->deadlineNs==beforeGap->deadlineNs);
        CHECK(!f.result.diagnostic.policyEvaluated&&f.submits==1);f.sample.raw.valid=true;
        // Native entry must occur inside the original command lease. Hold
        // continuity after a delayed accepted receipt is exercised below.
        for(unsigned step=0;step<3;++step){f.Tick(wrist(1,1.4f),true);CHECK(held()&&f.submits==1);}
        CHECK(f.Callback());
        for(unsigned step=0;step<12;++step){f.Tick(wrist(1,1.4f),true);CHECK(held()&&f.submits==1);}
        CHECK(!f.result.pending&&f.result.presentation->operation==FeedPhysicalOperation::OpenCover);
        f.Tick(wrist(1,1.4f),false);CHECK(!f.result.ownsHand&&!f.hands.Current(InteractionHand::Left));
        // Existing family completion recipe remains valid after release;
        // native ammunition still changes only through accepted attachment.
        f.Tick(wrist(1,1.4f),false);f.submitted.reset();
        CHECK(f.Place(FeedPhysicalOperation::DetachContainer)&&f.Reconcile());f.submitted.reset();
        CHECK(f.Place(FeedPhysicalOperation::AttachContainer)&&f.Reconcile());f.submitted.reset();
        CHECK(f.Place(FeedPhysicalOperation::SeatFeed)&&f.Reconcile());f.submitted.reset();
        CHECK(f.Close());const auto closeClaim=f.hands.Current(InteractionHand::Left);CHECK(closeClaim&&f.result.ownsHand);
        f.hideReceipt=true;
        CHECK(f.Callback());
        // The backend may retain an accepted receipt beyond its original
        // observation lease. Fresh held input must still corroborate it without
        // releasing the lid or dispatching another operation.
        for(unsigned step=0;step<20;++step){f.Tick(f.lastWrist,true);const auto current=f.hands.Current(InteractionHand::Left);
            CHECK(current&&current->token==closeClaim->token&&f.result.ownsHand&&f.result.blocksFire&&f.submits==5);}
        f.hideReceipt=false;
        for(unsigned step=0;step<10;++step){f.Tick(f.lastWrist,true);const auto current=f.hands.Current(InteractionHand::Left);
            CHECK(current&&current->token==closeClaim->token&&f.result.ownsHand&&f.result.presentation->mechanismWristWorldMeters);}
        CHECK(f.submits==5&&!f.result.blocksFire);
        f.Tick(f.lastWrist,false);CHECK(!f.result.ownsHand);++cases;
    }
    std::printf("Natural held belt-fed grasps: %u profiles, offset/roll/long-hold/completion/full-cycle checks\n",cases);
    return 0;
}
int OpenCoverDisplaySurvivesLongRawGapWithoutMotionAuthority(){
    Fixture f;CHECK(f.OpenCover()&&f.Reconcile());CHECK(f.result.presentation);
    const auto original=*f.result.presentation;const auto counts=f.native.counts;const auto submits=f.submits;
    // Keep native ownership and selection current for 350ms while raw render
    // input disappears. Its original 100ms raw lease must expire unchanged.
    for(unsigned frame=0;frame<35;++frame){
        f.PrepareTick(Identity(),frame%2==0);f.sample.raw.valid=false;
        f.result=f.physical.Tick(f.sample,f.hands,f.intent);
        CHECK(f.result.presentation&&f.result.presentation->displayInput);
        const auto& shown=*f.result.presentation;
        CHECK(shown.coverRadians==original.coverRadians&&NearPose(shown.weaponFromCoverHinge,original.weaponFromCoverHinge));
        CHECK(shown.lifetime==original.lifetime&&shown.supply==original.supply);
        CHECK(shown.input.sequence==original.input.sequence&&shown.input.deadlineNs==original.input.deadlineNs);
        CHECK(shown.displayInput->deadlineNs<=f.native.deadlineNs&&shown.displayInput->deadlineNs<=f.sample.selected->deadlineNs);
        CHECK(!shown.mechanismClaim&&!shown.mechanismInput&&!shown.mechanismWristWorldMeters&&!shown.supplyObject);
        CHECK(!Bc2FeedMechanismClaimFresh(shown,f.now));CHECK(f.submits==submits&&f.native.counts==counts);
        CHECK(ReadBc2PhysicalFeedPresentation(f.sample.owner,f.sample.input.nowNs)==f.result.presentation);
    }
    CHECK(f.now>original.input.deadlineNs);
    f.PrepareTick(Identity(),false,true);f.sample.raw.valid=false;f.result=f.physical.Tick(f.sample,f.hands,f.intent);
    CHECK(!f.result.presentation); // Holster/cancel is an actual ownership exit.
    f.PrepareTick(Identity(),false);f.sample.raw.valid=false;++f.sample.equipment->data;
    f.result=f.physical.Tick(f.sample,f.hands,f.intent);CHECK(!f.result.presentation);
    CHECK(f.submits==submits&&f.native.counts==counts);
    return 0;
}

}
int DetachedContainerCanClearTheMountWithoutParkingAtAnInvisiblePose(){
    using namespace feed_interaction_detail;
    unsigned cases=0;
    for(const auto& registration:RegisteredBc2FeedProfiles()){
        Fixture f(&registration);CHECK(f.OpenCover()&&f.Reconcile());f.submitted.reset();
        const auto counts=f.native.counts;const auto motion=f.PlacementMotion(FeedPhysicalOperation::DetachContainer);CHECK(motion);
        const auto wrist=[&](const fvr::math::Matrix4& object){return feed_interaction_detail::Multiply(motion->placement.sourceFromGrip,object);};
        const auto start=motion->placement.weaponFromSource;
        auto cleared=feed_interaction_detail::Multiply(Turn(.65f),start);
        for(unsigned axis=0;axis<3;++axis)cleared.values[3][axis]=start.values[3][axis]+
            1.4f*(motion->placement.weaponFromDestination.values[3][axis]-start.values[3][axis]);
        CHECK(Angle(cleared,motion->placement.weaponFromDestination)>motion->orientationToleranceRadians);
        f.Tick(wrist(start),false);f.Tick(wrist(start),false);f.Tick(wrist(start),true);
        CHECK(f.result.interactionStatus==FeedInteractionStatus::Acquired);
        for(unsigned step=1;step<=30;++step)f.Tick(wrist(reload_insertion_detail::BlendRigid(start,cleared,double(step)/30)),true);
        for(unsigned step=0;step<f.DwellPackets();++step)f.Tick(wrist(cleared),true);
        CHECK(f.result.interactionStatus==FeedInteractionStatus::Endpoint);
        CHECK(!f.submitted&&f.native.counts==counts&&f.submits==1);
        f.Tick(wrist(cleared),false);
        CHECK(f.submitted&&f.submitted->command.authority.operation==FeedPhysicalOperation::DetachContainer);
        CHECK(f.submitted->command.authority.effect==FeedAmmunitionEffect::PhysicalOnly&&f.native.counts==counts);
        CHECK(f.Reconcile()&&f.result.presentation->operation==FeedPhysicalOperation::AttachContainer);
        CHECK(!f.result.presentation->droppedOriginalAtNs&&!f.result.presentation->hiddenPartsMask);
        CHECK(f.native.counts==counts&&f.submits==2);++cases;
    }
    CHECK(cases==24);return 0;
}
int DetachClearanceCannotReplaceDirectionDwellOrCurrentHandEvidence(){
    using namespace feed_interaction_detail;
    unsigned cases=0;
    for(const auto& registration:RegisteredBc2FeedProfiles())for(unsigned branch=0;branch<7;++branch){
        Fixture f(&registration);CHECK(f.OpenCover()&&f.Reconcile());f.submitted.reset();
        const auto counts=f.native.counts;const auto motion=f.PlacementMotion(FeedPhysicalOperation::DetachContainer);CHECK(motion);
        const auto wrist=[&](const fvr::math::Matrix4& object){return feed_interaction_detail::Multiply(motion->placement.sourceFromGrip,object);};
        const auto start=motion->placement.weaponFromSource;
        std::array<double,3> outward{};double lengthSquared=0;
        for(unsigned axis=0;axis<3;++axis){outward[axis]=double(motion->placement.weaponFromDestination.values[3][axis])-start.values[3][axis];
            lengthSquared+=outward[axis]*outward[axis];}
        const auto length=std::sqrt(lengthSquared);CHECK(length>2*motion->pathToleranceMeters);
        auto end=feed_interaction_detail::Multiply(Turn(.65f),start);
        if(branch==0){ // A long sideways pull does not clear the mount.
            std::array<double,3> sideways{-outward[1],outward[0],0};
            if(std::hypot(sideways[0],sideways[1])<.001)sideways={-outward[2],0,outward[0]};
            const auto norm=std::sqrt(sideways[0]*sideways[0]+sideways[1]*sideways[1]+sideways[2]*sideways[2]);
            for(unsigned axis=0;axis<3;++axis)end.values[3][axis]+=float(sideways[axis]*length*1.4/norm);
        }else if(branch!=2){ // Turning in place also cannot clear the mount.
            const auto factor=branch==1?-1.4:branch==3?(length-motion->pathToleranceMeters+.0001)/length:1.4;
            for(unsigned axis=0;axis<3;++axis)end.values[3][axis]+=float(outward[axis]*factor);
        }
        f.Tick(wrist(start),false);f.Tick(wrist(start),false);f.Tick(wrist(start),true);
        CHECK(f.result.interactionStatus==FeedInteractionStatus::Acquired);
        for(unsigned step=1;step<=30;++step)f.Tick(wrist(reload_insertion_detail::BlendRigid(start,end,double(step)/30)),true);
        if(branch<3){
            for(unsigned step=0;step<f.DwellPackets();++step)f.Tick(wrist(end),true);
            CHECK(f.result.interactionStatus==FeedInteractionStatus::Moving);
        }else if(branch>3){
            for(unsigned step=0;step<f.DwellPackets();++step)f.Tick(wrist(end),true);
            CHECK(f.result.interactionStatus==FeedInteractionStatus::Endpoint);
        }else CHECK(f.result.interactionStatus==FeedInteractionStatus::Endpoint);
        if(branch==4)f.sample.input.tracked[0]=false;
        if(branch==6)f.now+=200*Ms;
        f.Tick(wrist(end),false,branch==5);
        CHECK(!f.submitted&&!f.result.pending&&f.submits==1&&f.native.counts==counts&&f.BlocksFire());
        CHECK(f.result.interactionStatus!=FeedInteractionStatus::Completed);
        // Replaying a release cannot turn an interrupted grasp into completion.
        f.Consume();CHECK(!f.submitted&&!f.result.pending&&f.submits==1&&f.native.counts==counts);
        ++cases;
    }
    CHECK(cases==168);return 0;
}
int EmptyWeaponStillRetainsReplacementCustodyAndInterruptedOriginalPresentation(){
    using namespace feed_interaction_detail;
    for(unsigned branch=0;branch<3;++branch){
        Fixture f;f.native.counts.loaded=0;CHECK(f.OpenCover()&&f.Reconcile());
        const auto counts=f.native.counts;f.submitted.reset();
        if(branch==0){
            // A tracking interruption of an unfinished extraction is not an
            // explicit discard, even when the old container has no rounds.
            const auto motion=f.PlacementMotion(FeedPhysicalOperation::DetachContainer);CHECK(motion);
            const auto wrist=[&](const fvr::math::Matrix4& object){return feed_interaction_detail::Multiply(motion->placement.sourceFromGrip,object);};
            const auto start=motion->placement.weaponFromSource;
            const auto middle=reload_insertion_detail::BlendRigid(start,motion->placement.weaponFromDestination,.4);
            f.Tick(wrist(start),false);f.Tick(wrist(start),false);f.Tick(wrist(start),true);
            for(unsigned step=1;step<=15;++step)f.Tick(wrist(reload_insertion_detail::BlendRigid(start,middle,double(step)/15)),true);
            CHECK(f.result.interactionStatus==FeedInteractionStatus::Moving);
            f.sample.input.tracked[0]=false;f.Tick(wrist(middle),true);
            CHECK(!f.result.ownsHand&&!f.submitted);
            f.sample.input.tracked[0]=true;f.Tick(wrist(middle),false);
        }else {
            CHECK(f.Place(FeedPhysicalOperation::DetachContainer)&&f.Reconcile());
            CHECK(f.result.presentation->droppedOriginalAtNs>0);
            CHECK(f.PickSupply());CHECK(f.result.presentation->supplyObject);
            const auto item=f.result.presentation->supplyObject->item;
            CHECK(item.id&&item.generation&&!f.result.presentation->droppedOriginalAtNs);
            f.submitted.reset();
            if(branch==2){f.sample.input.tracked[0]=false;f.Tick(f.lastWrist,true);f.sample.input.tracked[0]=true;}
            f.Tick(f.lastWrist,false);
            // This representation owns reserve rounds despite loaded==0.
            // Release or tracking loss must retain it, with no fresh belt copy.
            for(unsigned tick=0;tick<80;++tick){
                f.Tick(f.lastWrist,false);
                CHECK(f.result.presentation&&!f.result.presentation->hiddenPartsMask&&!f.result.presentation->droppedOriginalAtNs);
                CHECK(!f.result.presentation->supplyObject&&!f.result.bodyVisual);
            }
        }
        CHECK(f.result.presentation&&!f.result.presentation->hiddenPartsMask&&!f.result.presentation->droppedOriginalAtNs);
        CHECK(!f.submitted&&!f.result.pending&&f.native.counts==counts&&f.BlocksFire());
        CHECK(f.submits==(branch==0?1u:2u));
    }
    return 0;
}
int EveryEnrolledFeedCompletesNaturalSupplyApproachAndRegrasp(){
    const std::array<std::vector<unsigned>,2> cadences{{{1},{1,0}}};unsigned cycles=0;
    for(const auto& registration:RegisteredBc2FeedProfiles())for(unsigned recovery=0;recovery<3;++recovery)
    for(unsigned cadence=0;cadence<cadences.size();++cadence){
        Fixture f(&registration);f.native.counts.loaded=recovery?20:0;
        f.nativeCadence=cadences[cadence];f.delayedRender=cadence!=0;f.captureCadenceTrace=true;
        const auto before=f.native.counts;
        const auto reconcile=[&](){if(!f.Callback())return false;
            for(std::size_t n=0;n<f.nativeCadence.size()+3;++n)f.Tick(f.lastWrist,false);
            return !f.backend.Pending()&&!f.result.pending;};
        CHECK(f.OpenCover()&&reconcile());CHECK(f.Place(FeedPhysicalOperation::DetachContainer)&&reconcile());
        f.submitted.reset();
        const bool pickup=f.PickSupplyFromHeldApproach();
        if(!pickup)f.PrintCadenceFailure(cadence,FeedPhysicalOperation::AttachContainer);
        CHECK(pickup&&f.result.presentation&&f.result.presentation->supplyObject);
        const auto original=*f.result.presentation->supplyObject;const auto lifetime=f.result.presentation->lifetime;
        CHECK(f.native.counts==before&&f.submits==2&&!f.result.bodyVisual&&!f.result.pending);
        if(recovery){
            if(recovery==2){f.sample.input.tracked[0]=false;f.Tick(f.lastWrist,true);f.sample.input.tracked[0]=true;}
            else f.Tick(f.lastWrist,false);
            CHECK(!f.hands.Current(InteractionHand::Left)&&f.native.counts==before);
            CHECK(f.PickSupplyFromHeldApproach());CHECK(f.result.presentation->supplyObject);
            const auto& reclaimed=*f.result.presentation->supplyObject;
            CHECK(reclaimed.item==original.item&&reclaimed.units==original.units&&reclaimed.identity==original.identity);
            CHECK(reclaimed.claim.token.id!=original.claim.token.id&&f.result.presentation->lifetime==lifetime);
            CHECK(f.submits==2&&!f.result.bodyVisual);
        }
        CHECK(f.Place(FeedPhysicalOperation::AttachContainer,false,true,true));
        CHECK(f.submits==3&&f.native.counts==before&&f.submitted);
        CHECK(f.submitted->command.authority.rounds==int(original.units));
        CHECK(f.submitted->command.request.lifetime==lifetime);
        const auto after=f.submitted->command.after;CHECK(reconcile()&&f.native.counts==after);
        CHECK(f.Place(FeedPhysicalOperation::SeatFeed)&&reconcile());CHECK(f.Close()&&reconcile());
        CHECK(f.submits==5&&!f.BlocksFire()&&f.native.counts==after);
        CHECK(after.loaded==before.loaded+int(original.units)&&after.reserve==before.reserve-int(original.units));
        for(std::uint64_t id=1;id<=5;++id){const auto receipt=f.backend.Receipt(lifetime,id);CHECK(receipt);
            CHECK(receipt->command.authority.effect==FeedAmmunitionEffect::RefillLoaded?id==3:id!=3);}
        ++cycles;
    }
    CHECK(cycles==144);
    std::printf("Feed natural supply: %u full enrolled remove/grab/attach/seat/close cycles with exact mocked receipts\n",cycles);
    return 0;
}
int EveryEnrolledFeedCompletesAfterSkippedBeltPickupRender(){
    unsigned cycles=0;
    for(const auto& registration:RegisteredBc2FeedProfiles())for(bool delayed:{false,true}){
        Fixture f(&registration);f.native.counts.loaded=0;
        f.nativeCadence=delayed?std::vector<unsigned>{1,0}:std::vector<unsigned>{1};
        f.delayedRender=delayed;f.captureCadenceTrace=true;
        const auto before=f.native.counts;
        const auto reconcile=[&](){if(!f.Callback())return false;
            for(std::size_t n=0;n<f.nativeCadence.size()+3;++n)f.Tick(f.lastWrist,false);
            return !f.backend.Pending()&&!f.result.pending;};
        CHECK(f.OpenCover()&&reconcile());CHECK(f.Place(FeedPhysicalOperation::DetachContainer)&&reconcile());
        f.submitted.reset();const bool pickup=f.PickSupplyFromHeldApproach(true);
        if(!pickup)f.PrintCadenceFailure(delayed?1u:0u,FeedPhysicalOperation::AttachContainer);
        CHECK(pickup&&f.result.presentation&&f.result.presentation->supplyObject);
        CHECK(f.sample.bodyFromHand&&!Bc2FeedSupplyContact(*f.sample.bodyFromHand));
        const auto object=*f.result.presentation->supplyObject;const auto lifetime=f.result.presentation->lifetime;
        CHECK(f.native.counts==before&&f.submits==2&&!f.result.pending);
        CHECK(f.Place(FeedPhysicalOperation::AttachContainer,false,true,true));
        CHECK(f.submits==3&&f.submitted&&f.submitted->command.authority.rounds==int(object.units));
        CHECK(f.submitted->command.request.lifetime==lifetime);
        const auto after=f.submitted->command.after;CHECK(reconcile());
        CHECK(f.Place(FeedPhysicalOperation::SeatFeed)&&reconcile());CHECK(f.Close()&&reconcile());
        CHECK(f.submits==5&&!f.BlocksFire()&&f.native.counts==after);
        CHECK(after.loaded==int(object.units)&&after.reserve==before.reserve-int(object.units));++cycles;
    }
    CHECK(cycles==48);
    std::printf("Feed skipped pickup render: %u full enrolled carry/attach cycles; original pickup leases and exact mocked receipts\n",cycles);
    return 0;
}
int main(){
    CHECK(EveryEnrolledFeedCompletesAfterSkippedBeltPickupRender()==0);
    CHECK(EveryEnrolledFeedCompletesNaturalSupplyApproachAndRegrasp()==0);
    CHECK(DetachedContainerCanClearTheMountWithoutParkingAtAnInvisiblePose()==0);
    CHECK(DetachClearanceCannotReplaceDirectionDwellOrCurrentHandEvidence()==0);
    CHECK(EmptyWeaponStillRetainsReplacementCustodyAndInterruptedOriginalPresentation()==0);
    CHECK(EveryEnrolledFeedKeepsNaturalHeldGraspsThroughCompletion()==0);
    CHECK(OpenCoverDisplaySurvivesLongRawGapWithoutMotionAuthority()==0);
    CHECK(ExactSelectedJoinAndPhysicalOwner()==0);
    CHECK(WrongRegistrationCannotBorrowValidAuthority()==0);
    CHECK(ExactNativeConfigurationAndGeometryAreBothRequired()==0);
    CHECK(DiagnosticsIdentifyRegisteredProfileAndRetainSelection()==0);
    CHECK(MissingCallbackAndTrackingPreserveDebt()==0);
    CHECK(RefusedSubmitIsNeverAnAcknowledgement()==0);
    CHECK(OccupiedHandAndExactRigAreRequired()==0);
    CHECK(BackendLifetimesIncreaseAcrossOwners()==0);
    CHECK(FiveStagesRetainPartialAmmoAndRefillOnlyAtAttachment()==0);
    CHECK(ReattachingWithoutChestSupplyCannotRefill()==0);
    CHECK(DetachedDwellKeepsOriginalCarryAndRequiresFreshSupply()==0);
    CHECK(EveryEnrolledFeedUsesTheSharedChestRefillPath()==0);
    CHECK(EveryEnrolledFeedUsesAnatomicalControllerMapping()==0);
    CHECK(EveryEnrolledFeedAcceptsObservedGraspWithDelayedPublication()==0);
    CHECK(EveryEnrolledFeedStartsFromReleasedApproach()==0);
    CHECK(EveryEnrolledSupplyPresentsDelayedPickupOffsetAndDropsLostHand()==0);
    CHECK(EveryEnrolledSupplyPinsPickupToOriginalNativeObservation()==0);
    CHECK(EveryEnrolledSupplyCompletesAcrossSplitClockRenewalGaps()==0);
    CHECK(EveryEnrolledSupplySplitClockRetentionRejectsContradictions()==0);
    CHECK(EveryEnrolledFeedTransfersDelayedSupportAtEachAcquisition()==0);
    CHECK(EveryEnrolledFeedRejectsUnarmedOrUnrelatedSupportTransfer()==0);
    CHECK(EveryEnrolledSupplyRejectsPickupWithoutItsOriginalNative()==0);
    CHECK(EveryEnrolledSupplyFreezesMotionJumpWithoutDroppingHeldGrasp()==0);
    CHECK(EveryEnrolledFeedCompletesWithIndependentPublicationClocks()==0);
    CHECK(EveryEnrolledFeedCompletesAcrossGameplayPublicationLoss()==0);
    CHECK(ChestPreparationSurvivesNeutralJitterAndOutsideNeutral()==0);
    CHECK(EveryEnrolledSupplyRefreshesNeutralApproachBeforePickup()==0);
    CHECK(DelayedReleasePreservesExactGestureWithinOriginalDeadlines()==0);
    CHECK(IndependentNativeClockExpiryAndMutationAreRejected()==0);
    CHECK(DelayedDetachWaitsForPairedNativeWithoutRenewingOwnership()==0);
    CHECK(MissingCurrentViewPreservesOnlyLiveDetachClaims()==0);
    CHECK(FailedGatePublicationRetainsOnlyOriginalMissingViewLeases()==0);
    CHECK(MissingPacketNativeRetainsOnlyOriginalEvidenceLeases()==0);
    CHECK(BusyGatePreservesOnlyTheExistingHeldFeedGesture()==0);
    CHECK(GateDenialAndUntypedFailureStillInterruptHeldFeed()==0);
    CHECK(BusyGateCannotEnrollOrClearACompletedFeedBlocker()==0);
    CHECK(BusyGateCannotRetainExpiredOrChangedFeedOwnership()==0);
    CHECK(EveryEnrolledFeedRetainsRepeatedPacketThroughOpenCover()==0);
    CHECK(RepeatedFeedPacketRejectsChangedSourceAndExpiredAuthority()==0);
    CHECK(EveryEnrolledFeedConsumesMissedStartupEdge()==0);
    CHECK(EveryEnrolledFeedStartupPreservesAdmissionGates()==0);
    CHECK(EveryEnrolledFeedRejectsDelayedNeutralAfterRejectedPress()==0);
    CHECK(StartupRejectionBarrierStaysWithinItsInputLifetime()==0);
    CHECK(PartialCoverRequiresFreshRegraspAndPreservesProgress()==0);
    CHECK(LongStowMigratesOnlySamePinnedResource()==0);
    CHECK(SameAddressReplacementDoesNotInheritFeedDebt()==0);
    CHECK(RealNoEntryReceiptRecoversCompletedEndpointWithNewId()==0);
    CHECK(DrainedActorReplacementReclaimsSlotsWithoutInventingAmmunitionSuccess()==0);
    CHECK(AdmissionObservationRetainsFirstSeatPressWithoutGrantingAuthority()==0);
    CHECK(AdmissionObservationExplainsMissingViewRetentionBeforeSuspension()==0);
    CHECK(AdmissionObservationIgnoresUnrelatedMechanismClaim()==0);
    CHECK(CompactAdmissionUsesSameImmutableFailureAndPress()==0);
    CHECK(NeutralBlockedAdmissionObservesExistingRefusalsWithoutAuthority()==0);
    CHECK(HeldSupplyLossRetainsPreclearReasonAndCustody()==0);
    CHECK(CompactAdmissionUsesSameImmutableSupplyLoss()==0);
    CHECK(HeldMechanismLossRetainsFirstCauseAcrossSameGripReacquisition()==0);
    CHECK(MechanismReleaseWaitIsDistinctFromSuccessfulExplicitRelease()==0);
    CHECK(CompactAdmissionUsesSameImmutableMechanismLoss()==0);
    std::puts("Bc2PhysicalFeedTests: 54 deterministic owner/arbiter/callback/presentation compositions; no game or headset acceptance");
}
