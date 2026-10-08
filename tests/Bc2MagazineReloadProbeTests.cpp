#include "Bc2MagazineReloadProbe.h"
#include "Bc2MagazineReloadSession.h"
#include "NativeProbeConfig.h"
#include "Test.h"
#include <sstream>
using namespace fvr;using namespace fvr::bc2;
namespace {
struct Mock {
    static inline std::int64_t now=1000000000,submittedAt=0;
    static inline unsigned starts=0,submits=0,cancels=0,unseats=0;
    static inline bool active=false,unheld=false,ackTaken=false,gateTaken=false,badAck=false;
    static inline ReloadHoldIdentity id{{0x10000,0x20000,0x30000,0x40000,5,6,7},{0x50000,0x60000,0x70000},0x80000,0x90000,0xa0000};
    static inline ReloadCycleControl control{};
    static inline ReloadMagazineNativeRequest submitted{};
    static inline interaction::ManualReloadRequest unseat{};
    static void Reset(){now=1000000000;submittedAt=0;starts=submits=cancels=unseats=0;active=unheld=ackTaken=gateTaken=badAck=false;control={};submitted={};unseat={};}
    static std::optional<ReloadHoldIdentity> Identity()noexcept{return id;}
    static bool Start(const ReloadCycleControl& c,const interaction::ManualReloadRequest& r)noexcept{++starts;active=true;control=c;unseat=r;return r.operation==interaction::ReloadOperation::UnseatMagazine;}
    static bool Keep(const ReloadCycleControl& c)noexcept{if(!active||control.deadlineNs<=now)return false;control=c;return true;}
    static std::optional<ReloadMagazineLease> Lease(const ReloadHoldIdentity&,std::uint64_t cycle)noexcept {
        if(!active||submittedAt)return {};
        return ReloadMagazineLease{id,cycle,std::uint64_t(now),now,now+50000000,28,174,30,true,!unheld};
    }
    static std::optional<ReloadMagazineGateAcknowledgement> Gate(const ReloadHoldIdentity& owner,std::uint64_t cycle)noexcept {
        const auto l=Lease(owner,cycle);if(!l||!l->allThreeHeld||gateTaken)return {};gateTaken=true;++unseats;
        return ReloadMagazineGateAcknowledgement{{unseat.id,unseat.owner,unseat.operation,interaction::ReloadAcknowledgement::Applied},*l};
    }
    static bool Submit(const ReloadMagazineNativeRequest& r)noexcept{++submits;submitted=r;submittedAt=now;return true;}
    static std::optional<ReloadMagazineAckEvidence> Ack(const ReloadHoldIdentity&,std::uint64_t cycle)noexcept {
        if(!submittedAt||now-submittedAt<2800000000||ackTaken)return {};ackTaken=true;now+=1000;
        return ReloadMagazineAckEvidence{{{submitted.request.id,submitted.request.owner,interaction::ReloadOperation::SeatMagazine,interaction::ReloadAcknowledgement::Applied},id,badAck?cycle+1:cycle,100,777,28,174,30,172},now,now+50000000,true};
    }
    static void Cancel()noexcept{++cancels;active=false;}
    static std::int64_t Clock()noexcept{return now;}
    static MagazineReloadProbeApi Api(){return {Identity,Start,Keep,Lease,Submit,Ack,Cancel,nullptr,Clock,nullptr,nullptr,Gate};}
};
interaction::HandInteractionOwner Physical(){return {(std::uint64_t(Mock::id.owner.weak)<<32)|Mock::id.owner.soldier,5,17,7};}
interaction::InputFrame Input(std::uint64_t sequence,float marker){interaction::InputFrame f;f.generation=sequence;f.predictedNs=1;f.spaceGeneration=7;f.focused=f.headValid=true;f.hands[0].trigger=marker;
    for(auto& h:f.hands){h.active=interaction::Components;h.gripTracked=h.aimTracked=true;}return f;}
void Tick(Bc2MagazineReloadProbe& p,unsigned ms,float marker=-1){Mock::now=1000000000+std::int64_t(ms)*1000000;const auto f=Input(ms+1,marker<0?MagazineProbeSchedule(ms).marker:marker);p.Tick(f,Physical(),Mock::now,Mock::now+100000000,Mock::now);}
std::string Report(Bc2MagazineReloadProbe& p){std::ostringstream s;p.Report(s);return s.str();}
int InsertionAndCancellation(){for(bool cancel:{false,true}){Mock::Reset();Bc2MagazineReloadProbe p(true,Mock::Api(),cancel);
    for(unsigned ms=0;ms<30000;ms+=10)Tick(p,ms);
    CHECK(Mock::starts==1&&Mock::unseats==1&&Mock::cancels==1&&Mock::submits==(cancel?0u:1u));
    const auto r=Report(p);CHECK(r.find("\"reason\":0")!=r.npos&&r.find("\"dropped\":0")!=r.npos&&r.find("\"unseat_acknowledged\":true")!=r.npos);
    if(!cancel){CHECK(Mock::submittedAt==10000000000ll&&Mock::submitted.reservedUnits==2&&Mock::submitted.request.operation==interaction::ReloadOperation::SeatMagazine);
        CHECK(r.find("\"acknowledged\":true")!=r.npos);}
    }return 0;}
int OwnershipAndNoFakeGate(){for(unsigned mode=0;mode<3;++mode){Mock::Reset();Bc2MagazineReloadProbe p(true,Mock::Api());
    if(mode==0)Mock::unheld=true;if(mode==1)Mock::badAck=true;
    for(unsigned ms=0;ms<15000;ms+=10){Tick(p,ms);if(mode==2&&ms==5800){const auto f=Input(5821,.25f);auto owner=Physical();++owner.equipGeneration;
        Mock::now+=10000000;p.Tick(f,owner,Mock::now,Mock::now+100000000,Mock::now);}}
    CHECK(Mock::cancels==1&&Report(p).find("\"acknowledged\":false")!=std::string::npos);
    CHECK(Mock::submits==(mode==1?1u:0u));}return 0;}
int DefaultScheduleAndSchema(){Mock::Reset();Bc2MagazineReloadProbe p;Tick(p,5800);CHECK(!Mock::starts);
    unsigned reload=0,seats=0;for(unsigned ms=0;ms<30000;++ms){auto s=MagazineProbeSchedule(ms);reload+=s.reload;seats+=s.marker==1;}
    CHECK(reload==200&&seats==200&&MagazineProbeSchedule(14000).marker==0);
    NativeProbeConfig c;CHECK(c.bytes==1200&&sizeof(c)==1200&&c.ordinaryResourceInput==0&&c.m95OrdinaryBoltInputCycles==0&&c.m95StockShot==0&&c.m95PhysicalBoltCycles==0&&c.magazineReloadSession==0&&c.boatHeadAim==BoatHeadAimMode::Disabled&&c.bodyHolsterDiagnostic==BodyHolsterDiagnosticProfile::Disabled);
    CHECK(ValidOrdinaryResourceInputSession(0,0,0,0,0,0,0,0,0,0));
    CHECK(ValidOrdinaryResourceInputSession(1,0x12197809u,60000,3,0,0,0,0,0,0));
    for(unsigned bit=0;bit<32;++bit)CHECK(!ValidOrdinaryResourceInputSession(1,0x12197809u^(1u<<bit),60000,3,0,0,0,0,0,0));
    CHECK(!ValidOrdinaryResourceInputSession(2,0x12197809u,60000,3,0,0,0,0,0,0));
    CHECK(!ValidOrdinaryResourceInputSession(1,0x12197809u,30000,3,0,0,0,0,0,0));
    CHECK(!ValidOrdinaryResourceInputSession(1,0x12197809u,60000,7,0,0,0,0,0,0));
    for(unsigned n=0;n<6;++n){unsigned x[6]={};x[n]=1;
        CHECK(!ValidOrdinaryResourceInputSession(1,0x12197809u,60000,3,x[0],x[1],x[2],x[3],x[4],x[5]));}
    CHECK(ValidM95OrdinaryBoltInputSession(0,0,0,0,0,0,0,0,0));
    for(auto cycles:{1u,2u}){
        CHECK(ValidM95OrdinaryBoltInputSession(cycles,0x12197809u,30000,3,0,0,0,0,0));
        for(unsigned bit=0;bit<32;++bit)CHECK(!ValidM95OrdinaryBoltInputSession(cycles,0x12197809u^(1u<<bit),30000,3,0,0,0,0,0));
        for(unsigned conflict=0;conflict<8;++conflict)CHECK(!ValidM95OrdinaryBoltInputSession(
            conflict==0?3u:cycles,0x12197809u,conflict==1?15000u:30000u,conflict==2?7u:3u,
            conflict==3,conflict==4,conflict==5,conflict==6,conflict==7));
    }
    CHECK(ValidM95PhysicalBoltSession(0,0,0,0,0,0,0,0));
    for(auto cycles:{1u,2u}){
        CHECK(ValidM95PhysicalBoltSession(cycles,0x197809u,30000,0,0,0,0,0));
        CHECK(!ValidM95PhysicalBoltSession(cycles,0x197809u,15000,0,0,0,0,0));
        CHECK(!ValidM95PhysicalBoltSession(cycles,0x197809u,30000,3,0,0,0,0));
        CHECK(!ValidM95PhysicalBoltSession(cycles,0x197809u,30000,0,0,0,0,1));
        for(unsigned bit=0;bit<32;++bit)CHECK(!ValidM95PhysicalBoltSession(cycles,0x197809u^(1u<<bit),30000,0,0,0,0,0));
    }
    CHECK(!ValidM95PhysicalBoltSession(3,0x197809u,30000,0,0,0,0,0));
    constexpr auto flags=9u|0x197800u;CHECK(ValidMagazineReloadSession(1,flags,30000)&&ValidMagazineReloadSession(2,flags|0x200u,30000));
    CHECK(!ValidMagazineReloadSession(3,flags,30000)&&!ValidMagazineReloadSession(1,flags,15000));
    for(auto bad:{0x400u,0x1000000u,0x2000000u,0x200000u,0x10000000u,0x80000000u})CHECK(!ValidMagazineReloadSession(1,flags|bad,30000));
    for(auto missing:{0x800u,0x1000u,0x2000u,0x4000u,0x10000u,0x80000u,0x100000u})CHECK(!ValidMagazineReloadSession(1,flags&~missing,30000));
    CHECK(ValidMagazineReloadSession(0,0,3000));
    constexpr auto boat=9u|0x3800u;
    CHECK(ValidBoatHeadAimConfig(BoatHeadAimMode::Disabled,0,0));
    for(auto mode:{BoatHeadAimMode::AimOnly,BoatHeadAimMode::AimAndFire}){
        CHECK(ValidBoatHeadAimConfig(mode,boat,0));
        CHECK(ValidBoatHeadAimConfig(mode,9u|0x197800u|0x2000000u|0x10000000u|0x200000u|0x600u,3));
        for(auto bit:{0x800u,0x1000u,0x2000u})CHECK(!ValidBoatHeadAimConfig(mode,boat&~bit,0));
        for(auto bit:{0x100u,0x8000u,0x20000u,0x40000u,0x400000u,0x800000u,0x1000000u,0x4000000u,0x8000000u,0x20000000u,0x40000000u,0x80000000u})CHECK(!ValidBoatHeadAimConfig(mode,boat|bit,0));
        for(auto m:{1u,2u,4u,99u})CHECK(!ValidBoatHeadAimConfig(mode,boat,m));
        CHECK(!ValidBoatHeadAimConfig(mode,boat&~1u,0));
    }
    CHECK(!ValidBoatHeadAimConfig(static_cast<BoatHeadAimMode>(3),boat,0));return 0;}
}
int main(){if(InsertionAndCancellation()||OwnershipAndNoFakeGate()||DefaultScheduleAndSchema())return 1;std::puts("Three XM8 probe routing, ownership, schedule and schema groups passed");}
