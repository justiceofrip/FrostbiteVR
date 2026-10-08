#pragma once
#include "Bc2MenuBinding.h"
#include <array>
namespace fvr::bc2 {
struct MenuStateCodeSpan {std::uint32_t rva=0,size=0;};
struct MenuStateCandidates {
    std::uint32_t managerGlobal=0,ownerVtable=0,eventVtable=0,inputVtable=0;
    std::uint32_t notification=0,enterSetter=0,enterDispatcher=0;
    std::uint32_t controllerVtable=0,cancelPressed=0,cancelReleased=0;
    bool operator==(const MenuStateCandidates&)const=default;
};
// Validates the inspected enterMenu callback, exact showIngameMenu branch,
// constructor ownership and vtables. Every range must additionally match the
// loaded executable before invoking a function. No hash-only authorization.
std::optional<MenuStateCandidates> DiscoverMenuState(std::span<const std::byte>,const engine::PeImage&);
std::array<MenuStateCodeSpan,13> MenuStateCodeSpans() noexcept;
struct MenuStateSnapshot {
    std::uint32_t manager=0,owner=0,eventListener=0,inputController=0;
    bool entered=false,pauseRequested=false;
    bool operator==(const MenuStateSnapshot&)const=default;
};
// entered is the authoritative native enterMenu callback state, not proof that
// an APT movie has finished drawing. Pause alone never grants menu ownership.
std::optional<MenuStateSnapshot> ReadMenuState(const MenuMemory&,const MenuStateCandidates&,std::uint32_t imageBase);
// These are distinct native actions. Back can return to a parent submenu and
// need not close the root pause menu. MenuToggle sends native ConceptMenu57.
enum class MenuCloseAction : std::uint8_t {Back,MenuToggle};
struct MenuVisibilityCommand {
    std::uint64_t generation=0,menuEpoch=0;
    std::int64_t deadlineNs=0;
    bool visible=false;
    MenuCloseAction action=MenuCloseAction::Back;
};
struct MenuVisibilityGuard {
    std::uint64_t menuEpoch=0;
    std::int64_t nowNs=0;
    bool nativeUiThread=false,liveCodeVerified=false,focused=false,tracked=false;
};
struct MenuVisibilityCalls {
    void* context=nullptr;
    // Native __thiscall void(eventListener, category, subtype, arg3, arg4),
    // callee ret16. Only the checked (0x1b,0x16,0,bool) notification is emitted.
    bool (*notify)(void*,std::uint32_t function,std::uint32_t eventListener,
                   const std::array<std::uint32_t,4>& arguments)=nullptr;
    // Native __thiscall void(inputController,unsigned uiAction), callee ret4.
    // Only action8 (ConceptMenu57) and action9 (ConceptCancel58) are permitted.
    // Native release ignores8; do not synthesize an APT key-up for MenuToggle.
    bool (*input)(void*,std::uint32_t function,std::uint32_t inputController,std::uint32_t uiAction)=nullptr;
};
enum class MenuVisibilityResult : std::uint8_t {Rejected,OwnerChanged,AlreadyInState,CallFailed,Invoked,QueueFull,NotDue};
// Open only. Closing must use BeginMenuCancel/ReleaseMenuCancel below.
// Invoked means the native open notification was invoked.
// It does not mean an APT transition completed. The adapter must await a matching entered-state transition;
// never synthesize an acknowledgement or write pause/input state on timeout.
MenuVisibilityResult DispatchMenuVisibility(const MenuMemory&,const MenuStateCandidates&,std::uint32_t imageBase,
    const MenuStateSnapshot& expected,const MenuVisibilityCommand&,const MenuVisibilityGuard&,const MenuVisibilityCalls&);

// Diagnostic observations only, not a claim that a queued event was consumed.
// The modal predicate is NEVER invoked by this reader; its value is unknown.
struct MenuCancelObservation {
    MenuTargetSnapshot target{};
    std::uint32_t lastWord=0,modalPrimary=0,modalFallback=0,modalSelected=0,modalVtable=0,modalPredicate=0;
    std::uint8_t pressDisabled=0;
    bool targetValid=false,lastWordValid=false,pressDisabledValid=false,modalValid=false;
};
struct MenuCancelDiagnostic {
    MenuCancelObservation before{},after{};
    bool invoked=false,release=false;
    MenuCloseAction action=MenuCloseAction::Back;
};
// Minted only after the original native press was invoked. The same capability
// authorizes one up against the original owner/queue, even after focus or menu
// epoch loss. Caller retries cleanup on verified native pumps. Never forget an
// active lease merely because a deadline elapsed or the menu acknowledged.
class MenuOwnedCancel {
public:
    static constexpr std::int64_t HoldNs=100'000'000;
    MenuOwnedCancel()=default;
    MenuOwnedCancel(const MenuOwnedCancel&)=delete;
    MenuOwnedCancel& operator=(const MenuOwnedCancel&)=delete;
    bool Active()const noexcept{return generation_!=0;}
    std::uint64_t Epoch()const noexcept{return epoch_;}
    std::uint64_t Generation()const noexcept{return generation_;}
    std::int64_t ReleaseAtNs()const noexcept{return releaseAtNs_;}
    MenuCloseAction Action()const noexcept{return action_;}
    std::uint32_t UiAction()const noexcept{return action_==MenuCloseAction::MenuToggle?8u:9u;}
    const MenuStateSnapshot& State()const noexcept{return state_;}
    const MenuTargetSnapshot& Target()const noexcept{return target_;}
    // Only after the entire original UI/session has retired, never on timeout.
    void Forget()noexcept{generation_=epoch_=0;releaseAtNs_=pressedAtNs_=0;}
private:
    MenuStateSnapshot state_{};
    MenuTargetSnapshot target_{};
    MenuStateCandidates stateBinding_{};
    MenuBindingCandidates pointerBinding_{};
    std::uint32_t base_=0;
    std::uint64_t generation_=0,epoch_=0;
    std::int64_t pressedAtNs_=0,releaseAtNs_=0;
    MenuCloseAction action_=MenuCloseAction::Back;
    friend MenuVisibilityResult BeginMenuCancel(const MenuMemory&,const MenuStateCandidates&,const MenuBindingCandidates&,
        std::uint32_t,const MenuStateSnapshot&,const MenuVisibilityCommand&,const MenuVisibilityGuard&,const MenuVisibilityCalls&,
        MenuOwnedCancel&,MenuCancelDiagnostic*);
    friend MenuVisibilityResult ReleaseMenuCancel(const MenuMemory&,const MenuStateCandidates&,const MenuBindingCandidates&,
        std::uint32_t,const MenuVisibilityGuard&,const MenuVisibilityCalls&,MenuOwnedCancel&,MenuCancelDiagnostic*,bool);
};
// The binding proof must cover BOTH MenuStateCodeSpans and MenuBindingCodeSpans.
// An invoked press is not a transition ack. Native modal/disabled gates remain
// authoritative. No new press may begin while this capability remains active.
MenuVisibilityResult BeginMenuCancel(const MenuMemory&,const MenuStateCandidates&,const MenuBindingCandidates&,
    std::uint32_t imageBase,const MenuStateSnapshot& expected,const MenuVisibilityCommand&,const MenuVisibilityGuard&,
    const MenuVisibilityCalls&,MenuOwnedCancel&,MenuCancelDiagnostic* = nullptr);
// Exact original native owner/target and live code are required. Current focus,
// tracking and menuEpoch deliberately do not block cleanup. Normally waits
// 100ms; retiring permits earlier cleanup only while that same owner is live.
// Failed cleanup retains ownership. Success consumes it exactly once.
// Both edges use the exact leased action:8 for MenuToggle,9 for Back.
// Action8 native release is intentionally a no-op; ownership is still consumed
// only after invoking that verified native release against the original owner.
MenuVisibilityResult ReleaseMenuCancel(const MenuMemory&,const MenuStateCandidates&,const MenuBindingCandidates&,
    std::uint32_t imageBase,const MenuVisibilityGuard&,const MenuVisibilityCalls&,MenuOwnedCancel&,
    MenuCancelDiagnostic* = nullptr,bool retiring=false);
}
