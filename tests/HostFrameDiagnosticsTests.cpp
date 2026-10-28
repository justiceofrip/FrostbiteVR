#include "fvr/xr/HostSourceImportCache.h"
#include "fvr/graphics/SceneDepthImportPolicy.h"
#include "Test.h"
#include "fvr/xr/HostFrameDiagnostics.h"
#include "fvr/xr/HostCopyLease.h"
#include "fvr/xr/HostComposedCopyService.h"
#include "fvr/xr/PrivateGpuOrderedPresent.h"
#include <cstring>
#include "fvr/runtime/FrameProvider.h"
#include <sstream>
#include <memory>
using namespace fvr::xr;
namespace {
HostFrameObservation Frame(std::uint64_t n=1){
    HostFrameObservation f;f.frame=n;f.startedNs=std::int64_t(n)*10000000;f.endedNs=f.startedNs+1000000;
    f.predictedNs=std::int64_t(n)*11000000;f.space=1;f.shouldRender=f.focused=f.present=true;
    f.headValid=f.viewsValid=f.trackingValid=f.provider=true;f.providerQueried=true;
    f.worldLayer=f.endAttempted=f.endSucceeded=true;f.layers=1;return f;
}
int LayersAndReasons(){
    HostFrameDiagnostics d;auto f=Frame();d.Observe(f);
    f=Frame(2);f.worldLayer=false;f.menu=true;f.layers=f.menuLayers=2;d.Observe(f);
    CHECK(d.noWorldFrames==1&&d.zeroLayerFrames==0&&d.menuOnlyFrames==1);
    CHECK((d.Row(1).reasons&HostMenu)!=0);
    f=Frame(3);f.worldLayer=false;f.layers=0;f.retained=true;f.retainedGeneration=7;f.retainedSpace=1;
    f.retainedAgeNs=250000001;d.Observe(f);
    CHECK(d.zeroLayerFrames==1&&d.noWorldReasonFrames[10]==1&&d.noWorldReasonFrames[9]==2);
    f=Frame(4);f.worldLayer=false;f.layers=0;f.shouldRender=false;d.Observe(f);
    CHECK(d.Row(3).reasons&HostShouldRender);
    f=Frame(5);f.worldLayer=false;f.layers=0;f.focused=false;d.Observe(f);CHECK(d.Row(4).reasons&HostFocus);
    f=Frame(6);f.worldLayer=false;f.layers=0;f.headValid=false;d.Observe(f);CHECK(d.Row(5).reasons&HostHead);
    f=Frame(7);f.worldLayer=false;f.layers=0;f.viewsValid=false;d.Observe(f);CHECK(d.Row(6).reasons&HostViews);
    f=Frame(8);f.worldLayer=false;f.layers=0;f.space=2;d.Observe(f);CHECK(d.Row(7).reasons&HostSpace);
    f=Frame(9);f.space=2;d.Observe(f);CHECK(d.Row(8).reasons==0&&d.Row(8).worldLayer);
    // Failed EndFrame is recorded but cannot be called a successful zero-layer submission.
    f=Frame(10);f.worldLayer=false;f.layers=0;f.endSucceeded=false;f.endResult=-1;
    const auto before=d.zeroLayerFrames;d.Observe(f);CHECK(d.failedFrames==1&&d.zeroLayerFrames==before);
    d.CleanupEnd(100,200,-1,false);CHECK(d.cleanupEndAttempts==1&&d.cleanupZeroLayerFrames==0);
    d.CleanupEnd(300,400,0,true);CHECK(d.cleanupEndAttempts==2&&d.cleanupZeroLayerFrames==1);
    return 0;
}
int PublicationAndSlowStages(){
    HostFrameDiagnostics d;d.Input(90,900,true,true);d.Input(91,901,true,false);d.Input(92,902,false,false);
    auto f=Frame();f.inputAttempted=f.inputKnown=true;f.inputResult=1;
    f.stageNs[unsigned(HostStage::WaitFrame)]=23000000;
    f.stageNs[unsigned(HostStage::SwapchainWait)]=19000000;d.Observe(f);
    CHECK(d.inputSuccesses==1&&d.inputFailures==1&&d.inputUnknown==1);
    CHECK(d.Row(0).lastInputSuccessGeneration==90&&d.Row(0).lastInputSuccessReturnedNs==900);
    CHECK(d.Row(0).stageNs[unsigned(HostStage::WaitFrame)]==23000000);
    CHECK(d.Row(0).stageNs[unsigned(HostStage::SwapchainWait)]==0);
    CHECK(d.maxStageNs[unsigned(HostStage::SwapchainWait)]==19000000);
    f=Frame(2);f.inputAttempted=f.inputKnown=true;f.inputResult=1;d.Observe(f);CHECK(d.count==1);
    // Inter-frame stalls are separate from xrWaitFrame duration.
    f=Frame(20);f.inputAttempted=f.inputKnown=true;f.inputResult=1;d.Observe(f);
    CHECK(d.count==2&&d.Row(1).gapBeforeNs>=50000000);
    // Changes between failure results remain observable without a success edge.
    f=Frame(21);f.inputAttempted=f.inputKnown=true;f.inputResult=4;d.Observe(f);CHECK(d.count==3);
    d.Input(94,1000000900,true,true);CHECK(d.maxInputSuccessGapNs==1000000000);
    return 0;
}
int LegacyProviderContract(){
    struct Legacy final:fvr::runtime::IFrameProvider {
        unsigned calls=0;
        bool TryGetPair(const fvr::runtime::PresentationRequirements&,const fvr::runtime::TrackingFrame&,
            fvr::graphics::TextureDescriptor&,fvr::graphics::PairTicket&)noexcept override{return false;}
        void PairConsumed(const fvr::graphics::PairTicket&,bool)noexcept override{}
        void UpdateInput(const fvr::interaction::InputFrame&)noexcept override{++calls;}
    } legacy;
    legacy.ServiceFeedback(); // Optional local contract remains compatible.
    const auto result=legacy.UpdateInputObserved({});
    CHECK(legacy.calls==1&&!result.known&&!result.succeeded);return 0;
}
std::int64_t fakeNow=1000000000;
std::int64_t Clock()noexcept{return fakeNow;}
int BoundedAndTimer(){
    HostFrameDiagnostics d;
    for(unsigned n=1;n<=300;++n){auto f=Frame(n);{HostStageTimer t(f,HostStage::EndFrame,Clock);fakeNow+=30000000;}
        CHECK(f.stageNs[unsigned(HostStage::EndFrame)]==30000000);d.Observe(f);}
    CHECK(d.frames==300&&d.count==256&&d.recorded==300&&d.dropped==44);
    CHECK(d.Row(0).frame==45&&d.Row(255).frame==300);
    CHECK(d.stageAggregates[unsigned(HostStage::EndFrame)].frames==300);
    CHECK(d.stageAggregates[unsigned(HostStage::EndFrame)].totalNs==9000000000ull);
    std::ostringstream json;d.Write(json);CHECK(json.str().find("\"zero_layer_frames\":0")!=std::string::npos);
    CHECK(json.str().find("\"slow_stage_ns\":[")!=std::string::npos);
    CHECK(json.str().size()<384000);
    return 0;
}

int CopyPhasesBelowSlowThreshold(){
    // Existing stage indices are persisted in historical diagnostic arrays.
    CHECK(unsigned(HostStage::EndFrame)==17&&unsigned(HostStage::ColorPoll)==18);
    CHECK(std::string(HostStageNames[unsigned(HostStage::ColorPoll)])=="color_poll");
    HostFrameDiagnostics d;d.Observe(Frame());
    auto f=Frame(2);f.copySequence=41;f.copySubmitted=f.copyPending=true;
    f.copyResult=1;f.copyError=1;f.copyPendingAgeNs=1000;
    f.stageNs[unsigned(HostStage::ColorCopy)]=5000;d.Observe(f);
    CHECK(d.count==2&&d.Row(1).copySubmitted&&d.Row(1).copySequence==41);
    // Submit-to-pending transition is retained without a slow stage or layer change.
    f=Frame(3);f.copySequence=41;f.copyPending=true;f.copyResult=1;f.copyError=1;
    f.copyPendingAgeNs=10000000;f.stageNs[unsigned(HostStage::ColorPoll)]=500;d.Observe(f);
    CHECK(d.count==3&&d.Row(2).copyPending&&!d.Row(2).copySubmitted);
    // Repeated pending polls update totals/maximum, not the fixed-size ring.
    f.frame=4;f.startedNs+=10000000;f.endedNs+=10000000;f.copyPendingAgeNs=20000000;d.Observe(f);
    CHECK(d.count==3&&d.copyPendingFrames==3&&d.maxCopyPendingAgeNs==20000000);
    CHECK(d.maxStageNs[unsigned(HostStage::ColorPoll)]==500);
    // Same phase and result, changed HRESULT: retain diagnostic failure detail.
    f.frame=5;f.startedNs+=10000000;f.endedNs+=10000000;f.copyError=0x887a0005u;d.Observe(f);
    CHECK(d.count==4&&d.Row(3).copyError==0x887a0005u);
    f=Frame(6);f.copySequence=41;f.copyCompleted=true;f.copyPendingAgeNs=30000000;d.Observe(f);
    CHECK(d.count==5&&d.Row(4).copyCompleted&&!d.Row(4).copyPending);
    CHECK(d.copySubmittedFrames==1&&d.copyCompletedFrames==1&&d.maxCopyPendingAgeNs==30000000);
    // Repeated same phase with a new exact sequence must also remain visible.
    f=Frame(7);f.copySequence=42;f.copyCompleted=true;d.Observe(f);
    CHECK(d.count==6&&d.Row(5).copySequence==42&&d.copyCompletedFrames==2);
    f=Frame(8);f.copySequence=43;f.copyRetired=f.copyCancelled=true;
    f.copyResult=4;f.copyError=0x80004004u;d.Observe(f);
    CHECK(d.count==7&&d.copyRetiredFrames==1&&d.copyCancelledFrames==1);
    d.Observe(Frame(9)); // No copy observation must not erase the last outcome.
    CHECK(d.lastCopyResult==4&&d.lastCopyError==0x80004004u&&d.count==8);
    std::ostringstream json;json<<std::boolalpha;d.Write(json);const auto text=json.str();
    CHECK(text.find("\"copy_pending_frames\":4")!=std::string::npos);
    CHECK(text.find("\"copy_completed_frames\":2")!=std::string::npos);
    CHECK(text.find("\"max_copy_pending_age_ns\":30000000")!=std::string::npos);
    CHECK(text.find("\"copy_cancelled\":true")!=std::string::npos);
    CHECK(text.find("\"copy_sequence\":43")!=std::string::npos);
    CHECK(text.find("\"last_copy_error\":2147500036")!=std::string::npos);
    return 0;
}

struct LeaseProvider final:fvr::runtime::IFrameProvider {
    unsigned positive=0,negative=0,inputs=0,suspends=0,flushes=0;
    bool gateBusy=true,queued=true,deferred=false,discardQueued=false,released=false;
    fvr::graphics::PairTicket received{};
    bool TryGetPair(const fvr::runtime::PresentationRequirements&,const fvr::runtime::TrackingFrame&,
        fvr::graphics::TextureDescriptor&,fvr::graphics::PairTicket&)noexcept override{return false;}
    void UpdateInput(const fvr::interaction::InputFrame&)noexcept override{++inputs;}
    void PairConsumed(const fvr::graphics::PairTicket& ticket,bool okay)noexcept override{
        received=ticket;if(okay){++positive;deferred=true;}else ++negative;
    }
    void Suspend()noexcept override{++suspends;discardQueued=true;}
    void ServiceFeedback()noexcept override{
        ++flushes;if(gateBusy)return;
        if(discardQueued)queued=false; // Cancellation applies before deferred ACK.
        if(deferred){deferred=false;released=true;}
    }
};
int PostInputComposedServicePolicy(){
    struct Pending final:HostCopyLease {
        bool gpuOrdered=true;
        Pending(LeaseProvider& p,const fvr::graphics::PairTicket& t):HostCopyLease(p,t){}
    };
    // Outcomes are deliberately controlled here, not forced GPU timing. The
    // production helper, actual HostCopyLease ACK/destructor and diagnostics
    // are exercised together. Actual GPU pixel/drain coverage remains separate.
    enum class Query {Busy,Ready,Fault};
    for(unsigned scenario=0;scenario<5;++scenario){
        LeaseProvider p;fvr::graphics::PairTicket old{};old.sequence=41;old.frameId=42;
        old.resourceEpoch=43;old.trackingGeneration=44;old.spaceGeneration=45;old.predictedNs=46000000;old.session[0]=47;
        auto pending=std::make_unique<Pending>(p,old);CHECK(pending->MarkComposed());
        auto f=Frame(2);f.SetCopyTicket(old);f.copyPending=f.copyComposed=true;
        f.copySubmittedNs=10001000;f.copyComposedNs=10002000;f.copyPreWaitPolled=true;
        PrivateGpuOrderedPresentReport report;unsigned polls=0,archives=0;bool bad=false;
        Query result=Query::Busy;
        const auto poll=[&]{
            ++polls;
            if(!pending||std::memcmp(&pending->ticket,&old,sizeof(old)))bad=true;
            if(result==Query::Ready){
                if(scenario==2&&(!pending->Cancelled()||!p.discardQueued))bad=true;
                f.copyCompleted=pending->Complete();f.copyPending=false;f.copyCompletedNs=20003000;
            }else if(result==Query::Fault){pending.reset();f.copyPending=false;f.copyRetired=true;}
        };
        poll(); // Busy before WaitFrame: no ACK, still exact source owner.
        CHECK(polls==1&&pending->Copying()&&p.positive==0&&p.negative==0);
        p.UpdateInput({});f.inputStartedNs=20001000;f.inputReturnedNs=20002000;
        if(scenario==2)pending->CancelPresentation(); // Menu/recenter before retry.
        result=scenario==1?Query::Busy:scenario==3?Query::Fault:Query::Ready;
        const auto archive=[&](const Pending& completed){
            ++archives;++report.postInputRetirements;f.copyPostInputCompleted=true;
            if(!completed.Ready()||std::memcmp(&completed.ticket,&old,sizeof(old)))bad=true;
            return scenario!=4&&f.PreserveCompletedCopy();
        };
        const auto service=[&]{return ServiceComposedCopyAfterInput(pending,[&]{
            ++report.postInputPolls;f.copyPostInputPolled=true;poll();
        },archive);};
        const auto outcome=service();CHECK(!bad&&polls==2&&p.inputs==1&&report.postInputPolls==1);
        if(scenario==1){
            CHECK(outcome==ComposedCopyServiceResult::Pending&&pending&&pending->Copying());
            CHECK(p.positive==0&&p.negative==0&&archives==0&&!f.hasPriorCopy);
            pending.reset();CHECK(p.negative==1); // Later ordinary shutdown retire.
        }else if(scenario==3){
            CHECK(outcome==ComposedCopyServiceResult::Retired&&!pending);
            CHECK(p.positive==0&&p.negative==1&&archives==0&&f.copyRetired&&!f.hasPriorCopy);
        }else{
            CHECK(outcome==(scenario==4?ComposedCopyServiceResult::ArchiveFailed:ComposedCopyServiceResult::Completed));
            CHECK(!pending&&p.positive==1&&p.negative==0&&archives==1&&report.postInputRetirements==1);
            CHECK(std::memcmp(&p.received,&old,sizeof(old))==0);
            p.ServiceFeedback();CHECK(p.deferred&&!p.released); // Busy IPC keeps exact ACK.
            p.gateBusy=false;p.ServiceFeedback();CHECK(!p.deferred&&p.released&&p.queued==(scenario!=2));
            if(scenario!=4){
                CHECK(f.hasPriorCopy&&f.priorCopy.copyCompleted&&f.priorCopy.copyPostInputCompleted);
                CHECK(f.priorCopy.copyPostInputPolled&&!f.priorCopy.copyPreWaitCompleted&&!f.priorCopy.copyAfterEndCompleted);
                CHECK(f.priorCopy.copyCompletedNs>=f.inputReturnedNs&&std::memcmp(&f.priorCopy.copyTicket,&old,sizeof(old))==0);
                auto next=old;++next.resourceEpoch;++next.trackingGeneration;next.session[0]=48;
                f.SetCopyTicket(next);f.copySubmitted=f.copyPending=true;f.copySubmittedNs=20004000;
                HostFrameDiagnostics d;d.Observe(f);
                CHECK(d.copyCompletedObservations==1&&d.copyPostInputPollFrames==1&&d.copyPostInputCompletedFrames==1);
                CHECK(d.copyPreWaitCompletedFrames==0&&d.copyAfterEndCompletedFrames==0&&d.copyDualTicketFrames==1);
                CHECK(!d.copyObservationOverflowFrames&&d.Row(0).priorCopy.copyTicket.session[0]==47&&d.Row(0).copyTicket.session[0]==48);
                std::ostringstream json;d.Write(json);CHECK(json.str().find("\"copy_post_input_completed\":1")!=std::string::npos);
                CHECK(json.str().find("\"copy_post_input_completed_frames\":1")!=std::string::npos);
            }else CHECK(f.copyCompleted&&!f.hasPriorCopy); // Caller must defer admission.
        }
        const auto acknowledgements=p.positive+p.negative;
        CHECK(service()==ComposedCopyServiceResult::Skipped&&polls==2&&p.positive+p.negative==acknowledgements);
        std::ostringstream json;report.Write(json);
        CHECK(json.str().find("\"post_input_polls\":1")!=std::string::npos);
    }
    // Disabled/uncomposed/already-ready leases are outside this new service point.
    LeaseProvider p;fvr::graphics::PairTicket t{};t.sequence=51;
    auto pending=std::make_unique<Pending>(p,t);unsigned callbacks=0;
    const auto service=[&]{return ServiceComposedCopyAfterInput(pending,[&]{++callbacks;},[&](const Pending&){++callbacks;return true;});};
    CHECK(service()==ComposedCopyServiceResult::Skipped);CHECK(pending->MarkComposed());
    pending->gpuOrdered=false;CHECK(service()==ComposedCopyServiceResult::Skipped);
    pending->gpuOrdered=true;CHECK(pending->Complete());CHECK(service()==ComposedCopyServiceResult::Skipped);
    pending.reset();CHECK(callbacks==0&&p.positive==1&&p.negative==0);
    return 0;
}
int CopyLeaseCompletionAndCancellation(){
    LeaseProvider p;fvr::graphics::PairTicket t{};t.sequence=7;t.frameId=8;t.session[0]=9;
    {
        HostCopyLease lease(p,t);++t.sequence;
        CHECK(lease.Copying()&&!lease.Ready());
        // A Busy completion does nothing to the lease or input cadence.
        for(unsigned tick=0;tick<4;++tick){p.UpdateInput({});p.ServiceFeedback();CHECK(lease.Copying());}
        CHECK(p.inputs==4&&p.positive==0&&p.negative==0);
        CHECK(lease.Complete()&&!lease.Complete());
        CHECK(lease.Ready()&&!lease.Copying()&&p.positive==1&&p.negative==0);
        CHECK(p.received.sequence==7&&p.received.frameId==8&&p.received.session[0]==9);
        // Ready still exists and owns local destination images. Menu/recenter,
        // stop or failed WaitFrame drops those locally, never negative-ACKs.
        p.ServiceFeedback();CHECK(p.deferred&&!p.released);
        lease.CancelPresentation();lease.CancelPresentation();p.gateBusy=false;p.ServiceFeedback();
        CHECK(lease.Cancelled()&&p.released&&!p.queued&&!p.deferred&&p.suspends==2);
    }
    CHECK(p.positive==1&&p.negative==0);
    // A GPU fault/timeout/stop before completion retires exactly once.
    {HostCopyLease incomplete(p,t);incomplete.CancelPresentation();CHECK(incomplete.Copying());}
    CHECK(p.positive==1&&p.negative==1&&p.received.sequence==8);
    // Cancellation does not prevent a later successful source read receipt.
    {HostCopyLease cancelled(p,t);cancelled.CancelPresentation();CHECK(cancelled.Complete());}
    CHECK(p.positive==2&&p.negative==1);
    return 0;
}
int ComposedLeaseKeepsSourceOwnership(){
    LeaseProvider p;fvr::graphics::PairTicket t{};t.sequence=71;t.frameId=72;t.resourceEpoch=73;
    {
        HostCopyLease lease(p,t);
        CHECK(!lease.Composed()&&lease.Copying()&&!lease.Ready());
        CHECK(lease.MarkComposed()&&!lease.MarkComposed());
        // Queueing/releasing XR targets is not the source-copy completion event.
        CHECK(lease.Composed()&&lease.Copying()&&p.positive==0&&p.negative==0);
        for(unsigned n=0;n<3;++n){p.UpdateInput({});p.ServiceFeedback();CHECK(lease.Copying());}
        CHECK(lease.Complete()&&!lease.Complete());
        CHECK(lease.Composed()&&lease.Ready()&&p.positive==1&&p.received.sequence==71);
        // A reference/menu cancellation after composition still orders queued
        // request cancellation before a contended positive completion receipt.
        lease.CancelPresentation();p.gateBusy=false;p.ServiceFeedback();
        CHECK(lease.Cancelled()&&p.released&&!p.queued&&p.negative==0);
    }
    CHECK(p.positive==1&&p.negative==0);
    {HostCopyLease incomplete(p,t);CHECK(incomplete.MarkComposed());}
    CHECK(p.positive==1&&p.negative==1); // Fault/shutdown before source completion.
    {HostCopyLease cancelled(p,t);cancelled.CancelPresentation();
        CHECK(!cancelled.MarkComposed()&&cancelled.Complete());}
    CHECK(p.positive==2&&p.negative==1);
    {HostCopyLease ready(p,t);CHECK(ready.Complete()&&ready.MarkComposed());
        CHECK(!ready.MarkComposed()&&ready.Ready());}
    CHECK(p.positive==3&&p.negative==1);
    return 0;
}
int DualTicketCompletionKeepsExactEvidence(){
    LeaseProvider p;fvr::graphics::PairTicket old{};old.sequence=9;old.resourceEpoch=3;
    old.frameId=10;old.trackingGeneration=11;old.spaceGeneration=12;old.predictedNs=13000000;old.session[0]=14;
    auto pending=std::make_unique<HostCopyLease>(p,old);
    CHECK(pending->MarkComposed());
    HostFrameObservation f=Frame(2);f.SetCopyTicket(old);f.copyPending=f.copyComposed=true;
    f.copySubmittedNs=10001000;f.copyComposedNs=10002000;f.copyPendingAgeNs=10000000;
    CHECK(!f.PreserveCompletedCopy()); // Busy does not become completed evidence.
    CHECK(p.positive==0&&pending->Copying());
    CHECK(pending->Complete());f.copyPending=false;f.copyCompleted=true;
    f.copyPreWaitPolled=f.copyPreWaitCompleted=true;f.copyCompletedNs=20001000;
    CHECK(f.PreserveCompletedCopy());pending.reset();
    CHECK(p.positive==1&&p.negative==0&&f.hasPriorCopy&&!f.copyTicket.sequence);
    CHECK(f.priorCopy.copyTicket.resourceEpoch==3&&f.priorCopy.copyTicket.sequence==9);
    // Same sequence in a new generation is a distinct delivered image; the
    // immutable original completion must not inherit any successor identity.
    auto next=old;next.resourceEpoch=4;next.frameId=20;next.trackingGeneration=21;next.session[0]=24;
    pending=std::make_unique<HostCopyLease>(p,next);f.SetCopyTicket(next);
    f.copySubmitted=f.copyPending=true;f.copySubmittedNs=21001000;
    CHECK(pending->MarkComposed());f.copyComposed=true;f.copyComposedNs=21002000;
    CHECK(pending->Copying()&&p.positive==1);
    CHECK(pending->Complete());f.copyPending=false;f.copyCompleted=true;
    f.copyAfterEndPolled=f.copyAfterEndCompleted=true;f.copyCompletedNs=21003000;
    pending.reset();CHECK(p.positive==2&&p.negative==0&&p.received.resourceEpoch==4);
    CHECK(f.priorCopy.copyCompletedNs==20001000&&f.copyCompletedNs==21003000);
    CHECK(f.priorCopy.copyTicket.session[0]==14&&f.copyTicket.session[0]==24);
    HostFrameDiagnostics d;d.Observe(f);
    CHECK(d.copyCompletedFrames==1&&d.copyCompletedObservations==2&&d.copyDualTicketFrames==1);
    CHECK(d.copySubmittedFrames==1&&d.copyPreWaitCompletedFrames==1&&d.copyAfterEndCompletedFrames==1);
    CHECK(d.maxCopyPendingAgeNs==10000000&&d.Row(0).hasPriorCopy);
    std::ostringstream out;out<<std::boolalpha;d.Write(out);const auto json=out.str();
    CHECK(json.find("\"prior_copy\":{\"copy_submitted\":false")!=std::string::npos);
    CHECK(json.find("\"copy_ticket\":{\"resource_epoch\":3,\"sequence\":9")!=std::string::npos);
    CHECK(json.find("\"copy_ticket\":{\"resource_epoch\":4,\"sequence\":9")!=std::string::npos);
    CHECK(json.find("\"copy_composed_ns\":10002000")!=std::string::npos);
    CHECK(json.find("\"copy_completed_observations\":2")!=std::string::npos);
    CHECK(json.find("\"copy_after_end_completed\":true")!=std::string::npos);
    // A broken host integration attempting a third ticket cannot overwrite
    // either slot silently. The diagnostic flag is not copy/ACK authority.
    CHECK(!f.PreserveCompletedCopy()&&f.copyObservationOverflow);
    CHECK(f.priorCopy.copyTicket.resourceEpoch==3&&f.copyTicket.resourceEpoch==4&&p.positive==2);
    d.Observe(f);CHECK(d.copyObservationOverflowFrames==1);
    return 0;
}
int DualTicketFirstFailureSurvivesWrap(){
    HostFrameDiagnostics d;auto f=Frame();f.SetCopyTicket({});
    f.copyTicket.sequence=1;f.copyTicket.resourceEpoch=2;f.copyComposed=f.copyCompleted=true;
    f.copyComposedNs=101;f.copyCompletedNs=102;CHECK(f.PreserveCompletedCopy());
    f.copyTicket.sequence=2;f.copyTicket.resourceEpoch=2;f.copyRetired=f.pairRejected=true;
    f.copyError=0x80004005u;d.Observe(f);
    for(unsigned i=2;i<300;++i){auto later=Frame(i);later.endSucceeded=false;d.Observe(later);}
    const auto& first=d.firstRejectedFrame;
    CHECK(d.dropped>0&&first.hasPriorCopy&&first.priorCopy.copyTicket.sequence==1);
    CHECK(first.copyTicket.sequence==2&&first.copyRetired&&first.copyError==0x80004005u);
    CHECK(first.priorCopy.copyComposedNs==101&&first.priorCopy.copyCompletedNs==102);
    return 0;
}
int PreWaitCompletionTimestamps(){
    HostFrameDiagnostics d;auto f=Frame();f.copyPending=f.copySubmitted=true;
    f.copySequence=71;f.copySubmittedNs=10001000;d.Observe(f);
    f=Frame(2);f.copySequence=71;f.copyCompleted=f.copyPreWaitPolled=f.copyPreWaitCompleted=true;
    f.copySubmittedNs=10001000;f.copyCompletedNs=20001000;
    f.inputStartedNs=21000000;f.inputReturnedNs=21001000;d.Observe(f);
    CHECK(d.copyPreWaitPollFrames==1&&d.copyPreWaitCompletedFrames==1&&d.copyCompletedFrames==1);
    CHECK(d.Row(1).copyCompletedNs<d.Row(1).inputStartedNs&&d.Row(1).copySubmittedNs==10001000);
    std::ostringstream out;out<<std::boolalpha;d.Write(out);const auto json=out.str();
    CHECK(json.find("\"copy_submitted_ns\":10001000")!=std::string::npos);
    CHECK(json.find("\"copy_completed_ns\":20001000")!=std::string::npos);
    CHECK(json.find("\"copy_pre_wait_completed\":true")!=std::string::npos);
    CHECK(json.find("\"copy_pre_wait_completed_frames\":1")!=std::string::npos);
    return 0;
}

int CopyTicketOwnsDeliveredIdentity(){
    HostFrameDiagnostics d;auto f=Frame();
    fvr::graphics::PairTicket ticket;ticket.resourceEpoch=7;ticket.sequence=41;ticket.frameId=101;
    ticket.trackingGeneration=202;ticket.spaceGeneration=3;ticket.predictedNs=404;
    for(unsigned i=0;i<ticket.session.size();++i)ticket.session[i]=std::uint8_t(i+1);
    f.retained=true;f.retainedGeneration=99;f.retainedSpace=1;f.retainedPredictedNs=303;
    f.copyPending=true;f.SetCopyTicket(ticket);d.Observe(f);
    // Retained-image metadata and later producer mutation cannot change the
    // delivered ticket whose GPU completion is still pending.
    ++ticket.frameId;ticket.session[0]=55;
    CHECK(d.Row(0).copyTicket.frameId==101&&d.Row(0).copyTicket.session[0]==1);
    CHECK(d.Row(0).retainedGeneration==99&&d.Row(0).copyTicket.trackingGeneration==202);
    f.frame=2;f.startedNs+=10000000;f.endedNs+=10000000;d.Observe(f);CHECK(d.count==1);
    // The same sequence reused in a new resource generation/session must be
    // retained even with an identical phase and no slow stage.
    f.frame=3;++f.copyTicket.resourceEpoch;d.Observe(f);CHECK(d.count==2);
    f.frame=4;f.copyTicket.session[15]=255;d.Observe(f);CHECK(d.count==3);
    f.frame=5;f.copyPending=false;f.copyCompleted=true;d.Observe(f);CHECK(d.count==4);
    CHECK(d.Row(3).copyTicket.sequence==41&&d.Row(3).copyTicket.resourceEpoch==8);
    d.Observe(Frame(6));
    std::ostringstream json;d.Write(json);const auto text=json.str();
    CHECK(text.find("\"copy_ticket\":{\"resource_epoch\":7,\"sequence\":41,\"frame\":101,\"tracking\":202,\"space\":3,\"predicted_ns\":404,\"session\":[1,2,3,4,5,6,7,8,9,10,11,12,13,14,15,16]}")!=std::string::npos);
    CHECK(text.find("\"copy_ticket\":null")!=std::string::npos);
    return 0;
}

int UncensoredStageAggregates(){
    // Normal short stages must survive even when unchanged frames do not enter
    // the ring. A histogram sample is the stage's total for one observed frame.
    HostFrameDiagnostics d;
    constexpr auto controls=unsigned(HostStage::Controls),poll=unsigned(HostStage::ColorPoll);
    for(unsigned n=1;n<=600;++n){auto f=Frame(n);f.stageNs[controls]=500000;
        if(n%2==0)f.stageNs[poll]=15000;d.Observe(f);}
    CHECK(d.frames==600&&d.count==1&&d.recorded==1);
    CHECK(d.Row(0).stageNs[controls]==0&&d.maxStageNs[controls]==500000);
    CHECK(d.stageAggregates[controls].frames==600&&d.stageAggregates[controls].totalNs==300000000);
    CHECK(d.stageAggregates[controls].histogram[4]==600&&!d.stageAggregates[controls].saturated);
    CHECK(d.stageAggregates[poll].frames==300&&d.stageAggregates[poll].totalNs==4500000);
    CHECK(d.stageAggregates[poll].histogram[1]==300);
    CHECK(d.stageAggregates[unsigned(HostStage::WaitFrame)].frames==0);
    // Failed frames still contribute their actual observed work, once.
    auto failed=Frame(601);failed.endSucceeded=false;failed.stageNs[controls]=10000;d.Observe(failed);
    CHECK(d.failedFrames==1&&d.stageAggregates[controls].frames==601);
    CHECK(d.stageAggregates[controls].totalNs==300010000&&d.stageAggregates[controls].histogram[0]==1);
    std::ostringstream json;d.Write(json);const auto text=json.str();
    CHECK(text.find("\"stage_aggregate_kind\":\"per_observed_frame_elapsed_sum\"")!=std::string::npos);
    CHECK(text.find("\"stage_histogram_upper_ns\":[10000,50000,100000,")!=std::string::npos);
    CHECK(text.find("\"frames\":601,\"total_ns\":300010000,\"saturated\":false,\"histogram\":[1,0,0,0,600,")!=std::string::npos);
    CHECK(text.size()<12000); // Aggregates do not emit the other599 frame rows.
    return 0;
}

int StageHistogramBoundariesAndOverflow(){
    HostStageAggregate aggregate;
    for(auto value:std::array<std::uint64_t,9>{0,10000,10001,1000000,1000001,20000000,20000001,100000000,100000001})
        aggregate.Observe(value,false);
    CHECK(aggregate.frames==9&&!aggregate.saturated);
    CHECK(aggregate.histogram[0]==2&&aggregate.histogram[1]==1&&aggregate.histogram[5]==1);
    CHECK(aggregate.histogram[6]==1&&aggregate.histogram[10]==1&&aggregate.histogram[11]==1);
    CHECK(aggregate.histogram[13]==1&&aggregate.histogram[14]==1);
    std::uint64_t counted=0;for(auto n:aggregate.histogram)counted+=n;CHECK(counted==9);
    constexpr auto maximum=~std::uint64_t{0};
    aggregate.totalNs=maximum-5;aggregate.Observe(6,false);
    CHECK(aggregate.totalNs==maximum&&aggregate.saturated);
    aggregate.frames=maximum;aggregate.histogram[0]=maximum;aggregate.Observe(1,false);
    CHECK(aggregate.frames==maximum&&aggregate.histogram[0]==maximum&&aggregate.totalNs==maximum);
    HostFrameDiagnostics d;d.stageAggregates[0]=aggregate;
    std::ostringstream json;d.Write(json);
    CHECK(json.str().find("\"total_ns\":18446744073709551615,\"saturated\":true")!=std::string::npos);
    return 0;
}

int StageTimerZeroRepeatedAndInvalidClock(){
    HostFrameDiagnostics d;auto f=Frame();constexpr auto index=unsigned(HostStage::Provider);
    fakeNow=1000000000;
    {HostStageTimer timer(f,HostStage::Provider,Clock);} // Valid zero, not skipped.
    d.Observe(f);CHECK(d.stageAggregates[index].frames==1&&d.stageAggregates[index].histogram[0]==1);
    CHECK(d.stageAggregates[index].totalNs==0&&d.stageAggregates[unsigned(HostStage::Menu)].frames==0);
    f=Frame(2);
    {HostStageTimer timer(f,HostStage::Provider,Clock);fakeNow+=10000;}
    {HostStageTimer timer(f,HostStage::Provider,Clock);fakeNow+=10000;}
    d.Observe(f);CHECK(d.stageAggregates[index].frames==2&&d.stageAggregates[index].totalNs==20000);
    CHECK(d.stageAggregates[index].histogram[1]==1); // One20us frame, not two10us samples.
    f=Frame(3);{HostStageTimer timer(f,HostStage::Provider,Clock);--fakeNow;}
    CHECK(f.stageObservedMask==0&&f.stageNs[index]==0);d.Observe(f);
    fakeNow=0;f=Frame(4);{HostStageTimer timer(f,HostStage::Provider,Clock);fakeNow=100;}
    d.Observe(f);CHECK(d.stageAggregates[index].frames==2);
    // Overflow during repeated stage timers must be visible, not wrapped small.
    f=Frame(5);f.stageNs[index]=(~std::uint64_t{0})-3;
    {HostStageTimer timer(f,HostStage::Provider,Clock);fakeNow+=5;}
    CHECK(f.stageNs[index]==~std::uint64_t{0}&&(f.stageSaturatedMask&(1u<<index)));
    d.Observe(f);CHECK(d.stageAggregates[index].saturated&&d.stageAggregates[index].totalNs==~std::uint64_t{0});
    CHECK(d.maxStageNs[index]==~std::uint64_t{0}&&d.stageAggregates[index].histogram.back()==1);
    return 0;
}

int FirstRejectedFrameSurvivesOverwrite(){
    HostFrameDiagnostics d;d.Observe(Frame());
    std::ostringstream initial;d.Write(initial);
    CHECK(initial.str().find("\"first_rejected_frame\":null")!=std::string::npos);
    auto f=Frame(2);f.worldLayer=false;f.layers=f.menuLayers=1;f.menu=true;
    f.pairRejected=f.copyCompleted=f.copyCancelled=true;
    f.copySubmittedNs=17000000;f.copyCompletedNs=20500000;
    f.copyPendingAgeNs=3500000;f.copyPreWaitPolled=f.copyPreWaitCompleted=true;
    f.copyTicket.resourceEpoch=41;f.copyTicket.sequence=42;f.copyTicket.frameId=43;
    f.copyTicket.trackingGeneration=44;f.copyTicket.spaceGeneration=45;
    f.copyTicket.predictedNs=46000000;f.copyTicket.session[0]=47;f.copyTicket.session[15]=48;
    f.copySequence=f.copyTicket.sequence;
    f.stageNs[unsigned(HostStage::WaitFrame)]=23000000;
    f.stageNs[unsigned(HostStage::Provider)]=1000;
    d.Observe(f);
    const auto& first=d.firstRejectedFrame;
    CHECK(first.frame==2&&first.pairRejected&&first.copyCancelled&&!first.copyRetired);
    CHECK(first.copyTicket.sequence==42&&first.copyTicket.session[15]==48);
    CHECK(first.copySubmittedNs==17000000&&first.copyCompletedNs==20500000);
    CHECK(first.reasons&HostRejectedPair);
    CHECK(first.stageNs[unsigned(HostStage::WaitFrame)]==23000000);
    CHECK(first.stageNs[unsigned(HostStage::Provider)]==0); // Same semantics as ordinary rows.
    std::ostringstream saved;HostFrameDiagnostics::WriteFrame(saved,first);
    // Mutating the caller's observation, then overwriting every ring slot, must
    // not change the retained ticket, outcome, or timestamps.
    f.copyTicket.sequence=99;f.copyCancelled=false;f.copyRetired=true;
    for(unsigned n=3;n<=300;++n){f=Frame(n);f.stageNs[unsigned(HostStage::WaitFrame)]=23000000;d.Observe(f);}
    f=Frame(301);f.pairRejected=f.copyRetired=true;f.copySequence=f.copyTicket.sequence=99;d.Observe(f);
    CHECK(d.count==HostFrameDiagnostics::Capacity&&d.dropped>0&&d.Row(0).frame>2);
    std::ostringstream retained;HostFrameDiagnostics::WriteFrame(retained,d.firstRejectedFrame);
    CHECK(retained.str()==saved.str());
    std::ostringstream report;d.Write(report);
    CHECK(report.str().find("\"first_rejected_frame\":"+saved.str())!=std::string::npos);
    CHECK(d.frames==301&&d.copyCompletedFrames==1&&d.copyCancelledFrames==1&&d.copyRetiredFrames==1);
    // A rejected new pair can coexist with a retained world layer: do not use
    // no-world reason bits as the snapshot admission predicate.
    HostFrameDiagnostics retainedWorld;f=Frame(0);f.pairRejected=true;retainedWorld.Observe(f);
    CHECK(retainedWorld.firstRejectedFrame.pairRejected&&retainedWorld.firstRejectedFrame.worldLayer);
    CHECK(retainedWorld.firstRejectedFrame.frame==0&&retainedWorld.firstRejectedFrame.reasons==0);
    return 0;
}

int FirstPublicationGapSurvivesOverwrite(){
    HostFrameDiagnostics d;
    const auto publish=[&](unsigned frame,std::int64_t returned,bool known,bool okay,bool slow=false){
        auto f=Frame(frame);f.startedNs=returned-1000000;f.endedNs=returned+1000000;
        f.inputStartedNs=returned-1000;f.inputReturnedNs=returned;
        f.priorInputSuccessGeneration=d.lastInputSuccessGeneration;
        f.priorInputSuccessReturnedNs=d.lastInputSuccessReturnedNs;
        f.inputGeneration=frame;f.inputAttempted=true;f.inputKnown=known;f.inputSucceeded=okay;
        f.inputFocused=f.inputHeadValid=f.inputHandsTracked=true;
        f.inputResult=okay?0:1;
        if(slow)f.stageNs[unsigned(HostStage::WaitFrame)]=23000000;
        d.Input(frame,returned,known,okay);d.Observe(f);
    };
    const auto json=[&]{std::ostringstream out;d.Write(out);return out.str();};
    // Initial absence is not an observed gap between genuine successes.
    publish(1,1000000000,false,false);publish(2,1200000000,true,false);
    publish(3,1400000000,true,true);publish(4,1499999999,true,true);
    CHECK(json().find("\"first_input_success_gap\":null")!=std::string::npos);
    publish(5,1549999999,true,false);publish(6,1569999999,false,false);
    CHECK(json().find("\"first_input_success_gap\":null")!=std::string::npos);
    publish(7,1599999999,true,true,true); // Exactly100ms from actual success4.
    const auto first=json();const auto marker=first.find("\"first_input_success_gap\":");
    CHECK(marker!=std::string::npos);const auto saved=first.substr(marker);
    CHECK(first.find("\"first_input_success_gap_ns\":100000000")!=std::string::npos);
    CHECK(saved.find("\"frame\":7,")!=std::string::npos);
    CHECK(saved.find("\"prior_input_success_generation\":4,")!=std::string::npos);
    CHECK(saved.find("\"prior_input_success_returned_ns\":1499999999,")!=std::string::npos);
    CHECK(saved.find("\"input_returned_ns\":1599999999,")!=std::string::npos);
    CHECK(saved.find("\"slow_stage_ns\":[23000000,")!=std::string::npos);
    for(unsigned n=8;n<310;++n)publish(n,1599999999ll+10000000ll*(n-7),true,true,true);
    CHECK(d.dropped>0&&d.Row(0).frame>7);
    publish(310,5999999999,true,true,true); // Larger later gap does not replace first.
    CHECK(d.maxInputSuccessGapNs>100000000);
    const auto after=json();CHECK(after.substr(after.find("\"first_input_success_gap\":"))==saved);
    CHECK(d.inputFailures==2&&d.inputUnknown==2);
    return 0;
}

}
int SourceImportCacheCases(){
    using namespace fvr;
    const auto descriptor=[](std::uint64_t epoch,std::uint64_t first){graphics::TextureDescriptor d;
        d.width=2784;d.height=3024;d.format=29;d.adapterLow=7;d.resourceEpoch=epoch;d.sharing=graphics::TextureSharing::LegacyFenced;
        const std::array<std::uint64_t,2> ids{first,first+1};std::memcpy(d.session.data(),ids.data(),sizeof(ids));return d;};
    const auto ticket=[](const graphics::TextureDescriptor& d,std::uint64_t sequence){graphics::PairTicket t;
        t.session=d.session;t.resourceEpoch=d.resourceEpoch;t.sequence=sequence;t.frameId=100+sequence;
        t.spaceGeneration=4;t.trackingGeneration=20+sequence;t.predictedNs=100000+std::int64_t(sequence);return t;};
    const auto identity=[](unsigned slot,std::uint64_t request){runtime::SourceSlotIdentity i;
        i.channelSession[0]=17;i.requestId=request;i.slot=slot;i.slotCount=2;return i;};
    const auto a=descriptor(1,10),b=descriptor(1,20);
    HostSourceImportCache c;HostSourceImportCache::Selection selected;
    CHECK(c.Select(2,identity(0,1),a,ticket(a,1),selected)&&selected.open&&selected.resetAll&&selected.resetPresentation);
    CHECK(c.Bind(selected)&&c.ActiveEntry(ticket(a,1))==0);
    // Neither another slot nor a cached repeat can replace the sole active copy.
    CHECK(!c.Select(2,identity(1,2),b,ticket(b,1),selected));
    auto wrong=ticket(a,1);++wrong.trackingGeneration;
    CHECK(!c.Release(wrong,false)&&c.ActiveEntry(ticket(a,1))==0);
    CHECK(c.Release(ticket(a,1),false));
    CHECK(c.Select(2,identity(1,2),b,ticket(b,1),selected)&&selected.open&&!selected.resetPresentation&&!selected.resetAll);
    CHECK(c.Bind(selected)&&c.Release(ticket(b,1),false));
    CHECK(c.Select(2,identity(0,3),a,ticket(a,2),selected)&&!selected.open&&!selected.resetPresentation);
    const auto stale=selected;CHECK(c.Bind(selected)&&c.Release(ticket(a,2),false));CHECK(!c.Bind(stale));
    CHECK(c.Stats().opens==2&&c.Stats().hits==1&&c.Stats().resets==1);
    // A retired generation cannot become writable through another cache hit.
    CHECK(c.Select(2,identity(0,4),a,ticket(a,3),selected)&&c.Bind(selected));
    CHECK(c.Release(ticket(a,3),true));
    CHECK(!c.Select(2,identity(0,5),a,ticket(a,4),selected));
    const auto a2=descriptor(2,30);
    CHECK(c.Select(2,identity(0,5),a2,ticket(a2,1),selected)&&selected.open&&!selected.resetPresentation);
    CHECK(c.Bind(selected)&&c.Release(ticket(a2,1),false));
    // Same-number tickets across slots remain independent; no sequence-only ACK.
    CHECK(c.Select(2,identity(1,6),b,ticket(b,2),selected)&&!selected.open&&c.Bind(selected));
    CHECK(!c.Release(ticket(a2,2),false)&&c.ActiveEntry(ticket(b,2))==1);
    CHECK(c.Release(ticket(b,2),false));
    const auto alias=descriptor(3,20);CHECK(!c.Select(2,identity(0,7),alias,ticket(alias,1),selected));
    auto shape=a2;++shape.width;CHECK(!c.Select(2,identity(0,7),shape,ticket(shape,1),selected));
    auto sameEpoch=descriptor(2,40);CHECK(!c.Select(2,identity(0,7),sameEpoch,ticket(sameEpoch,1),selected));
    for(unsigned fault=0;fault<5;++fault){auto id=identity(0,7);
        if(fault==0)id.channelSession={};if(fault==1)id.requestId=0;if(fault==2)id.slot=2;
        if(fault==3)id.slotCount=1;if(fault==4)id.requestId=6;
        CHECK(!c.Select(2,id,a2,ticket(a2,2),selected));}
    auto next=identity(0,1);next.channelSession[0]=18;
    CHECK(c.Select(2,next,a,ticket(a,1),selected)&&selected.resetAll&&selected.resetPresentation);
    c.OpenFailed(selected);CHECK(c.ActiveEntry(ticket(a,1))==HostSourceImportCache::Capacity);
    CHECK(!c.Select(2,next,a,ticket(a,2),selected));
    next.requestId=2;CHECK(c.Select(2,next,a2,ticket(a2,1),selected)&&c.Bind(selected)&&c.Release(ticket(a2,1),false));
    // The optional path must not change the existing v8 replacement policy.
    HostSourceImportCache legacy;runtime::SourceSlotIdentity none;
    CHECK(legacy.Select(1,none,a,ticket(a,1),selected)&&selected.resetPresentation&&legacy.Bind(selected));
    CHECK(legacy.Release(ticket(a,1),false));
    CHECK(legacy.Select(1,none,b,ticket(b,1),selected)&&selected.open&&selected.resetPresentation&&legacy.Bind(selected));
    CHECK(legacy.Release(ticket(b,1),true));
    CHECK(legacy.Select(1,none,b,ticket(b,2),selected)&&selected.open&&selected.resetPresentation);
    CHECK(!legacy.Select(3,none,b,ticket(b,2),selected));
    return 0;
}

