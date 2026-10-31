#include "Bc2PhysicalFeed.h"
#include "Bc2SupportControllerMapping.h"
#include "Test.h"
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <memory>
#include <sstream>
using namespace fvr::bc2;
using namespace fvr::interaction;
namespace {
constexpr std::int64_t Ms=1000000;
template<std::size_t N>void Text(std::array<char,N>& out,std::string_view text){
    out={};std::memcpy(out.data(),text.data(),(std::min)(text.size(),N-1));
}
fvr::math::Matrix4 Identity(){fvr::math::Matrix4 m{};for(unsigned i=0;i<4;++i)m.values[i][i]=1;return m;}
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
    Bc2PhysicalFeedResult result{};
    std::optional<FeedEquipmentSubmission> submitted;
    bool refuse=false,occupyLeft=false;unsigned submits=0;
    Bc2PhysicalFeed physical;
    Bc2PhysicalFeedApi Api(){return {this,
        [](void* p,const ReloadStateOwner& o,std::int64_t now)noexcept{return static_cast<Fixture*>(p)->backend.Read(o,now);},
        [](void* p,const FeedEquipmentSubmission& s)noexcept{auto& f=*static_cast<Fixture*>(p);++f.submits;f.submitted=s;
            return !f.refuse&&f.backend.Submit(s,f.now+1);},
        [](void* p,std::uint64_t life,std::uint64_t id)noexcept{return static_cast<Fixture*>(p)->backend.Receipt(life,id);},
        [](void* p,const ReloadStateOwner& o,std::int64_t observed,std::int64_t now)noexcept{
            return static_cast<Fixture*>(p)->backend.ReadAt(o,observed,now);},
        [](void* p,const FeedEquipmentSubmission& s,bool noEntryOnly)noexcept{auto& f=*static_cast<Fixture*>(p);
            if(!noEntryOnly){++f.submits;f.submitted=s;}
            return f.backend.SubmitResult(s,f.now+1,noEntryOnly||f.refuse);},
        [](void* p,const FeedEquipmentGate& gate)noexcept{auto& f=*static_cast<Fixture*>(p);return f.backend.PublishGate(gate,f.now+1);},
        [](void* p,const AmmoResourceIdentity& resource)noexcept{return static_cast<Fixture*>(p)->backend.ReadGate(resource);},
        [](void* p,std::uint64_t after)noexcept{return static_cast<Fixture*>(p)->backend.Retirement(after);}};}
    explicit Fixture(const Bc2FeedRegistration* row=nullptr):physical(Api()){
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
    void PrepareTick(const fvr::math::Matrix4& wrist,bool grip,bool cancel=false){
        const auto age=(std::min)(100*Ms,registration->mechanism.maxAgeNs);
        now+=10*Ms;++sequence;native.observedNs=now-1;native.deadlineNs=native.observedNs+age;
        native.binding.observedNs=native.observedNs;native.binding.deadlineNs=native.deadlineNs;(void)backend.Publish(native,now);
        sample.binding=native.binding;
        selected->sequence=sequence;selected->observedNs=native.observedNs;selected->deadlineNs=native.deadlineNs;
        sample.input.sequence=sequence;sample.input.observedNs=now;sample.input.deadlineNs=now+age;sample.input.nowNs=now+1;
        sample.input.released[0]=!grip;sample.grip=grip;sample.cancel=cancel;
        sample.raw.inputEvidence=sample.input;sample.raw.rawLeftWristWorldMeters=wrist;sample.frameSequence=sequence;
        if(wristCalibration){
            InputFrame input;input.generation=sequence;input.spaceGeneration=sample.owner.space;input.predictedNs=now;
            input.focused=input.headValid=true;for(auto& hand:input.hands)hand.gripTracked=hand.aimTracked=true;
            auto rig=sample.raw.rig;rig.pose=0x110000;rig.skeleton=0x120000;
            sample.raw=BuildSupportControllerMapping(sample.owner,sample.input,input,rig,sample.rigFingerprint,
                feed_interaction_detail::Multiply(*wristCalibration,wrist),Identity(),Identity(),1,sample.input.nowNs);
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
        if(std::getenv("FVR_FEED_TEST_TRACE"))std::fprintf(stderr,"profile=%llu tick=%llu grip=%d selected=%d blocked=%d owns=%d pending=%d status=%u interaction=%u reload=%u hand=%u angle=%.4f submits=%u\n",
            static_cast<unsigned long long>(registration->key),static_cast<unsigned long long>(sequence),sample.grip,result.selected,result.blocksFire,result.ownsHand,result.pending,
            unsigned(result.status),unsigned(result.interactionStatus),unsigned(result.reloadStatus),unsigned(result.handReason),result.presentation?result.presentation->coverRadians:-99.f,submits);
        return result;
    }
    Bc2PhysicalFeedResult Tick(const fvr::math::Matrix4& wrist,bool grip,bool cancel=false){
        PrepareTick(wrist,grip,cancel);return Consume();
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
        Tick(sample.raw.rawLeftWristWorldMeters,false);
        Tick(sample.raw.rawLeftWristWorldMeters,false);
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
        sample.bodyFromHand=chest;Tick(chest,false);Tick(chest,false);Tick(chest,true);
        const auto hand=hands.Current(InteractionHand::Left);
        return hand&&hand->token.kind==HandClaimKind::AmmoObject&&result.ownsHand;
    }
    bool Place(FeedPhysicalOperation op,bool acquireSupply=true){
        const bool picked=op==FeedPhysicalOperation::AttachContainer&&acquireSupply;
        if(picked&&!PickSupply())return false;
        if(!result.presentation)return false;
        const auto retained=result.presentation;auto pose=registration->geometry.poses[unsigned(Bc2FeedPoseId::Resting)];
        pose.chainCount=retained->chainCount;
        if(retained->registrationKey!=registration->key||pose.chainCount!=registration->geometry.chainCount)return false;
        pose.weaponFromPouch=retained->weaponFromPouch;pose.weaponFromFeedRoot=retained->weaponFromChain[0];
        for(unsigned i=0;i+1<pose.chainCount;++i)pose.parentFromChainChild[i]=feed_interaction_detail::Multiply(
            retained->weaponFromChain[i+1],feed_interaction_detail::Inverse(retained->weaponFromChain[i]));
        const auto motion=BuildBc2FeedMotionFromRetainedPose(registration->geometry,op,registration->interactionPolicy,pose);
        if(!motion)return false;submitted.reset();
        const auto contact=[&](const fvr::math::Matrix4& object){return feed_interaction_detail::Multiply(motion->placement.sourceFromGrip,object);};
        const auto start=contact(motion->placement.weaponFromSource);
        if(!picked){Tick(start,false);Tick(start,false);}Tick(start,true);
        for(unsigned step=1;step<=30;++step)Tick(contact(reload_insertion_detail::BlendRigid(
            motion->placement.weaponFromSource,motion->placement.weaponFromDestination,double(step)/30)),true);
        const auto end=contact(motion->placement.weaponFromDestination);
        for(unsigned step=0;step<DwellPackets();++step)Tick(end,true);
        Tick(end,false);return submitted&&submitted->command.authority.operation==op;
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
int EveryEnrolledFeedUsesTheSharedChestRefillPath(){
    unsigned configurations=0;
    for(const auto& registration:RegisteredBc2FeedProfiles()){
        Fixture f(&registration);const auto before=f.native.counts;
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
        Fixture anatomical(&registration),support(&registration);
        anatomical.wristCalibration=Identity();support.wristCalibration=Identity();
        support.wristCalibration->values[1][1]=support.wristCalibration->values[2][2]=-1;
        // The same controller position can carry different native support and
        // anatomical wrist orientations. Exercise publication mapping through
        // real feed acquisition and completion, including its orientation gate.
        CHECK(anatomical.OpenCover());CHECK(!support.OpenCover());
        CHECK(anatomical.sample.raw.valid&&support.sample.raw.valid);
        const auto& a=anatomical.sample.raw;const auto& s=support.sample.raw;
        CHECK(feed_interaction_detail::Distance(a.rawLeftWristWorldMeters,s.rawLeftWristWorldMeters)<.00001f);
        CHECK(feed_interaction_detail::Angle(a.rawLeftWristWorldMeters,s.rawLeftWristWorldMeters)>
            registration.mechanism.contacts[unsigned(FeedPart::Cover)].maxAngleRadians);
        CHECK(a.owner==anatomical.sample.owner&&a.rigFingerprint==registration.geometry.rigFingerprint);
        CHECK(a.inputEvidence.sequence==anatomical.sample.input.sequence);
        CHECK(a.inputEvidence.observedNs==anatomical.sample.input.observedNs&&
            a.inputEvidence.deadlineNs==anatomical.sample.input.deadlineNs);
        CHECK(anatomical.result.pending&&anatomical.submits==1);
        CHECK(anatomical.submitted->command.authority.operation==FeedPhysicalOperation::OpenCover);
        CHECK(support.result.selected&&!support.result.ownsHand&&!support.result.pending&&!support.result.blocksFire&&support.submits==0);
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
    CHECK(f.Reconcile());CHECK(f.result.presentation->coverRadians>1.4f);return 0;
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
}
int main(){
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
    CHECK(EveryEnrolledFeedUsesTheSharedChestRefillPath()==0);
    CHECK(EveryEnrolledFeedUsesAnatomicalControllerMapping()==0);
    CHECK(EveryEnrolledFeedStartsFromReleasedApproach()==0);
    CHECK(EveryEnrolledFeedConsumesMissedStartupEdge()==0);
    CHECK(EveryEnrolledFeedStartupPreservesAdmissionGates()==0);
    CHECK(EveryEnrolledFeedRejectsDelayedNeutralAfterRejectedPress()==0);
    CHECK(StartupRejectionBarrierStaysWithinItsInputLifetime()==0);
    CHECK(PartialCoverRequiresFreshRegraspAndPreservesProgress()==0);
    CHECK(LongStowMigratesOnlySamePinnedResource()==0);
    CHECK(SameAddressReplacementDoesNotInheritFeedDebt()==0);
    CHECK(RealNoEntryReceiptRecoversCompletedEndpointWithNewId()==0);
    CHECK(DrainedActorReplacementReclaimsSlotsWithoutInventingAmmunitionSuccess()==0);
    std::puts("Bc2PhysicalFeedTests: 22 deterministic owner/arbiter/callback compositions; no game or headset acceptance");
}
