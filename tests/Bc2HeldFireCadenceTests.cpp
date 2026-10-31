#include "Test.h"
#include "Bc2HeldFireCadence.h"
#include "Bc2HolsterInput.h"
#include "Bc2MagazinePhysicalReload.h"
#include "Bc2NativeInputEpochs.h"
#include "Bc2PumpActions.h"
#include "fvr/interaction/MenuPointer.h"
#include "fvr/interaction/SupportGrip.h"
#include <bit>
#include <cstring>

using namespace fvr;
namespace {
constexpr std::int64_t Ms=1000000,Base=1000*Ms,PoseOffset=37000*Ms;
constexpr std::uint32_t Soldier=0x20000,Weak=0x30000,Weapon=0x40000;
struct Conditions {
    std::int64_t processing=0;
    bool playing=true,ownerCurrent=true,cacheWritable=true;
    bool magazine=false,pump=false,holster=false;
};
// Only the transport clock and native cache storage are fixtures. Encode/decode,
// Gather's lease, semantic policy, actual weapon-action filters, support pose
// policy, and native cache adapters are production code. No gun simulation or
// synthetic shot counter: Fire is read from the committed native byte offset.
struct Path {
    bc2::GameplayInputLease source;
    interaction::ControllerActions actions;
    interaction::SupportGrip support;
    interaction::InputFrame host{},decoded{};
    ipc::InputPacket wire{};
    bc2::NativeInputEpochs epochs{1,1};
    std::array<std::byte,bc2::InputBytes> cache{};
    interaction::ActionOutput mapped{},effective{};
    std::int64_t now=Base,lastGather=0,renderAt=0;
    bool admission=false,committed=false,codecOkay=true,supportPreserved=true;
    unsigned pressed=0,gathers=0;

