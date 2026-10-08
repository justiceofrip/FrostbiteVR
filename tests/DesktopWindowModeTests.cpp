#include "Test.h"
#include "fvr/graphics/DesktopWindowMode.h"
using namespace fvr::graphics;
namespace {
struct Fixture {
    DesktopWindowIdentity expected{0x100,0x200,0x300,42,71};
    DesktopWindowObservation observed{expected,71,0,1920,1080,0x999,0,0,true,true,false,true,true,false};
    DesktopWindowModeGuard guard{1'000'000'000,true,true,true,false,true,true,true};
    DesktopWindowMode mode;
    unsigned reads=0,sets=0;
    std::int32_t result=0;
    bool finishImmediately=true,readFail=false,changeOwner=false,stealFocus=false,reenter=false;
    DesktopWindowModeCalls Calls(){return {this,[](void* ctx,DesktopWindowObservation& out){
        auto& f=*static_cast<Fixture*>(ctx);++f.reads;out=f.observed;return !f.readFail;},
        [](void* ctx){auto& f=*static_cast<Fixture*>(ctx);++f.sets;
            if(f.reenter)f.mode.Step(f.expected,f.guard,f.Calls());
            if(f.finishImmediately){f.observed.windowed=true;f.observed.fullscreen=false;}
            if(f.changeOwner)++f.observed.identity.chain;if(f.stealFocus)f.observed.foregroundWindow=f.expected.window;
            return f.result;}};}
    const DesktopWindowModeReport& Step(){return mode.Step(expected,guard,Calls());}
};
int TransitionAndNoRecapture(){
    Fixture f;const auto report=f.Step();
    CHECK(report.phase==DesktopWindowModePhase::Ready&&report.confirmed&&report.requested);
    CHECK(report.attempts==1&&f.sets==1&&f.reads==2&&report.afterObserved);
    CHECK(!report.before.windowed&&report.before.fullscreen&&report.after.windowed&&!report.after.fullscreen);
    CHECK(!report.foregroundChanged&&!report.foregroundBecameGame&&report.before.foregroundWindow==0x999);
    // Success is terminal. A later application/user fullscreen request does not
    // cause a per-frame mode fight; native config governs subsequent sessions.
    f.observed.windowed=false;f.observed.fullscreen=true;f.guard.nowNs+=100;
    CHECK(f.Step().phase==DesktopWindowModePhase::Ready&&f.sets==1&&f.reads==2);
    Fixture g;g.observed.windowed=true;g.observed.fullscreen=false;
    CHECK(g.Step().initiallyWindowed&&g.mode.Report().confirmed&&!g.sets);
    Fixture h;h.stealFocus=true;
    CHECK(h.Step().foregroundBecameGame&&h.mode.Report().foregroundChanged);
    CHECK(h.sets==1); // Report the DXGI/OS outcome; no focus-restoration callback exists.
    return 0;
}
int ConfirmationAndRetries(){
    Fixture f;f.finishImmediately=false;
    CHECK(f.Step().phase==DesktopWindowModePhase::AwaitingConfirmation&&f.sets==1);
    f.guard.nowNs+=200'000'000;CHECK(!f.Step().confirmed&&f.sets==1);
    f.observed.windowed=true;f.observed.fullscreen=false;f.guard.nowNs+=10;
    CHECK(f.Step().confirmed&&f.sets==1);
    Fixture slow;slow.finishImmediately=false;slow.Step();slow.guard.nowNs+=DesktopWindowMode::TimeoutNs;
    CHECK(slow.Step().reason==DesktopWindowModeReason::TimedOut&&slow.sets==1);
    Fixture busy;busy.finishImmediately=false;busy.result=DesktopWindowMode::ModeChangeInProgress;
    CHECK(busy.Step().phase==DesktopWindowModePhase::AwaitingRetry&&busy.sets==1);
    busy.guard.nowNs+=DesktopWindowMode::RetryNs-1;CHECK(busy.Step().attempts==1);
    ++busy.guard.nowNs;CHECK(busy.Step().attempts==2);
    busy.guard.nowNs+=DesktopWindowMode::RetryNs;busy.result=0;busy.finishImmediately=true;
    CHECK(busy.Step().confirmed&&busy.sets==3);
    Fixture capped;capped.finishImmediately=false;capped.result=DesktopWindowMode::ModeChangeInProgress;
    for(unsigned i=0;i<3;++i){capped.Step();capped.guard.nowNs+=DesktopWindowMode::RetryNs;}
    CHECK(capped.Step().reason==DesktopWindowModeReason::RetryLimit&&capped.sets==3);
    // A native transition initially in progress can finish windowed itself, or
    // settle exclusive before our first request. Neither path invents success.
    Fixture native;native.observed.windowed=true;native.finishImmediately=false;
    CHECK(native.Step().phase==DesktopWindowModePhase::AwaitingConfirmation&&!native.sets);
    native.observed.windowed=false;native.guard.nowNs+=10;
    CHECK(native.Step().attempts==1);
    return 0;
}
int BoundariesAndFailures(){
    for(unsigned bad=0;bad<8;++bad){Fixture f;
        if(bad==0)f.guard.enabled=false;if(bad==1)f.guard.testPresent=true;if(bad==2)f.guard.nativeOwnerVerified=false;
        if(bad==3)f.guard.presentBoundary=false;if(bad==4)f.guard.nativeStateRestored=false;
        if(bad==5)f.guard.noModLocks=false;if(bad==6)f.guard.noModBackbufferReferences=false;if(bad==7)f.guard.nowNs=0;
        f.Step();CHECK(!f.sets&&!f.reads);
    }
    for(unsigned bad=0;bad<12;++bad){Fixture f;
        if(bad==0)f.observed.identity.device++;if(bad==1)f.observed.identity.window++;if(bad==2)f.observed.identity.process++;
        if(bad==3)f.observed.identity.presentThread++;if(bad==4)f.observed.windowThread++;
        if(bad==5)f.observed.swapEffect=4;if(bad==6)f.observed.minimized=true;
        if(bad==7)f.observed.windowClassVerified=false;if(bad==8)f.observed.windowValid=false;
        if(bad==9)f.readFail=true;if(bad==10)f.observed.fullscreenResult=-1;if(bad==11)f.observed.descriptionResult=-1;
        CHECK(f.Step().phase==DesktopWindowModePhase::Failed&&!f.sets);
    }
    Fixture changed;changed.changeOwner=true;
    CHECK(changed.Step().reason==DesktopWindowModeReason::OwnerChanged&&changed.sets==1);
    Fixture failure;failure.result=-1;failure.finishImmediately=false;
    CHECK(failure.Step().reason==DesktopWindowModeReason::TransitionFailed&&failure.mode.Report().afterObserved);
    failure.guard.nowNs+=100;CHECK(failure.Step().attempts==1);
    Fixture unexpected;unexpected.result=1;unexpected.finishImmediately=false;
    CHECK(unexpected.Step().reason==DesktopWindowModeReason::UnexpectedStatus);
    Fixture identity;identity.finishImmediately=false;identity.Step();identity.expected.chain++;
    CHECK(identity.Step().reason==DesktopWindowModeReason::OwnerChanged&&identity.sets==1);
    Fixture clock;clock.finishImmediately=false;clock.Step();clock.guard.nowNs--;
    CHECK(clock.Step().reason==DesktopWindowModeReason::InvalidClock&&clock.sets==1);
    Fixture reentrant;reentrant.reenter=true;
    CHECK(reentrant.Step().confirmed&&reentrant.sets==1&&reentrant.mode.Report().reentrantCalls==1);
    return 0;
}
}
int main(){CHECK(TransitionAndNoRecapture()==0);CHECK(ConfirmationAndRetries()==0);CHECK(BoundariesAndFailures()==0);return 0;}
