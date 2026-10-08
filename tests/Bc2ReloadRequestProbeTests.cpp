#include "Bc2ReloadRequestProbe.h"
#include "NativeProbeConfig.h"
#include "Test.h"
#include <sstream>
using namespace fvr;using namespace fvr::bc2;
namespace {
struct Mock {
    static inline std::int64_t now=1000000000,submittedAt=0;
    static inline unsigned starts=0,keeps=0,submits=0,cancels=0;
    static inline bool active=false,unheld=false,badAck=false,advanceAck=false;
    static inline ReloadHoldIdentity id{{0x10000,0x20000,0x30000,0x40000,5,6,7},{0x50000,0x60000,0x70000},0x80000,0x90000,0xa0000};
    static inline ReloadCycleControl control{};
    static inline Bc2ReloadNativeRequest submitted{};
    static inline bool took=false;
    static inline unsigned retireCalls=0,reserveCalls=0,retireMode=0,reserveMode=0;
    static inline std::int64_t retiredAt=0;
    static inline ReloadHoldIdentity retiredIdentity{};
    static inline std::uint64_t retiredCycle=0;
    static void Reset(){now=1000000000;submittedAt=0;starts=keeps=submits=cancels=0;active=unheld=badAck=took=advanceAck=false;control={};submitted={};
        retireCalls=reserveCalls=retireMode=reserveMode=0;retiredAt=0;retiredIdentity={};retiredCycle=0;}
    static std::optional<ReloadHoldIdentity> Identity()noexcept{return id;}
    static bool Start(const ReloadCycleControl& c)noexcept{++starts;active=true;control=c;return true;}
    static bool Keep(const ReloadCycleControl& c)noexcept{++keeps;if(!active||control.deadlineNs<=now)return false;control=c;return true;}
    static std::optional<ReloadRoundLease> Lease(const ReloadHoldIdentity&,std::uint64_t cycle)noexcept {
        if(!active)return {};const bool done=submittedAt&&now-submittedAt>=200000000;
        return ReloadRoundLease{id,cycle,std::uint64_t(now),now,now+50000000,done?7:6,done?20:21,8,true,!unheld&&(!submittedAt||done)};
    }
    static bool Submit(const Bc2ReloadNativeRequest& r)noexcept{++submits;submitted=r;submittedAt=now;return true;}
    static std::optional<Bc2ReloadAckEvidence> Ack(const ReloadHoldIdentity&,std::uint64_t cycle)noexcept {
        if(!submittedAt||now-submittedAt<200000000||took)return {};took=true;
        if(advanceAck)now+=1000000; // Native completion can occur during API call.
        return Bc2ReloadAckEvidence{{{submitted.request.id,submitted.request.owner,interaction::ReloadOperation::InsertRound,interaction::ReloadAcknowledgement::Applied},id,badAck?cycle+1:cycle,100,777},now,now+50000000,true};
    }
    static void Cancel()noexcept{++cancels;active=false;}
    static std::optional<reloadFlowRuntime::RequestProbeSnapshot> Snapshot()noexcept {
        reloadFlowRuntime::RequestProbeSnapshot s;s.identity=id;s.observedNs=now;s.cycle=control.cycle;s.phase=active?3:6;
        const bool done=submittedAt&&now-submittedAt>=200000000;
        for(unsigned b=0;b<3;++b){s.branches[b].address=id.firing[b];s.branches[b].loaded=done?7:6;s.branches[b].reserve=done?20:21;}return s;
    }
    static std::int64_t Clock()noexcept{return now;}
    static ReloadRequestProbeApi Api(){return {Identity,Start,Keep,Lease,Submit,Ack,Cancel,Snapshot,Clock};}
    static std::optional<ReloadCycleRetirement> Retire(const ReloadHoldIdentity& owner,std::uint64_t cycle)noexcept {
        ++retireCalls;if(active)return {};retiredIdentity=owner;retiredCycle=cycle;retiredAt=++now;
        ReloadCycleRetirement r{owner,cycle,retireCalls,now,now+200000000,true};
        if(retireMode==1)r.identity.owner.weapon++;
        if(retireMode==2)r.cycle++;
        if(retireMode==3){r.observedNs-=1000000;r.deadlineNs=now;}
        if(retireMode==4)return {};return r;
    }
    static std::optional<Bc2AmmoReserveLease> Reserve()noexcept {
        ++reserveCalls;Bc2AmmoReserveLease r{id,reserveCalls,++now,now+100000000,7,20,8,true};
        if(reserveMode==1)r.observedNs=retiredAt-1;
        if(reserveMode==2)r.identity.owner.weapon++;
        if(reserveMode==3)r.deadlineNs=now;
        return r;
    }
    static ReloadRequestProbeApi RecoveryApi(){auto a=Api();a.retire=Retire;a.reserve=Reserve;return a;}
};
interaction::HandInteractionOwner Physical(){return {(std::uint64_t(Mock::id.owner.weak)<<32)|Mock::id.owner.soldier,5,17,7};}
interaction::InputFrame Input(std::uint64_t sequence,float marker){interaction::InputFrame f;f.generation=sequence;f.predictedNs=1;f.spaceGeneration=7;f.focused=f.headValid=true;f.hands[0].trigger=marker;
    for(auto& h:f.hands){h.active=interaction::Components;h.gripTracked=h.aimTracked=true;}return f;}
void Tick(Bc2ReloadRequestProbe& p,std::uint64_t ms,float marker=-1){Mock::now=1000000000+std::int64_t(ms)*1000000;auto f=Input(ms+1,marker<0?RequestProbeSchedule(ms,true).marker:marker);p.Tick(f,Physical(),Mock::now,Mock::now+100000000,Mock::now);}
std::string Report(Bc2ReloadRequestProbe& p){std::ostringstream s;p.Report(s);return s.str();}
int SuccessfulThirtySeconds(){Mock::Reset();Mock::advanceAck=true;Bc2ReloadRequestProbe p(true,Mock::Api());for(unsigned ms=0;ms<30000;ms+=10)Tick(p,ms);
    CHECK(Mock::starts==1&&Mock::submits==1&&Mock::cancels==1&&Mock::submittedAt==23000000000ll);
    CHECK(Mock::submitted.request.id==22001&&Mock::submitted.reservation.request==22001&&Mock::submitted.reservation.claim.owner==Physical());
    CHECK(Mock::submitted.heldLease.allThreeHeld&&Mock::submitted.heldLease.observedNs==Mock::submittedAt);
    const auto r=Report(p);CHECK(r.find("\"acknowledged\":true")!=r.npos&&r.find("\"reheld_350ms\":true")!=r.npos&&r.find("\"reason\":0")!=r.npos&&r.find("\"dropped\":0")!=r.npos&&r.find("\"physical_bridge_integrated\":false")!=r.npos);return 0;}
int SourceCannotBeRefreshed(){Mock::Reset();Bc2ReloadRequestProbe p(true,Mock::Api());Tick(p,5800);const auto f=Input(5801,.25f);const auto source=Mock::now;Mock::now+=30000000;
    p.Tick(f,Physical(),source,source+100000000,Mock::now);CHECK(Mock::control.observedNs==source&&Mock::control.deadlineNs==source+100000000);
    Mock::now+=100000000;p.Tick(f,Physical(),source,source+100000000,Mock::now);CHECK(Mock::cancels==1&&Mock::submits==0);return 0;}
int OwnerLossAndBadAck(){for(unsigned mode=0;mode<3;++mode){Mock::Reset();Bc2ReloadRequestProbe p(true,Mock::Api());
    for(unsigned ms=0;ms<=22000;ms+=10){if(mode==2)Mock::badAck=true;Tick(p,ms);if(ms==5800&&mode<2){auto f=Input(5812,.25f);auto owner=Physical();if(mode==0)++owner.equipGeneration;else f.focused=false;Mock::now+=10000000;p.Tick(f,owner,Mock::now,Mock::now+100000000,Mock::now);break;}}
    if(mode==2)for(unsigned ms=22010;ms<=22500;ms+=10)Tick(p,ms);
    CHECK(Mock::cancels==1&&Report(p).find("\"acknowledged\":false")!=std::string::npos);
    }return 0;}
int NoFakeHeldOrDuplicateSeat(){Mock::Reset();Bc2ReloadRequestProbe p(true,Mock::Api());for(unsigned ms=0;ms<22000;ms+=10)Tick(p,ms);Mock::unheld=true;Tick(p,22000);CHECK(Mock::submits==0&&Mock::cancels==1);
    Mock::Reset();Bc2ReloadRequestProbe q(true,Mock::Api());for(unsigned ms=0;ms<=22300;ms+=10)Tick(q,ms);Tick(q,22310,1);CHECK(Mock::submits==1&&Mock::cancels==1);return 0;}
int DefaultOffAndSchedule(){Mock::Reset();Bc2ReloadRequestProbe p(false,Mock::Api());Tick(p,5800);CHECK(!Mock::starts);
    unsigned shots=0,reload=0,seats=0;for(unsigned ms=0;ms<30000;++ms){const auto s=RequestProbeSchedule(ms,true);shots+=s.trigger==1;reload+=s.reload;seats+=s.marker==1;}
    CHECK(shots==200&&reload==200&&seats==200&&RequestProbeSchedule(3300,false).trigger==0&&RequestProbeSchedule(24000,true).marker==0);return 0;}
int ModeBoundary(){constexpr auto flags=0x1000000u|0x110000u|9;CHECK(ValidReloadRequestProbeConfig(flags,30000));
    for(auto bad:{0x400u,0x8000u,0x20000u,0x40000u,0x200000u,0x400000u,0x800000u})CHECK(!ValidReloadRequestProbeConfig(flags|bad,30000));
    CHECK(!ValidReloadRequestProbeConfig(flags,15000)&&!ValidReloadRequestProbeConfig(flags&~0x10000u,30000)&&!ValidReloadRequestProbeConfig(flags&~0x100000u,30000)&&!ValidReloadRequestProbeConfig(flags&~9u,30000)&&ValidReloadRequestProbeConfig(0,3000));return 0;}
int ReserveCohortProof(){std::array<ReloadFlowBoundary,3> a{};const auto id=Mock::id;std::array<int,3> capacity{8,8,8};
    for(unsigned n=0;n<3;++n){auto& b=a[n];b.owner=id.owner;b.branch=std::uint8_t(n);b.firing=id.firing[n];b.wrapperOffset=n==0?0x3c:n==1?0x40:0x10;b.loaded=6;b.reserve=21;b.current=b.next=2;
        if(n==2){b.serverPlayer=id.serverPlayer;b.serverSoldier=id.serverSoldier;b.serverItem=id.serverItem;}}
    CHECK(ReloadReserveCopiesAgree(id,a,a,capacity,capacity));
    for(unsigned mode=0;mode<10;++mode){auto b=a;auto caps=capacity;switch(mode){case 0:b[2].loaded++;break;case 1:b[1].reserve--;break;case 2:b[0].owner.space++;break;case 3:b[2].serverItem++;break;case 4:b[1].timer=.1f;break;case 5:caps[2]=7;break;case 6:b[2].soldierFlags^=0x10;break;case 7:b[0].snapshotSequence++;break;case 8:b[0].current=16;break;case 9:b[0].flagsA8=8;break;}CHECK(!ReloadReserveCopiesAgree(id,a,b,capacity,caps));}
    CHECK(!reloadFlowRuntime::ReadReserve());return 0;}
int ExactRetirementAndFreshReserve(){Mock::Reset();Bc2ReloadRequestProbe p(true,Mock::RecoveryApi());
    for(unsigned ms=0;ms<30000;ms+=10)Tick(p,ms);
    CHECK(Mock::starts==1&&Mock::submits==1&&Mock::cancels==1&&Mock::retireCalls==1&&Mock::reserveCalls==1);
    CHECK(Mock::retiredIdentity==Mock::id&&Mock::retiredCycle==Mock::control.cycle&&Mock::retiredAt>25000000000ll);
    const auto r=Report(p);const auto recovery=r.substr(r.find("\"retirement\""));
    CHECK(recovery.find("\"verified\":true")!=recovery.npos&&recovery.find("\"event\":1")!=recovery.npos&&
          recovery.find("\"loaded\":7,\"count\":20,\"capacity\":8")!=recovery.npos);
    return 0;}
int BadOrExpiredReceiptCannotProveRecovery(){for(unsigned mode=1;mode<=4;++mode){Mock::Reset();Mock::retireMode=mode;Bc2ReloadRequestProbe p(true,Mock::RecoveryApi());
    for(unsigned ms=0;ms<=30100;ms+=10)Tick(p,ms);
    const auto calls=Mock::retireCalls;Tick(p,31000);
    CHECK(calls&&calls<=200&&Mock::retireCalls==calls&&!Mock::reserveCalls&&Mock::submits==1&&Mock::cancels==1);
    const auto r=Report(p);const auto recovery=r.substr(r.find("\"retirement\""));CHECK(recovery.find("\"verified\":false")!=recovery.npos);
    }return 0;}
int ReserveMustFollowExactRetirement(){for(unsigned mode=1;mode<=3;++mode){Mock::Reset();Mock::reserveMode=mode;Bc2ReloadRequestProbe p(true,Mock::RecoveryApi());
    for(unsigned ms=0;ms<=24010;ms+=10)Tick(p,ms);
    CHECK(Mock::retireCalls==1&&Mock::reserveCalls==1);
    const auto r=Report(p);CHECK(r.substr(r.find("\"retirement\"")).find("\"verified\":false")!=r.npos);
    Mock::reserveMode=0;Tick(p,24040);
    CHECK(Mock::retireCalls==1&&Mock::reserveCalls==2&&Mock::submits==1);
    const auto done=Report(p);CHECK(done.substr(done.find("\"retirement\"")).find("\"verified\":true")!=done.npos);
    }return 0;}
}
int main(){if(SuccessfulThirtySeconds()||SourceCannotBeRefreshed()||OwnerLossAndBadAck()||NoFakeHeldOrDuplicateSeat()||DefaultOffAndSchedule()||ModeBoundary()||ReserveCohortProof()||ExactRetirementAndFreshReserve()||BadOrExpiredReceiptCannotProveRecovery()||ReserveMustFollowExactRetirement())return 1;std::printf("Ten bounded external request fixture groups passed; no native process used.\n");}
