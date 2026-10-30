#include "Test.h"
#include "fvr/xr/OpenXrHost.h"
#include "fvr/xr/HostComposedCopyService.h"
#include "fvr/xr/HostCopyLease.h"
#include "fvr/graphics/D3D11SharedPair.h"
#include "fvr/graphics/D3D11SceneDepthPair.h"
#include <fstream>
#include "fvr/runtime/RetainedPresentation.h"
#include "fvr/platform/windows/ProcessLifetime.h"
#include <Windows.h>
#include <wrl/client.h>
#include <vector>
#include <iostream>
#include <limits>
#include <cstring>
#include <memory>
using Microsoft::WRL::ComPtr;
using namespace fvr;
std::unique_ptr<xr::HostReport> RunHostOnHeap(const xr::HostOptions& options){
    // Each report owns a large diagnostic ring. Construct the return value
    // directly in heap storage so nested GPU checks leave stack for D3DCompile.
    // make_unique(RunOpenXrHost(...)) would first materialize a stack temporary.
    return std::unique_ptr<xr::HostReport>(new xr::HostReport(xr::RunOpenXrHost(options)));
}
int NormalCopyPollRegression(){
    struct Provider final:runtime::IFrameProvider {
        graphics::PairTicket expected{};unsigned positive=0,negative=0,suspended=0;bool wrong=false;
        bool TryGetPair(const runtime::PresentationRequirements&,const runtime::TrackingFrame&,
            graphics::TextureDescriptor&,graphics::PairTicket&)noexcept override{return false;}
        void PairConsumed(const graphics::PairTicket& ticket,bool okay)noexcept override {
            wrong|=std::memcmp(&ticket,&expected,sizeof(ticket))!=0;
            if(okay)++positive;else ++negative;
        }
        void Suspend()noexcept override{++suspended;}
    };
    struct Pending final:xr::HostCopyLease {
        using HostCopyLease::HostCopyLease;
        bool gpuOrdered=false;
    };
    for(const auto outcome:{graphics::TransferResult::Ok,graphics::TransferResult::Busy,graphics::TransferResult::Faulted}){
        Provider provider;provider.expected.sequence=17;provider.expected.frameId=23;
        auto pending=std::make_unique<Pending>(provider,provider.expected);
        bool polled=false;unsigned polls=0;
        const auto poll=[&]{++polls;
            if(outcome==graphics::TransferResult::Ok)pending->Complete();
            else if(outcome==graphics::TransferResult::Faulted)pending.reset();
        };
        CHECK(xr::ServiceUncomposedCopyBeforePresentation(pending,true,polled,poll));
        CHECK(polled&&polls==1&&!provider.wrong);
        CHECK(!xr::ServiceUncomposedCopyBeforePresentation(pending,true,polled,poll)&&polls==1);
        if(outcome==graphics::TransferResult::Ok){
            // Query success makes this same-frame image eligible for presentation.
            CHECK(pending&&pending->Ready()&&!pending->Cancelled()&&provider.positive==1&&provider.negative==0);
            CHECK(!pending->Complete());
            polled=false;
            CHECK(!xr::ServiceUncomposedCopyBeforePresentation(pending,false,polled,poll)&&polls==1);
        }else if(outcome==graphics::TransferResult::Busy){
            CHECK(pending&&pending->Copying()&&provider.positive==0&&provider.negative==0);
            CHECK(std::memcmp(&pending->ticket,&provider.expected,sizeof(provider.expected))==0);
            // Cancellation suppresses presentation, never GPU ownership retirement.
            pending->CancelPresentation();CHECK(provider.suspended==1&&pending->Copying());
            polled=false; // A later frame may observe the same query's success.
            CHECK(xr::ServiceUncomposedCopyBeforePresentation(pending,false,polled,[&]{++polls;pending->Complete();}));
            CHECK(polls==2&&pending->Ready()&&pending->Cancelled()&&!pending->MarkComposed());
            CHECK(provider.positive==1&&provider.negative==0);
        }else CHECK(!pending&&provider.positive==0&&provider.negative==1);
        pending.reset();CHECK(!provider.wrong&&provider.positive+provider.negative==1);
    }
    Provider provider;provider.expected.sequence=19;
    auto pending=std::make_unique<Pending>(provider,provider.expected);unsigned polls=0;
    bool polled=true; // The opt-in private service already polled this submission.
    CHECK(!xr::ServiceUncomposedCopyBeforePresentation(pending,true,polled,[&]{++polls;}));
    CHECK(polls==0&&pending->Copying()&&provider.positive==0&&provider.negative==0);
    polled=false;pending->gpuOrdered=true;
    CHECK(!xr::ServiceUncomposedCopyBeforePresentation(pending,true,polled,[&]{++polls;}));
    CHECK(polls==0&&!polled&&pending->Copying());
    pending.reset();CHECK(provider.negative==1&&provider.positive==0&&!provider.wrong);
    std::cout<<"Normal pre-composition poll: same-frame readiness, Busy retention, cancellation, fault retirement and no duplicate poll/ACK passed\n";
    return 0;
}
struct Delayed final:runtime::IFrameProvider {
    ComPtr<ID3D11Device> device;ComPtr<ID3D11DeviceContext> context;std::array<ComPtr<ID3D11Texture2D>,2> images;
    graphics::D3D11PairProducer producer;runtime::TrackingFrame pending{};unsigned consumed=0,discarded=0;std::uint64_t epoch=0;
    bool faultInjected=false,invalidPoseInjected=false;std::string error;
    bool TryGetPair(const runtime::PresentationRequirements&,const runtime::TrackingFrame&,graphics::TextureDescriptor&,graphics::PairTicket&)noexcept override{return false;}
    void Suspend()noexcept override{pending={};}
    bool TryGetCompletedPair(const runtime::PresentationRequirements& req,const runtime::TrackingFrame& current,runtime::TrackingFrame& rendered,graphics::TextureDescriptor& d,graphics::PairTicket& ticket)noexcept override{
        try {
            const unsigned frame=unsigned((current.predictedNs-1000000000)/11000000);
            if(frame>=40&&frame<=67){pending={};return false;}
            if(!pending.generation||pending.spaceGeneration!=current.spaceGeneration){pending=current;return false;}
            if(frame%4!=0)return false;
            rendered=pending;pending={};
            if(!device){D3D_FEATURE_LEVEL level{};if(FAILED(D3D11CreateDevice(nullptr,D3D_DRIVER_TYPE_HARDWARE,nullptr,0,nullptr,0,D3D11_SDK_VERSION,&device,&level,&context)))return false;
                D3D11_TEXTURE2D_DESC desc{};desc.Width=req.width;desc.Height=req.height;desc.MipLevels=desc.ArraySize=desc.SampleDesc.Count=1;desc.Format=DXGI_FORMAT(req.format);
                for(auto& image:images)if(FAILED(device->CreateTexture2D(&desc,nullptr,&image)))return false;
            }
            if(!graphics::Valid(producer.Descriptor()))if(producer.Create(device.Get(),req.width,req.height,DXGI_FORMAT(req.format),++epoch)!=graphics::TransferResult::Ok)return false;
            const unsigned source=unsigned((rendered.predictedNs-1000000000)/11000000);
            for(unsigned eye=0;eye<2;++eye){std::vector<std::uint32_t> pixels(std::size_t(req.width)*req.height,0xff5a0000u|((eye+1)<<8)|source);context->UpdateSubresource(images[eye].Get(),0,nullptr,pixels.data(),req.width*4,0);}
            const std::array<graphics::TextureSlice,2> slices{{{images[0].Get(),0},{images[1].Get(),0}}};
            if(producer.Publish(slices,{source,rendered.spaceGeneration,rendered.generation,rendered.predictedNs},ticket)!=graphics::TransferResult::Ok)return false;
            d=producer.Descriptor();if(frame==20){ticket.sequence+=100;faultInjected=true;}
            if(frame==24){rendered.eyes[0].position.x=std::numeric_limits<float>::quiet_NaN();invalidPoseInjected=true;}
            return true;
        }catch(...){error="fixture failed";return false;}
    }
    void PairConsumed(const graphics::PairTicket& ticket,bool okay)noexcept override{if(okay)++consumed;else {++discarded;producer.Reset();}producer.Acknowledge(ticket,okay);}
};
struct InputObserver final:runtime::IFrameProvider {
    unsigned samples=0,tracked=0,released=0,invalid=0;bool floor=false;
    bool sawUntracked=false,sawInactive=false,sawBadStick=false,sawValidPose=false;
    bool TryGetPair(const runtime::PresentationRequirements&,const runtime::TrackingFrame&,graphics::TextureDescriptor&,graphics::PairTicket&)noexcept override{return false;}
    void PairConsumed(const graphics::PairTicket&,bool)noexcept override{}
    void UpdateInput(const interaction::InputFrame& f)noexcept override {
        ++samples;if(!interaction::ValidInput(f))++invalid;if(f.floorRelative)floor=true;
        const unsigned n=unsigned((f.predictedNs-1000000000)/11000000);
        if(!f.focused){++released;for(const auto& h:f.hands)if(h.active||h.held||h.gripTracked||h.aimTracked)++invalid;return;}
        if(f.worldUnitsPerMeter!=2.5f||f.referenceHead.orientation.x!=0||f.referenceHead.orientation.z!=0)++invalid;
        if(n==8||n==9){sawUntracked=true;if(f.hands[0].gripTracked||f.hands[1].aimTracked)++invalid;}
        if(n==12){sawInactive=true;if(f.hands[1].trigger!=0||(f.hands[1].active&interaction::Trigger))++invalid;}
        if(n==13){sawBadStick=true;if(f.hands[0].stickX!=0||(f.hands[0].active&interaction::Stick))++invalid;}
        if(f.hands[0].gripTracked&&f.hands[1].aimTracked){++tracked;
            if(f.hands[0].grip.position.x!=-.25f||f.hands[1].aim.position.x!=.25f||f.hands[0].grip.position.z!=-.4f||f.hands[1].aim.position.z!=-.6f)++invalid;
            else sawValidPose=true;
        }
    }
    runtime::InputPublicationResult UpdateInputObserved(const interaction::InputFrame& f)noexcept override {
        UpdateInput(f);return {true,samples%7!=0,samples%7?0u:1u};
    }
};
// Exercise the actual host with legacy shared textures and real GPU fences.
// Deliver just before the fake runtime's existing discontinuities. SubmitCopy
// queues legacy completion; the host may observe it in this frame or later.
struct LegacyAsync final:runtime::IFrameProvider {
    static constexpr std::array<unsigned,7> DeliveryFrames{10,30,50,69,74,84,100};
    ComPtr<ID3D11Device> device;ComPtr<ID3D11DeviceContext> context;
    std::array<ComPtr<ID3D11Texture2D>,2> images;graphics::D3D11PairProducer producer;
    runtime::TrackingFrame previous{},heldTracking{};graphics::PairTicket held{};
    std::array<unsigned,7> acknowledgements{},ackFrames{},inputWhileHeld{},suspendsWhileHeld{};
    std::array<bool,7> consumed{};
    std::array<graphics::PairTicket,7> deliveredTickets{};
    unsigned next=0,active=0,inputFrame=0,inputs=0,queriesWhileHeld=0,errors=0;
    bool outstanding=false;std::string error;
    bool Fail(const char* message){++errors;error=message;return false;}
    bool TryGetPair(const runtime::PresentationRequirements&,const runtime::TrackingFrame&,
        graphics::TextureDescriptor&,graphics::PairTicket&)noexcept override{return false;}
    void UpdateInput(const interaction::InputFrame& input)noexcept override {
        ++inputs;inputFrame=unsigned((input.predictedNs-1000000000)/11000000);
        if(outstanding&&inputFrame>DeliveryFrames[active]){
            ++inputWhileHeld[active];
            // Real GPU scheduling varies. Yield only in the fixture; input has
            // already arrived while the source remains owned by the host.
            Sleep(1);
        }
    }
    void Suspend()noexcept override {
        if(outstanding)++suspendsWhileHeld[active];
        previous={}; // Cancels a future request, never the delivered GPU source.
    }
    bool TryGetCompletedPair(const runtime::PresentationRequirements& req,const runtime::TrackingFrame& current,
        runtime::TrackingFrame& rendered,graphics::TextureDescriptor& descriptor,graphics::PairTicket& ticket)noexcept override {
        try {
            if(outstanding){++queriesWhileHeld;return Fail("provider queried before exact GPU feedback");}
            const auto frame=unsigned((current.predictedNs-1000000000)/11000000);
            if(next==DeliveryFrames.size()||frame<DeliveryFrames[next]){previous=current;return false;}
            if(frame!=DeliveryFrames[next])return Fail("missed scheduled legacy delivery");
            if(!previous.generation||previous.spaceGeneration!=current.spaceGeneration||previous.predictedNs>=current.predictedNs)
                return Fail("missing original earlier render pose");
            if(!device){D3D_FEATURE_LEVEL level{};
                if(FAILED(D3D11CreateDevice(nullptr,D3D_DRIVER_TYPE_HARDWARE,nullptr,0,nullptr,0,D3D11_SDK_VERSION,&device,&level,&context)))return Fail("legacy fixture device");
                D3D11_TEXTURE2D_DESC desc{};desc.Width=req.width;desc.Height=req.height;desc.MipLevels=desc.ArraySize=desc.SampleDesc.Count=1;desc.Format=DXGI_FORMAT(req.format);
                for(auto& image:images)if(FAILED(device->CreateTexture2D(&desc,nullptr,&image)))return Fail("legacy fixture texture");
                if(producer.Create(device.Get(),req.width,req.height,DXGI_FORMAT(req.format),1,graphics::TextureSharing::LegacyFenced)!=graphics::TransferResult::Ok)
                    return Fail("legacy producer creation");
            }
            if(!graphics::Valid(producer.Descriptor()))return Fail("successful cancellation retired the producer generation");
            rendered=previous;heldTracking=rendered;
            const auto source=unsigned((rendered.predictedNs-1000000000)/11000000);
            for(unsigned eye=0;eye<2;++eye){
                const std::vector<std::uint32_t> pixels(std::size_t(req.width)*req.height,0xff5a0000u|((eye+1)<<8)|source);
                context->UpdateSubresource(images[eye].Get(),0,nullptr,pixels.data(),req.width*4,0);
            }
            const std::array<graphics::TextureSlice,2> slices{{{images[0].Get(),0},{images[1].Get(),0}}};
            const graphics::FrameMetadata metadata{source,rendered.spaceGeneration,rendered.generation,rendered.predictedNs};
            // Wait only for fixture export readiness, retaining the exact same
            // source and metadata on Busy. The host's consumer is never waited
            // here. Source ownership is released only by actual GPU completion,
            // which may be observed before the next input publication.
            const auto deadline=GetTickCount64()+500;
            graphics::TransferResult result;
            do {result=producer.Publish(slices,metadata,ticket);if(result==graphics::TransferResult::Busy)Sleep(1);}
            while(result==graphics::TransferResult::Busy&&GetTickCount64()<deadline);
            if(result!=graphics::TransferResult::Ok)return Fail("legacy fixture export did not complete");
            descriptor=producer.Descriptor();held=ticket;active=next++;deliveredTickets[active]=ticket;outstanding=true;previous={};return true;
        }catch(...){return Fail("legacy fixture exception");}
    }
    void PairConsumed(const graphics::PairTicket& ticket,bool okay)noexcept override {
        if(!outstanding||std::memcmp(&ticket,&held,sizeof(ticket))){Fail("duplicate or wrong legacy acknowledgement");return;}
        if(ticket.predictedNs!=heldTracking.predictedNs||ticket.trackingGeneration!=heldTracking.generation||ticket.spaceGeneration!=heldTracking.spaceGeneration)
            Fail("legacy acknowledgement relabelled its original tracking");
        ++acknowledgements[active];ackFrames[active]=inputFrame;consumed[active]=okay;
        if(okay&&inputFrame<DeliveryFrames[active])Fail("legacy pair acknowledged before delivery");
        producer.Acknowledge(ticket,okay);outstanding=false;
    }
};
int LegacyAsyncHostRegression(const std::filesystem::path& path,unsigned(*runtimeErrors)()){
    LegacyAsync provider;xr::HostOptions options;options.loaderPath=path;options.probeOnly=false;options.seconds=3;options.provider=&provider;
    const auto reportStorage=RunHostOnHeap(options);const auto& report=*reportStorage;
    if(!report.okay||!provider.error.empty())std::cerr<<"legacy host: "<<report.error<<" fixture: "<<provider.error<<'\n';
    CHECK(report.okay&&runtimeErrors()==0&&provider.errors==0&&provider.error.empty());
    CHECK(report.endedFrames==100&&provider.inputs==101&&report.inputFrames==100);
    CHECK(provider.next==LegacyAsync::DeliveryFrames.size()&&!provider.outstanding&&provider.queriesWhileHeld==0);
    unsigned positives=0;
    for(unsigned n=0;n<LegacyAsync::DeliveryFrames.size();++n){
        CHECK(provider.acknowledgements[n]==1);
        if(n+1<LegacyAsync::DeliveryFrames.size()){
            CHECK(provider.consumed[n]&&provider.ackFrames[n]>=LegacyAsync::DeliveryFrames[n]);
        }else CHECK(provider.ackFrames[n]==100); // Same-frame query success may release the final source.
        positives+=unsigned(provider.consumed[n]);
    }
    // Completion may precede the next input's invalidation. Successful GPU
    // ownership and local presentation cancellation are deliberately separate;
    // suspension need not happen while the provider still owns those pixels.
    unsigned completedRows=0,presentedRows=0,rejectedRows=0,sameFrameCompleted=0,busySubmissions=0;
    std::array<unsigned,7> submitted{},completed{},presented{},sameFrame{};
    for(unsigned i=0;i<report.frameDiagnostics.count;++i){const auto& row=report.frameDiagnostics.Row(i);
        if(row.copyPending&&!row.copySubmitted)
            CHECK(row.inputAttempted&&row.inputGeneration&&row.inputReturnedNs>=row.inputStartedNs);
        if(!row.copyTicket.sequence)continue;
        unsigned index=unsigned(provider.deliveredTickets.size());
        for(unsigned n=0;n<provider.deliveredTickets.size();++n)
            if(std::memcmp(&row.copyTicket,&provider.deliveredTickets[n],sizeof(row.copyTicket))==0){index=n;break;}
        CHECK(index<provider.deliveredTickets.size());
        if(row.copySubmitted){++submitted[index];CHECK(row.frame==LegacyAsync::DeliveryFrames[index]&&row.providerQueried&&row.newPair);
            if(row.copyPending){++busySubmissions;CHECK(!row.copyCompleted&&row.copyResult==std::uint32_t(graphics::TransferResult::Busy));
                if(index==1)CHECK(provider.suspendsWhileHeld[index]>0);}}
        if(!row.copyCompleted)continue;++completedRows;++completed[index];
        CHECK(provider.consumed[index]);
        if(row.copySubmitted){++sameFrameCompleted;++sameFrame[index];CHECK(!row.copyPreWaitCompleted&&!row.copyPending);}
        else CHECK(!row.providerQueried&&!row.newPair&&row.frame>LegacyAsync::DeliveryFrames[index]);
        CHECK(row.copySubmittedNs>0&&row.copyCompletedNs>=row.copySubmittedNs);
        CHECK(row.copyTicket.sequence&&row.copyTicket.predictedNs>0&&!row.copyRetired);
        if(row.copyPreWaitCompleted){CHECK(row.copyPreWaitPolled);
            CHECK(row.copyCompletedNs<=row.inputStartedNs);}
        else CHECK(row.copyCompletedNs>=row.inputReturnedNs);
        if(row.pairRejected){++rejectedRows;CHECK(row.copyCancelled);}
        else {++presentedRows;++presented[index];CHECK(row.worldLayer&&row.endSucceeded&&!row.copyCancelled);
            CHECK(row.retainedGeneration==row.copyTicket.trackingGeneration&&row.retainedPredictedNs==row.copyTicket.predictedNs);}
    }
    for(unsigned n=0;n<submitted.size();++n){CHECK(submitted[n]==1&&completed[n]==unsigned(provider.consumed[n]));
        if(n>=1&&n<=4)CHECK(presented[n]==sameFrame[n]);}
    CHECK(presented[0]==1&&presented[5]==1&&presented[6]==unsigned(provider.consumed[6]));
    CHECK(completedRows==positives&&positives==6+unsigned(provider.consumed[6]));
    CHECK(report.submittedPairs==presentedRows&&report.rejectedPairs==rejectedRows);
    CHECK(report.frameDiagnostics.copySubmittedFrames==7&&report.frameDiagnostics.copyCompletedFrames==positives);
    CHECK(sameFrameCompleted+busySubmissions==7&&report.frameDiagnostics.copyPendingFrames>=busySubmissions);
    CHECK(report.frameDiagnostics.copyCancelledFrames>=rejectedRows);
    CHECK(report.maxRetainedAgeNs<=runtime::RetainedPresentation::MaxAgeNs);
    std::cout<<"Legacy async actual-host GPU check: 7 exact acknowledgements, "<<sameFrameCompleted<<" same-frame completions, "
             <<busySubmissions<<" Busy submissions, "<<rejectedRows<<" presentation cancellations; continuing input and original poses passed\n";
    return 0;
}
// Private R29 source extension: the regression above remains default-off.
// This scope restores process-local opt-ins on every CHECK exit.
struct OrderedPresentEnvironment {
    struct Value {const wchar_t* name;std::wstring old;bool existed=false;};
    std::array<Value,2> values{{{L"FVR_PRIVATE_GPU_ORDERED_PRESENT"},{L"FVR_PRIVATE_HOST_COPY_SERVICE_MS"}}};
    bool okay=true,changed=false;
    explicit OrderedPresentEnvironment(bool ordered=true){
        for(auto& v:values){
            SetLastError(ERROR_SUCCESS);const auto size=GetEnvironmentVariableW(v.name,nullptr,0);
            if(size){std::wstring buffer(size,L'\0');const auto copied=GetEnvironmentVariableW(v.name,buffer.data(),size);
                if(!copied||copied>=size){okay=false;return;}buffer.resize(copied);v.old=std::move(buffer);v.existed=true;
            }else if(GetLastError()==ERROR_SUCCESS)v.existed=true;
            else if(GetLastError()!=ERROR_ENVVAR_NOT_FOUND){okay=false;return;}
        }
        changed=true; // Capture both old values before making either mutation.
        okay=SetEnvironmentVariableW(values[0].name,ordered?L"1":L"0")&&SetEnvironmentVariableW(values[1].name,L"0");
    }
    ~OrderedPresentEnvironment(){if(changed)for(const auto& v:values)SetEnvironmentVariableW(v.name,v.existed?v.old.c_str():nullptr);}
};
int GpuOrderedActualHostRegression(const std::filesystem::path& path,unsigned(*runtimeErrors)()){
    OrderedPresentEnvironment environment;CHECK(environment.okay);
    LegacyAsync provider;xr::HostOptions options;options.loaderPath=path;options.probeOnly=false;options.seconds=3;options.provider=&provider;
    const auto reportStorage=RunHostOnHeap(options);const auto& report=*reportStorage;
    if(!report.okay||!provider.error.empty())std::cerr<<"ordered host: "<<report.error<<" fixture: "<<provider.error<<'\n';
    CHECK(report.okay&&runtimeErrors()==0&&provider.errors==0&&provider.error.empty());
    CHECK(report.endedFrames==100&&provider.inputs==101&&report.inputFrames==100);
    CHECK(provider.next==LegacyAsync::DeliveryFrames.size()&&!provider.outstanding&&provider.queriesWhileHeld==0);
    const auto& ordered=report.privateGpuOrderedPresent;
    CHECK(ordered.configured&&ordered.available&&ordered.composed==7);
    CHECK(ordered.drainAttempted&&ordered.drained&&!ordered.drainError&&!ordered.retainedUntilProcessExit);
    CHECK(ordered.drainPolls>0&&report.privateHostCopyService.budgetMs==0);
    // All seven current-frame deliveries are composed before the following
    // focus/reference/shouldRender/head-valid discontinuities. The unchanged
    // fake runtime checks every pixel plus the original source pose and FOV in
    // EndFrame, including delivery100. A negative final source retirement does
    // not retroactively erase the submitted, separately verified XR image.
    CHECK(report.submittedPairs==7&&report.rejectedPairs==0);
    unsigned positives=0;
    for(unsigned n=0;n<LegacyAsync::DeliveryFrames.size();++n){
        CHECK(provider.acknowledgements[n]==1&&provider.ackFrames[n]>=LegacyAsync::DeliveryFrames[n]);
        if(n+1<LegacyAsync::DeliveryFrames.size())CHECK(provider.consumed[n]);
        else CHECK(provider.ackFrames[n]==100); // Actual query outcome decides consumed vs discarded.
        positives+=unsigned(provider.consumed[n]);
    }
    const auto& diagnostics=report.frameDiagnostics;
    CHECK(diagnostics.dropped==0&&diagnostics.copySubmittedFrames==7);
    CHECK(diagnostics.copyCompletedObservations==positives&&diagnostics.copyObservationOverflowFrames==0);
    CHECK(ordered.postEndCompletions+ordered.preWaitRetirements+ordered.postInputRetirements==positives);
    std::array<unsigned,7> submitted{},completed{},composedOnDelivery{};
    unsigned tailObserved=0,preWaitObserved=0,postInputObserved=0;
    const auto inspect=[&](const xr::HostCopyObservation& copy,const xr::HostFrameObservation& frame,bool prior)->int {
        if(!copy.copyTicket.sequence)return 0;
        unsigned index=unsigned(provider.deliveredTickets.size());
        for(unsigned n=0;n<provider.deliveredTickets.size();++n)
            if(std::memcmp(&provider.deliveredTickets[n],&copy.copyTicket,sizeof(copy.copyTicket))==0){index=n;break;}
        CHECK(index<provider.deliveredTickets.size());
        if(copy.copySubmitted){++submitted[index];CHECK(frame.frame==LegacyAsync::DeliveryFrames[index]);}
        if(copy.copyComposed){
            CHECK(copy.copyComposedNs>0&&copy.copyComposedNs>=copy.copySubmittedNs);
            if(frame.frame==LegacyAsync::DeliveryFrames[index]){
                ++composedOnDelivery[index];CHECK(!prior&&frame.newPair&&frame.worldLayer&&frame.endSucceeded);
            }
        }
        if(copy.copyCompleted){
            ++completed[index];CHECK(provider.consumed[index]&&!copy.copyRetired);
            CHECK(copy.copyCompletedNs>=copy.copySubmittedNs&&copy.copyCompletedNs>=copy.copyComposedNs);
            if(copy.copyPreWaitCompleted){++preWaitObserved;CHECK(copy.copyPreWaitPolled&&prior);
                CHECK(copy.copyCompletedNs<=frame.inputStartedNs);}
            if(copy.copyAfterEndCompleted){++tailObserved;CHECK(copy.copyAfterEndPolled&&!prior&&frame.endSucceeded);}
            if(copy.copyPostInputCompleted){++postInputObserved;CHECK(copy.copyPostInputPolled&&prior);
                CHECK(copy.copyCompletedNs>=frame.inputReturnedNs);}
            CHECK(unsigned(copy.copyPreWaitCompleted)+unsigned(copy.copyPostInputCompleted)+unsigned(copy.copyAfterEndCompleted)==1);
        }
        if(prior)CHECK(copy.copyCompleted&&copy.copyComposed&&!copy.copyPending);
        return 0;
    };
    for(unsigned n=0;n<diagnostics.count;++n){const auto& frame=diagnostics.Row(n);
        CHECK(!frame.copyObservationOverflow);
        if(frame.hasPriorCopy)CHECK(inspect(frame.priorCopy,frame,true)==0);
        CHECK(inspect(frame,frame,false)==0);
        if(frame.hasPriorCopy&&frame.copyTicket.sequence)
            CHECK(std::memcmp(&frame.copyTicket,&frame.priorCopy.copyTicket,sizeof(frame.copyTicket))!=0);
        if(frame.copyPending)CHECK(frame.inputAttempted&&frame.inputGeneration);
    }
    for(unsigned n=0;n<submitted.size();++n){
        CHECK(submitted[n]==1&&composedOnDelivery[n]==1&&completed[n]==unsigned(provider.consumed[n]));
    }
    CHECK(tailObserved==ordered.postEndCompletions&&preWaitObserved==ordered.preWaitRetirements&&postInputObserved==ordered.postInputRetirements);
    CHECK(ordered.postInputRetirements<=ordered.postInputPolls);
    CHECK(diagnostics.copyAfterEndCompletedFrames==tailObserved&&diagnostics.copyPreWaitCompletedFrames==preWaitObserved&&
        diagnostics.copyPostInputCompletedFrames==postInputObserved);
    CHECK(report.maxRetainedAgeNs<=runtime::RetainedPresentation::MaxAgeNs);
    std::cout<<"GPU-ordered actual-host: seven original-pose stereo submissions, exact one ACK per ticket, "
             <<tailObserved<<" tail completions, "<<preWaitObserved<<" pre-Wait completions, "<<postInputObserved<<" post-input completions, final graphics drain\n";
    return 0;
}

