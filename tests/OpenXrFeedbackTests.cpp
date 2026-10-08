#define XR_NO_PROTOTYPES
#include "OpenXrInput.h"
#include "Test.h"
#include <cstring>
#include <map>
#include <string>
#include <iostream>
#include <type_traits>
using namespace fvr;
namespace {
template<class T>T Handle(std::uintptr_t value){if constexpr(std::is_pointer_v<T>)return reinterpret_cast<T>(value);else return static_cast<T>(value);}
std::map<std::string,XrPath> paths;unsigned actions=0,bindings=0,applied=0,stopped=0,errors=0;
XrAction vibration{};XrPath lastHand{};XrHapticVibration lastPulse{XR_TYPE_HAPTIC_VIBRATION};XrResult applyResult=XR_SUCCESS;
XrResult XRAPI_CALL StringToPath(XrInstance,const char* name,XrPath* out){auto [it,inserted]=paths.emplace(name,XrPath(paths.size()+1));(void)inserted;*out=it->second;return XR_SUCCESS;}
XrResult XRAPI_CALL CreateActionSet(XrInstance,const XrActionSetCreateInfo*,XrActionSet* out){*out=Handle<XrActionSet>(std::uintptr_t(1));return XR_SUCCESS;}
XrResult XRAPI_CALL DestroyActionSet(XrActionSet){return XR_SUCCESS;}
XrResult XRAPI_CALL CreateAction(XrActionSet,const XrActionCreateInfo* info,XrAction* out){*out=Handle<XrAction>(std::uintptr_t(++actions));
    if(std::strcmp(info->actionName,"feedback")==0){vibration=*out;if(info->actionType!=XR_ACTION_TYPE_VIBRATION_OUTPUT||info->countSubactionPaths!=2)++errors;}return XR_SUCCESS;}
XrResult XRAPI_CALL SuggestInteractionProfileBindings(XrInstance,const XrInteractionProfileSuggestedBinding* info){
    unsigned haptic=0;for(unsigned n=0;n<info->countSuggestedBindings;++n)if(info->suggestedBindings[n].action==vibration){++haptic;
        const auto path=info->suggestedBindings[n].binding;if(path!=paths["/user/hand/left/output/haptic"]&&path!=paths["/user/hand/right/output/haptic"])++errors;}
    if(haptic!=2)++errors;++bindings;return XR_SUCCESS;}
XrResult XRAPI_CALL AttachSessionActionSets(XrSession,const XrSessionActionSetsAttachInfo*){return XR_SUCCESS;}
XrResult XRAPI_CALL CreateActionSpace(XrSession,const XrActionSpaceCreateInfo*,XrSpace* out){*out=Handle<XrSpace>(std::uintptr_t(42));return XR_SUCCESS;}
XrResult XRAPI_CALL DestroySpace(XrSpace){return XR_SUCCESS;}
XrResult XRAPI_CALL SyncActions(XrSession,const XrActionsSyncInfo*){return XR_SUCCESS;}
XrResult XRAPI_CALL GetActionStatePose(XrSession,const XrActionStateGetInfo*,XrActionStatePose*){return XR_SUCCESS;}
XrResult XRAPI_CALL GetActionStateFloat(XrSession,const XrActionStateGetInfo*,XrActionStateFloat*){return XR_SUCCESS;}
XrResult XRAPI_CALL GetActionStateVector2f(XrSession,const XrActionStateGetInfo*,XrActionStateVector2f*){return XR_SUCCESS;}
XrResult XRAPI_CALL GetActionStateBoolean(XrSession,const XrActionStateGetInfo*,XrActionStateBoolean*){return XR_SUCCESS;}
XrResult XRAPI_CALL LocateSpace(XrSpace,XrSpace,XrTime,XrSpaceLocation*){return XR_SUCCESS;}
XrResult XRAPI_CALL ApplyHapticFeedback(XrSession,const XrHapticActionInfo* info,const XrHapticBaseHeader* pulse){++applied;
    if(info->action!=vibration||pulse->type!=XR_TYPE_HAPTIC_VIBRATION)++errors;lastHand=info->subactionPath;
    lastPulse=*reinterpret_cast<const XrHapticVibration*>(pulse);return applyResult;}
XrResult XRAPI_CALL StopHapticFeedback(XrSession,const XrHapticActionInfo* info){++stopped;if(info->action!=vibration)++errors;return XR_SUCCESS;}
XrResult XRAPI_CALL Get(XrInstance,const char* name,PFN_xrVoidFunction* out){
#define FN(n) if(std::strcmp(name,"xr" #n)==0){*out=reinterpret_cast<PFN_xrVoidFunction>(n);return XR_SUCCESS;}
    FN(StringToPath) FN(CreateActionSet) FN(DestroyActionSet) FN(CreateAction) FN(SuggestInteractionProfileBindings)
    FN(AttachSessionActionSets) FN(CreateActionSpace) FN(DestroySpace) FN(SyncActions) FN(GetActionStatePose)
    FN(GetActionStateFloat) FN(GetActionStateVector2f) FN(GetActionStateBoolean) FN(LocateSpace) FN(ApplyHapticFeedback) FN(StopHapticFeedback)
#undef FN
    *out=nullptr;return XR_ERROR_FUNCTION_UNSUPPORTED;
}
}
int main(){
    xr::OpenXrInput controls;controls.Initialize(Handle<XrInstance>(std::uintptr_t(1)),Handle<XrSession>(std::uintptr_t(2)),Get);
    CHECK(actions==12&&bindings==3&&vibration&&errors==0);
    interaction::InputFrame input;input.generation=20;input.spaceGeneration=7;input.predictedNs=1;
    input.focused=input.headValid=true;input.hands[0].gripTracked=input.hands[1].gripTracked=true;
    constexpr std::int64_t now=1000000000;
    interaction::FeedbackEvent event{1,19,7,now,now+100000000,interaction::FeedbackKind::ReloadCapture,0};
    controls.Feedback(input,now,&event);CHECK(applied==1&&lastHand==paths["/user/hand/left"]&&lastPulse.duration==35000000&&Near(lastPulse.amplitude,.55f));
    controls.Feedback(input,now,&event);CHECK(applied==1&&controls.FeedbackRejected()==1);
    ++event.id;event.kind=interaction::FeedbackKind::ReloadApplied;controls.Feedback(input,now+1,&event);
    CHECK(applied==2&&lastPulse.duration==75000000&&Near(lastPulse.amplitude,.9f)&&controls.FeedbackApplied()==2);
    CHECK(controls.CaptureFeedbackApplied()==1&&controls.ReceiptFeedbackApplied()==1&&controls.LastFeedbackEvent()==2);
    input.focused=false;++input.generation;controls.Feedback(input,now+2);CHECK(stopped>=2);
    ++event.id;controls.Feedback(input,now+3,&event);CHECK(applied==2);
    input.focused=true;++input.generation;++event.id;event.inputSequence=input.generation;applyResult=XR_ERROR_RUNTIME_FAILURE;
    controls.Feedback(input,now+4,&event);CHECK(applied==3&&controls.FeedbackErrors()==1&&controls.FeedbackApplied()==2);
    ++event.id;applyResult=XR_SESSION_NOT_FOCUSED;controls.Feedback(input,now+5,&event);CHECK(controls.FeedbackApplied()==2);
    CHECK(errors==0);std::cout<<"OpenXR feedback: actual vibration bindings, distinct pulses, dedup, focus stop and nonfatal runtime rejection passed\n";return 0;
}
