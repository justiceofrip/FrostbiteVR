#include "Test.h"
#include "fvr/math/ProjectionOverride.h"
#include "fvr/runtime/ExactWriteBatch.h"
#include "fvr/runtime/FrameObservationFailures.h"
#include "fvr/graphics/DiagnosticImageLifetime.h"
#include "../src/games/bc2/Bc2WeatherObservation.h"
#include "../src/games/bc2/Bc2WeatherTranslation.h"
#include <limits>
#if defined(_WIN32)&&defined(_MSC_VER)
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <Windows.h>
namespace {
constexpr DWORD WeatherTestException=0xe0425001;
struct NativeFinallyTest {
    NativeFinallyTest* child=nullptr;NativeFinallyTest* prior=nullptr;
    std::optional<fvr::graphics::DiagnosticImageLifetime::Scope>* lifetime=nullptr;
    unsigned calls=0,cleanups=0;bool fault=false,returned=false,tlsMatched=false;
    std::uintptr_t result=0;void* childResult=nullptr;
};
thread_local NativeFinallyTest* nativeFinallyTls=nullptr;
void CleanupNativeFinallyTest(void* raw,bool returned)noexcept {
    auto& t=*static_cast<NativeFinallyTest*>(raw);++t.cleanups;t.returned=returned;nativeFinallyTls=t.prior;
    if(!returned&&t.lifetime)t.lifetime->reset();
}
void* NativeFinallyTestCall(void* raw){
    auto& t=*static_cast<NativeFinallyTest*>(raw);++t.calls;t.tlsMatched=nativeFinallyTls==&t;
    if(t.child){t.child->prior=&t;nativeFinallyTls=t.child;
        t.childResult=fvr::bc2::weatherTranslation::InvokeWithFinally(NativeFinallyTestCall,t.child,CleanupNativeFinallyTest);}
    if(t.fault)RaiseException(WeatherTestException,0,0,nullptr);
    return reinterpret_cast<void*>(t.result);
}
// A real Windows handler outside the production leaf proves propagation. Keep
// this leaf free of C++ destructors; main's RAII state lives outside the SEH frame.
DWORD CatchNativeFinallyTest(NativeFinallyTest* state,void** result){
    __try {*result=fvr::bc2::weatherTranslation::InvokeWithFinally(NativeFinallyTestCall,state,CleanupNativeFinallyTest);}
    __except(GetExceptionCode()==WeatherTestException?EXCEPTION_EXECUTE_HANDLER:EXCEPTION_CONTINUE_SEARCH){return GetExceptionCode();}
    return 0;
}
}
#endif
using namespace fvr::math;
int main(){
#if defined(_WIN32)&&defined(_MSC_VER)
    // Exercise the actual production SEH boundary, including nested native
    // callbacks. Successful calls retain observer lifetime for postprocessing;
    // propagated exceptions drain both admitted callbacks and restore TLS.
    NativeFinallyTest sentinel{};
    fvr::graphics::DiagnosticImageLifetime outerLifetime,innerLifetime;
    std::optional<fvr::graphics::DiagnosticImageLifetime::Scope> outerScope,innerScope;
    outerScope.emplace(outerLifetime);innerScope.emplace(innerLifetime);
    NativeFinallyTest inner{},outer{};inner.lifetime=&innerScope;inner.result=0x13579;
    outer.child=&inner;outer.prior=&sentinel;outer.lifetime=&outerScope;outer.result=0x24680;
    nativeFinallyTls=&outer;void* nativeResult=nullptr;
    CHECK(CatchNativeFinallyTest(&outer,&nativeResult)==0);
    CHECK(nativeResult==reinterpret_cast<void*>(outer.result)&&outer.childResult==reinterpret_cast<void*>(inner.result));
    CHECK(outer.calls==1&&inner.calls==1&&outer.cleanups==1&&inner.cleanups==1&&outer.returned&&inner.returned);
    CHECK(outer.tlsMatched&&inner.tlsMatched&&nativeFinallyTls==&sentinel);
    CHECK(outerScope&&innerScope&&!outerLifetime.TryQuiesce(true)&&!innerLifetime.TryQuiesce(true));
    outerScope.reset();innerScope.reset();CHECK(outerLifetime.TryQuiesce(true)&&innerLifetime.TryQuiesce(true));
    fvr::graphics::DiagnosticImageLifetime faultOuterLifetime,faultInnerLifetime;
    std::optional<fvr::graphics::DiagnosticImageLifetime::Scope> faultOuterScope,faultInnerScope;
    faultOuterScope.emplace(faultOuterLifetime);faultInnerScope.emplace(faultInnerLifetime);
    NativeFinallyTest faultInner{},faultOuter{};faultInner.fault=true;faultInner.lifetime=&faultInnerScope;
    faultOuter.child=&faultInner;faultOuter.prior=&sentinel;faultOuter.lifetime=&faultOuterScope;
    nativeFinallyTls=&faultOuter;nativeResult=reinterpret_cast<void*>(0xabcdef);
    CHECK(CatchNativeFinallyTest(&faultOuter,&nativeResult)==WeatherTestException);
    CHECK(nativeResult==reinterpret_cast<void*>(0xabcdef)); // An exceptional call never published a return value.
    CHECK(faultOuter.calls==1&&faultInner.calls==1&&faultOuter.cleanups==1&&faultInner.cleanups==1);
    CHECK(!faultOuter.returned&&!faultInner.returned&&!faultOuterScope&&!faultInnerScope&&nativeFinallyTls==&sentinel);
    CHECK(faultOuterLifetime.TryQuiesce(true)&&faultInnerLifetime.TryQuiesce(true));
    nativeFinallyTls=nullptr;
#endif
    std::array<float,16> right{1,0,0,0,0,1.111111f,0,0,.2f,0,-1.00005f,-1,0,0,-.1f,0};
    auto left=right;left[0]=.9f;left[5]=1.2f;left[8]=-.24f;left[9]=.17f;
    auto biased=right;biased[10]=-1.53968f;biased[14]=-11.562f;
    auto fixed=RetargetDepthProjection(biased,right,left);CHECK(fixed);
    // Verify clip-space X/Y/W at several depths, keeping the native biased Z.
    for(float z:{-8.f,-30.f,-400.f}){std::array<float,4> point{2.f,-1.f,z,1.f};
        for(unsigned col=0;col<4;++col){float actual=0,expected=0;
            for(unsigned row=0;row<4;++row){actual+=point[row]*(*fixed)[row*4+col];expected+=point[row]*(col==2?biased:left)[row*4+col];}
            CHECK(Near(actual,expected));}}
    CHECK((*fixed)[10]==biased[10]&&(*fixed)[14]==biased[14]);
    CHECK(RetargetDepthProjection(biased,right,right)==biased);
    auto unrelated=biased;unrelated[0]*=2;CHECK(!RetargetDepthProjection(unrelated,right,left));
    unrelated=biased;unrelated[11]=1;CHECK(!RetargetDepthProjection(unrelated,right,left));
    unrelated=biased;unrelated[14]=std::numeric_limits<float>::quiet_NaN();CHECK(!RetargetDepthProjection(unrelated,right,left));
    auto invalid=left;invalid[9]=std::numeric_limits<float>::infinity();CHECK(!RetargetDepthProjection(biased,right,invalid));
    auto weapon=right;weapon[0]*=3.3f;weapon[5]*=3.3f;weapon[10]=-1.5f;weapon[14]=-.2f;
    auto firstPerson=RetargetFirstPersonProjection(weapon,left);CHECK(firstPerson);
    CHECK((*firstPerson)[0]==left[0]&&(*firstPerson)[5]==left[5]&&(*firstPerson)[8]==left[8]&&(*firstPerson)[9]==left[9]);
    CHECK((*firstPerson)[10]==weapon[10]&&(*firstPerson)[14]==weapon[14]);
    auto bad=weapon;bad[15]=1;CHECK(!RetargetFirstPersonProjection(bad,left));
    bad=weapon;bad[11]=1;CHECK(!RetargetFirstPersonProjection(bad,left));
    bad=weapon;bad[3]=.1f;CHECK(!RetargetFirstPersonProjection(bad,left));
    bad=weapon;bad[0]=0;CHECK(!RetargetFirstPersonProjection(bad,left));
    bad=weapon;bad[10]=std::numeric_limits<float>::quiet_NaN();CHECK(!RetargetFirstPersonProjection(bad,left));
    fvr::runtime::FrameObservationFailures observationFailures;
    bool restorationFailed=false;
    CHECK(!observationFailures.CanApply(restorationFailed));
    observationFailures.BeginFrame();
    const auto rejectedFrame=observationFailures.Capture();
    auto unavailable=weapon;unavailable[10]=std::numeric_limits<float>::quiet_NaN();
    if(!RetargetFirstPersonProjection(unavailable,left))observationFailures.Reject(rejectedFrame);
    CHECK(observationFailures.Total()==1&&!observationFailures.CanApply(restorationFailed));
    // A valid observation later in the SAME frame cannot erase the failed one.
    CHECK(RetargetFirstPersonProjection(weapon,left));
    CHECK(!observationFailures.CanApply(restorationFailed));
    observationFailures.BeginFrame();
    CHECK(observationFailures.Total()==1&&observationFailures.CanApply(restorationFailed));
    const auto validFrame=observationFailures.Capture();
    CHECK(validFrame!=rejectedFrame&&!observationFailures.Current(rejectedFrame));
    observationFailures.Reject(rejectedFrame); // Delayed completion belongs to the retired frame.
    CHECK(observationFailures.Total()==2&&observationFailures.CanApply(restorationFailed));
    observationFailures.Reject(validFrame);
    CHECK(!observationFailures.CanApply(restorationFailed));
    observationFailures.Reject(rejectedFrame); // Cannot erase the current frame's rejection.
    CHECK(observationFailures.Total()==4&&!observationFailures.CanApply(restorationFailed));
    observationFailures.BeginFrame();
    CHECK(observationFailures.CanApply(restorationFailed));
    // The next valid frame can perform a complete left-context correction.
    auto native=weapon;const auto corrected=RetargetFirstPersonProjection(native,left);CHECK(corrected);
    {
        fvr::runtime::ExactWriteBatch<1> writes;
        CHECK(writes.Stage(std::as_writable_bytes(std::span(native)),
            std::as_bytes(std::span(weapon)),std::as_bytes(std::span(*corrected))));
        CHECK(observationFailures.CanApply(restorationFailed)&&writes.Apply());
        CHECK(native==*corrected);
        CHECK(writes.Restore());CHECK(native==weapon);
    }
    // A real restore conflict is different: preserve native bytes and remain
    // blocked across later observation frames rather than forgetting the write.
    {
        fvr::runtime::ExactWriteBatch<1> writes;
        CHECK(writes.Stage(std::as_writable_bytes(std::span(native)),
            std::as_bytes(std::span(weapon)),std::as_bytes(std::span(*corrected))));
        CHECK(writes.Apply());native[0]+=1.f;
        restorationFailed=!writes.Restore();CHECK(restorationFailed);
    }
    const auto newerNative=native;
    observationFailures.BeginFrame();
    CHECK(observationFailures.Total()==4&&!observationFailures.CanApply(restorationFailed));
    CHECK(native==newerNative);
    // An opt-in weather observer admits one exact delayed owner/frame. A reload
    // before selection is allowed; a later frame/owner cannot append to it.
    using fvr::bc2::weatherObservation::Gate;
    using fvr::bc2::weatherObservation::Key;
    Gate weather;
    const Key sceneA{1,2,3,4,5},sceneB{11,12,13,14,15};
    CHECK(weather.State()==0&&!weather.Begin(100000,sceneA));
    CHECK(weather.Reserve(sceneA,0)==Gate::NoSlot);
    CHECK(!weather.Arm(1000,999)&&!weather.Arm(1000,90001));
    CHECK(!weather.Arm(UINT64_MAX-20,1000));
    CHECK(weather.Arm(1000,50000)&&!weather.Arm(1000,50000));
    CHECK(!weather.Begin(50999,sceneA)&&!weather.Begin(51000,{}));
    CHECK(weather.Begin(51000,sceneB)&&weather.Selected()==sceneB);
    CHECK(!weather.Begin(52000,sceneA)&&!weather.Active(sceneA));
    CHECK(weather.Reserve(sceneA,0)==Gate::NoSlot&&weather.Reserve(sceneB,2)==Gate::NoSlot);
    for(unsigned i=0;i<Gate::CapacityPerEye;++i)CHECK(weather.Reserve(sceneB,0)==i);
    CHECK(weather.Reserve(sceneB,0)==Gate::NoSlot&&weather.Overflow());
    CHECK(weather.Count(0)==Gate::CapacityPerEye&&weather.Count(1)==0);
    CHECK(weather.Reserve(sceneB,1)==0); // Each eye has an independent hard cap.
    weather.Image(sceneA,0);CHECK(weather.Images()==0);
    weather.Image(sceneB,0);weather.Image(sceneB,1);CHECK(weather.Images()==3);
    weather.Finish(sceneA,true);CHECK(weather.Active(sceneB));
    weather.Finish(sceneB,true);CHECK(weather.Published()&&!weather.Active(sceneB));
    CHECK(weather.Reserve(sceneB,1)==Gate::NoSlot); // Delayed callback cannot append.
    weather.Finish(sceneB,false);CHECK(weather.Published());
    CHECK(!weather.Begin(60000,sceneA)); // Never silently replace failed/full evidence.
    Gate cancelled;CHECK(cancelled.Arm(0,1000)&&cancelled.Begin(1000,sceneA));
    cancelled.Finish(sceneA,false);CHECK(!cancelled.Published()&&cancelled.State()==4);
    CHECK(!cancelled.Begin(2000,sceneB));
    Gate stoppedWaiting;CHECK(stoppedWaiting.Arm(0,1000));stoppedWaiting.Close();
    CHECK(stoppedWaiting.Closed()&&!stoppedWaiting.Begin(1000,sceneA));
    Gate stoppedActive;CHECK(stoppedActive.Arm(0,1000)&&stoppedActive.Begin(1000,sceneA));
    stoppedActive.Close();CHECK(stoppedActive.Selected()==sceneA&&!stoppedActive.Active(sceneA));
    CHECK(stoppedActive.Reserve(sceneA,0)==Gate::NoSlot);
    stoppedActive.Image(sceneA,0);stoppedActive.Finish(sceneA,true);
    CHECK(stoppedActive.Images()==0&&!stoppedActive.Published());
    weather.Close();CHECK(weather.Published()&&weather.Selected()==sceneB); // Keep completed evidence.
    // Dedicated observer callback admission must drain an in-flight wrapper,
    // then make late entries delegate without touching its retained evidence.
    fvr::graphics::DiagnosticImageLifetime callbacks;
    {
        fvr::graphics::DiagnosticImageLifetime::Scope inFlight(callbacks);CHECK(inFlight.Active());
        CHECK(!callbacks.TryQuiesce(true));
    }
    CHECK(!callbacks.TryQuiesce(false)&&callbacks.TryQuiesce(true));
    fvr::graphics::DiagnosticImageLifetime::Scope late(callbacks);CHECK(!late.Active());
    CHECK(fvr::bc2::weatherObservation::WorldSite(0x7de3cc));
    CHECK(fvr::bc2::weatherObservation::WorldSite(0x7dea5c));
    CHECK(fvr::bc2::weatherObservation::WorldSite(0x7df13e));
    CHECK(!fvr::bc2::weatherObservation::WorldSite(0x7df13d));
    // The cloud experiment changes exactly the translation in two verified,
    // disjoint native packets. It cannot infer a live allocation from a pointer.
    namespace wt=fvr::bc2::weatherTranslation;
    const auto bits=[](float value){return std::bit_cast<std::uint32_t>(value);};
    wt::Words eyeRight{};for(unsigned i:{0u,5u,10u,15u})eyeRight[i]=bits(1.f);
    eyeRight[12]=bits(-769.6483154296875f);eyeRight[13]=bits(6.736935138702393f);eyeRight[14]=bits(567.5116577148438f);
    auto eyeLeft=eyeRight;eyeLeft[12]=bits(-769.7108154296875f);eyeLeft[13]=bits(6.732197284698486f);eyeLeft[14]=bits(567.4988403320312f);
    wt::Packet packetA{0x100000,0x200000,0x200000,0x100080,0x2000,0x300000,0x100000,0x4000,0x1000,eyeRight};
    auto packetB=packetA;packetB.address+=0x400;packetB.world+=0x400;packetB.owner+=0x100;packetB.packetOwner=packetB.owner;
    CHECK(wt::Bounds(packetA)&&wt::Bounds(packetB));
    auto advanced=packetA;advanced.used+=0x800;advanced.flags|=0x40;
    CHECK(wt::MakePlan(packetA,advanced,eyeLeft,eyeRight)); // Other allocations and unrelated flags may advance.
    auto planA=wt::MakePlan(packetA,packetA,eyeLeft,eyeRight);auto planB=wt::MakePlan(packetB,packetB,eyeLeft,eyeRight);
    CHECK(planA&&planB);const std::array<wt::Plan,2> plans{*planA,*planB};
    CHECK(planA->after==eyeLeft&&Near(planA->delta[0],-.0625f));
    for(unsigned field=0;field<9;++field){auto badPacket=packetA;
        switch(field){case 0:badPacket.owner+=16;break;case 1:badPacket.packetOwner+=16;break;
            case 2:badPacket.world+=16;break;case 3:badPacket.flags=0;break;case 4:badPacket.arena+=16;break;
            case 5:badPacket.base+=16;break;case 6:badPacket.capacity+=16;break;case 7:badPacket.used-=16;break;
            default:badPacket.matrix[0]=bits(2.f);break;}
        CHECK(!wt::MakePlan(packetA,badPacket,eyeLeft,eyeRight));}
    auto invalidPacket=packetA;invalidPacket.used=invalidPacket.capacity+1;CHECK(!wt::Bounds(invalidPacket));
    invalidPacket=packetA;invalidPacket.world=invalidPacket.address+16;CHECK(!wt::Bounds(invalidPacket));
    invalidPacket=packetA;invalidPacket.base=0xfffffff0;CHECK(!wt::Bounds(invalidPacket));
    auto wrongCamera=eyeLeft;wrongCamera[0]=bits(2.f);CHECK(!wt::MakePlan(packetA,packetA,wrongCamera,eyeRight));
    wrongCamera=eyeLeft;wrongCamera[12]=bits(std::numeric_limits<float>::infinity());CHECK(!wt::MakePlan(packetA,packetA,wrongCamera,eyeRight));
    wrongCamera=eyeLeft;wrongCamera[12]=bits(-770.f);CHECK(!wt::MakePlan(packetA,packetA,wrongCamera,eyeRight));
    CHECK(!wt::MakePlan(packetA,packetA,eyeRight,eyeRight));
    auto wrongRight=eyeRight;wrongRight[13]=bits(7.f);CHECK(!wt::MakePlan(packetA,packetA,eyeLeft,wrongRight));
    auto nonIdentity=packetA;nonIdentity.matrix[0]=bits(2.f);CHECK(!wt::MakePlan(nonIdentity,nonIdentity,eyeLeft,eyeRight));
    // Reject overlap in all 64 validated bytes, even when 12-byte write slices
    // are disjoint, and reject either matrix crossing the other packet header.
    for(unsigned overlap=0;overlap<4;++overlap){auto overlapping=plans;
        if(overlap==0)overlapping[1].packet.world=packetA.world+16;
        if(overlap==1)overlapping[1].packet.address=packetA.address+16;
        if(overlap==2)overlapping[1].packet.world=packetA.address+16;
        if(overlap==3)overlapping[1].packet.address=packetA.world;
        CHECK(wt::Bounds(overlapping[1].packet));wt::Transaction t;CHECK(!t.Stage(overlapping));}
    std::array<wt::Words,2> memory{eyeRight,eyeRight};unsigned callbackCalls=0,nativeReads=0;bool ownerCurrent=true;
    const auto replace=[&](unsigned i,const wt::Words& expected,const wt::Words& replacement)noexcept {
        ++callbackCalls;if(!ownerCurrent)return wt::Replaced::Rejected;
        ++nativeReads;if(memory[i]!=expected)return wt::Replaced::Rejected;
        for(unsigned j=12;j<15;++j)memory[i][j]=replacement[j];return wt::Replaced::Applied;};
    wt::Transaction complete;CHECK(complete.Stage(plans)&&complete.Apply(replace));
    CHECK(memory[0]==eyeLeft&&memory[1]==eyeLeft&&complete.WrittenMask()==3);
    CHECK(!complete.Stage(plans)&&!complete.Apply(replace));
    CHECK(complete.Restore(replace)&&complete.UnresolvedMask()==0&&!complete.Failed());
    CHECK(memory[0]==eyeRight&&memory[1]==eyeRight&&callbackCalls==4);
    CHECK(complete.Restore(replace)&&callbackCalls==4); // Explicit finish is idempotent.
    // Rejection of packet 2 restores only packet 1, preserving the failure receipt.
    wt::Transaction partial;CHECK(partial.Stage(plans));
    CHECK(!partial.Apply([&](unsigned i,const auto& a,const auto& b)noexcept {return i?wt::Replaced::Rejected:replace(i,a,b);}));
    CHECK(partial.WrittenMask()==1&&partial.UnresolvedMask()==1&&partial.Failed());
    CHECK(partial.Restore(replace)&&memory[0]==eyeRight&&partial.UnresolvedMask()==0&&partial.Failed());
    // A newer native write is never overwritten during restoration.
    wt::Transaction conflict;CHECK(conflict.Stage(plans)&&conflict.Apply(replace));memory[0][12]=bits(123.f);
    const auto newer=memory[0];CHECK(!conflict.Restore(replace));
    CHECK(conflict.Failed()&&conflict.UnresolvedMask()==1&&memory[0]==newer&&memory[1]==eyeRight);
    memory={eyeRight,eyeRight};
    // Owner retirement rejects before any memory access. Destruction does not
    // read stale destinations or make a second restoration attempt.
    unsigned callsBeforeDestruction=0;
    {wt::Transaction retired;CHECK(retired.Stage(plans)&&retired.Apply(replace));ownerCurrent=false;
        const auto readsBefore=nativeReads;CHECK(!retired.Restore(replace)&&retired.UnresolvedMask()==3);
        CHECK(nativeReads==readsBefore);callsBeforeDestruction=callbackCalls;}
    CHECK(callbackCalls==callsBeforeDestruction);ownerCurrent=true;memory={eyeRight,eyeRight};
    // A protected write that partially faults cannot be treated as untouched;
    // guarded restoration refuses unknown mixed bytes and reports unresolved.
    wt::Transaction indeterminate;CHECK(indeterminate.Stage(plans));
    CHECK(!indeterminate.Apply([&](unsigned i,const auto&,const auto& replacement)noexcept {
        memory[i][12]=replacement[12];return wt::Replaced::Indeterminate;}));
    CHECK(indeterminate.WrittenMask()==1&&!indeterminate.Restore(replace)&&indeterminate.UnresolvedMask()==1&&indeterminate.Failed());
    // Production candidate: per-frame packets are published only by completed
    // producers bearing the entry epoch, not by old callbacks at reused keys.
    wt::FrameCache cache;CHECK(cache.Begin(sceneA));const auto tokenA=cache.Capture(sceneA);CHECK(tokenA);
    wt::FrameCache::Skip skip{};CHECK(!cache.Claim(tokenA,1,&skip)&&skip==wt::FrameCache::Skip::Missing);
    const auto producerA0=cache.EnterProducer(sceneA,0);CHECK(producerA0==tokenA);
    CHECK(!cache.Claim(tokenA,1,&skip)&&skip==wt::FrameCache::Skip::Busy);
    CHECK(cache.CompleteProducer(producerA0,0,&packetA,&eyeRight));
    const auto producerA1=cache.EnterProducer(sceneA,1);CHECK(cache.CompleteProducer(producerA1,1,&packetB,&eyeRight));
    auto pair=cache.Claim(tokenA,1);CHECK(pair&&pair->packets[0].matrix==eyeRight&&pair->token==tokenA);
    CHECK(cache.ConsumerIntact(tokenA,1)&&!cache.Begin(sceneB)&&!cache.Retire(tokenA));
    CHECK(!cache.Claim(tokenA,1,&skip)&&skip==wt::FrameCache::Skip::Busy);
    CHECK(cache.Release(tokenA,1)&&!cache.Release(tokenA,1));
    CHECK(!cache.Claim(tokenA,1,&skip)&&skip==wt::FrameCache::Skip::Phase);
    CHECK(cache.Claim(tokenA,2)&&cache.Release(tokenA,2)&&cache.Retire(tokenA));
    CHECK(!cache.Current(tokenA)&&!cache.Claim(tokenA,1));
    // An old producer completion must not be admitted even when every native
    // address and the frame number match a newly admitted epoch.
    CHECK(cache.Begin(sceneA));const auto oldProducer=cache.EnterProducer(sceneA,0);
    CHECK(cache.Begin(sceneA));const auto reused=cache.Capture(sceneA);CHECK(reused!=oldProducer);
    CHECK(!cache.Current(oldProducer)&&!cache.CompleteProducer(oldProducer,0,&packetA,&eyeRight));
    CHECK(cache.Current(reused));
    CHECK(cache.CompleteProducer(cache.EnterProducer(sceneA,0),0,&packetA,&eyeRight));
    CHECK(cache.CompleteProducer(cache.EnterProducer(sceneA,1),1,&packetB,&eyeRight));
    CHECK(cache.Claim(reused,1)&&cache.Release(reused,1));
    CHECK(cache.Retire(reused));
    // A duplicate builder entering after both rows publish prevents consumers
    // immediately, before its original native call has returned.
    CHECK(cache.Begin(sceneB));const auto duplicateFrame=cache.Capture(sceneB);
    CHECK(cache.CompleteProducer(cache.EnterProducer(sceneB,0),0,&packetA,&eyeRight));
    CHECK(cache.CompleteProducer(cache.EnterProducer(sceneB,1),1,&packetB,&eyeRight));
    const auto duplicate=cache.EnterProducer(sceneB,0);
    CHECK(!cache.Claim(duplicateFrame,1,&skip)&&skip==wt::FrameCache::Skip::Invalid);
    CHECK(!cache.CompleteProducer(duplicate,0,&packetA,&eyeRight));CHECK(cache.Retire(duplicateFrame));
    // A native producer overlapping an admitted consumer invalidates its
    // integrity receipt; explicit restoration and Release remain possible.
    CHECK(cache.Begin(sceneB));const auto overlapFrame=cache.Capture(sceneB);
    CHECK(cache.CompleteProducer(cache.EnterProducer(sceneB,0),0,&packetA,&eyeRight));
    CHECK(cache.CompleteProducer(cache.EnterProducer(sceneB,1),1,&packetB,&eyeRight));
    CHECK(cache.Claim(overlapFrame,1));const auto overlappingProducer=cache.EnterProducer(sceneB,1);
    CHECK(!cache.ConsumerIntact(overlapFrame,1));CHECK(!cache.CompleteProducer(overlappingProducer,1,&packetB,&eyeRight));
    CHECK(cache.Release(overlapFrame,1)&&cache.Retire(overlapFrame));
    // Missing/abandoned optional sky and invalid previous frames do not poison
    // the next frame; both slots must be freshly completed before it can draw.
    CHECK(cache.Begin(sceneA));const auto abandoned=cache.EnterProducer(sceneA,0);
    CHECK(!cache.CompleteProducer(abandoned,0,nullptr,nullptr));CHECK(cache.Retire(abandoned));
    CHECK(cache.Begin(sceneB));const auto recovered=cache.Capture(sceneB);
    CHECK(!cache.Claim(recovered,1,&skip)&&skip==wt::FrameCache::Skip::Missing);
    CHECK(cache.CompleteProducer(cache.EnterProducer(sceneB,0),0,&packetA,&eyeRight));
    CHECK(cache.CompleteProducer(cache.EnterProducer(sceneB,1),1,&packetB,&eyeRight));
    CHECK(cache.Claim(recovered,1)&&cache.ConsumerIntact(recovered,1)&&cache.Release(recovered,1));
    CHECK(cache.Claim(recovered,2)&&cache.ConsumerIntact(recovered,2)&&cache.Release(recovered,2)&&cache.Retire(recovered));
    return 0;
}
