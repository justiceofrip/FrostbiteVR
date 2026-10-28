#pragma once
#include "fvr/runtime/PresentationPolicy.h"
#include "fvr/runtime/FrameSourceSlot.h"
#include "fvr/interaction/ControllerInput.h"
#include "fvr/interaction/Feedback.h"
#include "fvr/graphics/BodyPropFrame.h"
#include "fvr/graphics/SceneDepthFrame.h"
namespace fvr::runtime {
// Local C++ contract shared by native producers, IPC and XR presentation.
// Not an exported ABI: serialize explicitly at process/module boundaries.
struct InputPublicationResult {bool known=false,succeeded=false;std::uint32_t code=0;};
class IFrameProvider {
public:
    virtual ~IFrameProvider()=default;
    // Called on the frame thread. Use a bounded wait; false means no fresh pair.
    // The presenter may retain its own last complete images with their render poses.
    virtual bool TryGetPair(const PresentationRequirements&,const TrackingFrame&,
        graphics::TextureDescriptor&,graphics::PairTicket&) noexcept=0;
    // Asynchronous sources may complete an earlier request. Its ORIGINAL tracking
    // is returned explicitly and must match the ticket; current is never substituted.
    virtual bool TryGetCompletedPair(const PresentationRequirements& r,const TrackingFrame& current,
        TrackingFrame& rendered,graphics::TextureDescriptor& d,graphics::PairTicket& t) noexcept {
        rendered=current;return TryGetPair(r,current,d,t);
    }
    // Explicit negotiated delivery layout. Count2 never falls back to count1
    // when the exact delivered metadata is absent or stale.
    virtual unsigned SourceSlotCount()const noexcept{return 1;}
    virtual bool ReadSourceSlot(const graphics::PairTicket&,SourceSlotIdentity& out)noexcept {out={};return false;}
    // Optional render-only metadata for the EXACT delivered image pair.
    virtual bool ReadBodyProps(const graphics::PairTicket&,graphics::BodyPropFrame& out)noexcept {out={};return false;}
    virtual bool ReadSceneDepth(const graphics::TextureDescriptor&,const graphics::PairTicket&,graphics::SceneDepthFrame& out)noexcept {out={};return false;}
    virtual void UpdateInput(const interaction::InputFrame&) noexcept {}
    // Optional local diagnostic result. Legacy providers still receive exactly
    // one UpdateInput call; lack of an acknowledgement remains UNKNOWN.
    virtual InputPublicationResult UpdateInputObserved(const interaction::InputFrame& input)noexcept {
        UpdateInput(input);return {};
    }
    virtual bool TakeFeedback(interaction::FeedbackEvent& event)noexcept {event={};return false;}
    virtual void Suspend() noexcept {}
    // Retry deferred exact-ticket feedback without admitting a successor. The
    // caller must process known cancellation first. Local contract, not wire ABI.
    virtual void ServiceFeedback() noexcept {}
    // Optional nonblocking request admission on the frame thread, AFTER this
    // frame's input and all known cancellation, including while a source copy
    // is held. It never delivers pixels or acknowledges a GPU operation.
    virtual void ServiceRequests(const PresentationRequirements&,const TrackingFrame&) noexcept {}
    // False means the pair was not consumed; reset/reclaim that texture stream.
    // True reports GPU consumption, not that xrEndFrame succeeded or was seen.
    virtual void PairConsumed(const graphics::PairTicket&,bool consumed) noexcept=0;
};
}