// Actual host integration, independent of the mapped-channel scheduling tests.
// These sources share one producer device but keep separate color/depth
// resources. The unchanged fake XR runtime checks every output pixel and the
// earlier source pose/FOV; no fake completion or new runtime entry point exists.
template<unsigned SourceCount=2>
struct TwoSourceAsync final:runtime::IFrameProvider {
    static_assert(SourceCount==2||SourceCount==3);
    static constexpr std::array<unsigned,11> DeliveryFrames{10,20,26,30,40,50,60,69,74,84,100};
    static constexpr auto Slots=SourceCount==2?std::array<unsigned,11>{0,1,0,1,1,0,1,0,1,0,1}:
        std::array<unsigned,11>{0,1,2,1,1,0,2,0,1,2,0};
    static constexpr unsigned BadDepth=3;
    enum class DepthFault {Stale,Missing,Unexpected};
    DepthFault depthFault=DepthFault::Stale;
    // Color-only before any depth, then color-only after slot B established it.
    static bool Absent(unsigned index)noexcept{return index==0||index==2;}
    struct Source {graphics::D3D11PairProducer color;graphics::D3D11SceneDepthProducer depth;std::uint64_t epoch=1;};
    std::array<Source,SourceCount> sources;
    ComPtr<ID3D11Device> device;ComPtr<ID3D11DeviceContext> context;
    std::array<ComPtr<ID3D11Texture2D>,2> images;
    ComPtr<ID3D11Texture2D> nativeDepth;ComPtr<ID3D11DepthStencilView> depthTarget;
    runtime::TrackingFrame previous{},heldTracking{};graphics::PairTicket held{};
    graphics::SceneDepthFrame heldDepth{};runtime::SourceSlotIdentity heldSource{};
    std::array<graphics::PairTicket,11> tickets{};
    std::array<graphics::TextureDescriptor,11> descriptors{};
    std::array<graphics::SceneDepthFrame,11> attachments{};
    std::array<unsigned,11> acknowledgements{},ackFrames{},slotReads{},depthReads{};
    std::array<bool,11> consumed{};
    struct RequestServiceObservation {
        unsigned inputOrder=0,feedbackOrder=0,serviceOrder=0,queryOrder=0,services=0,queries=0;
        bool sourceHeld=false,inputPublished=false,inputEligible=false;
        runtime::PresentationRequirements requirements{};
        runtime::TrackingFrame tracking{};
    };
    using RequestServiceTrace=std::array<RequestServiceObservation,101>;
    // The bounded trace contains full poses; keep it off the already large
    // actual-host test stack while retaining its fixed101-frame capacity.
    std::unique_ptr<RequestServiceTrace> requestServiceStorage=std::make_unique<RequestServiceTrace>();
    RequestServiceTrace& requestService=*requestServiceStorage;
    interaction::InputFrame latestInput{};
    unsigned order=0,lastInputOrder=0,lastFeedbackOrder=0,lastSuspendOrder=0,lastServiceOrder=0;
    unsigned requestServices=0,servicesWhileHeld=0,serviceSpaceChanges=0;
    std::uint64_t lastServiceSpace=0;
    unsigned next=0,active=0,inputFrame=0,inputs=0,queriesWhileHeld=0,errors=0,suspensions=0;
    bool outstanding=false;std::string error;
    bool Fail(const char* message){++errors;error=message;return false;}
    unsigned SourceSlotCount()const noexcept override{return SourceCount;}
    bool TryGetPair(const runtime::PresentationRequirements&,const runtime::TrackingFrame&,
        graphics::TextureDescriptor&,graphics::PairTicket&)noexcept override{return false;}
    void UpdateInput(const interaction::InputFrame& input)noexcept override {
        ++inputs;inputFrame=unsigned((input.predictedNs-1000000000)/11000000);
        latestInput=input;lastInputOrder=++order;
        if(inputFrame<requestService.size()&&!requestService[inputFrame].inputPublished){
            requestService[inputFrame].inputPublished=true;
            requestService[inputFrame].inputEligible=input.focused&&input.headValid;
        } // The final neutral teardown input must not rewrite frame100's evidence.
        if(outstanding&&inputFrame>DeliveryFrames[active])Sleep(1); // Fixture yield only, never fake GPU progress.
    }
    void Suspend()noexcept override{++suspensions;lastSuspendOrder=++order;previous={};}
    void ServiceFeedback()noexcept override{lastFeedbackOrder=++order;}
    void ServiceRequests(const runtime::PresentationRequirements& req,const runtime::TrackingFrame& current)noexcept override {
        // Observe the actual host service boundary without creating a pair or
        // completing a GPU query here. Mapped request admission is tested with
        // RemoteFrameProvider separately; this fixture owns real GPU sources.
        if(!inputFrame||inputFrame>=requestService.size()){Fail("request service outside an XR input frame");return;}
        auto& observation=requestService[inputFrame];
        if(++observation.services!=1||observation.queries||lastInputOrder<=lastServiceOrder||
           lastFeedbackOrder<=lastInputOrder||!latestInput.focused||!latestInput.headValid||
           !current.focused||!current.headValid||current.predictedNs!=latestInput.predictedNs||
           current.spaceGeneration!=latestInput.spaceGeneration){Fail("request service before current input/cancellation/feedback");return;}
        if(lastServiceSpace&&current.spaceGeneration!=lastServiceSpace){
            ++serviceSpaceChanges;
            if(lastSuspendOrder<=lastServiceOrder){Fail("new-space request service preceded suspension");return;}
        }
        observation.inputOrder=lastInputOrder;observation.feedbackOrder=lastFeedbackOrder;
        observation.serviceOrder=++order;observation.sourceHeld=outstanding;
        observation.requirements=req;observation.tracking=current;
        lastServiceOrder=observation.serviceOrder;lastServiceSpace=current.spaceGeneration;
        ++requestServices;servicesWhileHeld+=unsigned(outstanding);
    }
    bool Initialize(const runtime::PresentationRequirements& req){
        D3D_FEATURE_LEVEL level{};
        if(FAILED(D3D11CreateDevice(nullptr,D3D_DRIVER_TYPE_HARDWARE,nullptr,0,nullptr,0,D3D11_SDK_VERSION,&device,&level,&context)))return Fail("two-source device");
        D3D11_TEXTURE2D_DESC desc{};desc.Width=req.width;desc.Height=req.height;desc.MipLevels=desc.ArraySize=desc.SampleDesc.Count=1;desc.Format=DXGI_FORMAT(req.format);
        for(auto& image:images)if(FAILED(device->CreateTexture2D(&desc,nullptr,&image)))return Fail("two-source color input");
        desc.Format=DXGI_FORMAT_R24G8_TYPELESS;desc.BindFlags=D3D11_BIND_DEPTH_STENCIL;
        D3D11_DEPTH_STENCIL_VIEW_DESC view{};view.Format=DXGI_FORMAT_D24_UNORM_S8_UINT;view.ViewDimension=D3D11_DSV_DIMENSION_TEXTURE2D;
        if(FAILED(device->CreateTexture2D(&desc,nullptr,&nativeDepth))||FAILED(device->CreateDepthStencilView(nativeDepth.Get(),&view,&depthTarget)))return Fail("two-source depth input");
        for(auto& s:sources)if(!s.depth.Initialize(device.Get())||s.color.Create(device.Get(),req.width,req.height,DXGI_FORMAT(req.format),s.epoch,
            graphics::TextureSharing::LegacyFenced)!=graphics::TransferResult::Ok)return Fail("two-source producer creation");
        return true;
    }
    bool TryGetCompletedPair(const runtime::PresentationRequirements& req,const runtime::TrackingFrame& current,
        runtime::TrackingFrame& rendered,graphics::TextureDescriptor& descriptor,graphics::PairTicket& ticket)noexcept override {
        try {
            if(!inputFrame||inputFrame>=requestService.size())return Fail("provider query outside an XR input frame");
            auto& service=requestService[inputFrame];service.queryOrder=++order;
            if(++service.queries!=1||service.services!=1||service.serviceOrder>=service.queryOrder||service.sourceHeld||
               service.tracking.generation!=current.generation||service.tracking.spaceGeneration!=current.spaceGeneration||
               service.tracking.predictedNs!=current.predictedNs||service.requirements.width!=req.width||
               service.requirements.height!=req.height||service.requirements.format!=req.format||
               service.requirements.adapterLow!=req.adapterLow||service.requirements.adapterHigh!=req.adapterHigh)
                return Fail("provider delivery bypassed current request service or replaced its identity");
            if(outstanding){++queriesWhileHeld;return Fail("two-source queried with a delivered source owned");}
            const auto frame=unsigned((current.predictedNs-1000000000)/11000000);
            if(next==DeliveryFrames.size()||frame<DeliveryFrames[next]){previous=current;return false;}
            if(frame!=DeliveryFrames[next])return Fail("missed two-source delivery");
            if(!previous.generation||previous.spaceGeneration!=current.spaceGeneration||previous.predictedNs>=current.predictedNs)return Fail("missing two-source original pose");
            if(!device&&!Initialize(req))return false;
            const auto slot=Slots[next];auto& s=sources[slot];
            if(!graphics::Valid(s.color.Descriptor())){
                if(next!=BadDepth+1||slot!=1||s.epoch!=1)return Fail("unexpected source-generation retirement");
                ++s.epoch;
                if(s.color.Create(device.Get(),req.width,req.height,DXGI_FORMAT(req.format),s.epoch,graphics::TextureSharing::LegacyFenced)!=graphics::TransferResult::Ok)
                    return Fail("two-source recreation");
            }
            rendered=previous;heldTracking=rendered;
            const unsigned source=unsigned((rendered.predictedNs-1000000000)/11000000);
            const graphics::FrameMetadata metadata{source,rendered.spaceGeneration,rendered.generation,rendered.predictedNs};
            const bool withDepth=!Absent(next);heldDepth={};
            if(withDepth&&!s.depth.Begin(metadata))return Fail("two-source depth begin");
            for(unsigned eye=0;eye<2;++eye){
                const std::vector<std::uint32_t> pixels(std::size_t(req.width)*req.height,0xff5a0000u|((eye+1)<<8)|source);
                context->UpdateSubresource(images[eye].Get(),0,nullptr,pixels.data(),req.width*4,0);
                if(withDepth)for(unsigned stage=0;stage<2;++stage){
                    const float z=SourceCount==2?.1f+.1f*float(slot*4+eye*2+stage):.05f+.07f*float(slot*4+eye*2+stage);
                    context->ClearDepthStencilView(depthTarget.Get(),D3D11_CLEAR_DEPTH|D3D11_CLEAR_STENCIL,z,0);
                    if(!s.depth.Capture(eye,stage,nativeDepth.Get()))return Fail("two-source depth capture");
                }
            }
            const std::array<graphics::TextureSlice,2> slices{{{images[0].Get(),0},{images[1].Get(),0}}};
            const auto deadline=GetTickCount64()+500;graphics::TransferResult result;
            do{result=s.color.Publish(slices,metadata,ticket);if(result==graphics::TransferResult::Busy)Sleep(1);}
            while(result==graphics::TransferResult::Busy&&GetTickCount64()<deadline);
            if(result!=graphics::TransferResult::Ok)return Fail("two-source export fence");
            descriptor=s.color.Descriptor();
            if(withDepth&&!s.depth.Seal(descriptor,ticket,false,heldDepth))return Fail("two-source depth seal");
            auto later=metadata;++later.frameId;++later.trackingGeneration;graphics::PairTicket blocked;
            if(s.color.Publish(slices,later,blocked)!=graphics::TransferResult::Busy||(withDepth&&s.depth.Begin(later)))return Fail("delivered source became writable without ACK");
            held=ticket;active=next++;outstanding=true;previous={};
            heldSource={};heldSource.channelSession[0]=0x31;heldSource.channelSession[15]=0x17;
            heldSource.requestId=next;heldSource.slot=slot;heldSource.slotCount=SourceCount;
            heldSource.depthPresent=withDepth&&!(active==BadDepth&&depthFault==DepthFault::Unexpected);
            tickets[active]=ticket;descriptors[active]=descriptor;attachments[active]=heldDepth;
            return true;
        }catch(...){return Fail("two-source fixture exception");}
    }
    bool ReadSourceSlot(const graphics::PairTicket& ticket,runtime::SourceSlotIdentity& out)noexcept override {
        out={};if(!outstanding||std::memcmp(&ticket,&held,sizeof(ticket)))return Fail("wrong two-source slot read");
        ++slotReads[active];out=heldSource;return true;
    }
    bool ReadSceneDepth(const graphics::TextureDescriptor& descriptor,const graphics::PairTicket& ticket,graphics::SceneDepthFrame& out)noexcept override {
        out={};if(!outstanding||std::memcmp(&ticket,&held,sizeof(ticket))||std::memcmp(&descriptor,&descriptors[active],sizeof(descriptor)))return Fail("wrong two-source depth read");
        ++depthReads[active];
        if(Absent(active)||(active==BadDepth&&depthFault==DepthFault::Missing))return false;
        out=heldDepth;
        // Corrupt only the attachment result, never the actual held source.
        // Unexpected also advertises absence above, so even valid returned
        // depth must be rejected before any import/copy rather than accepted.
        if(active==BadDepth&&depthFault==DepthFault::Stale)++out.color.spaceGeneration;
        return true;
    }
    void PairConsumed(const graphics::PairTicket& ticket,bool okay)noexcept override {
        if(!outstanding||std::memcmp(&ticket,&held,sizeof(ticket))){Fail("duplicate or wrong two-source ACK");return;}
        if(ticket.predictedNs!=heldTracking.predictedNs||ticket.trackingGeneration!=heldTracking.generation||ticket.spaceGeneration!=heldTracking.spaceGeneration)
            Fail("two-source ACK changed original pose identity");
        ++acknowledgements[active];ackFrames[active]=inputFrame;consumed[active]=okay;
        if(active==BadDepth&&okay)Fail("invalid depth attachment received positive ACK");
        auto& s=sources[Slots[active]];if(!Absent(active))s.depth.Acknowledge(ticket,okay);s.color.Acknowledge(ticket,okay);
        outstanding=false;heldSource={};
    }
};
struct SourceImportLog {
    std::filesystem::path directory;
    SourceImportLog(){
        directory=std::filesystem::temp_directory_path()/(L"fvr-two-source-host-"+std::to_wstring(GetCurrentProcessId())+L"-"+std::to_wstring(GetTickCount64()));
        if(!std::filesystem::create_directory(directory))directory.clear();
    }
    ~SourceImportLog(){if(!directory.empty()){std::error_code ec;std::filesystem::remove(directory/L"ready.json",ec);
        std::filesystem::remove(directory/L"xr-events.jsonl",ec);std::filesystem::remove(directory,ec);}}
    std::string Snapshot()const {
        const auto path=directory/L"xr-events.jsonl";if(std::filesystem::file_size(path)>1024*1024)return {};
        std::ifstream input(path);std::string line,result;unsigned count=0;
        while(std::getline(input,line))if(line.starts_with("{\"event\":\"host_source_imports\",")){result=line;++count;}
        return count==1?result:std::string{};
    }
};
bool SourceImportCounter(const std::string& row,const char* name,std::uint64_t expected){
    const auto prefix=std::string("\"")+name+"\":";const auto begin=row.find(prefix);
    if(begin==std::string::npos||row.find(prefix,begin+1)!=std::string::npos)return false;
    const auto first=begin+prefix.size(),end=row.find_first_not_of("0123456789",first);
    if(end==first||end==std::string::npos||(row[end]!=','&&row[end]!='}'))return false;
    return row.substr(first,end-first)==std::to_string(expected);
}
template<unsigned SourceCount=2>
int TwoSourceActualHostRegression(const std::filesystem::path& path,unsigned(*runtimeErrors)(),bool ordered,
    typename TwoSourceAsync<SourceCount>::DepthFault fault=TwoSourceAsync<SourceCount>::DepthFault::Stale){
    using Provider=TwoSourceAsync<SourceCount>;
    OrderedPresentEnvironment environment(ordered);CHECK(environment.okay);
    SourceImportLog log;CHECK(!log.directory.empty());Provider provider;provider.depthFault=fault;
    xr::HostOptions options;options.loaderPath=path;options.probeOnly=false;options.seconds=3;options.provider=&provider;options.readyPath=log.directory/L"ready.json";
    const auto reportStorage=RunHostOnHeap(options);const auto& report=*reportStorage;
    if(!report.okay||!provider.error.empty())std::cerr<<"two-source host: "<<report.error<<" fixture: "<<provider.error<<'\n';
    CHECK(report.okay&&runtimeErrors()==0&&provider.errors==0&&provider.error.empty());
    CHECK(report.endedFrames==100&&provider.inputs==101&&report.inputFrames==100);
    CHECK(provider.next==provider.DeliveryFrames.size()&&!provider.outstanding&&provider.queriesWhileHeld==0&&provider.suspensions>0);
    CHECK(report.waitedFrames==report.endedFrames&&report.frameDiagnostics.frames==report.endedFrames);
    unsigned eligibleServiceFrames=0,heldServiceFrames=0;
    for(unsigned frame=1;frame<provider.requestService.size();++frame){const auto& service=provider.requestService[frame];
        CHECK(service.inputPublished&&service.services==unsigned(service.inputEligible)&&service.queries<=1);
        if(!service.inputEligible){CHECK(!service.queryOrder);continue;}
        ++eligibleServiceFrames;heldServiceFrames+=unsigned(service.sourceHeld);
        CHECK(service.inputOrder<service.feedbackOrder&&service.feedbackOrder<service.serviceOrder);
        if(service.queries)CHECK(service.serviceOrder<service.queryOrder&&!service.sourceHeld);
        if(service.sourceHeld)CHECK(!service.queries);
    }
    // The production journal retains changed/slow rows, not every XR frame.
    for(unsigned n=0;n<report.frameDiagnostics.count;++n){const auto& frame=report.frameDiagnostics.Row(n);
        CHECK(frame.frame>0&&frame.frame<provider.requestService.size());
        const auto& service=provider.requestService[std::size_t(frame.frame)];
        const bool allowed=frame.shouldRender&&frame.focused&&frame.trackingValid&&!frame.menu;
        CHECK(service.services==unsigned(allowed)&&service.queries==unsigned(frame.providerQueried));
        if(!allowed){CHECK(!service.queryOrder);continue;}
        CHECK(frame.inputAttempted&&service.inputOrder<service.feedbackOrder&&service.feedbackOrder<service.serviceOrder);
        CHECK(service.tracking.predictedNs==frame.predictedNs&&service.tracking.spaceGeneration==frame.space);
        if(service.queries)CHECK(service.serviceOrder<service.queryOrder&&!service.sourceHeld);
        if(service.sourceHeld)CHECK(!frame.providerQueried&&!frame.newPair&&!frame.copySubmitted);
    }
    CHECK(provider.requestServices==eligibleServiceFrames&&provider.servicesWhileHeld==heldServiceFrames);
    CHECK(eligibleServiceFrames>0&&eligibleServiceFrames<report.endedFrames);
    CHECK(provider.serviceSpaceChanges>0); // Existing runtime reference-space replacement.
    CHECK(report.sceneDepthReceived==(fault==Provider::DepthFault::Missing?8u:9u));
    CHECK(report.sceneDepthCopied==8&&report.sceneDepthOmitted==2);
    CHECK(report.privateHostCopyService.budgetMs==0&&report.privateGpuOrderedPresent.configured==ordered);
    if(ordered)CHECK(report.submittedPairs==10&&report.rejectedPairs==1);
    if(ordered)CHECK(report.privateGpuOrderedPresent.composed==10&&report.privateGpuOrderedPresent.drained&&
        !report.privateGpuOrderedPresent.drainError&&!report.privateGpuOrderedPresent.retainedUntilProcessExit);
    unsigned positives=0;
    for(unsigned n=0;n<provider.DeliveryFrames.size();++n){
        CHECK(provider.acknowledgements[n]==1&&provider.ackFrames[n]>=provider.DeliveryFrames[n]);
        CHECK(provider.slotReads[n]==1&&provider.depthReads[n]==1);
        if(n==provider.BadDepth)CHECK(!provider.consumed[n]&&provider.ackFrames[n]==30);
        else if(n+1<provider.DeliveryFrames.size())CHECK(provider.consumed[n]);
        if(n+1==provider.DeliveryFrames.size())CHECK(provider.ackFrames[n]==100);
        positives+=unsigned(provider.consumed[n]);
    }
    // Exact per-slot allocations survive A/B[/C]/A reuse. Only rejected B advances.
    if constexpr(SourceCount==2)CHECK(std::memcmp(&provider.descriptors[0],&provider.descriptors[2],sizeof(graphics::TextureDescriptor))==0);
    else {CHECK(provider.descriptors[0].session!=provider.descriptors[2].session&&provider.descriptors[1].session!=provider.descriptors[2].session);
        CHECK(std::memcmp(&provider.descriptors[2],&provider.descriptors[6],sizeof(graphics::TextureDescriptor))==0);
        CHECK(std::memcmp(&provider.descriptors[2],&provider.descriptors[9],sizeof(graphics::TextureDescriptor))==0);
        CHECK(graphics::SceneDepthMatches(provider.attachments[6],provider.descriptors[6],provider.tickets[6]));
        CHECK(provider.attachments[6].resources==provider.attachments[9].resources);
        CHECK(provider.attachments[6].resources!=provider.attachments[5].resources&&provider.attachments[6].resources!=provider.attachments[4].resources);}
    CHECK(graphics::SceneDepthAbsent(provider.attachments[0])&&graphics::SceneDepthAbsent(provider.attachments[2]));
    CHECK(provider.descriptors[0].session!=provider.descriptors[1].session);
    CHECK(graphics::SceneDepthMatches(provider.attachments[1],provider.descriptors[1],provider.tickets[1]));
    CHECK(graphics::SceneDepthMatches(provider.attachments[5],provider.descriptors[5],provider.tickets[5]));
    CHECK(provider.attachments[5].resources!=provider.attachments[1].resources);
    CHECK(provider.descriptors[4].resourceEpoch>provider.descriptors[3].resourceEpoch);
    CHECK(provider.attachments[4].resourceGeneration>provider.attachments[3].resourceGeneration&&provider.attachments[4].resources!=provider.attachments[3].resources);
    CHECK(std::memcmp(&provider.descriptors[0],&provider.descriptors[5],sizeof(graphics::TextureDescriptor))==0);
    std::array<unsigned,11> submitted{},completed{},composed{};
    unsigned normalPresented=0,normalRejected=0;
    const auto inspect=[&](const xr::HostCopyObservation& copy,const xr::HostFrameObservation& frame)->int {
        if(!copy.copyTicket.sequence)return 0;unsigned index=unsigned(provider.tickets.size());
        for(unsigned n=0;n<provider.tickets.size();++n)if(std::memcmp(&copy.copyTicket,&provider.tickets[n],sizeof(copy.copyTicket))==0){index=n;break;}
        CHECK(index<provider.tickets.size());
        if(copy.copySubmitted){++submitted[index];CHECK(frame.frame==provider.DeliveryFrames[index]);}
        if(copy.copyCompleted){++completed[index];CHECK(provider.consumed[index]&&copy.copyCompletedNs>=copy.copySubmittedNs);
            if(!ordered){
                if(frame.pairRejected){++normalRejected;CHECK(copy.copyCancelled);}
                else {++normalPresented;CHECK(frame.worldLayer&&frame.endSucceeded&&!copy.copyCancelled);
                    CHECK(frame.retainedGeneration==copy.copyTicket.trackingGeneration&&frame.retainedPredictedNs==copy.copyTicket.predictedNs);}
                if(copy.copySubmitted)CHECK(frame.frame==provider.DeliveryFrames[index]&&!copy.copyPending&&!copy.copyPreWaitCompleted);
            }}
        if(copy.copyComposed&&frame.frame==provider.DeliveryFrames[index]){++composed[index];CHECK(ordered&&frame.worldLayer&&frame.endSucceeded);}
        return 0;
    };
    for(unsigned n=0;n<report.frameDiagnostics.count;++n){const auto& row=report.frameDiagnostics.Row(n);CHECK(!row.copyObservationOverflow);
        CHECK(inspect(row,row)==0);if(row.hasPriorCopy)CHECK(inspect(row.priorCopy,row)==0);}
    for(unsigned n=0;n<provider.tickets.size();++n){CHECK(submitted[n]==unsigned(n!=provider.BadDepth));
        CHECK(completed[n]==unsigned(provider.consumed[n]));if(ordered)CHECK(composed[n]==unsigned(n!=provider.BadDepth));}
    CHECK(report.frameDiagnostics.copyCompletedObservations==positives&&report.frameDiagnostics.copySubmittedFrames==10);
    if(!ordered)CHECK(report.submittedPairs==normalPresented&&report.rejectedPairs==normalRejected+1);
    const auto snapshot=log.Snapshot();CHECK(!snapshot.empty());
    CHECK(SourceImportCounter(snapshot,"source_slots",SourceCount));
    CHECK(SourceImportCounter(snapshot,"color_open_attempts",SourceCount+1)&&SourceImportCounter(snapshot,"color_cache_hits",10-SourceCount));
    CHECK(SourceImportCounter(snapshot,"lifetime_resets",1)&&SourceImportCounter(snapshot,"rejected",0));
    CHECK(SourceImportCounter(snapshot,"poisoned",11-positives));
    // Statistics count individual textures: each source and recreated B import four,
    // while every frame reuses the same local quartet.
    CHECK(SourceImportCounter(snapshot,"depth_source_opens",4*(SourceCount+1))&&SourceImportCounter(snapshot,"depth_local_creates",4));
    std::cout<<SourceCount<<"-source actual-host ordered="<<ordered<<" depthFault="<<unsigned(fault)<<": per-slot optional depth, original poses, exact ACKs, malformed-depth retirement/recreation, one local depth quartet and discontinuity/stop lifecycle passed; post-input request services="<<provider.requestServices<<", with source held="<<provider.servicesWhileHeld<<" (observed, not forced GPU delay)\n";
    return 0;
}