    Path(){
        host.spaceGeneration=1;host.trackingBasisGeneration=1;
        host.focused=host.headValid=true;
        for(auto& hand:host.hands){hand.gripTracked=hand.aimTracked=true;hand.active=interaction::Components;}
        host.hands[0].grip.position={0,0,-.3f};
    }
    void Publish(std::int64_t time){
        now=time;++host.generation;host.predictedNs=time+PoseOffset;
        host.menu={interaction::InputMenuOwner::Gameplay,host.generation,1,time+100*Ms};
        codecOkay=codecOkay&&ipc::EncodeInput(host,time+100*Ms,wire)&&ipc::DecodeInput(wire,decoded);
    }
    std::uint32_t Word(std::size_t at)const {std::uint32_t value;std::memcpy(&value,cache.data()+at,4);return value;}
    void Put(std::size_t at,std::uint32_t value){std::memcpy(cache.data()+at,&value,4);}
    float Fire()const {return std::bit_cast<float>(Word(8+4*unsigned(bc2::EntryAction::Fire)));}
    void Gather(std::int64_t time,ipc::ChannelResult read=ipc::ChannelResult::Ok,Conditions c={}){
        now=time;++gathers;admission=committed=false;
        // Original native Gather reconstructs all actions before the VR hook.
        cache={};Put(0x98,(1u<<28)|(c.playing?0x103u:0u));Put(0x9c,0x80000000u);
        const bool pause=lastGather&&time-lastGather>150*Ms;
        const auto next=bc2::AdvanceNativeInputEpochs(epochs,false,pause);
        if(!next){codecOkay=false;return;}epochs=*next;lastGather=time;
        source.Observe(read,decoded,wire.deadlineQpc);
        const bool fresh=source.Fresh(time);
        const auto input=fresh?source.input:interaction::InputFrame{};
        const interaction::InputOwner owner{Soldier,epochs.input,c.playing,true,1};
        mapped=actions.Update(input,owner,input.predictedNs,time);effective=mapped;
        if(!fresh||!c.playing||!interaction::ValidInput(input)||!input.focused||!input.headValid)return;
        if(!effective.active||effective.owner!=Soldier){effective={};effective.owner=Soldier;}
        bc2::ApplyPumpActionGate(effective,c.pump);
        bc2::MagazinePhysicalResult magazine;magazine.blocksWeaponActions=c.magazine;
        bc2::ApplyMagazinePhysicalActions(effective,static_cast<const bc2::MagazineEquipmentProfile*>(nullptr),magazine);
        const interaction::SupportGripContact contact{renderAt>0&&time-renderAt<100*Ms,.02f};
        const auto supported=support.Update({(std::uint64_t(Weak)<<32)|Soldier,1,Weapon},input,contact);
        supportPreserved=supportPreserved&&supported.input.generation==input.generation&&
            supported.input.hands[1].trigger==input.hands[1].trigger;
        now=time+c.processing;
        admission=bc2::GameplayInputDispatchCurrent(source,now,c.ownerCurrent,c.cacheWritable);
        if(!admission){(void)actions.Update({}, {Soldier,epochs.input,false,true,1},0,now);return;}
        bc2::InputOverride patch;
        if(!patch.Apply(cache,effective))return;
        if(c.holster){
            bc2::HolsterSuppressionRequest request;
            request.owner={0x10000,Soldier,Weak,Weapon,1,epochs.equipment,1};
            request.input={{(std::uint64_t(Weak)<<32)|Soldier,1,1,1},input.generation,
                source.deadlineQpc-100*Ms,source.deadlineQpc,time+c.processing,true,{true,true}};
            request.request=1;request.nativeTick=gathers;request.cache=0x50000;
            bc2::HolsterInputOverride suppression;
            const bc2::HolsterInputOwner check{nullptr,[](void*,const bc2::HolsterSuppressionRequest&)noexcept{return true;}};
            if(!suppression.Apply(cache,request,check)||!suppression.Commit()){codecOkay=false;return;}
        }
        patch.Commit();committed=true;if(effective.pressed&interaction::Fire)++pressed;
    }
    void Step(std::int64_t dt=10*Ms,Conditions c={}){Publish(now+dt);Gather(now,ipc::ChannelResult::Ok,c);}
    void Arm(){for(unsigned n=0;n<6;++n)Step();}
};

int RateAndRenderSchedules(){
    // Different native/host phase offsets exercise both skipped and repeated
    // source generations. Rendering can remain absent for the whole burst.
    struct Rates {unsigned host,native;};
    for(const auto rate:{Rates{30,240},Rates{72,120},Rates{90,30},Rates{120,60},Rates{144,90}})
        for(const auto renderMs:{0,16,38,80,180})for(const auto phaseMs:{0,3,11}){
            Path p;const auto hostStep=1000000000/rate.host,nativeStep=1000000000/rate.native;
            auto nextHost=Base,nextNative=Base+phaseMs*Ms,nextRender=Base;
            unsigned heldSamples=0;
            for(;nextNative<Base+2200*Ms;nextNative+=nativeStep){
                while(nextHost<=nextNative){
                    p.host.hands[1].trigger=nextHost>=Base+300*Ms&&nextHost<Base+1900*Ms?1.f:0.f;
                    p.host.hands[0].squeeze=nextHost>=Base+200*Ms?.9f:0.f;
                    p.Publish(nextHost);nextHost+=hostStep;
                }
                if(renderMs&&nextNative>=nextRender){p.renderAt=nextNative;nextRender+=renderMs*Ms;}
                // A contended IPC read is permitted only while the last actual
                // reception remains current; use a bounded run of Busy reads.
                // Two reads are not necessarily a bounded lease: at 30Hz,
                // two misses after a 60ms stall exceed the original 100ms.
                // This continuity schedule recovers transport before expiry;
                // the expired-stall case below separately requires release.
                const bool busy=p.gathers>10&&(p.gathers%17==3||p.gathers%17==4)&&
                    p.source.Fresh(nextNative);
                Conditions c;c.processing=renderMs==180&&!busy&&p.gathers%13==6?60*Ms:0;
                p.Gather(nextNative,busy?ipc::ChannelResult::Busy:ipc::ChannelResult::Ok,c);
                CHECK(p.codecOkay&&p.supportPreserved);
                if(nextNative>=Base+450*Ms&&nextNative<Base+1850*Ms){
                    if(!p.source.Fresh(nextNative)||!p.mapped.active)
                        std::fprintf(stderr,"host=%u native=%u render=%d phase=%d time=%lld deadline=%lld generation=%llu busy=%d processing=%lld\n",
                            rate.host,rate.native,renderMs,phaseMs,nextNative,p.source.deadlineQpc,p.source.input.generation,busy,c.processing);
                    CHECK(p.source.Fresh(nextNative)&&p.mapped.active);
                    CHECK((p.mapped.held&interaction::Fire)&&p.admission&&p.committed&&p.Fire()==1.f);++heldSamples;
                }
                CHECK((p.Word(0x98)&(1u<<28))&&p.Word(0x9c)==0x80000000u);
                // A delayed native frame advances the schedule once. The next
                // gather receives the host's latest packet; no replay/catch-up.
                nextNative+=c.processing;
            }
            CHECK(heldSamples>20&&p.pressed==1&&p.Fire()==0.f);
        }
    return 0;
}

int SuppressionTransitions(){
    for(unsigned kind=0;kind<3;++kind){Path p;p.Arm();p.host.hands[1].trigger=1;p.Step();CHECK(p.Fire()==1);
        const auto sourceGeneration=p.source.input.generation;
        for(unsigned n=0;n<5;++n){Conditions c;c.magazine=kind==0;c.pump=kind==1;c.holster=kind==2;
            // Native suppression can begin/end between host packets. It must
            // not erase the semantic hold or require a new trigger edge.
            p.Gather(p.now+Ms,ipc::ChannelResult::Busy,c);
            CHECK((p.mapped.held&interaction::Fire)&&p.Fire()==0&&p.committed);
        }
        p.Gather(p.now+Ms,ipc::ChannelResult::Busy);
        CHECK(p.source.input.generation==sourceGeneration&&p.Fire()==1&&!(p.mapped.pressed&interaction::Fire));
        CHECK(p.codecOkay);
    }
    // Host left-face menu-chord suppression does not touch the gun trigger.
    Path p;p.Arm();p.host.hands[1].trigger=1;
    interaction::MenuPointerFrame pointer;pointer.suppressLeftFaceButtons=true;
    p.host.hands[0].held=interaction::Primary|interaction::Secondary;pointer.FilterGameplayInput(p.host);
    p.Step();CHECK(p.Fire()==1&&p.host.hands[0].held==0);
    return 0;
}

int LeaseAndDispatchBoundaries(){
    {Path p;p.Arm();p.host.hands[1].trigger=1;Conditions stall;stall.processing=60*Ms;
        p.Step(10*Ms,stall);CHECK(p.Fire()==1&&p.committed);
        const auto originalDeadline=p.source.deadlineQpc;
        p.Gather(p.now+33*Ms,ipc::ChannelResult::Busy);CHECK(p.Fire()==1);
        p.Gather(p.now+33*Ms,ipc::ChannelResult::Busy);
        CHECK(p.now>originalDeadline&&p.Fire()==0&&!p.committed);
        p.Step();CHECK(p.Fire()==0); // Transport recovery cannot rearm held Fire.
        p.host.hands[1].trigger=0;p.Arm();p.host.hands[1].trigger=1;p.Step();CHECK(p.Fire()==1);}
    {Path p;p.Arm();p.host.hands[1].trigger=1;Conditions c;c.processing=90*Ms;
        for(unsigned n=0;n<8;++n){p.Step(5*Ms,c);CHECK(p.Fire()==1&&p.committed);}
        CHECK(p.pressed==1);}
    for(const auto delay:std::array<std::int64_t,4>{0,99*Ms,100*Ms,101*Ms}){Path p;p.Arm();p.host.hands[1].trigger=1;
        Conditions c;c.processing=delay;p.Step(10*Ms,c);
        CHECK(p.source.Fresh(p.now-delay)&&(p.mapped.held&interaction::Fire));
        CHECK(p.committed==(delay<100*Ms)&&p.Fire()==(delay<100*Ms?1.f:0.f));
        if(delay>=100*Ms){
            // Regression: entry freshness used to reach InputOverride alone,
            // which demonstrably writes stale Fire=1 at this exact boundary.
            auto beforeFix=p.cache;bc2::InputOverride old;
            CHECK(old.Apply(beforeFix,p.effective));old.Commit();
            float fire=0;std::memcpy(&fire,beforeFix.data()+40,4);CHECK(fire==1);
            p.Step();CHECK(p.Fire()==0);p.host.hands[1].trigger=0;p.Arm();p.host.hands[1].trigger=1;p.Step();CHECK(p.Fire()==1);
        }
    }
    for(unsigned fault=0;fault<3;++fault){Path p;p.Arm();p.host.hands[1].trigger=1;Conditions c;
        c.ownerCurrent=fault!=0;c.cacheWritable=fault!=1;c.processing=fault==2?-p.now-10*Ms:0;
        p.Step(10*Ms,c);CHECK(!p.admission&&!p.committed&&p.Fire()==0);
    }
    Path p;p.Arm();p.host.hands[1].trigger=1;p.Step();const auto deadline=p.source.deadlineQpc;
    p.Gather(deadline-1,ipc::ChannelResult::Busy);CHECK(p.Fire()==1&&p.source.deadlineQpc==deadline);
    p.Gather(deadline,ipc::ChannelResult::Busy);CHECK(p.Fire()==0&&!p.committed&&p.source.deadlineQpc==deadline);
    p.Step();CHECK(p.Fire()==0); // Fresh held recovery cannot rearm an expired hold.
    for(const auto result:{ipc::ChannelResult::Timeout,ipc::ChannelResult::Invalid,ipc::ChannelResult::Closed}){
        Path q;q.Arm();q.host.hands[1].trigger=1;q.Step();q.Gather(q.now+Ms,result);
        CHECK(q.Fire()==0&&!q.source.input.generation&&q.source.deadlineQpc==0);
    }
    return 0;
}

int SafetyTransitions(){
    for(unsigned fault=0;fault<7;++fault){Path p;p.Arm();p.host.hands[1].trigger=1;p.Step();CHECK(p.Fire()==1);
        const auto good=p.host;Conditions c;
        switch(fault){
        case 0:p.host.hands[1].aimTracked=false;break;
        case 1:p.host.headValid=false;break;
        case 2:p.host.focused=false;p.host.hands={};break;
        case 3:c.playing=false;break;
        case 4:++p.host.spaceGeneration;break;
        case 5:++p.epochs.input;break;
        case 6:break; // Valid newest packet after a native scheduling pause.
        }
        p.Step(fault==6?151*Ms:10*Ms,c);CHECK(p.Fire()==0);
        const auto sequence=p.host.generation,space=p.host.spaceGeneration;p.host=good;p.host.generation=sequence;p.host.spaceGeneration=space;
        for(unsigned n=0;n<6;++n){p.Step();CHECK(p.Fire()==0);}
        p.host.hands[1].trigger=0;p.Arm();p.host.hands[1].trigger=1;p.Step();CHECK(p.Fire()==1);
    }
    // Losing only the support hand cannot cancel a valid independently armed
    // gun hand, even when the renderer loses its support-contact source too.
    Path p;p.Arm();p.host.hands[1].trigger=1;p.Step();p.host.hands[0].gripTracked=false;
    for(unsigned n=0;n<20;++n){p.Step();CHECK(p.Fire()==1);}
    return 0;
}
}
int main(){
    CHECK(RateAndRenderSchedules()==0);CHECK(SuppressionTransitions()==0);
    CHECK(LeaseAndDispatchBoundaries()==0);CHECK(SafetyTransitions()==0);
    return 0;
}
