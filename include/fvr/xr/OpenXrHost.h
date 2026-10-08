#pragma once
#include "fvr/runtime/FrameProvider.h"
#include "fvr/graphics/D3D11BodyPropCompositor.h"
#include "fvr/interaction/ControllerInput.h"
#include <filesystem>
#include <string>
#include <functional>
namespace fvr::ipc {class MenuChannel;}
namespace fvr::xr {
using IFrameProvider=runtime::IFrameProvider;
struct HostOptions {
    std::filesystem::path loaderPath;
    bool probeOnly=true;
    bool controllers=false,roomscale=false;
    float worldUnitsPerMeter=1;
    unsigned seconds=15; // Zero requires a stopRequested lifetime source.
    std::function<bool()> stopRequested;
    unsigned eyeWidth=0,eyeHeight=0; // Zero selects the runtime recommendation.
    std::filesystem::path readyPath; // Optional bounded-launcher readiness file.
    IFrameProvider* provider=nullptr;
    ipc::MenuChannel* menu=nullptr;
    std::shared_ptr<const graphics::BodyPropCatalog> bodyProps;
};
struct HostReport {
    bool instanceCreated=false,systemAvailable=false,sessionCreated=false,okay=false;
    std::string runtimeName,systemName,error;
    runtime::PresentationRequirements requirements{};
    std::uint32_t minimumFeatureLevel=0;
    std::uint64_t waitedFrames=0,endedFrames=0,validTrackingFrames=0,submittedPairs=0,rejectedPairs=0;
    std::uint64_t presentedFrames=0,reusedFrames=0,blankFrames=0,providerMisses=0,maxProviderWaitUs=0,maxRetainedAgeNs=0;
    bool controllersEnabled=false,floorRelative=false,userPresenceSupported=false;
    std::uint64_t userPresenceEvents=0;
    unsigned controllerProfiles=0;
    std::uint64_t inputFrames=0,trackedHandFrames=0,recenters=0,automaticRecenters=0;
    interaction::RecenterEvidence recenterInput{};
    std::uint64_t menuFrames=0,menuErrors=0;
    graphics::BodyPropComposeStats bodyProps{};bool bodyPropsReady=false;
    bool ammoCounterReady=false;
    std::uint64_t ammoCounterFrames=0,ammoCounterUploads=0,ammoCounterErrors=0,ammoCounterValidSamples=0,ammoCounterInvalidSamples=0;
    std::uint64_t feedbackApplied=0,feedbackRejected=0,feedbackErrors=0;
    std::uint64_t captureFeedbackApplied=0,receiptFeedbackApplied=0,lastFeedbackEvent=0;
    std::int32_t lastSessionState=0;
};
// Uses the installed active runtime; never changes the registry or game settings.
HostReport RunOpenXrHost(const HostOptions&) noexcept;
}
