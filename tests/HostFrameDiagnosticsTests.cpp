#include "Test.h"
#include "fvr/xr/HostFrameDiagnostics.h"
#include "fvr/runtime/FrameProvider.h"
#include <sstream>
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
    std::ostringstream json;d.Write(json);CHECK(json.str().find("\"zero_layer_frames\":0")!=std::string::npos);
    CHECK(json.str().find("\"slow_stage_ns\":[")!=std::string::npos);
    CHECK(json.str().size()<300000);
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
int main(int argc,char** argv){
    if(argc==2&&std::string(argv[1])=="--first-input-gap")return FirstPublicationGapSurvivesOverwrite();
    if(FirstPublicationGapSurvivesOverwrite()||LayersAndReasons()||PublicationAndSlowStages()||LegacyProviderContract()||BoundedAndTimer())return 1;
    std::puts("Host diagnostics: menu/zero distinction, loss/recovery reasons, input outcomes, slow stages and bounded overwrite passed");return 0;}
