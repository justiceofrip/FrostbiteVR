#include "Bc2MenuState.h"
#include "fvr/engine/BindingValidation.h"
#include <cstring>
#include <limits>
namespace fvr::bc2 {
namespace {
// This evidence belongs to the inspected x86 BC2 build, not other Frostbite games.
constexpr std::uint32_t PreferredBase=0x400000;
std::optional<std::size_t> At(std::span<const std::byte> b,const engine::PeImage& pe,std::uint32_t va,std::size_t size){
    if(va<PreferredBase)return {};const auto rva=va-PreferredBase;
    for(const auto& s:pe.sections)if(rva>=s.rva){const auto d=std::uint64_t(rva)-s.rva,at=std::uint64_t(s.rawOffset)+d;
        if(d<=s.rawSize&&size<=s.rawSize-d&&at<=b.size()&&size<=b.size()-at)return std::size_t(at);}return {};
}
std::uint32_t Word(std::span<const std::byte> b,std::size_t at){std::uint32_t value=0;std::memcpy(&value,b.data()+at,4);return value;}
bool Match(std::span<const std::byte> b,const engine::PeImage& pe,std::uint32_t va,const char* text){
    const auto p=engine::ParsePattern(text);if(!p)return false;const auto at=At(b,pe,va,p->size());if(!at)return false;
    for(std::size_t i=0;i<p->size();++i)if(!(*p)[i].wildcard&&std::to_integer<unsigned char>(b[*at+i])!=(*p)[i].value)return false;return true;
}
bool EqualsWord(std::span<const std::byte> b,const engine::PeImage& pe,std::uint32_t va,std::uint32_t word){
    const auto at=At(b,pe,va,4);return at&&Word(b,*at)==word;
}
bool Call(std::span<const std::byte> b,const engine::PeImage& pe,std::uint32_t va,std::uint32_t target){
    const auto at=At(b,pe,va,5);return at&&b[*at]==std::byte{0xe8}&&
        std::int64_t(va)+5+std::int32_t(Word(b,*at+1))==target;
}
bool Read(const MenuMemory& m,std::uint64_t at,void* out,std::size_t size){
    return m.read&&at>=0x10000&&at<=UINT32_MAX&&size&&size<=UINT32_MAX-at&&m.read(m.context,std::uint32_t(at),out,size);
}
bool Candidates(const MenuStateCandidates& c){
    return c.managerGlobal==0x15a4ca8&&c.ownerVtable==0x102b7a0&&c.eventVtable==0x102b794&&
        c.inputVtable==0x102b764&&c.notification==0x3875b0&&c.enterSetter==0x33f9c0&&c.enterDispatcher==0x3764f0&&
        c.controllerVtable==0x102b780&&c.cancelPressed==0x33ef70&&c.cancelReleased==0x331f30;
}
}
std::array<MenuStateCodeSpan,13> MenuStateCodeSpans() noexcept {
    // Includes dispatch maps and all instructions on the selected notification
    // branch. The final two spans include vtables, not mutable runtime data.
    return {{{0x3875b0,0x1680},{0x3764f0,0xf00},{0x33f9c0,0x17a},
             {0x383db0,0x75},{0x4fec10,0x78},{0x102ba2c,15},
             {0x102b764,0x74},{0x103cda8,8},{0x33ef70,0xb5},{0x331f30,0x44},
             {0x4fe2d0,0x140},{0x5490cb,0x16},{0xb822b0,0x29}}};
}
std::optional<MenuStateCandidates> DiscoverMenuState(std::span<const std::byte> b,const engine::PeImage& pe){
    if(pe.machine!=0x14c||b.size()<0x40)return {};const auto nt=Word(b,0x3c);
    if(nt>b.size()||56>b.size()-nt||Word(b,std::size_t(nt)+52)!=PreferredBase)return {};
    for(const auto span:MenuStateCodeSpans())if(!At(b,pe,PreferredBase+span.rva,span.size))return {};
    if(!Match(b,pe,0x7875b0,"55 8B EC 83 E4 F0 81 EC 84 01 00 00 8B 45 08 53 56 57 8B F9 8B 0D A8 4C 9A 01 83 C0 F0 83 F8 12 8B D9 89 5C 24 10 0F 87 66 15 00 00 0F B6 80 70 8B 78 00 FF 24 85 4C 8B 78 00")||
       !Match(b,pe,0x7876c6,"8B 45 0C 83 C0 F0 83 F8 1D 0F 87 6D 14 00 00 0F B6 80 F0 8B 78 00 FF 24 85 A4 8B 78 00")||
       !Match(b,pe,0x788b7b,"05")||!EqualsWord(b,pe,0x788b60,0x7876c6)||
       !Match(b,pe,0x788bf6,"03")||!EqualsWord(b,pe,0x788bb0,0x7877a2))return {};
    if(!Match(b,pe,0x7877a2,"E8 ?? ?? ?? ?? 8B F0 85 F6 0F 84 91 13 00 00 68 2C BA 42 01 8B CE E8 ?? ?? ?? ?? 8B 4D 14 51 E8 ?? ?? ?? ?? E9 2A 10 00 00")||
       !Call(b,pe,0x7877a2,0x920360)||!Call(b,pe,0x7877b8,0x588910)||!Call(b,pe,0x7877c1,0xf7c5d0)||
       !Match(b,pe,0x7887f5,"83 C4 04 50 8B CE E8 ?? ?? ?? ?? 8B 0D A8 4C 9A 01 56 E8 ?? ?? ?? ?? 5F 5E 5B 8B E5 5D C2 10 00")||
       !Call(b,pe,0x7887fb,0x588940)||!Call(b,pe,0x788807,0x9299b0))return {};
    const auto name=At(b,pe,0x142ba2c,15);if(!name||std::memcmp(b.data()+*name,"showIngameMenu",14)!=0)return {};
    // Exact enterMenu table ID, dispatcher branch and setter; not pauseGame.
    if(!EqualsWord(b,pe,0x143cda8,0x143f714)||!EqualsWord(b,pe,0x143cdac,0x8f))return {};
    const auto enterName=At(b,pe,0x143f714,10);if(!enterName||std::memcmp(b.data()+*enterName,"enterMenu",10)!=0)return {};
    if(!Match(b,pe,0x776553,"83 C5 FF 81 FD BA 00 00 00 0F 87 AE 0C 00 00 0F B6 85 F4 72 77 00 FF 24 85 34 72 77 00")||
       !Match(b,pe,0x776ba3,"8B 8C 24 CC 00 00 00 51 FF 15 FC 54 40 01 83 C4 04 85 C0 0F 95 C2 8B CE 52 E8 ?? ?? ?? ??")||
       !Call(b,pe,0x776bbc,0x73f9c0))return {};
    const auto map=At(b,pe,0x7772f4+0x8e,1);if(!map)return {};
    if(!EqualsWord(b,pe,0x777234+4*std::to_integer<unsigned char>(b[*map]),0x776ba3))return {};
    if(!Match(b,pe,0x73fa34,"8A 8E AC 00 00 00 88 9E AC 00 00 00 A1 A8 4C 9A 01 88 98 2A 3F 0C 00")||
       !Match(b,pe,0x73fb31,"5F 5E 5B 8B E5 5D C2 04 00")||
       !Match(b,pe,0x783def,"C7 06 A0 B7 42 01 C7 07 94 B7 42 01 C7 86 94 00 00 00 64 B7 42 01")||
       !Match(b,pe,0x783dc3,"8D 7E 2C")||!Match(b,pe,0x783e1e,"C6 86 AC 00 00 00 01")||
       !Call(b,pe,0x8fec36,0x783db0)||!Match(b,pe,0x8fec46,"8B CE 89 B7 20 2B 08 00"))return {};
    if(!EqualsWord(b,pe,0x142b798,0x7875b0)||!EqualsWord(b,pe,0x142b7d0,0x7764f0))return {};
    // Actual native Cancel path: UI action9 is ConceptCancel58; Menu57 is8.
    if(!Match(b,pe,0x9490cb,"6A 39 6A 08 8B CE E8 ?? ?? ?? ?? 6A 3A 6A 09 8B CE E8 ?? ?? ?? ??")||
       !Call(b,pe,0x9490d1,0x7c3310)||!Call(b,pe,0x9490dc,0x7c3310)||
       !Match(b,pe,0x7c3310,"8B 01 8B 4C 24 08 8B 54 24 04 89 0C 90 C2 08 00")||
       !EqualsWord(b,pe,0x1c2f6a8,57)||!EqualsWord(b,pe,0x1c2f6c0,58)||
       !EqualsWord(b,pe,0x142b780,0x73ef70)||!EqualsWord(b,pe,0x142b784,0x731f30))return {};
    if(!Match(b,pe,0x73ef70,"83 EC 0C 80 3D 7C 0F 57 01 00 57 8B F9 0F 85 9B 00 00 00 56 8B 74 24 18 83 FE 08 74 33 83 FE 09 74 2E")||
       !Call(b,pe,0x73efc6,0xf822b0)||
       !Match(b,pe,0x73efcb,"84 C0 75 22 83 FE 09 75 1D E8 ?? ?? ?? ?? 8B 48 18 8B 01 8B 50 18 6A 00 6A 14 6A 21 FF D2")||
       !Call(b,pe,0x73efd4,0x7ff060)||
       !Match(b,pe,0x73eff1,"56 8B CF C7 44 24 10 00 00 00 00 C7 44 24 14 02 00 00 00 E8 ?? ?? ?? ?? 8B 0D A8 4C 9A 01 89 44 24 08 8D 44 24 08 50 E8 ?? ?? ?? ?? 5E 5F 83 C4 0C C2 04 00")||
       !Call(b,pe,0x73f004,0x8fe2d0)||!Call(b,pe,0x73f018,0xf82020))return {};
    if(!Match(b,pe,0x731f30,"8B 44 24 04 83 EC 0C 83 F8 09 74 0A 83 F8 1B 74 05 83 F8 20 75 28 50 C7 44 24 08 01 00 00 00 C7 44 24 0C 02 00 00 00 E8 ?? ?? ?? ?? 8B 0D A8 4C 9A 01 89 04 24 8D 04 24 50 E8 ?? ?? ?? ?? 83 C4 0C C2 04 00")||
       !Call(b,pe,0x731f57,0x8fe2d0)||!Call(b,pe,0x731f69,0xf82020)||
       !Match(b,pe,0x8fe2d0,"8B 44 24 04 83 F8 20 0F 87 A7 00 00 00 FF 24 85 8C E3 8F 00")||
       !EqualsWord(b,pe,0x8fe3b0,0x8fe34c)||!Match(b,pe,0x8fe34c,"B8 2C 01 00 00 C2 04 00")||
       !EqualsWord(b,pe,0x8fe3ac,0x8fe344)||!Match(b,pe,0x8fe344,"B8 2D 01 00 00 C2 04 00"))return {};
    if(!Match(b,pe,0xf822b0,"8B C1 8B 88 34 24 08 00 85 C9 74 07 8B 01 8B 50 14 FF E2 8B 80 38 24 08 00 85 C0 74 09 8B 10 8B C8 8B 42 14 FF E0 B0 01 C3"))return {};
    return MenuStateCandidates{0x15a4ca8,0x102b7a0,0x102b794,0x102b764,0x3875b0,0x33f9c0,0x3764f0,
        0x102b780,0x33ef70,0x331f30};
}
std::optional<MenuStateSnapshot> ReadMenuState(const MenuMemory& m,const MenuStateCandidates& c,std::uint32_t base){
    if(!Candidates(c)||base<0x10000||std::uint64_t(base)+c.managerGlobal+4>UINT32_MAX)return {};
    const auto one=[&]()->std::optional<MenuStateSnapshot>{
        MenuStateSnapshot s;std::uint32_t primary=0,event=0,input=0,controller=0;unsigned char entered=0,pause=0;
        if(!Read(m,std::uint64_t(base)+c.managerGlobal,&s.manager,4)||
           !Read(m,std::uint64_t(s.manager)+0x82b20,&s.owner,4)||s.owner<0x10000||
           !Read(m,s.owner,&primary,4)||primary!=std::uint64_t(base)+c.ownerVtable||
           !Read(m,std::uint64_t(s.owner)+0x2c,&event,4)||event!=std::uint64_t(base)+c.eventVtable||
           !Read(m,std::uint64_t(s.owner)+0x30,&controller,4)||controller!=std::uint64_t(base)+c.controllerVtable||
           !Read(m,std::uint64_t(s.owner)+0x94,&input,4)||input!=std::uint64_t(base)+c.inputVtable||
           !Read(m,std::uint64_t(s.owner)+0xac,&entered,1)||entered>1||
           !Read(m,std::uint64_t(s.manager)+0xc3f2a,&pause,1)||pause>1)return {};
        s.eventListener=s.owner+0x2c;s.inputController=s.owner+0x30;s.entered=entered!=0;s.pauseRequested=pause!=0;return s;
    };
    const auto first=one(),second=one();if(!first||!second||*first!=*second)return {};return first;
}

namespace {
bool ValidCommand(const MenuVisibilityCommand& c,const MenuVisibilityGuard& g){
    return g.nativeUiThread&&g.liveCodeVerified&&g.focused&&g.tracked&&g.menuEpoch&&g.menuEpoch==c.menuEpoch&&
        (c.action==MenuCloseAction::Back||c.action==MenuCloseAction::MenuToggle)&&c.generation&&g.nowNs>0&&c.deadlineNs>g.nowNs&&c.deadlineNs-g.nowNs<=2'000'000'000;
}
bool SameOwner(const MenuStateSnapshot& a,const MenuStateSnapshot& b){
    return a.manager==b.manager&&a.owner==b.owner&&a.eventListener==b.eventListener&&a.inputController==b.inputController;
}
bool SameTarget(const MenuTargetSnapshot& a,const MenuTargetSnapshot& b){
    return a.manager==b.manager&&a.ui==b.ui&&a.target==b.target&&a.queue==b.queue&&a.buffer==b.buffer&&a.capacity==b.capacity;
}
MenuCancelObservation ObserveCancel(const MenuMemory& memory,const MenuBindingCandidates& b,std::uint32_t base,std::uint32_t manager){
    MenuCancelObservation d;
    // The press function's exact signature proves this global and F822B0's
    // selected modal fields. These event-only reads add no native side effect.
    d.pressDisabledValid=Read(memory,std::uint64_t(base)+0x1170f7c,&d.pressDisabled,1);
    const auto first=ReadMenuTarget(memory,b,base);
    if(first&&first->manager==manager){
        d.target=*first;d.targetValid=true;
        if(first->count){
            std::uint32_t word=0;
            if(Read(memory,std::uint64_t(first->buffer)+std::uint64_t(first->count-1)*4,&word,4)){
                const auto second=ReadMenuTarget(memory,b,base);std::uint32_t repeated=0;
                if(second&&*second==*first&&Read(memory,std::uint64_t(first->buffer)+std::uint64_t(first->count-1)*4,&repeated,4)&&word==repeated){
                    d.lastWord=word;d.lastWordValid=true;
                }
            }
        }
    }
    const auto modal=[&](std::array<std::uint32_t,5>& v){
        if(!Read(memory,std::uint64_t(manager)+0x82434,&v[0],4)||!Read(memory,std::uint64_t(manager)+0x82438,&v[1],4))return false;
        v[2]=v[0]?v[0]:v[1];
        return !v[2]||(Read(memory,v[2],&v[3],4)&&Read(memory,std::uint64_t(v[3])+0x14,&v[4],4));
    };
    std::array<std::uint32_t,5> a{},z{};
    if(modal(a)&&modal(z)&&a==z){
        d.modalPrimary=a[0];d.modalFallback=a[1];d.modalSelected=a[2];d.modalVtable=a[3];d.modalPredicate=a[4];d.modalValid=true;
    }
    return d;
}
}
MenuVisibilityResult DispatchMenuVisibility(const MenuMemory& memory,const MenuStateCandidates& c,std::uint32_t base,
    const MenuStateSnapshot& expected,const MenuVisibilityCommand& command,const MenuVisibilityGuard& guard,const MenuVisibilityCalls& calls){
    if(!Candidates(c)||!command.visible||!ValidCommand(command,guard)||!calls.notify||
       std::uint64_t(base)+c.notification>UINT32_MAX)return MenuVisibilityResult::Rejected;
    const auto current=ReadMenuState(memory,c,base);if(!current||*current!=expected)return MenuVisibilityResult::OwnerChanged;
    if(current->entered)return MenuVisibilityResult::AlreadyInState;
    const std::array<std::uint32_t,4> arguments{0x1b,0x16,0,1};
    return calls.notify(calls.context,base+c.notification,current->eventListener,arguments)?
        MenuVisibilityResult::Invoked:MenuVisibilityResult::CallFailed;
}
MenuVisibilityResult BeginMenuCancel(const MenuMemory& memory,const MenuStateCandidates& c,const MenuBindingCandidates& b,std::uint32_t base,
    const MenuStateSnapshot& expected,const MenuVisibilityCommand& command,const MenuVisibilityGuard& guard,const MenuVisibilityCalls& calls,
    MenuOwnedCancel& owned,MenuCancelDiagnostic* diagnostic){
    if(diagnostic){*diagnostic={};diagnostic->action=command.action;}
    if(owned.Active()||!Candidates(c)||command.visible||!ValidCommand(command,guard)||!calls.input||
       b.managerGlobal!=c.managerGlobal||std::uint64_t(base)+c.cancelPressed>UINT32_MAX||
       guard.nowNs>std::numeric_limits<std::int64_t>::max()-MenuOwnedCancel::HoldNs)return MenuVisibilityResult::Rejected;
    const auto current=ReadMenuState(memory,c,base);if(!current||*current!=expected)return MenuVisibilityResult::OwnerChanged;
    if(!current->entered)return MenuVisibilityResult::AlreadyInState;
    const auto target=ReadMenuTarget(memory,b,base);if(!target||target->manager!=current->manager)return MenuVisibilityResult::OwnerChanged;
    if(diagnostic)diagnostic->before=ObserveCancel(memory,b,base,current->manager);
    if(target->count>=target->capacity)return MenuVisibilityResult::QueueFull;
    // Revalidate after optional diagnostics; only native code may enqueue.
    const auto before=ReadMenuState(memory,c,base);const auto queue=ReadMenuTarget(memory,b,base);
    if(!before||*before!=*current||!queue||!SameTarget(*target,*queue))return MenuVisibilityResult::OwnerChanged;
    if(queue->count>=queue->capacity)return MenuVisibilityResult::QueueFull;
    if(!calls.input(calls.context,base+c.cancelPressed,current->inputController,command.action==MenuCloseAction::MenuToggle?8u:9u))return MenuVisibilityResult::CallFailed;
    // Mint before any post-call read: an owner replacement/read failure cannot
    // erase the obligation to pair a native press that was actually invoked.
    owned.state_=*current;owned.target_=*queue;owned.stateBinding_=c;owned.pointerBinding_=b;owned.base_=base;
    owned.generation_=command.generation;owned.epoch_=command.menuEpoch;owned.pressedAtNs_=guard.nowNs;owned.action_=command.action;
    owned.releaseAtNs_=guard.nowNs+MenuOwnedCancel::HoldNs;
    if(diagnostic){diagnostic->invoked=true;diagnostic->after=ObserveCancel(memory,b,base,current->manager);}
    return MenuVisibilityResult::Invoked;
}
MenuVisibilityResult ReleaseMenuCancel(const MenuMemory& memory,const MenuStateCandidates& c,const MenuBindingCandidates& b,std::uint32_t base,
    const MenuVisibilityGuard& guard,const MenuVisibilityCalls& calls,MenuOwnedCancel& owned,MenuCancelDiagnostic* diagnostic,bool retiring){
    if(diagnostic){*diagnostic={};diagnostic->release=true;diagnostic->action=owned.Action();}
    if(!owned.Active()||!guard.nativeUiThread||!guard.liveCodeVerified||guard.nowNs<=0||guard.nowNs<owned.pressedAtNs_||
       !calls.input||base!=owned.base_||c!=owned.stateBinding_||b!=owned.pointerBinding_||
       std::uint64_t(base)+c.cancelReleased>UINT32_MAX)return MenuVisibilityResult::Rejected;
    if(!retiring&&guard.nowNs<owned.releaseAtNs_)return MenuVisibilityResult::NotDue;
    const auto current=ReadMenuState(memory,c,base);const auto target=ReadMenuTarget(memory,b,base);
    if(!current||!SameOwner(*current,owned.state_)||!target||!SameTarget(*target,owned.target_))return MenuVisibilityResult::OwnerChanged;
    if(diagnostic)diagnostic->before=ObserveCancel(memory,b,base,current->manager);
    if(target->count>=target->capacity)return MenuVisibilityResult::QueueFull;
    const auto before=ReadMenuState(memory,c,base);const auto queue=ReadMenuTarget(memory,b,base);
    if(!before||!SameOwner(*before,owned.state_)||!queue||!SameTarget(*queue,owned.target_))return MenuVisibilityResult::OwnerChanged;
    if(queue->count>=queue->capacity)return MenuVisibilityResult::QueueFull;
    if(!calls.input(calls.context,base+c.cancelReleased,current->inputController,owned.UiAction()))return MenuVisibilityResult::CallFailed;
    owned.Forget();
    if(diagnostic){diagnostic->invoked=true;diagnostic->after=ObserveCancel(memory,b,base,current->manager);}
    return MenuVisibilityResult::Invoked;
}
}
