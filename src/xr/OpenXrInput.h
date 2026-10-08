#pragma once
#include "fvr/interaction/ControllerInput.h"
#include "fvr/interaction/Feedback.h"
#include <openxr/openxr.h>
namespace fvr::xr {
// XR handles and interaction profiles stay inside the runtime layer.
class OpenXrInput {
public:
    ~OpenXrInput();
    void Initialize(XrInstance,XrSession,PFN_xrGetInstanceProcAddr);
    void Sample(XrSpace,XrTime,interaction::InputFrame&);
    unsigned ProfilesSuggested()const noexcept{return profiles_;}
    void Feedback(const interaction::InputFrame&,std::int64_t now,const interaction::FeedbackEvent* event=nullptr)noexcept;
    void StopFeedback()noexcept;
    std::uint64_t FeedbackApplied()const noexcept{return feedbackApplied_;}
    std::uint64_t FeedbackRejected()const noexcept{return feedbackRejected_;}
    std::uint64_t FeedbackErrors()const noexcept{return feedbackErrors_;}
    std::uint64_t CaptureFeedbackApplied()const noexcept{return captureFeedbackApplied_;}
    std::uint64_t ReceiptFeedbackApplied()const noexcept{return receiptFeedbackApplied_;}
    std::uint64_t LastFeedbackEvent()const noexcept{return lastFeedbackEvent_;}
private:
    XrInstance instance_=XR_NULL_HANDLE;XrSession session_=XR_NULL_HANDLE;
    XrActionSet set_=XR_NULL_HANDLE;
    std::array<XrPath,2> paths_{};
    enum ActionIndex {GripPose,AimPose,Trigger,Squeeze,Stick,Primary,Secondary,StickClick,Menu,ThumbTouch,IndexTouch,Vibration,Count};
    std::array<XrAction,Count> actions_{};
    std::array<std::array<XrSpace,2>,2> spaces_{};
    unsigned profiles_=0;
    interaction::HapticFeedbackPolicy feedback_;
    std::uint64_t feedbackApplied_=0,feedbackRejected_=0,feedbackErrors_=0;
    std::uint64_t captureFeedbackApplied_=0,receiptFeedbackApplied_=0,lastFeedbackEvent_=0;
#define FIELD(n) PFN_xr##n n=nullptr;
    FIELD(StringToPath) FIELD(CreateActionSet) FIELD(DestroyActionSet) FIELD(CreateAction)
    FIELD(SuggestInteractionProfileBindings) FIELD(AttachSessionActionSets) FIELD(CreateActionSpace)
    FIELD(DestroySpace) FIELD(SyncActions) FIELD(GetActionStatePose) FIELD(GetActionStateFloat)
    FIELD(GetActionStateVector2f) FIELD(GetActionStateBoolean) FIELD(LocateSpace)
    FIELD(ApplyHapticFeedback) FIELD(StopHapticFeedback)
#undef FIELD
    XrPath Path(const char*);
};
}