int ThreeSourceImportsKeepOneCopyAndDepthQuartet(){
    using namespace fvr;
    std::array<graphics::TextureDescriptor,3> descriptors{};
    std::array<graphics::PairTicket,3> tickets{};
    std::array<graphics::SceneDepthFrame,3> depths{};
    const auto identity=[](unsigned slot,std::uint64_t request){runtime::SourceSlotIdentity id;
        id.channelSession[0]=37;id.slot=slot;id.slotCount=3;id.requestId=request;id.depthPresent=true;return id;};
    for(unsigned slot=0;slot<3;++slot){
        auto& d=descriptors[slot];d.width=2784;d.height=3024;d.format=29;d.adapterLow=7;
        d.sharing=graphics::TextureSharing::LegacyFenced;d.resourceEpoch=1;
        const std::array<std::uint64_t,2> colors{10+std::uint64_t(slot)*10,11+std::uint64_t(slot)*10};
        std::memcpy(d.session.data(),colors.data(),sizeof(colors));
        auto& t=tickets[slot];t.session=d.session;t.resourceEpoch=1;t.sequence=1;t.frameId=100+slot;
        t.spaceGeneration=1;t.trackingGeneration=10+slot;t.predictedNs=1000000+slot;
        auto& z=depths[slot];z.color=t;z.width=d.width;z.height=d.height;z.samples=1;z.format=44;
        z.adapterLow=d.adapterLow;z.resourceGeneration=1;z.flags=graphics::SceneDepthCanonicalClip;
        for(unsigned eye=0;eye<2;++eye)for(unsigned stage=0;stage<2;++stage)z.resources[eye][stage]=100+slot*10+eye*2+stage;
        CHECK(graphics::SceneDepthMatches(z,d,t));
    }
    HostSourceImportCache cache;HostSourceImportCache::Selection selected;
    graphics::SceneDepthImportPolicy depth;graphics::SceneDepthImportPolicy::Selection depthSelection;
    for(unsigned slot=0;slot<3;++slot){
        CHECK(cache.Select(3,identity(slot,slot+1),descriptors[slot],tickets[slot],selected));
        CHECK(selected.open&&selected.resetPresentation==(slot==0)&&cache.Bind(selected));
        CHECK(cache.SourceCount()==3&&cache.ActiveEntry(tickets[slot])==slot);
        // Cached imports are not extra deliveries. The held exact ticket alone
        // must release the one host copy before any other selection can bind.
        const auto other=(slot+1)%3;
        CHECK(!cache.Select(3,identity(other,slot+2),descriptors[other],tickets[other],selected));
        CHECK(!cache.Release(tickets[other],false)&&cache.ActiveEntry(tickets[slot])==slot);
        CHECK(depth.Select(depths[slot],descriptors[slot],tickets[slot],slot,3,depthSelection));
        CHECK(depthSelection.openSources&&depthSelection.createLocal==(slot==0)&&depth.Commit(depthSelection));
        CHECK(cache.Release(tickets[slot],false));
    }
    auto a=tickets[0];++a.sequence;++a.frameId;a.trackingGeneration=20;++a.predictedNs;
    auto az=depths[0];az.color=a;
    CHECK(cache.Select(3,identity(0,4),descriptors[0],a,selected)&&!selected.open&&!selected.resetPresentation&&cache.Bind(selected));
    CHECK(depth.Select(az,descriptors[0],a,0,3,depthSelection)&&!depthSelection.openSources&&!depthSelection.createLocal);
    CHECK(depth.Commit(depthSelection)&&!depth.Select(depths[0],descriptors[0],tickets[0],0,3,depthSelection));
    CHECK(cache.Release(a,false)&&cache.Stats().opens==3&&cache.Stats().hits==1);
    // Slot2 must check BOTH old slots, including cross color/depth aliases.
    auto c=tickets[2];++c.sequence;++c.frameId;c.trackingGeneration=21;++c.predictedNs;
    for(unsigned other=0;other<2;++other){
        auto alias=descriptors[other];alias.resourceEpoch=2;
        auto aliasTicket=c;aliasTicket.session=alias.session;aliasTicket.resourceEpoch=2;
        CHECK(!cache.Select(3,identity(2,5),alias,aliasTicket,selected));
        auto z=depths[2];z.color=c;++z.resourceGeneration;z.resources[0][0]=depths[other].resources[0][0];
        CHECK(!depth.Select(z,descriptors[2],c,2,3,depthSelection));
        z=depths[2];z.color=c;++z.resourceGeneration;z.resources[0][0]=graphics::LegacyResourceIds(descriptors[other])[0];
        CHECK(!depth.Select(z,descriptors[2],c,2,3,depthSelection));
        auto d=descriptors[2];const std::array<std::uint64_t,2> colors{depths[other].resources[0][0],999};
        std::memcpy(d.session.data(),colors.data(),sizeof(colors));auto t=c;t.session=d.session;
        z=depths[2];z.color=t;CHECK(!depth.Select(z,d,t,2,3,depthSelection));
    }
    auto cz=depths[2];cz.color=c;
    CHECK(cache.Select(3,identity(2,5),descriptors[2],c,selected)&&cache.Bind(selected));
    CHECK(cache.Release(c,true)); // Fault/cancel poisons only this generation.
    CHECK(!cache.Select(3,identity(2,6),descriptors[2],c,selected));
    CHECK(!cache.Select(4,identity(0,6),descriptors[0],a,selected));
    CHECK(!cache.Select(3,identity(3,6),descriptors[0],a,selected));
    CHECK(!depth.Select(cz,descriptors[2],c,3,3,depthSelection));
    CHECK(!depth.Select(cz,descriptors[2],c,2,4,depthSelection));
    cz.samples=2;CHECK(!depth.Select(cz,descriptors[2],c,2,3,depthSelection));
    cz.samples=1;cz.quality=1;CHECK(!depth.Select(cz,descriptors[2],c,2,3,depthSelection));
    auto renewed=identity(0,1);renewed.channelSession[0]=38;
    CHECK(cache.Select(3,renewed,descriptors[0],tickets[0],selected)&&selected.resetAll&&selected.resetPresentation);
    return 0;
}

int main(int argc,char** argv){
    if(argc==2&&std::string(argv[1])=="--first-input-gap")return FirstPublicationGapSurvivesOverwrite();
    if(SourceImportCacheCases()||ThreeSourceImportsKeepOneCopyAndDepthQuartet())return 1;
    if(PostInputComposedServicePolicy()||ComposedLeaseKeepsSourceOwnership()||DualTicketCompletionKeepsExactEvidence()||DualTicketFirstFailureSurvivesWrap()||CopyLeaseCompletionAndCancellation()||PreWaitCompletionTimestamps()||CopyTicketOwnsDeliveredIdentity()||UncensoredStageAggregates()||StageHistogramBoundariesAndOverflow()||StageTimerZeroRepeatedAndInvalidClock()||FirstPublicationGapSurvivesOverwrite()||FirstRejectedFrameSurvivesOverwrite()||CopyPhasesBelowSlowThreshold()||LayersAndReasons()||PublicationAndSlowStages()||LegacyProviderContract()||BoundedAndTimer())return 1;
    std::puts("Host diagnostics: menu/zero distinction, loss/recovery reasons, input outcomes, slow stages and bounded overwrite passed");return 0;}
