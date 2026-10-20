#include "Test.h"
#include "Bc2MenuBinding.h"
#include <cstring>
#include <fstream>
#include <iterator>
#include <limits>
#include <unordered_map>
#include <vector>
using namespace fvr;
namespace {
struct Fixture {
    static constexpr std::uint32_t base=0x400000,manager=0x30000,ui=0x31000,target=0x32000,queue=0x33000,buffer=0x34000;
    bc2::MenuOwnedPress owned;
    bc2::MenuBindingCandidates binding{0x1000,0x2000,0x2100,0x100,0x104,0x108,0x10c,1280,720};
    std::unordered_map<std::uint32_t,std::uint32_t> words;
    unsigned reads=0,cursorCalls=0,eventCalls=0;bool failCursor=false,failEvent=false,changeOnCursor=false,fillOnCursor=false,unstable=false;
    unsigned mapCalls=0;bool failMap=false,changeOnMap=false;
    std::array<float,2> mapped{351.75f,191.9f},pixels{};
    static constexpr std::uint32_t display=0x80000;
    std::uint32_t displayWidthOffset=0;
    std::array<std::uint32_t,3> event{};std::int32_t x=0,y=0,eventX=0,eventY=0;
    Fixture(){words={{base+0x100,manager},{base+0x104,1},{base+0x108,0},{manager+0x14,ui},{ui+4,target},{target+0x18,queue},{queue+8,8},{queue+0x28,0},{queue+0x2c,buffer},{buffer,0},{buffer+28,0}};}
    bc2::MenuMemory Memory(){return {this,[](void* ctx,std::uint32_t at,void* out,std::size_t n){
        auto& f=*static_cast<Fixture*>(ctx);if(n!=4&&n!=1)return false;
        if(at==base+0x100&&f.unstable&&++f.reads>1)return false;
        const auto found=f.words.find(at);if(found==f.words.end())return false;
        std::memcpy(out,&found->second,n);return true;}};}
    void Display(unsigned width,unsigned height,unsigned profile=2){
        binding.coordinateMapper=0x509060;binding.displayGlobal=0x1172a60;
        const std::array<std::uint32_t,3> tables{0x10497a0,0x10496c8,0x1049970};
        const std::array<std::array<std::uint32_t,5>,3> getters{{
            {0x5d8180,0x5d8190,0x5d8290,0x5f4050,0x5f4060},
            {0x5d7270,0x5d7280,0x5d7370,0x5f2dc0,0x5f2dd0},
            {0x5d60a0,0x5d60b0,0x5d6060,0x5fa2c0,0x5fa2d0}}};
        const std::array<unsigned,5> slots{0x28,0x2c,0x98,0x9c,0xa0};
        const auto table=base+tables[profile];
        words[base+binding.displayGlobal]=display;words[display]=table;
        for(unsigned i=0;i<5;++i)words[table+slots[i]]=base+getters[profile][i];
        displayWidthOffset=profile==2?0x0c:0x18;
        words[display+displayWidthOffset]=width;words[display+displayWidthOffset+4]=height;
        const unsigned flag=profile==0?0xec:profile==1?0x165:0xb0;words[display+flag]=0;
        if(profile==2){const float aspect=float(width)/float(height);std::memcpy(&words[display+0x124],&aspect,4);}
        else {const unsigned aspect=profile==0?0xcc:0xd8;words[display+aspect]=width;words[display+aspect+4]=height;}
    }
    bc2::MenuNativeCalls Calls(){return {this,
        [](void* ctx,std::uint32_t fn,std::uint32_t self,std::int32_t x,std::int32_t y){auto& f=*static_cast<Fixture*>(ctx);
            ++f.cursorCalls;if(fn!=base+f.binding.setCursor||self!=manager)return false;f.x=x;f.y=y;
            if(f.changeOnCursor)f.words[ui+4]=target+4;if(f.fillOnCursor)f.words[queue+0x28]=8;
            return !f.failCursor;},
        [](void* ctx,std::uint32_t fn,std::uint32_t self,const std::array<std::uint32_t,3>& event){auto& f=*static_cast<Fixture*>(ctx);
            ++f.eventCalls;f.event=event;f.eventX=f.x;f.eventY=f.y;return fn==base+f.binding.sendEvent&&self==manager&&!f.failEvent;},
        [](void* ctx,std::uint32_t fn,std::array<float,2>& out,const std::array<float,2>& pixels){auto& f=*static_cast<Fixture*>(ctx);
            ++f.mapCalls;f.pixels=pixels;out=f.mapped;
            if(f.changeOnMap)++f.words[display+f.displayWidthOffset];
            return fn==base+0x509060&&!f.failMap;}};}
};
int RasterMapping(){
    using enum bc2::MenuDispatchResult;
    const bc2::MenuDispatchGuard guard{2,900,true,true,true,true};
    const auto dispatch=[&](Fixture& f,const bc2::MenuDispatchGuard& g,bc2::MenuPointerKind kind=bc2::MenuPointerKind::PrimaryDown){
        const auto target=bc2::ReadMenuTarget(f.Memory(),f.binding,f.base);if(!target)return TargetChanged;
        return bc2::DispatchMenuRasterPointer(f.Memory(),f.binding,f.base,*target,.5f,.25f,kind,1,1000,g,f.Calls(),f.owned);
    };
    for(const auto dimensions:{std::array{1024u,768u},std::array{1280u,800u},std::array{1920u,1080u},std::array{2560u,1080u}}){
        for(unsigned profile=0;profile<3;++profile){
            Fixture f;f.Display(dimensions[0],dimensions[1],profile);
            CHECK(dispatch(f,guard)==Invoked&&f.mapCalls==1&&f.cursorCalls==1&&f.eventCalls==1);
            CHECK(f.pixels[0]==float(dimensions[0])*.5f&&f.pixels[1]==float(dimensions[1])*.25f);
            // Cursor/event use the original mapper's output, not UV*1280/720.
            CHECK(f.x==351&&f.y==191&&f.eventX==351&&f.eventY==191);
            f.mapped={710.8f,510.2f};
            CHECK(dispatch(f,guard,bc2::MenuPointerKind::PrimaryUp)==Invoked&&!f.owned.Active());
            CHECK(f.eventX==710&&f.eventY==510);
        }
    }
    for(unsigned bad=0;bad<6;++bad){Fixture f;f.Display(1024,768);auto denied=guard;
        if(bad==0)denied.nativeUiThread=false;if(bad==1)denied.menuOwned=false;if(bad==2)denied.focused=false;
        if(bad==3)denied.tracked=false;if(bad==4)denied.nowNs=1000;if(bad==5)denied.menuEpoch=0;
        CHECK(dispatch(f,denied)==Rejected&&!f.mapCalls&&!f.cursorCalls&&!f.eventCalls);
    }
    for(unsigned bad=0;bad<12;++bad){Fixture f;f.Display(1024,768);
        if(bad==0)f.words[f.display]=0x99999;
        if(bad==1)f.words[f.base+0x1049970+0x98]+=4;
        if(bad==2)f.words[f.display+0x0c]=0;
        if(bad==3)f.words[f.display+0x10]=32769;
        if(bad==4)f.words.erase(f.display+0x124);
        if(bad==5)f.words[f.display+0x124]=0x7fc00000;
        if(bad==6)f.failMap=true;
        if(bad==7)f.changeOnMap=true;
        if(bad==8)f.mapped[0]=std::numeric_limits<float>::quiet_NaN();
        if(bad==9)f.mapped[1]=-1;
        if(bad==10)f.mapped[0]=1280.5f;
        if(bad==11)f.binding.coordinateMapper+=1;
        CHECK(dispatch(f,guard)!=Invoked&&!f.cursorCalls&&!f.eventCalls&&!f.owned.Active());
        CHECK(f.mapCalls==(bad>=6&&bad<=10?1u:0u));
    }
    // Current native viewport is consumed by the original mapper. Reject bad
    // rectangles without calling it; do not silently clamp to a menu button.
    Fixture viewport;viewport.Display(1280,800);viewport.words[viewport.display+0xb0]=1;
    for(unsigned i=0;i<4;++i)viewport.words[viewport.display+0xc0+4*i]=std::array<unsigned,4>{0,40,1280,720}[i];
    CHECK(dispatch(viewport,guard)==Invoked);
    const auto maps=viewport.mapCalls;viewport.words[viewport.display+0xcc]=800;
    CHECK(dispatch(viewport,guard,bc2::MenuPointerKind::PrimaryUp)==Rejected&&viewport.mapCalls==maps&&viewport.owned.Active());
    CHECK(bc2::ReleaseMenuPointer(viewport.Memory(),viewport.binding,viewport.base,viewport.owned.Target(),2,guard,viewport.Calls(),viewport.owned)==Invoked);
    return 0;
}
int TargetAndBounds(){
    Fixture f;const auto original=f.words;const auto target=bc2::ReadMenuTarget(f.Memory(),f.binding,f.base);
    CHECK(target&&target->manager==f.manager&&target->target==f.target&&target->capacity==8&&f.words==original);
    for(unsigned bad=0;bad<10;++bad){Fixture g;
        if(bad==0)g.words[g.base+0x104]=0;if(bad==1)g.words[g.base+0x108]=1;
        if(bad==2)g.words[g.manager+0x14]=0;if(bad==3)g.words[g.ui+4]=0;
        if(bad==4)g.words[g.queue+8]=0;if(bad==5)g.words[g.queue+8]=16385;
        if(bad==6)g.words[g.queue+0x28]=9;if(bad==7)g.words[g.queue+0x2c]=0xfffffff0;
        if(bad==8)g.unstable=true;if(bad==9)g.words.erase(g.buffer+28);
        CHECK(!bc2::ReadMenuTarget(g.Memory(),g.binding,g.base));}
    CHECK(!bc2::ReadMenuTarget(f.Memory(),f.binding,UINT32_MAX));return 0;
}
int CoordinatesAndDispatch(){
    Fixture f;auto target=bc2::ReadMenuTarget(f.Memory(),f.binding,f.base);CHECK(target);
    const bc2::MenuDispatchGuard guard{2,900,true,true,true,true};
    for(auto kind:{bc2::MenuPointerKind::Move,bc2::MenuPointerKind::PrimaryDown,bc2::MenuPointerKind::PrimaryUp}){
        const auto command=bc2::MakeMenuPointerCommand(f.binding,.5f,.25f,kind,1,2,1000);CHECK(command&&command->x==640&&command->y==180);
        const auto before=f.eventCalls;CHECK(bc2::DispatchMenuPointer(f.Memory(),f.binding,f.base,*target,*command,guard,f.Calls(),f.owned)==bc2::MenuDispatchResult::Invoked);
        if(kind==bc2::MenuPointerKind::Move)CHECK(f.eventCalls==before);
        else CHECK(f.eventCalls==before+1&&f.event[0]==0&&f.event[1]==(kind==bc2::MenuPointerKind::PrimaryDown?0u:1u)&&f.event[2]==1);
    }
    CHECK(bc2::MakeMenuPointerCommand(f.binding,1,1,bc2::MenuPointerKind::Move,1,2,1000)->x==1280);
    for(float invalid:{-.01f,1.01f,std::numeric_limits<float>::quiet_NaN(),std::numeric_limits<float>::infinity()})
        CHECK(!bc2::MakeMenuPointerCommand(f.binding,invalid,0,bc2::MenuPointerKind::Move,1,2,1000));
    for(unsigned bad=0;bad<9;++bad){Fixture g;auto expected=bc2::ReadMenuTarget(g.Memory(),g.binding,g.base);CHECK(expected);
        auto command=*bc2::MakeMenuPointerCommand(g.binding,0,0,bc2::MenuPointerKind::PrimaryDown,1,2,1000);auto denied=guard;
        if(bad==0)denied.nativeUiThread=false;if(bad==1)denied.menuOwned=false;if(bad==2)denied.focused=false;if(bad==3)denied.tracked=false;
        if(bad==4)denied.menuEpoch=3;if(bad==5)denied.nowNs=1000;if(bad==6)command.kind=bc2::MenuPointerKind(255);
        if(bad==7)command.x=1281;if(bad==8)command.generation=0;
        CHECK(bc2::DispatchMenuPointer(g.Memory(),g.binding,g.base,*expected,command,denied,g.Calls(),g.owned)==bc2::MenuDispatchResult::Rejected);
        CHECK(!g.cursorCalls&&!g.eventCalls);
    }
    for(unsigned bad=0;bad<5;++bad){Fixture g;auto expected=bc2::ReadMenuTarget(g.Memory(),g.binding,g.base);CHECK(expected);
        auto command=*bc2::MakeMenuPointerCommand(g.binding,0,0,bc2::MenuPointerKind::PrimaryDown,1,2,1000);
        if(bad==0)g.words[g.queue+0x28]=7;if(bad==1)g.words[g.ui+4]=g.target+4;
        if(bad==2)g.changeOnCursor=true;if(bad==3)g.fillOnCursor=true;if(bad==4)g.failCursor=true;
        const auto result=bc2::DispatchMenuPointer(g.Memory(),g.binding,g.base,*expected,command,guard,g.Calls(),g.owned);
        CHECK(result!=bc2::MenuDispatchResult::Invoked&&!g.eventCalls);if(bad<2)CHECK(!g.cursorCalls);
    }
    return 0;
}
int OwnedRelease(){
    using enum bc2::MenuDispatchResult;
    Fixture f;const auto target=*bc2::ReadMenuTarget(f.Memory(),f.binding,f.base);
    const bc2::MenuDispatchGuard live{2,900,true,true,true,true};
    const auto down=*bc2::MakeMenuPointerCommand(f.binding,.5f,.25f,bc2::MenuPointerKind::PrimaryDown,1,2,1000);
    auto up=down;up.kind=bc2::MenuPointerKind::PrimaryUp;
    auto lost=live;lost.menuEpoch=3;lost.menuOwned=lost.focused=lost.tracked=false;
    CHECK(bc2::DispatchMenuPointer(f.Memory(),f.binding,f.base,target,up,live,f.Calls(),f.owned)==Rejected);
    CHECK(bc2::ReleaseMenuPointer(f.Memory(),f.binding,f.base,target,2,lost,f.Calls(),f.owned)==Rejected);
    CHECK(!f.cursorCalls&&!f.eventCalls);
    f.failEvent=true;
    CHECK(bc2::DispatchMenuPointer(f.Memory(),f.binding,f.base,target,down,live,f.Calls(),f.owned)==CallFailed);
    CHECK(!f.owned.Active());f.failEvent=false;
    CHECK(bc2::DispatchMenuPointer(f.Memory(),f.binding,f.base,target,down,live,f.Calls(),f.owned)==Invoked);
    CHECK(f.owned.Active()&&f.owned.Epoch()==2&&f.owned.Generation()==1);
    const auto cursors=f.cursorCalls,events=f.eventCalls;
    CHECK(bc2::DispatchMenuPointer(f.Memory(),f.binding,f.base,target,down,live,f.Calls(),f.owned)==Rejected);
    CHECK(bc2::DispatchMenuPointer(f.Memory(),f.binding,f.base,target,up,lost,f.Calls(),f.owned)==Rejected);
    for(unsigned bad=0;bad<5;++bad){auto expected=target;auto guard=lost;auto binding=f.binding;auto epoch=2ull;
        if(bad==0)expected.target+=4;if(bad==1)epoch=3;if(bad==2)guard.nativeUiThread=false;
        if(bad==3)binding.sendEvent+=4;if(bad==4)guard.nowNs=0;
        CHECK(bc2::ReleaseMenuPointer(f.Memory(),binding,f.base,expected,epoch,guard,f.Calls(),f.owned)==Rejected);
        CHECK(f.owned.Active()&&f.cursorCalls==cursors&&f.eventCalls==events);
    }
    f.words[f.ui+4]=f.target+4;
    CHECK(bc2::ReleaseMenuPointer(f.Memory(),f.binding,f.base,target,2,lost,f.Calls(),f.owned)==TargetChanged);
    CHECK(f.owned.Active()&&f.eventCalls==events);f.words[f.ui+4]=f.target;
    f.words[f.queue+0x28]=8;
    CHECK(bc2::ReleaseMenuPointer(f.Memory(),f.binding,f.base,target,2,lost,f.Calls(),f.owned)==QueueFull);
    CHECK(f.owned.Active()&&f.eventCalls==events);f.words[f.queue+0x28]=0;
    f.failEvent=true;
    CHECK(bc2::ReleaseMenuPointer(f.Memory(),f.binding,f.base,target,2,lost,f.Calls(),f.owned)==CallFailed);
    CHECK(f.owned.Active());f.failEvent=false;
    auto cleanupCalls=f.Calls();cleanupCalls.cursor=nullptr;
    CHECK(bc2::ReleaseMenuPointer(f.Memory(),f.binding,f.base,target,2,lost,cleanupCalls,f.owned)==Invoked);
    CHECK(!f.owned.Active()&&f.cursorCalls==cursors&&(f.event==std::array<std::uint32_t,3>{0,1,1}));
    const auto releasedEvents=f.eventCalls;
    CHECK(bc2::ReleaseMenuPointer(f.Memory(),f.binding,f.base,target,2,lost,cleanupCalls,f.owned)==Rejected);
    CHECK(f.eventCalls==releasedEvents);
    return 0;
}
int TrackedReleaseAfterDesktopMove(){
    using enum bc2::MenuDispatchResult;
    Fixture f;const auto target=*bc2::ReadMenuTarget(f.Memory(),f.binding,f.base);
    const bc2::MenuDispatchGuard live{2,900,true,true,true,true};
    const auto down=*bc2::MakeMenuPointerCommand(f.binding,.6f,.2f,bc2::MenuPointerKind::PrimaryDown,1,2,1000);
    CHECK(bc2::DispatchMenuPointer(f.Memory(),f.binding,f.base,target,down,live,f.Calls(),f.owned)==Invoked);
    // An ordinary desktop mouse pump occurs between tracked down and up.
    f.x=1200;f.y=650;
    const auto up=*bc2::MakeMenuPointerCommand(f.binding,.55f,.22f,bc2::MenuPointerKind::PrimaryUp,2,2,1000);
    CHECK(bc2::DispatchMenuPointer(f.Memory(),f.binding,f.base,target,up,live,f.Calls(),f.owned)==Invoked);
    CHECK(!f.owned.Active()&&f.event[1]==1&&f.eventX==up.x&&f.eventY==up.y);
    CHECK(f.cursorCalls==2&&f.eventCalls==2);
    // Expired input cannot relocate the pointer or produce a click. Cleanup
    // can still balance an already-owned down, without stale cursor authority.
    CHECK(bc2::DispatchMenuPointer(f.Memory(),f.binding,f.base,target,down,live,f.Calls(),f.owned)==Invoked);
    f.x=100;f.y=100;auto expired=live;expired.nowNs=1000;
    const auto cursors=f.cursorCalls,events=f.eventCalls;
    CHECK(bc2::DispatchMenuPointer(f.Memory(),f.binding,f.base,target,up,expired,f.Calls(),f.owned)==Rejected);
    CHECK(f.owned.Active()&&f.cursorCalls==cursors&&f.eventCalls==events);
    CHECK(bc2::ReleaseMenuPointer(f.Memory(),f.binding,f.base,target,2,expired,f.Calls(),f.owned)==Invoked);
    CHECK(!f.owned.Active()&&f.cursorCalls==cursors&&f.eventX==100&&f.eventY==100);
    return 0;
}
int LeaseLifetime(){
    using enum bc2::MenuDispatchResult;
    for(unsigned scenario=0;scenario<9;++scenario){
        Fixture f;const auto original=*bc2::ReadMenuTarget(f.Memory(),f.binding,f.base);
        const bc2::MenuDispatchGuard guard{2,900,true,true,true,true};
        const auto down=*bc2::MakeMenuPointerCommand(f.binding,.5f,.25f,bc2::MenuPointerKind::PrimaryDown,1,2,1000);
        CHECK(bc2::DispatchMenuPointer(f.Memory(),f.binding,f.base,original,down,guard,f.Calls(),f.owned)==Invoked);
        if(scenario==0){ // Native queue growth does not create a new APT owner.
            f.words[f.queue+8]=16;f.words[f.queue+0x2c]=0x40000;
            f.words[0x40000]=0;f.words[0x40000+60]=0;
        }
        if(scenario==1){
            f.words[f.target+0x18]=0x41000;
            f.words[0x41008]=8;f.words[0x41028]=0;f.words[0x4102c]=f.buffer;
        }
        if(scenario>=2){f.words[f.ui+4]=0x42000;f.words[0x42018]=f.queue;}
        if(scenario==3)f.words[f.base+f.binding.inputBlocked]=1;
        if(scenario==4)f.unstable=true;
        if(scenario==5)f.words.erase(f.buffer+28);
        if(scenario==6)f.words[f.base+f.binding.inputEnabled]=0;
        if(scenario==7){f.words[f.manager+0x14]=0x43000;f.words[0x43004]=f.target;}
        if(scenario==8){f.words[f.base+f.binding.managerGlobal]=0x44000;f.words[0x44014]=f.ui;}
        const auto before=f.words;const auto events=f.eventCalls,cursors=f.cursorCalls;
        auto rejected=guard;rejected.nativeUiThread=false;
        CHECK(bc2::ReleaseMenuPointer(f.Memory(),f.binding,f.base,original,2,rejected,f.Calls(),f.owned)==Rejected);
        CHECK(f.owned.Active()&&f.eventCalls==events);
        const auto result=bc2::ReleaseMenuPointer(f.Memory(),f.binding,f.base,original,2,guard,f.Calls(),f.owned);
        const bool replaced=scenario==2||scenario==7||scenario==8;
        CHECK(result==(scenario<2?Invoked:replaced?Retired:TargetChanged));
        CHECK(f.owned.Active()==(scenario>=3&&scenario<=6));
        CHECK(f.eventCalls==events+(scenario<2?1:0)&&f.cursorCalls==cursors&&f.words==before);
        if(replaced){ // Even address reuse cannot resurrect the consumed lease.
            Fixture restored;f.words=restored.words;
            CHECK(bc2::ReleaseMenuPointer(f.Memory(),f.binding,f.base,original,2,guard,f.Calls(),f.owned)==Rejected);
            CHECK(f.eventCalls==events);
        }
    }
    // A tracked controller up must also survive queue growth, and still place
    // the cursor at the current VR position before invoking that up.
    Fixture f;const auto old=*bc2::ReadMenuTarget(f.Memory(),f.binding,f.base);
    const bc2::MenuDispatchGuard guard{2,900,true,true,true,true};
    auto command=*bc2::MakeMenuPointerCommand(f.binding,.4f,.6f,bc2::MenuPointerKind::PrimaryDown,1,2,1000);
    CHECK(bc2::DispatchMenuPointer(f.Memory(),f.binding,f.base,old,command,guard,f.Calls(),f.owned)==Invoked);
    f.words[f.queue+8]=16;f.words[f.buffer+60]=0;
    const auto current=*bc2::ReadMenuTarget(f.Memory(),f.binding,f.base);
    command.kind=bc2::MenuPointerKind::PrimaryUp;command.x=701;
    CHECK(bc2::DispatchMenuPointer(f.Memory(),f.binding,f.base,current,command,guard,f.Calls(),f.owned)==Invoked);
    CHECK(!f.owned.Active()&&f.eventX==701&&f.eventCalls==2);
    return 0;
}
int ExactPumpBoundary(){
    Fixture f;f.binding.mousePumpCaller=0x1000;f.binding.mousePumpReturn=0x11ab;f.binding.inputNodeGlobal=0x110;
    constexpr std::uint32_t controller=0x60000,input=0x70000;
    f.words[f.base+0x110]=input;
    const auto test=[&](std::uint64_t ret,std::uint64_t self,std::uint64_t node,std::uint64_t expected){
        return bc2::IsMenuPumpBoundary(f.Memory(),f.binding,f.base,ret,self,node,expected);};
    CHECK(test(f.base+0x11ab,controller,input,controller));
    CHECK(!test(f.base+0x11aa,controller,input,controller));
    CHECK(!test(f.base+0x11ab,controller+4,input,controller));
    CHECK(!test(f.base+0x11ab,controller,input+4,controller));
    CHECK(!test(f.base+0x11ab,controller,0,controller));
    CHECK(!test(f.base+0x11ab,controller,input,0));
    CHECK(!test(f.base+0x11ab,std::uint64_t(UINT32_MAX)+1,input,std::uint64_t(UINT32_MAX)+1));
    f.words[f.base+0x110]=input+4;CHECK(!test(f.base+0x11ab,controller,input,controller));
    f.words[f.base+0x110]=input;
    // Subsequent exact invocations remain valid: worker thread identity is not
    // captured by this object. Adapter serialization is independently required.
    CHECK(test(f.base+0x11ab,controller,input,controller));
    ++f.binding.mousePumpReturn;CHECK(!test(f.base+0x11ac,controller,input,controller));
    return 0;
}
int StaticEvidence(const char* path){
    std::ifstream in(path,std::ios::binary);CHECK(in);const std::vector<char> raw((std::istreambuf_iterator<char>(in)),{});
    std::vector<std::byte> bytes(raw.size());std::memcpy(bytes.data(),raw.data(),raw.size());const auto pe=engine::InspectPe(bytes);CHECK(pe.valid);
    const auto binding=bc2::DiscoverMenuBinding(bytes,pe.image);CHECK(binding);
    CHECK(binding->mousePump==0x509290&&binding->setCursor==0xb82050&&binding->sendEvent==0xb82020&&binding->managerGlobal==0x15a4ca8);
    CHECK(binding->inputEnabled==0x17ea910&&binding->inputBlocked==0x17ea704&&binding->targetGlobal==0x17e9c5c);
    CHECK(binding->logicalWidth==1280&&binding->logicalHeight==720);
    CHECK(binding->mousePumpCaller==0x541c90&&binding->mousePumpReturn==0x541e3b&&binding->inputNodeGlobal==0x1170e90);
    CHECK(binding->coordinateMapper==0x509060&&binding->displayGlobal==0x1172a60);
    const auto spans=bc2::MenuBindingCodeSpans(bytes,pe.image,*binding);CHECK(spans&&spans->size()==33);
    CHECK((*spans)[0].rva==binding->mousePump&&(*spans)[0].size==0x25c);
    auto wrong=*binding;++wrong.sendEvent;CHECK(!bc2::MenuBindingCodeSpans(bytes,pe.image,wrong));
    // Every native function relied on for cursor/button dispatch has a verified
    // entry. A changed entry must reject both discovery and the live-span plan.
    for(const auto& span:*spans){std::optional<std::size_t> offset;
        for(const auto& section:pe.image.sections)if(span.rva>=section.rva&&span.rva-section.rva<section.rawSize){
            const auto delta=span.rva-section.rva;CHECK(span.size<=section.rawSize-delta);offset=std::size_t(section.rawOffset)+delta;break;}
        CHECK(offset&&span.size&&*offset+span.size<=bytes.size());auto mutated=bytes;mutated[*offset]^=std::byte{0xff};
        CHECK(!bc2::DiscoverMenuBinding(mutated,pe.image));CHECK(!bc2::MenuBindingCodeSpans(mutated,pe.image,*binding));
        if(span.rva==binding->coordinateMapper){mutated=bytes;mutated[*offset+0x188]^=std::byte{1};
            CHECK(!bc2::DiscoverMenuBinding(mutated,pe.image));}
    }

    for(const auto relative:{0x3du,0x1a3u,0x1a5u,0x1a7u}){
        const auto rva=binding->mousePumpCaller+relative;bool found=false;
        for(const auto& section:pe.image.sections)if(rva>=section.rva&&rva-section.rva<section.rawSize){
            const auto at=section.rawOffset+rva-section.rva;const auto old=bytes[at];bytes[at]^=std::byte{1};
            CHECK(!bc2::DiscoverMenuBinding(bytes,pe.image));bytes[at]=old;found=true;break;}CHECK(found);
    }
    for(const auto& section:pe.image.sections)if(binding->setCursor>=section.rva&&binding->setCursor-section.rva<section.rawSize){
        const auto at=section.rawOffset+binding->setCursor-section.rva;bytes[at]=std::byte{0x90};break;}
    CHECK(!bc2::DiscoverMenuBinding(bytes,pe.image));return 0;
}
}
int main(int argc,char** argv){
    CHECK(RasterMapping()==0);CHECK(TargetAndBounds()==0);CHECK(CoordinatesAndDispatch()==0);CHECK(OwnedRelease()==0);CHECK(TrackedReleaseAfterDesktopMove()==0);CHECK(LeaseLifetime()==0);CHECK(ExactPumpBoundary()==0);
    if(argc>1)CHECK(StaticEvidence(argv[1])==0);
    return 0;
}
