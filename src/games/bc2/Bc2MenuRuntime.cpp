#include "Bc2MenuRuntime.h"
#include "Bc2MenuBinding.h"
#include "Bc2MenuState.h"
#include "Bc2MenuControl.h"
#include "fvr/graphics/D3D11MenuProducer.h"
#include <Windows.h>
#include <MinHook.h>
#include <atomic>
#include <intrin.h>
#include <cstring>
#include <vector>
namespace fvr::bc2::menu {
namespace {
using PumpFn=void(__thiscall*)(void*,void*);PumpFn original=nullptr;void* hook=nullptr;
ipc::MenuChannel channel;graphics::D3D11MenuProducer raster;
MenuBindingCandidates binding{};MenuStateCandidates states{};unsigned base=0;
std::atomic<bool> enabled=false,stopping=false;std::atomic<unsigned> callbacks=0,uiThread=0;
std::atomic<unsigned> pumps=0,stateReads=0,stateMisses=0,visibilityRequests=0,visibilityAcks=0,visibilityTimeouts=0,moves=0,downs=0,ups=0,rejected=0,releaseFailures=0;
std::atomic_flag pumping=ATOMIC_FLAG_INIT;
std::atomic<unsigned> foreignBoundary=0,concurrentBoundary=0,threadChanges=0;
thread_local bool nativeBoundary=false;
MenuOwnedPress press;std::atomic<bool> pressActive=false;
MenuOwnedCancel nativeCancel;std::atomic<bool> cancelActive=false;
std::atomic<unsigned> cancelUps=0,cancelReleaseFailures=0;
std::array<MenuCancelDiagnostic,32> cancelEvidence{};unsigned cancelEvidenceCount=0;
std::optional<MenuStateSnapshot> lastState;
std::uint64_t menuEpoch=1,stateSequence=0,lastPointerSequence=0;
MenuControlSequence controlSequence;
std::array<std::atomic<unsigned>,5> controlReadResults{}; // Ok, Busy, Invalid, Timeout, Closed
std::atomic<unsigned> controlRearms=0,controlInactive=0,toggleEdges=0,cancelEdges=0,edgesWhilePending=0;
struct ControlEvidence {
    std::uint64_t sequence=0,space=0,toggle=0,cancel=0;
    MenuControlRead read=MenuControlRead::Inactive;
    bool toggleEdge=false,cancelEdge=false,entered=false,pending=false;
};
std::array<ControlEvidence,64> controlEvidence{};unsigned controlEvidenceCount=0;
bool buttonArmed=false,pendingVisible=false,releasePending=false;
std::uint64_t pendingUntil=0;float lastU=0,lastV=0;
bool Copy(unsigned address,void* dst,std::size_t n)noexcept {
    if(address<0x10000||!dst||!n||n>65536||n>UINT32_MAX-address)return false;
    __try {std::memcpy(dst,reinterpret_cast<const void*>(address),n);return true;}__except(EXCEPTION_EXECUTE_HANDLER){return false;}
}
const MenuMemory memory{nullptr,[](void*,unsigned at,void* dst,std::size_t n){return Copy(at,dst,n);}};
std::int64_t frequency=0;
std::int64_t Nanos(std::int64_t qpc){if(qpc<=0||frequency<=0)return 0;return (qpc/frequency)*1000000000+(qpc%frequency)*1000000000/frequency;}
std::int64_t Now(){LARGE_INTEGER n{};return QueryPerformanceCounter(&n)?Nanos(n.QuadPart):0;}
const MenuNativeCalls calls{nullptr,
    [](void*,unsigned fn,unsigned owner,std::int32_t x,std::int32_t y){reinterpret_cast<void(__thiscall*)(void*,int,int)>(fn)(reinterpret_cast<void*>(owner),x,y);return true;},
    [](void*,unsigned fn,unsigned owner,const std::array<unsigned,3>& event){reinterpret_cast<void(__thiscall*)(void*,const unsigned*)>(fn)(reinterpret_cast<void*>(owner),event.data());return true;}};
const MenuVisibilityCalls visibilityCalls{nullptr,[](void*,unsigned fn,unsigned owner,const std::array<unsigned,4>& args){
    reinterpret_cast<void(__thiscall*)(void*,unsigned,unsigned,unsigned,unsigned)>(fn)(reinterpret_cast<void*>(owner),args[0],args[1],args[2],args[3]);return true;},
    [](void*,unsigned fn,unsigned owner,unsigned action){reinterpret_cast<void(__thiscall*)(void*,unsigned)>(fn)(reinterpret_cast<void*>(owner),action);return true;}};
void ReleaseOwned()noexcept {
    if(!press.Active()){releasePending=false;return;}
    releasePending=true;
    MenuDispatchGuard guard;guard.nativeUiThread=nativeBoundary;guard.nowNs=Now();
    const auto result=ReleaseMenuPointer(memory,binding,base,press.Target(),press.Epoch(),guard,calls,press);
    if(result==MenuDispatchResult::Invoked)++ups;else ++releaseFailures;
    pressActive.store(press.Active(),std::memory_order_release);releasePending=press.Active();
}
void ReleaseCancel(bool retiring=false)noexcept {
    if(!nativeCancel.Active())return;
    MenuVisibilityGuard guard;guard.nativeUiThread=nativeBoundary;guard.liveCodeVerified=true;guard.nowNs=Now();
    MenuCancelDiagnostic diagnostic;
    const auto result=ReleaseMenuCancel(memory,states,binding,base,guard,visibilityCalls,nativeCancel,&diagnostic,retiring);
    if(diagnostic.invoked&&cancelEvidenceCount<cancelEvidence.size())cancelEvidence[cancelEvidenceCount++]=diagnostic;
    if(result==MenuVisibilityResult::Invoked)++cancelUps;else if(result!=MenuVisibilityResult::NotDue)++cancelReleaseFailures;
    cancelActive.store(nativeCancel.Active(),std::memory_order_release);
}
void __fastcall Pump(void* self,void*,void* input){
    struct Callback{Callback(){++callbacks;}~Callback(){--callbacks;}} callback;
    const auto caller=reinterpret_cast<unsigned>(_ReturnAddress());
    original(self,input);
    if(!enabled.load(std::memory_order_acquire)&&!stopping.load(std::memory_order_acquire))return;
    const auto state=ReadMenuState(memory,states,base);
    if(!state){++stateMisses;return;}
    if(!IsMenuPumpBoundary(memory,binding,base,caller,reinterpret_cast<unsigned>(self),reinterpret_cast<unsigned>(input),state->inputController)){++foreignBoundary;return;}
    if(pumping.test_and_set(std::memory_order_acquire)){++concurrentBoundary;return;}
    struct Boundary {Boundary(){nativeBoundary=true;}~Boundary(){nativeBoundary=false;pumping.clear(std::memory_order_release);}} boundary;
    const auto thread=GetCurrentThreadId();const auto prior=uiThread.exchange(thread);if(prior&&prior!=thread)++threadChanges;
    ++pumps;
    if(stopping.load(std::memory_order_acquire)){ReleaseOwned();ReleaseCancel(true);return;}
    ReleaseCancel();
    if(releasePending){ReleaseOwned();if(press.Active())return;}
    ++stateReads;
    if(!lastState||lastState->owner!=state->owner||lastState->manager!=state->manager||lastState->entered!=state->entered){
        ReleaseOwned();if(press.Active())return;++menuEpoch;buttonArmed=false;lastPointerSequence=0;
    }
    lastState=state;
    if(pendingUntil){
        if(state->entered==pendingVisible){++visibilityAcks;pendingUntil=0;}
        else if(GetTickCount64()>=pendingUntil){++visibilityTimeouts;pendingUntil=0;}
    }
    const auto target=ReadMenuTarget(memory,binding,base);
    ipc::MenuState status{++stateSequence,menuEpoch,0,state->entered?ipc::MenuMode::Menu:ipc::MenuMode::Gameplay,binding.logicalWidth,binding.logicalHeight,target?1u:0u};
    channel.PublishState(status);
    ipc::MenuControl control;
    const auto controlResult=channel.ReadControl(control);
    if(unsigned(controlResult)<controlReadResults.size())++controlReadResults[unsigned(controlResult)];
    const auto edge=controlSequence.Observe(controlResult,control);
    if(edge.read==MenuControlRead::Busy){
        // Contention supplies no usable pointer/control sample, but it must not
        // absorb a new toggle published while the channel lock was occupied.
        ReleaseOwned();buttonArmed=false;return;
    }
    if(edge.read==MenuControlRead::Inactive){
        ++controlInactive;ReleaseOwned();ReleaseCancel(true);buttonArmed=false;return;
    }
    const bool toggle=edge.toggle,cancel=edge.cancel;
    if(edge.read==MenuControlRead::Rearmed||toggle||cancel){
        if(controlEvidenceCount<controlEvidence.size())controlEvidence[controlEvidenceCount++]={control.sequence,control.space,control.toggle,control.cancel,
            edge.read,toggle,cancel,state->entered,pendingUntil!=0||nativeCancel.Active()};
    }
    if(edge.read==MenuControlRead::Rearmed){
        ++controlRearms;ReleaseOwned();if(press.Active())return;buttonArmed=false;
    }
    if(toggle)++toggleEdges;if(cancel)++cancelEdges;
    if((toggle||cancel)&&(pendingUntil||nativeCancel.Active()))++edgesWhilePending;
    if((toggle||(cancel&&state->entered))&&!pendingUntil&&!nativeCancel.Active()){
        ReleaseOwned();buttonArmed=false;if(press.Active())return;
        const MenuVisibilityCommand command{control.sequence,menuEpoch,Nanos(control.deadlineQpc),toggle?!state->entered:false,toggle?MenuCloseAction::MenuToggle:MenuCloseAction::Back};
        const MenuVisibilityGuard guard{menuEpoch,Now(),true,true,true,true};
        MenuCancelDiagnostic diagnostic;
        const auto result=command.visible?DispatchMenuVisibility(memory,states,base,*state,command,guard,visibilityCalls):
            BeginMenuCancel(memory,states,binding,base,*state,command,guard,visibilityCalls,nativeCancel,&diagnostic);
        cancelActive.store(nativeCancel.Active(),std::memory_order_release);
        if(diagnostic.invoked&&cancelEvidenceCount<cancelEvidence.size())cancelEvidence[cancelEvidenceCount++]=diagnostic;
        if(result==MenuVisibilityResult::Invoked){++visibilityRequests;
            // Native Back may leave entered=true while returning to a parent.
            // Only Menu/open requests promise an entered-state transition.
            if(command.visible||command.action==MenuCloseAction::MenuToggle){pendingVisible=command.visible;pendingUntil=GetTickCount64()+1500;}
        }else if(result!=MenuVisibilityResult::AlreadyInState)++rejected;
        return;
    }
    if(!state->entered||!target||control.menuEpoch!=menuEpoch||!(control.flags&ipc::MenuPoint)||pendingUntil||nativeCancel.Active()){ReleaseOwned();buttonArmed=false;return;}
    const bool down=(control.flags&ipc::MenuDown)!=0;
    if(!down){ReleaseOwned();if(press.Active())return;buttonArmed=true;}
    if(control.sequence==lastPointerSequence)return;
    lastPointerSequence=control.sequence;lastU=control.u;lastV=control.v;
    const auto kind=down&&buttonArmed&&!press.Active()?MenuPointerKind::PrimaryDown:MenuPointerKind::Move;
    const auto command=MakeMenuPointerCommand(binding,lastU,lastV,kind,control.sequence,menuEpoch,Nanos(control.deadlineQpc));
    const MenuDispatchGuard guard{menuEpoch,Now(),true,true,true,true};
    if(command){const auto result=DispatchMenuPointer(memory,binding,base,*target,*command,guard,calls,press);
        if(result==MenuDispatchResult::Invoked){if(kind==MenuPointerKind::PrimaryDown){++downs;buttonArmed=false;}else ++moves;}else ++rejected;
    }
    pressActive.store(press.Active(),std::memory_order_release);
}
bool Same(std::span<const std::byte> bytes,const engine::PeImage& pe,unsigned rva,unsigned n){
    for(const auto& s:pe.sections)if(rva>=s.rva&&rva-s.rva<=s.rawSize&&n<=s.rawSize-(rva-s.rva)){
        const auto at=std::size_t(s.rawOffset)+rva-s.rva;if(at>bytes.size()||n>bytes.size()-at)return false;
        std::vector<std::byte> live(n);return Copy(base+rva,live.data(),n)&&!std::memcmp(live.data(),bytes.data()+at,n);
    }return false;
}
}
bool Install(std::span<const std::byte> bytes,const engine::PeImage& pe,std::uintptr_t image,const std::wstring& token){
    if(hook||image>UINT32_MAX||!channel.ConnectProducer(token))return false;
    base=unsigned(image);LARGE_INTEGER f{};if(!QueryPerformanceFrequency(&f)||f.QuadPart<=0){channel.Close();return false;}frequency=f.QuadPart;
    const auto pointer=DiscoverMenuBinding(bytes,pe);const auto state=DiscoverMenuState(bytes,pe);
    if(!pointer||!state){channel.Close();return false;}binding=*pointer;states=*state;
    for(const auto& span:MenuStateCodeSpans())if(!Same(bytes,pe,span.rva,span.size)){channel.Close();return false;}
    const auto spans=MenuBindingCodeSpans(bytes,pe,binding);if(!spans){channel.Close();return false;}
    for(const auto& span:*spans)if(!Same(bytes,pe,span.rva,span.size)){channel.Close();return false;}
    auto* at=reinterpret_cast<void*>(image+binding.mousePump);
    if(MH_CreateHook(at,Pump,reinterpret_cast<void**>(&original))!=MH_OK){channel.Close();return false;}
    hook=at;return true;
}
void Start()noexcept {if(hook)enabled.store(true,std::memory_order_release);}
bool Stop()noexcept {
    enabled.store(false,std::memory_order_release);stopping.store(true,std::memory_order_release);
    // Only a verified native UI callback may emit the final button-up. Never substitute
    // a shutdown-worker call or a global OS input event.
    for(unsigned n=0;n<100&&(pressActive.load(std::memory_order_acquire)||cancelActive.load(std::memory_order_acquire));++n)Sleep(5);
    const bool okay=!hook||MH_DisableHook(hook)==MH_OK;
    for(unsigned n=0;n<100&&callbacks.load();++n)Sleep(1);
    stopping.store(false,std::memory_order_release);return okay&&!pressActive.load()&&!cancelActive.load()&&!callbacks.load();
}
void Present(IDXGISwapChain* chain)noexcept {if(enabled.load(std::memory_order_acquire))raster.Pump(chain,channel);}
void Report(std::ostream& out){
    out<<"{\"installed\":"<<(hook?"true":"false")<<",\"ui_thread\":"<<uiThread.load()<<",\"pumps\":"<<pumps.load()<<",\"state_reads\":"<<stateReads.load()<<",\"state_misses\":"<<stateMisses.load()
       <<",\"visibility_requests\":"<<visibilityRequests.load()<<",\"visibility_acknowledgements\":"<<visibilityAcks.load()<<",\"visibility_timeouts\":"<<visibilityTimeouts.load()
       <<",\"moves\":"<<moves.load()<<",\"downs\":"<<downs.load()<<",\"ups\":"<<ups.load()<<",\"rejected\":"<<rejected.load()<<",\"release_failures\":"<<releaseFailures.load()
       <<",\"foreign_boundary\":"<<foreignBoundary.load()<<",\"concurrent_boundary\":"<<concurrentBoundary.load()<<",\"worker_thread_changes\":"<<threadChanges.load()
       <<",\"button_still_owned\":"<<(pressActive.load()?"true":"false")<<",\"captured\":"<<raster.Captured()<<",\"published\":"<<raster.Published()<<",\"discarded_rasters\":"<<raster.Discarded()<<",\"graphics_failures\":"<<raster.Failed()<<",\"cancel_ups\":"<<cancelUps.load()<<",\"cancel_release_failures\":"<<cancelReleaseFailures.load()<<",\"cancel_still_owned\":"<<cancelActive.load()<<",\"cancel_evidence\":[";
    const auto observation=[&](const MenuCancelObservation& o){out<<"{\"target_valid\":"<<o.targetValid<<",\"queue\":"<<o.target.queue<<",\"count\":"<<o.target.count<<",\"last_word_valid\":"<<o.lastWordValid<<",\"last_word\":"<<o.lastWord<<",\"press_disabled_valid\":"<<o.pressDisabledValid<<",\"press_disabled\":"<<unsigned(o.pressDisabled)<<",\"modal_valid\":"<<o.modalValid<<",\"modal_selected\":"<<o.modalSelected<<",\"modal_vtable\":"<<o.modalVtable<<",\"modal_predicate\":"<<o.modalPredicate<<'}';};
    for(unsigned n=0;n<cancelEvidenceCount;++n){if(n)out<<',';const auto& e=cancelEvidence[n];out<<"{\"action\":"<<unsigned(e.action)<<",\"release\":"<<e.release<<",\"invoked\":"<<e.invoked<<",\"before\":";observation(e.before);out<<",\"after\":";observation(e.after);out<<'}';}
    out<<"],\"control_reads\":{\"ok\":"<<controlReadResults[0].load()<<",\"busy\":"<<controlReadResults[1].load()
       <<",\"invalid\":"<<controlReadResults[2].load()<<",\"timeout\":"<<controlReadResults[3].load()<<",\"closed\":"<<controlReadResults[4].load()<<'}'
       <<",\"control_rearms\":"<<controlRearms.load()<<",\"control_inactive\":"<<controlInactive.load()
       <<",\"toggle_edges\":"<<toggleEdges.load()<<",\"cancel_edges\":"<<cancelEdges.load()<<",\"edges_while_pending\":"<<edgesWhilePending.load()
       <<",\"control_edge_evidence\":[";
    for(unsigned n=0;n<controlEvidenceCount;++n){if(n)out<<',';const auto& e=controlEvidence[n];
        out<<"{\"sequence\":"<<e.sequence<<",\"space\":"<<e.space<<",\"toggle\":"<<e.toggle<<",\"cancel\":"<<e.cancel
           <<",\"read\":"<<unsigned(e.read)<<",\"toggle_edge\":"<<e.toggleEdge<<",\"cancel_edge\":"<<e.cancelEdge
           <<",\"entered\":"<<e.entered<<",\"pending\":"<<e.pending<<'}';}
    out<<"],\"headset_verified\":false}";
}
}
