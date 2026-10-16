#pragma once
#include "fvr/interaction/ControllerInput.h"
#include <cmath>
#include <cstdint>
#include <optional>

// PRIVATE candidate394 semantic core. No native pointer access, input writes or reset API.
// One independent trial per adapter lifetime. Caller facts must come from the
// actual current adapter; a saved baseline or an external capture cannot set them.
namespace fvr::bc2::private_grenade {
struct Identity {
    std::uint64_t actor=0,weak=0,actorGeneration=0,equipmentGeneration=0,space=0;
    std::uint64_t item=0,itemGeneration=0,bodyRevision=0;
    std::uint64_t inventory=0,switching=0,targetItem=0,targetData=0,targetFiring=0;
    std::uint64_t gunToken=0;
    bool operator==(const Identity&)const=default;
    bool Valid()const noexcept {
        return actor&&weak&&actorGeneration&&equipmentGeneration&&space&&item&&itemGeneration&&bodyRevision&&
            inventory&&switching&&targetItem&&targetData&&targetFiring&&gunToken;
    }
};
struct Admission {
    bool onFoot=false,nativePlaying=false,bindingVerified=false,ownerCurrent=false;
    bool routeCurrent=false,configCurrent=false,ammoAvailable=false,visibleHeld=false,nativeGrenadeNeutral=false,nativeFireNeutral=false;
    bool currentGunHold=false,leftFree=false,bodySettled=false,shellSettled=false;
    bool magazineSettled=false,pumpSettled=false,boltSettled=false,sightSettled=false;
    // A direct isolated diagnostic receiver can supply this provenance.
    // Ordinary host packets currently cannot expose pending-menu ownership.
    bool menuOwnershipKnown=false,menuClear=false;
    // Diagnostic bit order is stable; zero means every admission predicate passed.
    static constexpr const char* Names[]{"on_foot","native_playing","binding_verified","owner_current",
        "route_current","config_current","ammo_available","visible_held","native_grenade_neutral","native_fire_neutral",
        "current_gun_hold","left_free","body_settled","shell_settled","magazine_settled","pump_settled",
        "bolt_settled","sight_settled","menu_ownership_known","menu_clear"};
    std::uint32_t FailureMask()const noexcept {
        const bool values[]{onFoot,nativePlaying,bindingVerified,ownerCurrent,routeCurrent,configCurrent,
            ammoAvailable,visibleHeld,nativeGrenadeNeutral,nativeFireNeutral,currentGunHold,leftFree,bodySettled,shellSettled,
            magazineSettled,pumpSettled,boltSettled,sightSettled,menuOwnershipKnown,menuClear};
        static_assert(sizeof(values)/sizeof(values[0])==sizeof(Names)/sizeof(Names[0]));
        std::uint32_t mask=0;for(unsigned i=0;i<sizeof(values)/sizeof(values[0]);++i)if(!values[i])mask|=1u<<i;
        return mask;
    }
    bool All()const noexcept {
        return onFoot&&nativePlaying&&bindingVerified&&ownerCurrent&&routeCurrent&&configCurrent&&
            ammoAvailable&&visibleHeld&&nativeGrenadeNeutral&&nativeFireNeutral&&currentGunHold&&leftFree&&bodySettled&&shellSettled&&
            magazineSettled&&pumpSettled&&boltSettled&&sightSettled&&menuOwnershipKnown&&menuClear;
    }
};
struct Sample {
    Identity identity{};
    interaction::InputFrame input{};
    interaction::ActionOutput semantic{}; // Actual shared ControllerActions result.
    Admission admission{};
    std::int64_t observedNs=0,deadlineNs=0,nowNs=0;
};
struct Request {
    Identity identity{};
    std::uint64_t id=0,input=0;
    std::int64_t observedNs=0,deadlineNs=0;
    bool operator==(const Request&)const=default;
};
enum class Phase : unsigned {NeedNeutral,Armed,Pressed,Issued,Committed,Rejected};
enum class Reason : unsigned {None,InvalidSource,Admission,IdentityChanged,SequenceRollback,
    ChangedDuplicate,ObservationGap,ConflictingInput,ShortRelease,HoldTimeout,FinalValidation,FinalBitSuppressed};

class OneGrenade {
public:
    static constexpr std::int64_t HoldNs=500000000,MaxHoldNs=1500000000;
    static constexpr std::int64_t MaxSourceLifetimeNs=100000000,MaxGapNs=150000000;
    std::optional<Request> Observe(const Sample& s)noexcept {
        if(phase_==Phase::Committed||phase_==Phase::Rejected)return {};
        if(!SourceValid(s)||(lastNow_&&s.nowNs<lastNow_)){Unavailable(Reason::InvalidSource);return {};}
        lastNow_=s.nowNs;
        if(!s.admission.All()){Unavailable(Reason::Admission);return {};}
        if(!ControlsClear(s)){Unavailable(Reason::ConflictingInput);return {};}
        if(identity_.Valid()&&identity_!=s.identity){
            if(Started()){Reject(Reason::IdentityChanged);return {};}
            ClearArming();
        }
        if(lastInput_&&s.input.generation<lastInput_){Unavailable(Reason::SequenceRollback);return {};}
        if(lastInput_&&s.input.generation==lastInput_){
            if(s.observedNs!=lastObserved_||s.deadlineNs!=lastDeadline_||
               s.input.hands[1].held!=lastRightHeld_)Unavailable(Reason::ChangedDuplicate);
            return {}; // Processing time never advances a held gesture.
        }
        if(lastObserved_&&(s.observedNs<=lastObserved_||s.observedNs-lastObserved_>MaxGapNs)){
            Unavailable(Reason::ObservationGap);return {};
        }
        identity_=s.identity;lastInput_=s.input.generation;
        lastObserved_=s.observedNs;lastDeadline_=s.deadlineNs;lastRightHeld_=s.input.hands[1].held;
        const bool held=(s.input.hands[1].held&interaction::StickClick)!=0;
        if(phase_==Phase::Issued)return {}; // Never reissue a lost native attempt.
        if(phase_==Phase::NeedNeutral){if(!held)phase_=Phase::Armed;return {};}
        if(phase_==Phase::Armed){if(held){phase_=Phase::Pressed;startedNs_=s.observedNs;}return {};}
        const auto duration=s.observedNs-startedNs_;
        if(duration>MaxHoldNs){Reject(Reason::HoldTimeout);return {};}
        if(held)return {};
        if(duration<HoldNs){Reject(Reason::ShortRelease);return {};}
        request_={s.identity,1,s.input.generation,s.observedNs,s.deadlineNs};
        phase_=Phase::Issued;return request_;
    }
    // Call immediately before staging the native38 bit. No packet/identity or
    // current native eligibility may change after the request was recognized.
    bool CanStage(const Request& request,const Sample& current)const noexcept {
        return phase_==Phase::Issued&&request==request_&&SourceValid(current)&&current.nowNs>=lastNow_&&current.admission.All()&&
            ControlsClear(current)&&current.input.hands[1].held==0&&request.identity==current.identity&&
            request.input==current.input.generation&&request.observedNs==current.observedNs&&
            request.deadlineNs==current.deadlineNs;
    }
    // This observes the final cache result only after existing suppression and
    // the actual InputOverride commit. It never sets, restores or retries a bit.
    bool Committed(const Request& request,const Sample& current,bool finalBit38)noexcept {
        // Preserve actual observed writes even if post-commit freshness fails.
        // Never report zero native edges merely because acceptance was rejected.
        if(phase_==Phase::Issued&&request==request_&&!commitObserved_){
            commitObserved_=true;if(finalBit38)++nativeEdges_;
        }
        if(!CanStage(request,current)){if(phase_!=Phase::Committed)Reject(Reason::FinalValidation);return false;}
        if(!finalBit38){Reject(Reason::FinalBitSuppressed);return false;}
        phase_=Phase::Committed;++commits_;return true;
    }
    void Reject(Reason reason)noexcept {if(phase_!=Phase::Committed){phase_=Phase::Rejected;reason_=reason;}}
    Phase Current()const noexcept{return phase_;}
    Reason Rejection()const noexcept{return reason_;}
    unsigned Commits()const noexcept{return commits_;}
    unsigned ObservedNativeEdges()const noexcept{return nativeEdges_;}
private:
    static bool SourceValid(const Sample& s)noexcept {
        return s.identity.Valid()&&interaction::ValidInput(s.input)&&s.input.generation&&
            s.identity.space==s.input.spaceGeneration&&s.input.focused&&s.input.headValid&&
            s.input.hands[0].gripTracked&&s.input.hands[1].gripTracked&&s.input.hands[1].aimTracked&&
            s.observedNs>0&&s.nowNs>=s.observedNs&&s.deadlineNs>s.nowNs&&
            s.deadlineNs>s.observedNs&&s.deadlineNs-s.observedNs<=MaxSourceLifetimeNs&&
            s.semantic.active&&s.semantic.owner==s.identity.actor;
    }
    static bool ControlsClear(const Sample& s)noexcept {
        constexpr auto components=interaction::Stick|interaction::Trigger|interaction::Squeeze|
            interaction::Primary|interaction::Secondary|interaction::StickClick;
        if(s.semantic.held||s.semantic.pressed||s.semantic.released||
           s.semantic.forward!=0||s.semantic.strafe!=0||s.semantic.turnDegrees!=0||s.input.hands[0].held||
           (s.input.hands[1].held&~interaction::StickClick))return false;
        for(const auto& hand:s.input.hands){
            if((hand.active&components)!=components||hand.trigger>.1f||hand.squeeze>.35f||
               std::hypot(hand.stickX,hand.stickY)>.18f)return false;
        }
        return true;
    }
    bool Started()const noexcept{return phase_==Phase::Pressed||phase_==Phase::Issued;}
    void ClearArming()noexcept {
        phase_=Phase::NeedNeutral;identity_={};lastInput_=0;lastObserved_=lastDeadline_=startedNs_=lastNow_=0;lastRightHeld_=0;
    }
    void Unavailable(Reason why)noexcept {if(Started())Reject(why);else ClearArming();}
    Phase phase_=Phase::NeedNeutral;Reason reason_=Reason::None;
    Identity identity_{};Request request_{};std::uint64_t lastInput_=0;
    std::int64_t lastObserved_=0,lastDeadline_=0,startedNs_=0,lastNow_=0;
    std::uint32_t lastRightHeld_=0;unsigned commits_=0,nativeEdges_=0;bool commitObserved_=false;
};
}
