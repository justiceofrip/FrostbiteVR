#include "Test.h"
#include "fvr/graphics/FrameBridgePresentService.h"

using namespace fvr::graphics;
FrameBridgePresentBoundary Boundary(const FrameBridgePresentIdentity& owner){
    FrameBridgePresentBoundary b;b.current=owner;b.presentedSwapchain=owner.swapchain;
    b.enabled=b.lifetimeHeld=b.bridgeOwnerCurrent=b.connected=b.nativeStateRestored=true;return b;
}
int FullGraphicsOwnership(){
    FrameBridgePresentService service;unsigned graphics=0,original=0;
    const auto present=[&](std::uint32_t actual,std::uint32_t expected,std::uintptr_t chain){
        FrameBridgePresentService::Scope scope(service,actual,expected,chain,50);
        if(!scope.Owned()){++original;return;}
        // Represents menu/diagnostic work before the optional bridge service.
        ++graphics;++original;
    };
    CHECK(!service.Omissions().firstAvailable);
    present(7,6,51);CHECK(graphics==0&&original==1);
    auto omitted=service.Omissions();CHECK(omitted.foreignThread==1&&omitted.reentrant==0);
    CHECK(omitted.firstAvailable&&omitted.firstForeignThread&&omitted.actualThread==7&&omitted.expectedThread==6);
    CHECK(omitted.presentedSwapchain==51&&omitted.expectedSwapchain==50);
    {
        FrameBridgePresentService::Scope outer(service,6,6,50,50);CHECK(outer.Owned());
        present(6,6,50);CHECK(graphics==0&&original==2);
        present(8,6,52);CHECK(graphics==0&&original==3);
        // Rejected callbacks must not release the outer owner's gate.
        FrameBridgePresentService::Scope stillNested(service,6,6,50,50);CHECK(!stillNested.Owned());
    }
    present(6,6,50);CHECK(graphics==1&&original==4);
    omitted=service.Omissions();CHECK(omitted.foreignThread==2&&omitted.reentrant==2);
    CHECK(omitted.firstForeignThread&&omitted.actualThread==7&&omitted.presentedSwapchain==51);
    FrameBridgePresentService nestedFirst;
    {
        FrameBridgePresentService::Scope outer(nestedFirst,6,6,50,50);CHECK(outer.Owned());
        FrameBridgePresentService::Scope nested(nestedFirst,6,6,50,50);CHECK(!nested.Owned());
    }
    const auto firstNested=nestedFirst.Omissions();CHECK(firstNested.firstAvailable&&!firstNested.firstForeignThread);
    CHECK(firstNested.foreignThread==0&&firstNested.reentrant==1&&firstNested.actualThread==6&&firstNested.expectedThread==6);
    // Absent owner identity cannot grant graphics access.
    FrameBridgePresentService absent;
    FrameBridgePresentService::Scope unknown(absent,0,0,50,50);CHECK(!unknown.Owned());
    CHECK(absent.Omissions().foreignThread==1);
    return 0;
}
int main(){
    CHECK(FullGraphicsOwnership()==0);
    const FrameBridgePresentIdentity owner{1,2,3,4,5,6};const auto valid=Boundary(owner);
    FrameBridgePresentService service;unsigned calls=0;auto pump=[&]()noexcept {++calls;};
    // Every identity component must remain exact, including the chain actually
    // being presented, rather than merely the renderer's retained chain field.
    for(unsigned field=0;field<7;++field){
        auto bad=valid;
        switch(field){case 0:++bad.current.renderer;break;case 1:++bad.current.rendererVtable;break;
        case 2:++bad.current.device;break;case 3:++bad.current.context;break;case 4:++bad.current.swapchain;break;
        case 5:++bad.current.thread;break;case 6:++bad.presentedSwapchain;break;}
        FrameBridgePresentService::Scope scope(service,true);CHECK(!scope.Run(owner,bad,pump));CHECK(!scope.RunAfterPresent(0,owner,bad,pump));CHECK(calls==0);
    }
    for(unsigned field=0;field<9;++field){
        auto bad=valid;
        switch(field){case 0:bad.enabled=false;break;case 1:bad.lifetimeHeld=false;break;
        case 2:bad.bridgeOwnerCurrent=false;break;case 3:bad.connected=false;break;case 4:bad.fatal=true;break;
        case 5:bad.test=true;break;case 6:bad.drawingNativeView=true;break;case 7:bad.nativeStateRestored=false;break;
        case 8:bad.current={};break;}
        FrameBridgePresentService::Scope scope(service,true);CHECK(!scope.Run(owner,bad,pump));CHECK(!scope.RunAfterPresent(0,owner,bad,pump));CHECK(calls==0);
    }
    {FrameBridgePresentService::Scope wrongThread(service,false);CHECK(!wrongThread.Owned());CHECK(!wrongThread.Run(owner,valid,pump));}
    {FrameBridgePresentService::Scope scope(service,true);const FrameBridgePresentIdentity absent{};
        CHECK(!scope.Run(absent,Boundary(absent),pump));CHECK(calls==0);}
    {
        FrameBridgePresentService::Scope outer(service,true);CHECK(outer.Owned());
        // A native callback may reenter Present before the outer service point.
        {FrameBridgePresentService::Scope nested(service,true);CHECK(!nested.Owned());CHECK(!nested.Run(owner,valid,pump));}
        bool nestedRan=false;
        CHECK(outer.Run(owner,valid,[&]()noexcept {
            ++calls;FrameBridgePresentService::Scope nested(service,true);nestedRan=nested.Run(owner,valid,pump);
        }));
        CHECK(!nestedRan&&calls==1);CHECK(!outer.Run(owner,valid,pump));CHECK(calls==1);
    }
    // A later original Present gets one fresh opportunity, without a retry loop.
    {FrameBridgePresentService::Scope next(service,true);CHECK(next.Run(owner,valid,pump));CHECK(calls==2);}
    // Original Present may finish work after the before phase found nothing.
    // One outer scope still blocks nested calls throughout the original call.
    {
        FrameBridgePresentService::Scope outer(service,true);
        bool ready=false,reaped=false;unsigned phaseCalls=0;
        const auto poll=[&]()noexcept {++phaseCalls;if(ready)reaped=true;};
        CHECK(outer.Run(owner,valid,poll));CHECK(phaseCalls==1&&!reaped);
        {FrameBridgePresentService::Scope nested(service,true);
            CHECK(!nested.Run(owner,valid,poll));CHECK(!nested.RunAfterPresent(0,owner,valid,poll));}
        ready=true; // Only the original call made the pending completion visible.
        CHECK(outer.RunAfterPresent(0,owner,valid,poll));CHECK(phaseCalls==2&&reaped);
        CHECK(!outer.RunAfterPresent(0,owner,valid,poll));CHECK(!outer.Run(owner,valid,poll));CHECK(phaseCalls==2);
    }
    const std::int32_t unsuccessful[]={1,0x087a0001,-2005270523,-2005270518};
    for(const auto result:unsuccessful){
        FrameBridgePresentService::Scope scope(service,true);unsigned phaseCalls=0;
        auto poll=[&]()noexcept {++phaseCalls;};CHECK(scope.Run(owner,valid,poll));
        CHECK(!scope.RunAfterPresent(result,owner,valid,poll));CHECK(phaseCalls==1);
        CHECK(!scope.RunAfterPresent(0,owner,valid,poll));CHECK(!scope.Run(owner,valid,poll));
    }
    {
        FrameBridgePresentService::Scope scope(service,true);unsigned phaseCalls=0;
        auto poll=[&]()noexcept {++phaseCalls;};CHECK(scope.Run(owner,valid,poll));
        auto changed=valid;++changed.current.context;
        CHECK(!scope.RunAfterPresent(0,owner,changed,poll));CHECK(phaseCalls==1);
    }
    {
        FrameBridgePresentService::Scope scope(service,true);unsigned phaseCalls=0;
        auto poll=[&]()noexcept {++phaseCalls;};auto unrestored=valid;unrestored.nativeStateRestored=false;
        CHECK(!scope.Run(owner,unrestored,poll));
        CHECK(scope.RunAfterPresent(0,owner,valid,poll));CHECK(phaseCalls==1);
    }
    return 0;
}
