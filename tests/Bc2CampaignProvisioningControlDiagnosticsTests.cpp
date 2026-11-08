#include "Bc2CampaignProvisioning.h"
#include "Bc2CampaignProvisioningInputGate.h"
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <latch>
#include <thread>

namespace cp=fvr::bc2::campaignProvisioning;
#define CHECK(x) do {if(!(x)){std::fprintf(stderr,"FAIL line %d: %s\n",__LINE__,#x);std::exit(1);}}while(false)

struct Fixture {
    cp::Service service;
    cp::InputGate gate;
    cp::ProcessIdentity process{};
    unsigned nativeCalls=0;
    static constexpr std::int64_t Now=1000000000,Deadline=Now+2000000000ll;
    Fixture(){
        process.pid=3;process.creation=4;process.session=5;process.image[0]=1;process.executable[0]=u'g';
        const cp::policy::Memory memory{this,
            [](void* p,std::uint32_t,void*,std::size_t){++static_cast<Fixture*>(p)->nativeCalls;return false;},
            [](void* p,std::uint32_t,const char*){++static_cast<Fixture*>(p)->nativeCalls;return false;}};
        const cp::policy::Host host{this,
            [](void* p,const cp::policy::Owner&,std::int64_t){++static_cast<Fixture*>(p)->nativeCalls;return false;},
            [](void* p,std::uint32_t,std::uint32_t,std::uint32_t,std::uint32_t&){++static_cast<Fixture*>(p)->nativeCalls;return false;}};
        const cp::LifetimeHost lifetime{this,
            [](void* p,const fvr::bc2::ReloadServerSnapshot&,std::uint64_t)noexcept{
                ++static_cast<Fixture*>(p)->nativeCalls;return cp::LifetimeHost::Exclusion::Refused;},
            [](void* p,const fvr::bc2::ReloadServerSnapshot&,std::uint32_t,std::uint32_t,std::uint64_t,std::int64_t)noexcept{
                ++static_cast<Fixture*>(p)->nativeCalls;return false;},
            [](void* p,const fvr::bc2::ReloadServerSnapshot&,std::uint64_t)noexcept{
                ++static_cast<Fixture*>(p)->nativeCalls;return false;}};
        CHECK(service.Configure(process,memory,host,lifetime,true));
    }
    cp::Request Request(std::uint64_t nonce=1,std::int64_t now=Now)const{
        cp::Request request{};request.bytes=sizeof(request);request.process=process;
        request.nonce=nonce;request.job=7;request.sourceSequence=9;request.sourceObservedNs=now;
        request.issuedNs=now;request.deadlineNs=now+2000000000ll;request.source.session=process.session;
        return request;
    }
    void Begin(){CHECK(service.Submit(Request(),Now));}
    cp::policy::Boundary Boundary(std::int64_t now=Now)const{
        cp::policy::Boundary boundary{};boundary.sequence=11;boundary.nowNs=now;
        boundary.thread=123;boundary.returnAddress=0x6b0999;boundary.seat=2;
        boundary.nativeFiringDepth=3;boundary.seatDepth=4;return boundary;
    }
    void Record(const cp::InputGate::Control& control,std::int64_t now=Now){
        service.RecordControlRefusal(service.AdmissionToken(),Boundary(now),control.Acquisition());
    }
};

