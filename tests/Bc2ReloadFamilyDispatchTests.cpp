#include "Bc2ReloadFlowRuntime.h"
#include "Bc2FixedContainerNative.h"
#include "Bc2MagazineEmptyControl.h"
#include "Test.h"
#include <cstring>

using namespace fvr::bc2;
using namespace fvr::interaction;
namespace {
template<class T,std::size_t N> void Put(std::array<std::byte,N>& bytes,unsigned at,T value){std::memcpy(bytes.data()+at,&value,sizeof(value));}
template<class T,std::size_t N> T Get(const std::array<std::byte,N>& bytes,unsigned at){T value{};std::memcpy(&value,bytes.data()+at,sizeof(value));return value;}
ReloadObservedConfig Config(const ReloadConfigDescriptor& d){
    ReloadObservedConfig c;const auto& v=d.values;
    std::memcpy(c.assetName.data(),d.assetName.data(),d.assetName.size());std::memcpy(c.assetPath.data(),d.assetPath.data(),d.assetPath.size());
    c.weaponData=0xd0000;c.firingData=0xf0000;c.primaryFire=0x100000;c.ammoAddress=c.primaryFire+0x170;
    c.fireLogicType=v.fireLogicType;c.reloadType=v.reloadType;c.fireInputAction=v.fireInputAction;c.reloadInputAction=v.reloadInputAction;
    c.baseCapacity=v.baseCapacity;c.numberOfMagazines=v.numberOfMagazines;c.reloadDelay=v.reloadDelay;c.reloadTime=v.reloadTime;
    c.reloadThreshold=v.reloadThreshold;c.postReloadTime=v.postReloadTime;c.boltDelay=v.boltDelay;c.boltTime=v.boltTime;
    c.holdBoltUntilFireRelease=v.holdBoltUntilFireRelease;c.holdBoltUntilZoomRelease=v.holdBoltUntilZoomRelease;return c;
}
// Exact admitted object/input offsets; no game callback, actor scheduler or
// shot listener is simulated. The native instruction matrix is a separate
// private executable test. These stubs drive the production dispatch, guards
// and own-Update completion path, including its failure/retention behavior.
struct Fixture {
    ReloadHoldIdentity identity{{0x10000,0x20000,0x30000,0x40000,1,2,3},{0x50000,0x60000,0x70000},0x80000,0x90000,0xa0000};
    ReloadObservedConfig config{};
    std::array<std::byte,0x194> primary{};
    std::array<std::array<std::byte,0xb0>,3> firing{};
    std::array<std::byte,0x30> context{};
    static constexpr unsigned ContextAddress=0x110000;
    std::int64_t now=1000000000;
    unsigned nativeCalls=0,originalCalls=0;bool corruptCall=false,dispatchResult=false;
    std::array<unsigned,2> consumed{};
    FeedEquipmentAdapter adapter;
    FeedEquipmentSubmission submission{};
    std::optional<FeedAmmunition> ledger;
    explicit Fixture(const ReloadConfigDescriptor& d,int loaded):config(Config(d)){
        for(const auto& word:d.timing)Put(primary,word.offset,word.expected);
        Put(primary,0x20,config.reloadType);Put(primary,0x24,config.fireLogicType);
        Put(context,0x18,.016f);Put(context,0x20,1.f);context[0x24]=std::byte{1};
        for(auto& b:firing){Put(b,0,0x401234u);Put(b,4,0x401224u);Put(b,8,config.firingData);Put(b,12,config.ammoAddress);
            Put(b,0x3c,2u);Put(b,0x40,2u);Put(b,0x44,2u);Put(b,0x74,1.f);Put(b,0x78,1.f);
            Put(b,0x7c,loaded);Put(b,0x80,config.baseCapacity+3);Put(b,0x98,config.baseCapacity);
            b[0x8a]=std::byte{1};b[0xa4]=std::byte{1};b[0xa5]=std::byte{1};}
    }
    ReloadStateMemory Memory(){return {this,[](void* p,unsigned at,void* out,std::size_t size){
        auto& f=*static_cast<Fixture*>(p);
        const auto copy=[&](unsigned base,const auto& bytes){if(at<base||std::uint64_t(at)+size>std::uint64_t(base)+bytes.size())return false;
            std::memcpy(out,bytes.data()+at-base,size);return true;};
        if(copy(f.config.primaryFire,f.primary)||copy(ContextAddress,f.context))return true;
        for(unsigned n=0;n<3;++n)if(copy(f.identity.firing[n],f.firing[n]))return true;return false;
    },nullptr};}
    NativeCycleInputAccess Input(){return {this,[](void* p,unsigned index,unsigned expected,unsigned replacement,unsigned& seen)noexcept{
        auto& f=*static_cast<Fixture*>(p);if(index>1)return false;seen=Get<unsigned>(f.context,0x28+4*index);
        if(seen==expected)Put(f.context,0x28+4*index,replacement);return true;
    }};}
    static void Original(void* p){auto& f=*static_cast<Fixture*>(p);++f.originalCalls;
        f.consumed={Get<unsigned>(f.context,0x28),Get<unsigned>(f.context,0x2c)};}
    ReloadFeedCommandAccess Access(){return {Memory(),ContextAddress,this,[](void* p)noexcept{
        auto& f=*static_cast<Fixture*>(p);++f.nativeCalls;
        const auto plan=PlanNativeAmmoRefill(Get<int>(f.firing[2],0x7c),Get<int>(f.firing[2],0x80),f.config.baseCapacity,f.config.reloadType);
        if(!plan)return false;Put(f.firing[2],0x7c,plan->expectedLoaded);Put(f.firing[2],0x80,plan->expectedReserve);
        if(f.corruptCall)f.firing[2][0xa4]=std::byte{0};return true;
    },[](void* p)noexcept{return ++static_cast<Fixture*>(p)->now;}};}
    bool Submit(const Bc2FeedRegistration& registration,FeedAmmunitionEffect effect=FeedAmmunitionEffect::RefillLoaded){
        const auto fail=[&](const char* stage){
            std::fprintf(stderr,"Feed fixture rejected at %s: %.*s, loaded=%d, effect=%u\n",stage,
                int(registration.config.assetName.size()),registration.config.assetName.data(),Get<int>(firing[2],0x7c),unsigned(effect));
            return false;
        };
        FeedEquipmentNativeView view;view.identity=identity;view.config=config;
        view.binding={identity.owner,{{identity.owner.soldier,1,identity.owner.weapon,19},2,3},0xb0000,0xc0000,config.weaponData,0xe0000,now,now+100000000};
        view.counts={Get<int>(firing[2],0x7c),Get<int>(firing[2],0x80),config.baseCapacity};view.observedNs=now;
        // Match TryFeedEquipment's original native/profile deadline. Generated
        // rows can have a shorter lease than the adapter's 100 ms upper bound.
        view.deadlineNs=(std::min)(view.binding.deadlineNs,now+(std::min)(registration.mechanism.maxAgeNs,std::int64_t{100000000}));
        if(!adapter.Publish(view,now))return fail("publish view");
        const auto issued=adapter.Read(identity.owner,now);if(!issued)return fail("read issued view");
        if(!adapter.PublishGate({issued->binding.context.resource,10,1,false,true},now))return fail("enroll gate");
        submission.source=*issued;auto& g=submission.geometry;g.profile=registration.mechanism;g.registrationKey=registration.key;
        g.rigFingerprint=registration.geometry.rigFingerprint;g.inputSequence=50;g.observedNs=now;
        g.deadlineNs=(std::min)(issued->deadlineNs,now+90000000);g.selectedRigVerified=true;
        // Match the normal physical owner's Authority selection: attachment
        // refills the same supply; closing the cover only changes the mechanism.
        const auto operation=effect==FeedAmmunitionEffect::RefillLoaded?FeedPhysicalOperation::AttachContainer:FeedPhysicalOperation::CloseCover;
        const auto part=feed_mechanism_detail::OperationPart(operation);
        const auto motion=BuildBc2FeedMotion(registration.geometry,operation,registration.interactionPolicy);
        if(!motion)return fail("registered physical motion");g.motion=*motion;
        const auto owner=FeedEquipmentOwner(identity.owner);const FeedKey mechanism{30,1},binding{40,1};const FeedSupplyKey supply{{60,1},{61,1}};
        FeedAmmunitionSnapshot before{owner,mechanism,10,issued->sequence,issued->observedNs,issued->deadlineNs,issued->counts,supply,true};
        ledger.emplace(g.profile,owner,mechanism,supply,10);if(!ledger->Bind(before,now))return fail("bind ledger");
        FeedPhysicalCandidate candidate{owner,mechanism,operation==FeedPhysicalOperation::AttachContainer?supply:FeedSupplyKey{},
            supply,g.profile.id,g.profile.revision,before.sequence,operation,part,g.profile.contacts[unsigned(part)].role};
        FeedReloadRequest request{1,10,binding,{1,g.inputSequence,now+10,candidate},now+20};
        const auto plan=PlanNativeAmmoRefill(view.counts.loaded,view.counts.reserve,view.counts.capacity,1);if(!plan)return fail("plan refill");
        FeedAmmunitionAuthority authority{{41,1},binding,owner,mechanism,10,g.profile.id,g.profile.revision,candidate.operation,effect,
            effect==FeedAmmunitionEffect::PhysicalOnly?FeedSupplyKey{}:supply,effect==FeedAmmunitionEffect::PhysicalOnly?0:plan->units,true};
        const auto command=ledger->Dispatch(request,authority,before,now+20);if(!command)return fail("dispatch ledger");
        submission.command=*command;now+=30;
        if(!FeedEquipmentSubmissionValid(submission,now))return fail("validate submission");
        return adapter.Submit(submission,now)||fail("submit adapter");
    }
    ReloadFlowBoundary Boundary(unsigned branch){const auto& b=firing[branch];ReloadFlowBoundary out;
        out.owner=identity.owner;out.snapshotSequence=1;out.firing=identity.firing[branch];out.wrapperOffset=branch==0?0x3c:branch==1?0x40:0x10;
        out.branch=std::uint8_t(branch);out.current=Get<unsigned>(b,0x3c);out.previous=Get<unsigned>(b,0x40);out.next=Get<unsigned>(b,0x44);
        out.timer=Get<float>(b,0x50);out.loaded=Get<int>(b,0x7c);out.reserve=Get<int>(b,0x80);out.flagsA8=std::to_integer<unsigned char>(b[0xa8]);
        if(branch==2){out.serverPlayer=identity.serverPlayer;out.serverSoldier=identity.serverSoldier;out.serverItem=identity.serverItem;}return out;
    }
    std::optional<ReloadFlowRecord> Update(unsigned branch,std::uint64_t id,bool dispatch=false,bool code=true){
        ReloadFlowEventInput entry;entry.kind=ReloadFlowEvent::Update;entry.thread=1;entry.depth=1;entry.nowNs=++now;
        entry.boundary=Boundary(branch);entry.context=ContextAddress;entry.contextCopied=true;entry.copiedContext=context;
        ReloadFlowInvocation invocation;if(!invocation.Begin(id,entry))return {};
        if(dispatch){const auto nativeBefore=firing[2];const auto input=context;
            dispatchResult=DispatchReloadFeedCommand(adapter,submission.source,id,code,Access(),nativeBefore,input);}
        FixedContainerInputGuard guard;
        const std::array<unsigned,2> words{Get<unsigned>(context,0x28),Get<unsigned>(context,0x2c)};
        RunReloadFamilyInput(Memory(),config,Input(),words,true,entry.boundary.loaded,Original,this,guard);
        ReloadFlowEventEnd end;end.thread=1;end.nowNs=++now;end.boundary=Boundary(branch);end.contextCopied=true;end.copiedContext=context;
        end.feedInput=guard;return invocation.Finish(end);
    }
    bool Observe(const ReloadFlowRecord& record){return adapter.Observe(ReloadFamilyOwnUpdate(record),++now);}
};

int PartialEmptyAndPhysicalOnlyDispatch(){
    CHECK(!RegisteredBc2FeedProfiles().empty());
    for(const auto& row:RegisteredBc2FeedProfiles())for(int loaded:{0,row.config.values.baseCapacity-1})for(bool physicalOnly:{false,true}){
        Fixture f(row.config,loaded);CHECK(f.Submit(row,physicalOnly?FeedAmmunitionEffect::PhysicalOnly:FeedAmmunitionEffect::RefillLoaded));
        const auto source=f.firing;const auto server=f.Update(2,100,true);CHECK(server&&f.dispatchResult&&server->exit.feedInput.Exact());
        CHECK(f.nativeCalls==unsigned(!physicalOnly)&&f.originalCalls==1&&!f.Observe(*server));
        // A helper result and a server row cannot manufacture prediction convergence.
        CHECK(!f.adapter.Receipt(10,1));const auto beforeClient=f.Update(0,101);CHECK(beforeClient&&!f.Observe(*beforeClient));
        CHECK(!DispatchReloadFeedCommand(f.adapter,f.submission.source,102,true,f.Access(),source[2],f.context));
        CHECK(f.nativeCalls==unsigned(!physicalOnly));
        for(unsigned n=0;n<2;++n){f.firing[n]=f.firing[2];const auto client=f.Update(n,103+n);CHECK(client);
            if(n==0)CHECK(!f.Observe(*client));else CHECK(f.Observe(*client));}
        const auto receipt=f.adapter.Receipt(10,1);CHECK(receipt&&receipt->authorityInvocation==100&&receipt->post.counts==f.submission.command.after);
        CHECK(f.ledger->Complete(*receipt,++f.now));CHECK(f.nativeCalls==unsigned(!physicalOnly));
        for(unsigned n=0;n<3;++n)for(unsigned at:{0x8au,0xa4u,0xa5u})CHECK(f.firing[n][at]==source[n][at]);
    }
    std::printf("Feed dispatch: %zu registry rows passed empty/partial refill and physical-only own-Update completion.\n",RegisteredBc2FeedProfiles().size());
    return 0;
}
int FailedDispatchCannotRetryOrForgiveDebt(){
    const auto& row=RegisteredBc2FeedProfiles().front();
    {Fixture f(row.config,0);CHECK(f.Submit(row));const auto server=f.Update(2,100,true,false);CHECK(server&&f.dispatchResult&&f.nativeCalls==0);
        CHECK(!f.Observe(*server));for(unsigned n=0;n<2;++n){const auto client=f.Update(n,101+n);CHECK(client);if(n==0)CHECK(!f.Observe(*client));else CHECK(f.Observe(*client));}
        const auto rejected=f.adapter.Receipt(10,1);CHECK(rejected&&rejected->acknowledgement.status==FeedReloadAcknowledgement::Rejected&&rejected->post.counts==f.submission.command.before.counts);}
    for(unsigned mode=0;mode<3;++mode){Fixture f(row.config,0);CHECK(f.Submit(row));f.corruptCall=mode==0;
        const auto source=f.firing[2];const auto server=f.Update(2,100,true);CHECK(server&&f.nativeCalls==1);
        auto own=*server;if(mode==1)own.exit.boundary->serverSoldier++;if(mode==2)own.exit.boundary->owner.actorGeneration++;
        CHECK(!f.Observe(own)&&f.adapter.Uncertain()&&f.adapter.Pending());
        CHECK(!DispatchReloadFeedCommand(f.adapter,f.submission.source,200,true,f.Access(),source,f.context)&&f.nativeCalls==1);
        CHECK(!f.adapter.PublishGate({f.submission.source.binding.context.resource,10,2,false,true},++f.now));
        CHECK(!f.adapter.Receipt(10,1)&&Get<int>(f.firing[2],0x7c)==f.config.baseCapacity);
    }return 0;
}
int InputScopeAndStepUseExactFamilyProof(){
    const auto exercise=[](const ReloadConfigDescriptor& row)->int{
        for(int loaded:{0,row.values.baseCapacity-1}){Fixture f(row,loaded);Put(f.context,0x2c,5u);const auto before=f.context;
            FixedContainerInputGuard guard;CHECK(RunReloadFamilyInput(f.Memory(),f.config,f.Input(),{0,5},false,loaded,Fixture::Original,&f,guard));
            CHECK(f.originalCalls==1&&f.context==before&&guard.Exact()&&f.consumed[1]==1);
            MagazineEmptyStep step;step.identity=f.identity;step.branch=0;step.ownerRevision=1;step.sourceSequence=1;step.observedNs=f.now;step.deadlineNs=f.now+100000000;
            step.before.address=f.identity.firing[0];step.before.wrapperOffset=0x3c;step.before.currentState=step.before.nextState=2;
            step.before.loaded=loaded;step.before.reserve=10;
            const auto decoded=DecodeReloadUpdateContext(before);CHECK(decoded&&ReloadDirectFamilyEmptyStepEligible(f.Memory(),f.config,step,*decoded,f.now));
            auto bad=step;bad.identity.firing[1]=bad.identity.firing[0];CHECK(!ReloadDirectFamilyEmptyStepEligible(f.Memory(),f.config,bad,*decoded,f.now));
            CHECK(!ReloadDirectFamilyEmptyStepEligible(f.Memory(),f.config,step,*decoded,step.deadlineNs));
            Put(f.primary,0x10,0u);CHECK(!ReloadDirectFamilyEmptyStepEligible(f.Memory(),f.config,step,*decoded,f.now));
            FixedContainerInputGuard unsupported;CHECK(!RunReloadFamilyInput(f.Memory(),f.config,f.Input(),{0,5},true,loaded,Fixture::Original,&f,unsupported));
            CHECK(!unsupported.requested&&f.originalCalls==2&&f.context==before);
        }return 0;
    };
    for(const auto& row:RegisteredBc2FeedProfiles())if(exercise(row.config))return 1;
    for(const auto* row:FixedContainerConfigs)if(exercise(*row))return 1;
    return 0;
}
int PersistentScopeAndExactCompletionContext(){
    const auto& row=RegisteredBc2FeedProfiles().front();Fixture f(row.config,0);CHECK(f.Submit(row));
    ReloadFeedInputScopes scopes;CHECK(scopes.Retain(f.submission.source,f.now));scopes.Fault(f.identity);
    for(auto firing:f.identity.firing)CHECK(scopes.Find(firing)&&scopes.Find(firing)->fault);
    auto selected=f.submission.source;selected.identity.owner.equipGeneration++;selected.identity.owner.space++;
    selected.binding.owner=selected.identity.owner;selected.binding.context.equipGeneration++;selected.binding.context.space++;
    CHECK(scopes.Retain(selected,f.now)&&scopes.Faulted(selected.binding.context.resource));
    CHECK(!scopes.Retain(selected,selected.deadlineNs)&&scopes.Find(f.identity.firing[0]));
    auto foreign=f.identity;foreign.owner.actorGeneration++;scopes.Retire(foreign);CHECK(scopes.Find(f.identity.firing[0]));
    scopes.Retire(f.identity);CHECK(!scopes.Find(f.identity.firing[0]));
    Put(f.context,0x2c,4u);const auto record=f.Update(2,100);CHECK(record&&ReloadFamilyOwnUpdate(*record).neutralContext);
    for(unsigned n=0;n<4;++n){auto bad=*record;
        if(n==0)bad.exit.feedInput.original[1]=0;if(n==1)Put(bad.exit.copiedContext,0x2c,0u);
        if(n==2)bad.exit.feedInput.restored=false;if(n==3)bad.exit.contextCopied=false;
        CHECK(!ReloadFamilyOwnUpdate(bad).neutralContext);}
    return 0;
}
}
int main(){
    if(PartialEmptyAndPhysicalOnlyDispatch()||FailedDispatchCannotRetryOrForgiveDebt()||
       InputScopeAndStepUseExactFamilyProof()||PersistentScopeAndExactCompletionContext())return 1;
    std::printf("Four production reload family dispatch/completion groups passed with exact native-layout memory stubs; no game/headset claim.\n");return 0;
}
