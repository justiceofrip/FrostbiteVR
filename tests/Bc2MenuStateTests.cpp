#include "Test.h"
#include "Bc2MenuState.h"
#include <cstring>
#include <fstream>
#include <iterator>
#include <unordered_map>
#include <vector>
using namespace fvr;
namespace {
struct Fixture {
    static constexpr std::uint32_t base=0x400000,manager=0x4000000,owner=0x5000000;
    static constexpr std::uint32_t ui=0x6000000,target=0x6100000,queue=0x6200000,buffer=0x6300000,modal=0x6400000,modalVtable=0x6500000;
    bc2::MenuBindingCandidates pointer{0x509290,0xb82050,0xb82020,0x15a4ca8,0x17ea910,0x17ea704,0,1280,720,0x541c90,0x541e3b,0x1170e90};
    bc2::MenuStateCandidates binding{0x15a4ca8,0x102b7a0,0x102b794,0x102b764,0x3875b0,0x33f9c0,0x3764f0,0x102b780,0x33ef70,0x331f30};
    std::unordered_map<std::uint32_t,std::uint32_t> words;
    std::array<std::uint32_t,4> arguments{};
    unsigned calls=0,rootReads=0,inputCalls=0,failInputAt=0;std::vector<std::uint32_t> functions,actions;
    bool replaceOnPress=false,leaveOnPress=false,suppressPress=false;bool unstable=false,callSucceeds=true;
    Fixture(){words={{base+binding.managerGlobal,manager},{manager+0x82b20,owner},
        {owner,base+binding.ownerVtable},{owner+0x2c,base+binding.eventVtable},{owner+0x30,base+binding.controllerVtable},{owner+0x94,base+binding.inputVtable},
        {owner+0xac,0},{manager+0xc3f2a,0},
        {base+pointer.inputEnabled,1},{base+pointer.inputBlocked,0},
        {manager+0x14,ui},{ui+4,target},{target+0x18,queue},{queue+8,8},{queue+0x28,0},{queue+0x2c,buffer},
        {buffer,0},{buffer+28,0},{base+0x1170f7c,0},{manager+0x82434,modal},{manager+0x82438,0},
        {modal,modalVtable},{modalVtable+0x14,0x1400000}};}
    bc2::MenuMemory Memory(){return {this,[](void* ctx,std::uint32_t at,void* out,std::size_t n){
        auto& f=*static_cast<Fixture*>(ctx);if(n!=1&&n!=4)return false;
        if(at==base+f.binding.managerGlobal&&f.unstable&&++f.rootReads>1)return false;
        const auto i=f.words.find(at);if(i==f.words.end())return false;std::memcpy(out,&i->second,n);return true;}};}
    bc2::MenuVisibilityCalls Calls(){return {this,[](void* ctx,std::uint32_t function,std::uint32_t self,const std::array<std::uint32_t,4>& args){
        auto& f=*static_cast<Fixture*>(ctx);++f.calls;f.arguments=args;
        return function==base+f.binding.notification&&self==owner+0x2c&&f.callSucceeds;},
        [](void* ctx,std::uint32_t function,std::uint32_t self,std::uint32_t action){
            auto& f=*static_cast<Fixture*>(ctx);++f.inputCalls;f.functions.push_back(function);f.actions.push_back(action);
            if(self!=owner+0x30||(action!=8&&action!=9)||f.inputCalls==f.failInputAt)return false;
            const bool down=function==base+f.binding.cancelPressed;
            if(!down&&function!=base+f.binding.cancelReleased)return false;
            if((down&&!f.suppressPress)||(!down&&action==9)){
                auto& count=f.words[queue+0x28];if(count>=8)return false;
                f.words[buffer+count*4]=down?(action==8?0x025a0009:0x02580009):0x02580409;++count;
            }
            if(down){
                if(f.replaceOnPress)f.words[manager+0x82b20]=owner+4;
                if(f.leaveOnPress)f.words[owner+0xac]=0;
            }
            return true;
        }};}
};
int Reader(){
    Fixture f;const auto before=f.words;const auto closed=bc2::ReadMenuState(f.Memory(),f.binding,f.base);
    CHECK(closed&&!closed->entered&&!closed->pauseRequested&&closed->eventListener==f.owner+0x2c&&f.words==before);
    // Pause is independent: a cutscene/general pause is not menu-entered proof.
    f.words[f.manager+0xc3f2a]=1;auto paused=bc2::ReadMenuState(f.Memory(),f.binding,f.base);
    CHECK(paused&&!paused->entered&&paused->pauseRequested);
    f.words[f.owner+0xac]=1;CHECK(bc2::ReadMenuState(f.Memory(),f.binding,f.base)->entered);
    for(unsigned bad=0;bad<10;++bad){Fixture g;
        if(bad==0)g.words[g.manager+0x82b20]=0;
        if(bad==1)g.words[g.owner]=g.base+g.binding.eventVtable;
        if(bad==2)g.words[g.owner+0x2c]=0;
        if(bad==3)g.words[g.owner+0x94]=0;
        if(bad==4)g.words[g.owner+0xac]=2;
        if(bad==5)g.words[g.manager+0xc3f2a]=255;
        if(bad==6)g.words[g.manager+0x82b20]=0xfffffff0;
        if(bad==7)g.unstable=true;
        if(bad==8)g.binding.notification++;
        if(bad==9)g.words.erase(g.owner+0xac);
        CHECK(!bc2::ReadMenuState(g.Memory(),g.binding,g.base));
    }
    CHECK(!bc2::ReadMenuState(f.Memory(),f.binding,UINT32_MAX));return 0;
}
int Dispatch(){
    Fixture f;auto original=bc2::ReadMenuState(f.Memory(),f.binding,f.base);CHECK(original);
    const auto words=f.words;
    bc2::MenuVisibilityCommand command{1,4,1000,true};
    bc2::MenuVisibilityGuard guard{4,900,true,true,true,true};
    CHECK(bc2::DispatchMenuVisibility(f.Memory(),f.binding,f.base,*original,command,guard,f.Calls())==bc2::MenuVisibilityResult::Invoked);
    CHECK((f.calls==1&&f.arguments==std::array<std::uint32_t,4>({0x1b,0x16,0,1})));
    CHECK(f.words==words); // Calling the original queue is not a fabricated ack.
    f.words[f.owner+0xac]=1;f.words[f.manager+0xc3f2a]=1;
    CHECK(bc2::DispatchMenuVisibility(f.Memory(),f.binding,f.base,*original,command,guard,f.Calls())==bc2::MenuVisibilityResult::OwnerChanged);
    auto open=bc2::ReadMenuState(f.Memory(),f.binding,f.base);CHECK(open);
    CHECK(bc2::DispatchMenuVisibility(f.Memory(),f.binding,f.base,*open,command,guard,f.Calls())==bc2::MenuVisibilityResult::AlreadyInState);
    command.visible=false;
    CHECK(bc2::DispatchMenuVisibility(f.Memory(),f.binding,f.base,*open,command,guard,f.Calls())==bc2::MenuVisibilityResult::Rejected);
    CHECK(f.inputCalls==0);
    command.visible=true;
    for(unsigned bad=0;bad<9;++bad){Fixture g;auto snapshot=bc2::ReadMenuState(g.Memory(),g.binding,g.base);CHECK(snapshot);
        auto c=command;auto deny=guard;
        if(bad==0)deny.nativeUiThread=false;if(bad==1)deny.liveCodeVerified=false;if(bad==2)deny.focused=false;if(bad==3)deny.tracked=false;
        if(bad==4)c.generation=0;if(bad==5)deny.menuEpoch=5;if(bad==6)c.deadlineNs=900;if(bad==7)c.deadlineNs=2'000'001'000;if(bad==8)deny.nowNs=0;
        CHECK(bc2::DispatchMenuVisibility(g.Memory(),g.binding,g.base,*snapshot,c,deny,g.Calls())==bc2::MenuVisibilityResult::Rejected);CHECK(!g.calls&&!g.inputCalls);
    }
    return 0;
}

int HeldCancel(){
    using R=bc2::MenuVisibilityResult;
    Fixture f;f.words[f.owner+0xac]=1;
    const auto open=bc2::ReadMenuState(f.Memory(),f.binding,f.base);CHECK(open);
    bc2::MenuVisibilityCommand command{7,4,2'000'000'000,false};
    bc2::MenuVisibilityGuard guard{4,1'000'000'000,true,true,true,true};
    bc2::MenuOwnedCancel owned;bc2::MenuCancelDiagnostic report;
    const auto begin=[&](){return bc2::BeginMenuCancel(f.Memory(),f.binding,f.pointer,f.base,*open,command,guard,f.Calls(),owned,&report);};
    CHECK(begin()==R::Invoked&&owned.Active()&&owned.Epoch()==4&&owned.Generation()==7&&f.inputCalls==1);
    CHECK(owned.ReleaseAtNs()==1'100'000'000);
    CHECK(report.invoked&&!report.release&&report.before.targetValid&&report.before.target.count==0&&!report.before.lastWordValid);
    CHECK(report.after.targetValid&&report.after.target.count==1&&report.after.lastWordValid&&report.after.lastWord==0x02580009);
    CHECK(report.before.pressDisabledValid&&!report.before.pressDisabled&&report.before.modalValid&&report.before.modalSelected==f.modal);
    CHECK(report.before.modalVtable==f.modalVtable&&report.before.modalPredicate==0x1400000);
    CHECK(begin()==R::Rejected&&f.inputCalls==1); // Outstanding down cannot be duplicated.
    guard.nowNs=1'099'999'999;
    CHECK(bc2::ReleaseMenuCancel(f.Memory(),f.binding,f.pointer,f.base,guard,f.Calls(),owned,&report)==R::NotDue&&owned.Active()&&f.inputCalls==1);
    // Native may close on key-down. Cleanup must still reach that original
    // controller after menu epoch, focus and tracking have been lost.
    f.words[f.owner+0xac]=0;guard.menuEpoch=8;guard.focused=guard.tracked=false;guard.nowNs=1'100'000'000;
    CHECK(bc2::ReleaseMenuCancel(f.Memory(),f.binding,f.pointer,f.base,guard,f.Calls(),owned,&report)==R::Invoked&&!owned.Active()&&f.inputCalls==2);
    CHECK(report.invoked&&report.release&&report.before.lastWord==0x02580009&&report.after.lastWord==0x02580409);
    CHECK(report.before.target.count==1&&report.after.target.count==2);
    CHECK(bc2::ReleaseMenuCancel(f.Memory(),f.binding,f.pointer,f.base,guard,f.Calls(),owned)==R::Rejected&&f.inputCalls==2);
    CHECK(f.functions[0]==f.base+f.binding.cancelPressed&&f.functions[1]==f.base+f.binding.cancelReleased);
    return 0;
}
int CancelFailures(){
    using R=bc2::MenuVisibilityResult;
    const bc2::MenuVisibilityCommand command{7,4,2'000'000'000,false};
    const bc2::MenuVisibilityGuard guard{4,1'000'000'000,true,true,true,true};
    for(unsigned bad=0;bad<15;++bad){
        Fixture f;f.words[f.owner+0xac]=1;const auto open=bc2::ReadMenuState(f.Memory(),f.binding,f.base);CHECK(open);
        auto c=command;auto g=guard;auto binding=f.binding;auto pointer=f.pointer;auto calls=f.Calls();bc2::MenuOwnedCancel owned;
        if(bad==0)g.nativeUiThread=false;if(bad==1)g.liveCodeVerified=false;if(bad==2)g.focused=false;if(bad==3)g.tracked=false;
        if(bad==4)g.menuEpoch++;if(bad==5)c.generation=0;if(bad==6)c.deadlineNs=g.nowNs;if(bad==7)c.visible=true;
        if(bad==8)calls.input=nullptr;if(bad==9)binding.cancelPressed++;if(bad==10)pointer.managerGlobal++;
        if(bad==11)f.words[f.queue+0x28]=8;if(bad==12)f.failInputAt=1;if(bad==13)f.words[f.owner+0xac]=0;if(bad==14)c.action=static_cast<bc2::MenuCloseAction>(255);
        const auto result=bc2::BeginMenuCancel(f.Memory(),binding,pointer,f.base,*open,c,g,calls,owned);
        CHECK(result==(bad==11?R::QueueFull:bad==12?R::CallFailed:bad==13?R::OwnerChanged:R::Rejected));
        CHECK(!owned.Active()&&f.inputCalls==(bad==12?1u:0u));
    }
    for(unsigned edge=0;edge<12;++edge){
        Fixture f;f.words[f.owner+0xac]=1;const auto open=bc2::ReadMenuState(f.Memory(),f.binding,f.base);CHECK(open);
        bc2::MenuOwnedCancel owned;CHECK(bc2::BeginMenuCancel(f.Memory(),f.binding,f.pointer,f.base,*open,command,guard,f.Calls(),owned)==R::Invoked);
        auto g=guard;g.nowNs+=bc2::MenuOwnedCancel::HoldNs;auto binding=f.binding;auto pointer=f.pointer;auto base=f.base;
        if(edge==0)f.words[f.manager+0x82b20]=f.owner+4;
        if(edge==1)f.words[f.ui+4]=f.target+4;
        if(edge==2)f.words[f.queue+0x28]=8;
        if(edge==3)f.failInputAt=2;
        if(edge==4)binding.cancelReleased++;
        if(edge==5)pointer.sendEvent++;
        if(edge==6)base++;
        if(edge==7)g.nativeUiThread=false;
        if(edge==8)g.liveCodeVerified=false;
        if(edge==9)g.nowNs=guard.nowNs-1;
        if(edge==10)f.words.erase(f.owner+0xac);
        if(edge==11)f.words[f.queue+0x2c]=f.buffer+4;
        const auto result=bc2::ReleaseMenuCancel(f.Memory(),binding,pointer,base,g,f.Calls(),owned);
        CHECK(result==(edge==2?R::QueueFull:edge==3?R::CallFailed:(edge<2||edge>=10)?R::OwnerChanged:R::Rejected));
        CHECK(owned.Active()&&owned.Generation()==7&&f.inputCalls==(edge==3?2u:1u));
        // Repair the read-only fixture, not production state: the same owned
        // release can now complete, and no second native down was generated.
        Fixture restore;f.words=restore.words;f.words[f.owner+0xac]=1;f.failInputAt=0;g=guard;g.nowNs+=bc2::MenuOwnedCancel::HoldNs;
        CHECK(bc2::ReleaseMenuCancel(f.Memory(),f.binding,f.pointer,f.base,g,f.Calls(),owned)==R::Invoked&&!owned.Active());
    }
    for(unsigned edge=0;edge<4;++edge){
        Fixture f;f.words[f.owner+0xac]=1;const auto open=bc2::ReadMenuState(f.Memory(),f.binding,f.base);CHECK(open);
        if(edge==0)f.leaveOnPress=true;if(edge==1)f.replaceOnPress=true;
        if(edge==2){f.suppressPress=true;f.words[f.base+0x1170f7c]=1;}
        if(edge==3){f.words[f.manager+0x82434]=0;f.words[f.manager+0x82438]=f.modal;}
        bc2::MenuOwnedCancel owned;bc2::MenuCancelDiagnostic d;
        CHECK(bc2::BeginMenuCancel(f.Memory(),f.binding,f.pointer,f.base,*open,command,guard,f.Calls(),owned,&d)==R::Invoked&&owned.Active());
        if(edge==2)CHECK(d.after.target.count==0&&d.before.pressDisabled==1); // Native gate, no fabricated consumed event.
        if(edge==3)CHECK(d.before.modalValid&&d.before.modalSelected==f.modal&&d.before.modalPrimary==0);
        auto g=guard;g.focused=g.tracked=false;g.menuEpoch=0;
        const auto result=bc2::ReleaseMenuCancel(f.Memory(),f.binding,f.pointer,f.base,g,f.Calls(),owned,&d,true);
        CHECK(result==(edge==1?R::OwnerChanged:R::Invoked));
        CHECK(owned.Active()==(edge==1)); // Replacement never receives old up.
    }
    return 0;
}

int MenuToggle(){
    using R=bc2::MenuVisibilityResult;
    Fixture f;f.words[f.owner+0xac]=1;
    const auto open=bc2::ReadMenuState(f.Memory(),f.binding,f.base);CHECK(open);
    bc2::MenuVisibilityCommand command{12,5,2'000'000'000,false,bc2::MenuCloseAction::MenuToggle};
    bc2::MenuVisibilityGuard guard{5,1'000'000'000,true,true,true,true};
    bc2::MenuOwnedCancel owned;bc2::MenuCancelDiagnostic d;
    CHECK(bc2::BeginMenuCancel(f.Memory(),f.binding,f.pointer,f.base,*open,command,guard,f.Calls(),owned,&d)==R::Invoked);
    CHECK(owned.Active()&&owned.Action()==bc2::MenuCloseAction::MenuToggle&&owned.UiAction()==8);
    CHECK(d.action==bc2::MenuCloseAction::MenuToggle&&d.after.lastWordValid&&d.after.lastWord==0x025a0009);
    CHECK(f.actions.size()==1&&f.actions[0]==8);
    command.action=bc2::MenuCloseAction::Back; // A later command cannot change the leased native action.
    CHECK(bc2::BeginMenuCancel(f.Memory(),f.binding,f.pointer,f.base,*open,command,guard,f.Calls(),owned)==R::Rejected);
    guard.nowNs+=bc2::MenuOwnedCancel::HoldNs;guard.focused=false;guard.menuEpoch=20;f.words[f.owner+0xac]=0;
    CHECK(bc2::ReleaseMenuCancel(f.Memory(),f.binding,f.pointer,f.base,guard,f.Calls(),owned,&d)==R::Invoked&&!owned.Active());
    CHECK(f.actions.size()==2&&f.actions[1]==8&&d.action==bc2::MenuCloseAction::MenuToggle);
    // The real release function ignores action8. Never turn native MenuToggle
    // into a fabricated key-up or a different Back release.
    CHECK(d.before.target.count==1&&d.after.target.count==1&&d.before.lastWord==d.after.lastWord);
    CHECK(d.after.lastWord==0x025a0009);
    return 0;
}
int StaticEvidence(const char* path){
    std::ifstream in(path,std::ios::binary);CHECK(in);
    const std::vector<char> raw((std::istreambuf_iterator<char>(in)),{});std::vector<std::byte> bytes(raw.size());
    std::memcpy(bytes.data(),raw.data(),raw.size());const auto pe=engine::InspectPe(bytes);CHECK(pe.valid);
    const auto binding=bc2::DiscoverMenuState(bytes,pe.image);CHECK(binding);
    CHECK(binding->notification==0x3875b0&&binding->enterSetter==0x33f9c0&&binding->managerGlobal==0x15a4ca8);
    // Independently disturb category/subtype routing, argument ABI, enterMenu
    // meaning, constructor owner publication and each reflected vtable linkage.
    for(const std::uint32_t va:{0x7875ca,0x788b7b,0x788bf6,0x788bb0,0x7877be,0x788812,
        0x143cdac,0x776bbd,0x73fa3c,0x783df1,0x8fec4a,0x142b798,0x142b7d0,
        0x9490d7,0x73ef8f,0x73efd0,0x73eff8,0x731f50,0x8fe3b0,0x8fe34d,0x142b780,0xf822b4,0xf822d3,0x8fe3ac,0x8fe345,0x731f39}){
        bool touched=false;for(const auto& s:pe.image.sections)if(va>=Fixture::base+s.rva&&va-Fixture::base-s.rva<s.rawSize){
            const auto at=s.rawOffset+va-Fixture::base-s.rva;const auto old=bytes[at];bytes[at]^=std::byte{1};
            CHECK(!bc2::DiscoverMenuState(bytes,pe.image));bytes[at]=old;touched=true;break;}CHECK(touched);
    }
    return 0;
}
}
int main(int argc,char** argv){CHECK(Reader()==0);CHECK(Dispatch()==0);CHECK(HeldCancel()==0);CHECK(CancelFailures()==0);CHECK(MenuToggle()==0);if(argc>1)CHECK(StaticEvidence(argv[1])==0);return 0;}
