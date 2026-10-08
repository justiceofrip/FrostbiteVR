#define XR_NO_PROTOTYPES
#include "OpenXrInput.h"
#include <cmath>
#include <algorithm>
#include <cstring>
#include <string>
#include <vector>
#include <stdexcept>
namespace fvr::xr {
namespace {
void Check(XrResult result,const char* what){if(XR_FAILED(result))throw std::runtime_error(std::string(what)+" XrResult="+std::to_string(result));}
math::Pose Pose(const XrPosef& p){return {{p.position.x,p.position.y,p.position.z},{p.orientation.x,p.orientation.y,p.orientation.z,p.orientation.w}};}
}
OpenXrInput::~OpenXrInput(){
    StopFeedback();
    if(DestroySpace)for(auto& hand:spaces_)for(auto space:hand)if(space)DestroySpace(space);
    // Action set destruction destroys its actions; spaces must go first.
    if(set_&&DestroyActionSet)DestroyActionSet(set_);
}
XrPath OpenXrInput::Path(const char* name){XrPath path=XR_NULL_PATH;Check(StringToPath(instance_,name,&path),"Input path");return path;}
void OpenXrInput::Initialize(XrInstance instance,XrSession session,PFN_xrGetInstanceProcAddr get){
    instance_=instance;session_=session;
#define LOAD(n) {PFN_xrVoidFunction raw=nullptr;Check(get(instance,"xr" #n,&raw),"Load xr" #n);if(!raw)throw std::runtime_error("Missing xr" #n);n=reinterpret_cast<PFN_xr##n>(raw);}
    LOAD(StringToPath) LOAD(CreateActionSet) LOAD(DestroyActionSet) LOAD(CreateAction)
    LOAD(SuggestInteractionProfileBindings) LOAD(AttachSessionActionSets) LOAD(CreateActionSpace)
    LOAD(DestroySpace) LOAD(SyncActions) LOAD(GetActionStatePose) LOAD(GetActionStateFloat)
    LOAD(GetActionStateVector2f) LOAD(GetActionStateBoolean) LOAD(LocateSpace)
    LOAD(ApplyHapticFeedback) LOAD(StopHapticFeedback)
#undef LOAD
    paths_={Path("/user/hand/left"),Path("/user/hand/right")};
    XrActionSetCreateInfo set{XR_TYPE_ACTION_SET_CREATE_INFO};strcpy_s(set.actionSetName,"frostbite_controls");strcpy_s(set.localizedActionSetName,"Frostbite VR controls");
    Check(CreateActionSet(instance,&set,&set_),"Create controller action set");
    const char* names[]={"grip_pose","aim_pose","trigger","squeeze","stick","primary","secondary","stick_click","menu","thumb_touch","index_touch","feedback"};
    const char* labels[]={"Hand grip pose","Hand aim pose","Trigger","Grip pressure","Movement / turn stick","Primary button","Secondary button","Stick click","Menu","Thumb contact","Index finger contact","Interaction feedback"};
    const XrActionType types[]={XR_ACTION_TYPE_POSE_INPUT,XR_ACTION_TYPE_POSE_INPUT,XR_ACTION_TYPE_FLOAT_INPUT,XR_ACTION_TYPE_FLOAT_INPUT,XR_ACTION_TYPE_VECTOR2F_INPUT,
        XR_ACTION_TYPE_BOOLEAN_INPUT,XR_ACTION_TYPE_BOOLEAN_INPUT,XR_ACTION_TYPE_BOOLEAN_INPUT,XR_ACTION_TYPE_BOOLEAN_INPUT,XR_ACTION_TYPE_BOOLEAN_INPUT,XR_ACTION_TYPE_BOOLEAN_INPUT,XR_ACTION_TYPE_VIBRATION_OUTPUT};
    for(unsigned a=0;a<Count;++a){XrActionCreateInfo info{XR_TYPE_ACTION_CREATE_INFO};strcpy_s(info.actionName,names[a]);strcpy_s(info.localizedActionName,labels[a]);
        info.actionType=types[a];info.countSubactionPaths=2;info.subactionPaths=paths_.data();Check(CreateAction(set_,&info,&actions_[a]),"Create controller action");}
    // Standard core 1.0 profiles; unsupported optional profiles are skipped.
    for(unsigned profile=0;profile<3;++profile){
        const char* profileName=profile==0?"/interaction_profiles/oculus/touch_controller":profile==1?"/interaction_profiles/valve/index_controller":"/interaction_profiles/khr/simple_controller";
        std::vector<XrActionSuggestedBinding> bindings;
        for(unsigned hand=0;hand<2;++hand){const std::string prefix=hand?"/user/hand/right/input/":"/user/hand/left/input/";
            const auto bind=[&](unsigned a,const char* suffix){bindings.push_back({actions_[a],Path((prefix+suffix).c_str())});};
            bind(GripPose,"grip/pose");bind(AimPose,"aim/pose");
            bindings.push_back({actions_[Vibration],Path(hand?"/user/hand/right/output/haptic":"/user/hand/left/output/haptic")});
            if(profile==2){bind(Primary,"select/click");bind(Menu,"menu/click");continue;}
            bind(Trigger,"trigger/value");bind(Squeeze,"squeeze/value");bind(Stick,"thumbstick");bind(StickClick,"thumbstick/click");
            bind(Primary,profile==0&&!hand?"x/click":"a/click");bind(Secondary,profile==0&&!hand?"y/click":"b/click");
            if(profile==0&&!hand)bind(Menu,"menu/click");
            // Core profile touch sources aggregate with boolean OR per hand.
            bind(IndexTouch,"trigger/touch");bind(ThumbTouch,"thumbstick/touch");
            bind(ThumbTouch,profile==0?"thumbrest/touch":"trackpad/touch");
            bind(ThumbTouch,profile==0&&!hand?"x/touch":"a/touch");
            bind(ThumbTouch,profile==0&&!hand?"y/touch":"b/touch");
        }
        XrInteractionProfileSuggestedBinding suggested{XR_TYPE_INTERACTION_PROFILE_SUGGESTED_BINDING};suggested.interactionProfile=Path(profileName);
        suggested.countSuggestedBindings=std::uint32_t(bindings.size());suggested.suggestedBindings=bindings.data();
        auto result=SuggestInteractionProfileBindings(instance,&suggested);
        if(result==XR_ERROR_PATH_UNSUPPORTED&&profile!=2){
            // Optional touch must not remove an otherwise supported controller.
            bindings.erase(std::remove_if(bindings.begin(),bindings.end(),[&](const auto& b){return b.action==actions_[ThumbTouch]||b.action==actions_[IndexTouch];}),bindings.end());
            suggested.countSuggestedBindings=std::uint32_t(bindings.size());suggested.suggestedBindings=bindings.data();
            result=SuggestInteractionProfileBindings(instance,&suggested);
        }
        if(result==XR_ERROR_PATH_UNSUPPORTED){
            bindings.erase(std::remove_if(bindings.begin(),bindings.end(),[&](const auto& b){return b.action==actions_[Vibration];}),bindings.end());
            suggested.countSuggestedBindings=std::uint32_t(bindings.size());suggested.suggestedBindings=bindings.data();
            result=SuggestInteractionProfileBindings(instance,&suggested);
        }
        if(result==XR_ERROR_PATH_UNSUPPORTED)continue;
        Check(result,"Suggest controller bindings");++profiles_;
    }
    if(!profiles_)throw std::runtime_error("No controller interaction profile accepted");
    XrSessionActionSetsAttachInfo attach{XR_TYPE_SESSION_ACTION_SETS_ATTACH_INFO};attach.countActionSets=1;attach.actionSets=&set_;
    Check(AttachSessionActionSets(session,&attach),"Attach controller actions");
    for(unsigned hand=0;hand<2;++hand)for(unsigned pose=0;pose<2;++pose){
        XrActionSpaceCreateInfo space{XR_TYPE_ACTION_SPACE_CREATE_INFO};space.action=actions_[pose];space.subactionPath=paths_[hand];space.poseInActionSpace.orientation.w=1;
        Check(CreateActionSpace(session,&space,&spaces_[hand][pose]),"Create controller space");}
}
void OpenXrInput::StopFeedback()noexcept {
    feedback_.Suspend();if(!session_||!actions_[Vibration]||!StopHapticFeedback)return;
    for(unsigned hand=0;hand<2;++hand){XrHapticActionInfo info{XR_TYPE_HAPTIC_ACTION_INFO};info.action=actions_[Vibration];info.subactionPath=paths_[hand];
        const auto result=StopHapticFeedback(session_,&info);if(XR_FAILED(result)&&result!=XR_ERROR_SESSION_NOT_RUNNING)++feedbackErrors_;}
}
void OpenXrInput::Feedback(const interaction::InputFrame& input,std::int64_t now,const interaction::FeedbackEvent* event)noexcept {
    const auto decision=feedback_.Update(input,now,event);if(decision.stop)StopFeedback();
    if(event&&!decision.pulse)++feedbackRejected_;
    if(!decision.pulse||!session_||!actions_[Vibration]||!ApplyHapticFeedback)return;
    const auto& pulse=*decision.pulse;XrHapticActionInfo info{XR_TYPE_HAPTIC_ACTION_INFO};info.action=actions_[Vibration];info.subactionPath=paths_[pulse.hand];
    XrHapticVibration vibration{XR_TYPE_HAPTIC_VIBRATION};vibration.duration=pulse.durationNs;vibration.frequency=XR_FREQUENCY_UNSPECIFIED;vibration.amplitude=pulse.amplitude;
    // Feedback is optional. A runtime haptic failure must not abort video/input.
    const auto result=ApplyHapticFeedback(session_,&info,reinterpret_cast<const XrHapticBaseHeader*>(&vibration));
    if(result==XR_SUCCESS){++feedbackApplied_;lastFeedbackEvent_=event->id;
        if(event->kind==interaction::FeedbackKind::ReloadCapture)++captureFeedbackApplied_;else ++receiptFeedbackApplied_;
    }else if(result==XR_SESSION_NOT_FOCUSED)++feedbackRejected_;else ++feedbackErrors_;
}
void OpenXrInput::Sample(XrSpace reference,XrTime time,interaction::InputFrame& frame){
    frame.hands={};XrActiveActionSet active{set_,XR_NULL_PATH};XrActionsSyncInfo sync{XR_TYPE_ACTIONS_SYNC_INFO};sync.countActiveActionSets=1;sync.activeActionSets=&active;
    const auto synced=SyncActions(session_,&sync);
    if(synced==XR_SESSION_NOT_FOCUSED||!frame.focused)return;
    Check(synced,"Sync controller actions");
    for(unsigned hand=0;hand<2;++hand){auto& h=frame.hands[hand];XrActionStateGetInfo get{XR_TYPE_ACTION_STATE_GET_INFO};get.subactionPath=paths_[hand];
        for(unsigned pose=0;pose<2;++pose){get.action=actions_[pose];XrActionStatePose state{XR_TYPE_ACTION_STATE_POSE};Check(GetActionStatePose(session_,&get,&state),"Get hand pose activity");
            if(!state.isActive)continue;XrSpaceLocation location{XR_TYPE_SPACE_LOCATION};Check(LocateSpace(spaces_[hand][pose],reference,time,&location),"Locate controller");
            constexpr auto tracked=XR_SPACE_LOCATION_POSITION_VALID_BIT|XR_SPACE_LOCATION_ORIENTATION_VALID_BIT|XR_SPACE_LOCATION_POSITION_TRACKED_BIT|XR_SPACE_LOCATION_ORIENTATION_TRACKED_BIT;
            if((location.locationFlags&tracked)!=tracked)continue;const auto p=Pose(location.pose);if(!math::MakeRelativePose(p,p))continue;
            if(pose==0){h.grip=p;h.gripTracked=true;}else{h.aim=p;h.aimTracked=true;}
        }
        for(unsigned a=Trigger;a<=Squeeze;++a){get.action=actions_[a];XrActionStateFloat state{XR_TYPE_ACTION_STATE_FLOAT};Check(GetActionStateFloat(session_,&get,&state),"Get analog controller action");
            if(state.isActive&&std::isfinite(state.currentState)&&state.currentState>=0&&state.currentState<=1){
                if(a==Trigger){h.active|=interaction::Trigger;h.trigger=state.currentState;}else{h.active|=interaction::Squeeze;h.squeeze=state.currentState;}}
        }
        get.action=actions_[Stick];XrActionStateVector2f stick{XR_TYPE_ACTION_STATE_VECTOR2F};Check(GetActionStateVector2f(session_,&get,&stick),"Get controller stick");
        if(stick.isActive&&std::isfinite(stick.currentState.x)&&std::isfinite(stick.currentState.y)&&std::abs(stick.currentState.x)<=1&&std::abs(stick.currentState.y)<=1){h.active|=interaction::Stick;h.stickX=stick.currentState.x;h.stickY=stick.currentState.y;}
        for(unsigned a=Primary;a<=Menu;++a){get.action=actions_[a];XrActionStateBoolean button{XR_TYPE_ACTION_STATE_BOOLEAN};Check(GetActionStateBoolean(session_,&get,&button),"Get controller button");
            if(button.isActive){const auto bit=std::uint32_t(interaction::Primary)<<(a-Primary);h.active|=bit;if(button.currentState)h.held|=bit;}}
        for(unsigned a=ThumbTouch;a<=IndexTouch;++a){get.action=actions_[a];XrActionStateBoolean touch{XR_TYPE_ACTION_STATE_BOOLEAN};
            Check(GetActionStateBoolean(session_,&get,&touch),"Get capacitive contact");
            if(touch.isActive){const auto bit=std::uint32_t(interaction::ThumbTouch)<<(a-ThumbTouch);h.touchActive|=bit;if(touch.currentState)h.touched|=bit;}
        }
    }
}
}