int wmain(int argc,wchar_t** argv){
    if(argc==2&&std::wstring(argv[1])==L"--normal-copy-poll")return NormalCopyPollRegression();
    if(argc==2&&std::wstring(argv[1])==L"--lifetime-child"){Sleep(1000);return 0;}CHECK(argc==2);const auto path=std::filesystem::absolute(argv[1]);HMODULE runtime=LoadLibraryExW(path.c_str(),nullptr,LOAD_LIBRARY_SEARCH_DLL_LOAD_DIR|LOAD_LIBRARY_SEARCH_SYSTEM32);CHECK(runtime);
    CHECK(NormalCopyPollRegression()==0);
    Delayed provider;xr::HostOptions options;options.loaderPath=path;options.probeOnly=false;options.seconds=3;options.provider=&provider;
    const auto reportStorage=RunHostOnHeap(options);const auto& report=*reportStorage;
    if(!report.okay)std::cerr<<report.error<<'\n';CHECK(report.okay);
    const auto errors=reinterpret_cast<unsigned(*)()>(GetProcAddress(runtime,"FakeXrErrors"));
    const auto reuses=reinterpret_cast<unsigned(*)()>(GetProcAddress(runtime,"FakeXrReuses"));
    const auto layers=reinterpret_cast<unsigned(*)()>(GetProcAddress(runtime,"FakeXrLayers"));CHECK(errors&&reuses&&layers);
    std::cout<<"frames="<<report.endedFrames<<" fresh="<<report.submittedPairs<<" reused="<<report.reusedFrames<<" blank="<<report.blankFrames<<" rejected="<<report.rejectedPairs<<" errors="<<errors()<<'\n';
    CHECK(errors()==0);CHECK(report.endedFrames==100);CHECK(report.reusedFrames>35);CHECK(reuses()>35);CHECK(report.presentedFrames==layers());
    CHECK(report.presentedFrames==report.submittedPairs+report.reusedFrames);CHECK(report.endedFrames==report.presentedFrames+report.blankFrames);
    CHECK(report.submittedPairs==provider.consumed);CHECK(provider.faultInjected&&provider.invalidPoseInjected&&provider.discarded==2&&report.rejectedPairs==2);
    CHECK(report.maxRetainedAgeNs<=250000000&&report.maxRetainedAgeNs>100000000);CHECK(provider.error.empty());
    const auto& diagnostic=report.frameDiagnostics;
    CHECK(diagnostic.frames==report.endedFrames&&diagnostic.noWorldFrames==report.blankFrames);
    CHECK(diagnostic.zeroLayerFrames==report.blankFrames&&diagnostic.menuOnlyFrames==0);
    CHECK(diagnostic.inputUnknown==report.inputFrames&&diagnostic.inputSuccesses==0);
    CHECK(diagnostic.noWorldReasonFrames[1]>0&&diagnostic.noWorldReasonFrames[2]>0);
    // The fixture's frame51 space reset discards the retained pair before it
    // reaches250ms; age expiry is covered by HostFrameDiagnosticsTests.
    CHECK(diagnostic.noWorldReasonFrames[4]>0&&diagnostic.noWorldReasonFrames[7]>0);
    CHECK(diagnostic.noWorldReasonFrames[10]==0&&diagnostic.noWorldReasonFrames[14]>0);
    CHECK(diagnostic.count<=xr::HostFrameDiagnostics::Capacity);
    // Unlimited mode must have a stop source, and must exit when it signals.
    xr::HostOptions continuous;continuous.loaderPath=path;continuous.probeOnly=false;continuous.seconds=0;
    const auto invalidStorage=RunHostOnHeap(continuous);const auto& invalid=*invalidStorage;
    CHECK(!invalid.okay&&!invalid.instanceCreated);
    unsigned polls=0;continuous.stopRequested=[&]{return ++polls>=10;};
    const auto stoppedStorage=RunHostOnHeap(continuous);const auto& stopped=*stoppedStorage;
    CHECK(stopped.okay&&stopped.endedFrames>0&&stopped.endedFrames<20);CHECK(errors()==0);
    const auto executable=std::make_unique<wchar_t[]>(32768);CHECK(GetModuleFileNameW(nullptr,executable.get(),32768));
    const auto filename=std::filesystem::path(executable.get()).filename().wstring();
    platform::ProcessLifetime wrong;CHECK(!wrong.Open(GetCurrentProcessId(),L"not-the-process.exe"));
    platform::ProcessLifetime own;CHECK(own.Open(GetCurrentProcessId(),filename.c_str())&&!own.Ended());
    STARTUPINFOW startup{};startup.cb=sizeof(startup);PROCESS_INFORMATION child{};
    auto command=std::wstring(L"\"")+executable.get()+L"\" --lifetime-child";
    CHECK(CreateProcessW(nullptr,command.data(),nullptr,nullptr,FALSE,CREATE_NO_WINDOW,nullptr,nullptr,&startup,&child));
    platform::ProcessLifetime lifetime;CHECK(lifetime.Open(child.dwProcessId,filename.c_str())&&!lifetime.Ended());
    CHECK(WaitForSingleObject(child.hProcess,5000)==WAIT_OBJECT_0&&lifetime.Ended());CloseHandle(child.hThread);CloseHandle(child.hProcess);
    std::cout<<"Continuous stop callback and exact-process lifetime checks passed\n";
    const auto stage=reinterpret_cast<void(*)(bool)>(GetProcAddress(runtime,"FakeXrStageAvailable"));CHECK(stage);
    for(bool floor:{true,false}){
        stage(floor);InputObserver input;xr::HostOptions control;control.loaderPath=path;control.probeOnly=false;control.seconds=3;control.provider=&input;
        control.controllers=control.roomscale=true;control.worldUnitsPerMeter=2.5f;
        const auto observedStorage=RunHostOnHeap(control);const auto& observed=*observedStorage;
        if(!observed.okay)std::cerr<<observed.error<<'\n';
        CHECK(observed.okay&&observed.controllersEnabled&&observed.controllerProfiles==3&&observed.floorRelative==floor);
        CHECK(input.invalid==0&&input.samples==101&&input.released>=5&&input.tracked>80&&input.floor==floor);
        CHECK(input.sawUntracked&&input.sawInactive&&input.sawBadStick&&input.sawValidPose&&errors()==0);
        CHECK(observed.frameDiagnostics.inputAttempts==observed.inputFrames);
        CHECK(observed.frameDiagnostics.inputFailures>0&&observed.frameDiagnostics.inputSuccesses>0);
        CHECK(observed.frameDiagnostics.inputUnknown==0&&observed.frameDiagnostics.lastInputSuccessGeneration>0);
    }
    std::cout<<"OpenXR controller lifecycle, predicted poses, loss handling, scale and STAGE/LOCAL fallback passed\n";
    CHECK(LegacyAsyncHostRegression(path,errors)==0);
    CHECK(GpuOrderedActualHostRegression(path,errors)==0);
    CHECK(TwoSourceActualHostRegression(path,errors,false)==0);
    CHECK(TwoSourceActualHostRegression(path,errors,true,TwoSourceAsync<>::DepthFault::Missing)==0);
    CHECK(TwoSourceActualHostRegression(path,errors,true,TwoSourceAsync<>::DepthFault::Unexpected)==0);
    CHECK(TwoSourceActualHostRegression<3>(path,errors,false)==0);
    CHECK(TwoSourceActualHostRegression<3>(path,errors,true)==0);
    FreeLibrary(runtime);return 0;
}