int main(){unsigned groups=0;
    // Genuine same-thread nested callbacks: acquired recursive lock, exact
    // callback kinds/depths, and constructor snapshot retained after unwinding.
    {Fixture f;f.Begin();cp::ControlAcquisition captured;
        {cp::InputGate::Update outer(f.gate);
            {cp::InputGate::Gather gather(f.gate);cp::InputGate::Update inner(f.gate);
                cp::InputGate::Control control(f.gate);CHECK(!control);
                captured=control.Acquisition();CHECK(captured.reason==cp::ControlRefusalReason::ActiveCallbacks);
                CHECK(captured.callbacks==3&&captured.updateDepth==2&&captured.gatherDepth==1);f.Record(control);}
            cp::InputGate::Control control(f.gate);CHECK(!control);f.Record(control);
            CHECK(control.Acquisition().callbacks==1&&control.Acquisition().gatherDepth==0);}
        cp::InputGate::Control admitted(f.gate);CHECK(admitted);
        CHECK(admitted.Acquisition().reason==cp::ControlRefusalReason::None);f.Record(admitted);
        const auto out=f.service.ReadControlRefusals();const auto& first=out.details.first[1];
        CHECK(!out.details.counts[0]&&out.details.counts[1]==2);
        CHECK(first.acquisition.callbacks==captured.callbacks&&first.acquisition.updateDepth==2&&first.acquisition.gatherDepth==1);
        CHECK(first.thread==123&&first.boundaryNs==f.Now&&first.seatSequence==11&&first.caller==0x6b0999);
        CHECK(first.seat==2&&first.nativeFiringDepth==3&&first.seatDepth==4);
        CHECK(!f.nativeCalls&&!f.service.BlocksInput());++groups;
    }
    // Real cross-thread lock contention. Never read another thread's mutable
    // callback counts; zeros here are explicitly UNKNOWN in this reason.
    {Fixture f;f.Begin();std::latch entered(1),release(1);
        std::thread callback([&]{cp::InputGate::Update update(f.gate);cp::InputGate::Gather gather(f.gate);
            entered.count_down();release.wait();});
        entered.wait();{cp::InputGate::Control control(f.gate);CHECK(!control);
            const auto observation=control.Acquisition();CHECK(observation.reason==cp::ControlRefusalReason::MutexUnavailable);
            CHECK(!observation.callbacks&&!observation.updateDepth&&!observation.gatherDepth);f.Record(control);}
        release.count_down();callback.join();cp::InputGate::Control control(f.gate);CHECK(control);
        const auto out=f.service.ReadControlRefusals();CHECK(out.details.counts[0]==1&&!out.details.counts[1]);
        CHECK(out.details.first[0].thread==123&&!f.nativeCalls);++groups;
    }
    // Disabled scopes add no depth; standalone Gather still identifies
    // same-thread recursion, independent of Update nesting or exclusion.
    {Fixture f;f.Begin();cp::InputGate::Update disabledUpdate(f.gate,false);cp::InputGate::Gather disabledGather(f.gate,false);
        {cp::InputGate::Control control(f.gate);CHECK(control);f.Record(control);}
        {cp::InputGate::Gather gather(f.gate);cp::InputGate::Control control(f.gate);CHECK(!control);f.Record(control);
            CHECK(control.Acquisition().callbacks==1&&!control.Acquisition().updateDepth&&control.Acquisition().gatherDepth==1);}
        CHECK(f.service.ReadControlRefusals().details.counts[1]==1&&!f.nativeCalls);++groups;
    }
    // Bounds, unchanged original arm, terminal immutability, stale scope,
    // exact shared metadata and no native/policy call caused by diagnostics.
    {Fixture f;cp::InputGate::Update update(f.gate);cp::InputGate::Control control(f.gate);CHECK(!control);
        f.Record(control);CHECK(!f.service.ReadControlRefusals().details.counts[1]);f.Begin();
        f.Record(control,f.Now-1);f.Record(control,f.Deadline);
        CHECK(!f.service.ReadControlRefusals().details.counts[1]);
        const auto before=cp::ToWire(f.service.Read());
        for(unsigned n=0;n<100;++n)f.Record(control,f.Now+n);
        const auto after=cp::ToWire(f.service.Read());CHECK(std::memcmp(&before,&after,sizeof(before))==0);
        const auto live=f.service.ReadControlRefusals();CHECK(live.details.counts[1]==31&&live.details.saturatedMask==2);
        CHECK(live.details.first[1].boundaryNs==f.Now&&live.admission.deadlineNs==f.Deadline);
        CHECK(!live.admission.counts[unsigned(cp::AdmissionEvent::InputGateRefused)]); // Existing counter is not reinterpreted.
        f.service.Expire(f.Deadline+12850800);const auto frozen=f.service.ReadControlRefusals();
        CHECK(frozen.admission.frozen&&frozen.admission.frozenNs==f.Deadline+12850800);
        CHECK(frozen.admission.status==unsigned(cp::Status::Expired));
        f.Record(control,f.Now);f.service.Stop();const auto stopped=f.service.ReadControlRefusals();
        CHECK(std::memcmp(&frozen,&stopped,sizeof(frozen))==0&&!f.nativeCalls);++groups;
    }
    {Fixture f;f.Begin();const auto oldToken=f.service.AdmissionToken();
        cp::InputGate::Gather gather(f.gate);cp::InputGate::Control control(f.gate);f.Record(control);
        f.service.Expire(f.Deadline);CHECK(f.service.Submit(f.Request(2,f.Deadline),f.Deadline));
        f.service.RecordControlRefusal(oldToken,f.Boundary(f.Deadline),control.Acquisition());
        CHECK(!f.service.ReadControlRefusals().details.counts[1]);f.Record(control,f.Deadline);
        const auto out=f.service.ReadControlRefusals();const auto admission=f.service.ReadAdmission();
        CHECK(out.admission.nonce==2&&out.details.counts[1]==1);
        CHECK(std::memcmp(&out.admission,&admission,sizeof(admission))==0&&!f.nativeCalls);++groups;
    }
    // Diagnostic scope exhaustion cannot revive a prior record or disable the
    // request; frozen metadata is still joined to the new accepted operation.
    {Fixture f;cp::InputGate::Update update(f.gate);cp::InputGate::Control control(f.gate);
        for(unsigned n=1;n<=cp::AdmissionCounters::MaxScopes;++n){
            const auto now=f.Now+std::int64_t(n-1)*2000000000ll;
            CHECK(f.service.Submit(f.Request(n,now),now));f.Record(control,now);f.service.Expire(now+2000000000ll);}
        const auto now=f.Now+std::int64_t(cp::AdmissionCounters::MaxScopes)*2000000000ll;
        CHECK(f.service.Submit(f.Request(cp::AdmissionCounters::MaxScopes+1,now),now));f.Record(control,now);
        auto out=f.service.ReadControlRefusals();CHECK(!out.admission.scope&&out.admission.frozen&&!out.admission.frozenNs);
        CHECK(!out.details.counts[0]&&!out.details.counts[1]);
        f.service.Expire(now+2000000000ll);out=f.service.ReadControlRefusals();
        CHECK(out.admission.status==unsigned(cp::Status::Expired)&&out.admission.frozenNs==now+2000000000ll);
        CHECK(!f.nativeCalls);++groups;
    }
    static_assert(sizeof(cp::Request)==1264&&sizeof(cp::WireReceipt)==1272&&sizeof(cp::AdmissionReceipt)==1200);
    static_assert(sizeof(cp::ControlRefusalReceipt)==1344);
    std::printf("%u Control-refusal diagnostic groups passed\n",groups);return 0;
}
