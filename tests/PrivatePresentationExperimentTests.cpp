#include "fvr/graphics/PrivatePresentationExperiment.h"
#include "Test.h"
#include <sstream>
using namespace fvr::graphics;
namespace {
constexpr wchar_t Token[]=L"0123456789abcdef0123456789abcdef";
struct Fake {
    using Device=std::uintptr_t;
    unsigned thread=7,value=3,opens=0,reads=0,writes=0,releases=0,eventOpens=0,eventCloses=0,polls=0,acks=0;
    unsigned changeOnRead=0,changedValue=2;
    bool signal=false,eventsOkay=true,ackOkay=true,pollOkay=true,writeOnError=false,opened=false;
    HRESULT openResult=S_OK,readResult=S_OK,writeResult=S_OK;
    std::int64_t frequency=10000000,now=100;
    static std::uintptr_t Address(Device d)noexcept{return d;}
    unsigned Thread()const noexcept{return thread;}
    std::int64_t Frequency()const noexcept{return frequency;}
    std::int64_t Qpc()noexcept {if(now<=0)return now;return now+=10;}
    bool OpenEvents(const wchar_t*)noexcept {++eventOpens;return eventsOkay;}
    void CloseEvents()noexcept {++eventCloses;}
    int PollRequest()noexcept {++polls;if(!pollOkay)return -1;const bool ready=signal;signal=false;return ready?1:0;}
    bool Acknowledge()noexcept {++acks;return ackOkay;}
    HRESULT Open(Device)noexcept {++opens;opened=SUCCEEDED(openResult);return openResult;}
    HRESULT Read(unsigned& out)noexcept {++reads;if(changeOnRead==reads)value=changedValue;if(SUCCEEDED(readResult))out=value;return readResult;}
    HRESULT Write(unsigned next)noexcept {++writes;if(SUCCEEDED(writeResult)||writeOnError)value=next;return writeResult;}
    void Release()noexcept {++releases;opened=false;}
};
const FrameBridgePresentIdentity Identity{11,12,13,14,15,7};
FrameBridgePresentBoundary Boundary(){
    FrameBridgePresentBoundary b;b.current=Identity;b.presentedSwapchain=Identity.swapchain;
    b.enabled=b.lifetimeHeld=b.bridgeOwnerCurrent=b.connected=b.nativeStateRestored=true;return b;
}
using Policy=BasicPrivatePresentationExperiment<Fake>;
void Configure(Policy& p){p.Configure(Token,32,true);}
void Request(Fake& f,Policy& p){f.signal=true;p.Tick(Identity.device,Identity,Boundary(),RenderQueueMode::Observe);}
void ReachC(Fake& f,Policy& p){Configure(p);Request(f,p);Request(f,p);Request(f,p);}
int OptInAndAdmission(){
    {Fake f;Policy p(f);p.Configure(nullptr,0,false);p.Tick(13,Identity,Boundary(),RenderQueueMode::Observe);p.Close(13,Identity);
        CHECK(!p.Enabled()&&p.AfterEnabled()&&!p.Phase()&&!f.eventOpens&&!f.opens&&!f.polls&&!f.reads&&!f.writes);}
    const wchar_t* bad[]={L"",L"0123456789abcdef0123456789abcde",L"0123456789abcdef0123456789abcdef0",L"0123456789ABCDEF0123456789abcdef",L"00000000000000000000000000000000"};
    for(auto token:bad){Fake f;Policy p(f);p.Configure(token,std::wcslen(token),true);p.Tick(13,Identity,Boundary(),RenderQueueMode::Observe);
        CHECK(p.Enabled()&&!p.AfterEnabled()&&p.Snapshot().failure==PresentationExperimentFailure::Token&&!f.eventOpens&&!f.opens&&!f.polls);p.Close(13,Identity);}
    {Fake f;f.eventsOkay=false;Policy p(f);Configure(p);Request(f,p);CHECK(p.Snapshot().failure==PresentationExperimentFailure::Events&&!f.opens&&!f.polls);p.Close(13,Identity);}
    {Fake f;Policy p(f);Configure(p);Configure(p);CHECK(f.eventOpens==1&&!p.AfterEnabled());
        auto b=Boundary();b.nativeStateRestored=false;f.signal=true;p.Tick(13,Identity,b,RenderQueueMode::Observe);
        CHECK(!f.opens&&!f.polls&&!p.Phase());p.Tick(13,Identity,Boundary(),RenderQueueMode::Observe);CHECK(p.Phase()==1&&f.acks==1);p.Close(13,Identity);}
    {Fake f;Policy p(f);Configure(p);p.Tick(13,Identity,Boundary(),RenderQueueMode::LimitOne);
        CHECK(p.Snapshot().failure==PresentationExperimentFailure::OtherLimiter&&!f.opens&&!f.polls&&!f.writes);p.Close(13,Identity);}
    {Fake f;f.value=2;Policy p(f);Configure(p);Request(f,p);CHECK(p.Snapshot().failure==PresentationExperimentFailure::QueueChanged&&!f.writes&&!f.polls&&!f.acks);p.Close(13,Identity);CHECK(f.value==2&&p.Snapshot().closed);}
    return 0;
}
int CompleteSequence(){
    Fake f;Policy p(f);Configure(p);p.Tick(13,Identity,Boundary(),RenderQueueMode::Observe);
    CHECK(p.Snapshot().ownerPinned&&!p.Phase()&&!p.AfterEnabled()&&f.reads==1);
    for(unsigned n=0;n<16;++n)p.Tick(13,Identity,Boundary(),RenderQueueMode::Observe);
    CHECK(f.reads==1&&!f.writes&&!f.acks&&p.Snapshot().noEvents==17);
    for(unsigned phase=1;phase<=5;++phase){
        Request(f,p);CHECK(p.Phase()==phase&&f.acks==phase&&p.Snapshot().rows==phase);
        CHECK(f.value==(phase==3?1u:3u));CHECK(p.AfterEnabled()==(phase==2||phase==3));
        auto s=p.Snapshot();const auto& row=s.phases[phase-1];
        CHECK(row.phase==phase&&row.applied&&row.acknowledged&&row.failure==PresentationExperimentFailure::None&&row.beginQpc>0);
        CHECK(row.queueValue==f.value&&row.afterEnabled==p.AfterEnabled());
        if(phase>1)CHECK(row.beginQpc>s.phases[phase-2].beginQpc);
    }
    const auto s=p.Snapshot();CHECK(s.complete&&s.closed&&s.restored&&s.requests==5&&s.acks==5&&s.failure==PresentationExperimentFailure::None);
    CHECK(f.writes==2&&f.eventCloses==1&&p.QueueSnapshot().restored&&p.QueueSnapshot().closed);
    const auto polls=f.polls,reads=f.reads;Request(f,p);p.Close(13,Identity);
    CHECK(f.polls==polls&&f.reads==reads&&f.writes==2&&f.acks==5&&f.eventCloses==1);
    return 0;
}
int OwnershipAndCleanup(){
    {Fake f;Policy p(f);ReachC(f,p);CHECK(p.Phase()==3&&f.value==1);p.Close(13,Identity);
        CHECK(f.value==3&&f.writes==2&&p.Snapshot().closed&&p.Snapshot().restored&&!p.Snapshot().complete);}
    {Fake f;Policy p(f);ReachC(f,p);f.thread=8;const auto reads=f.reads;Request(f,p);p.Close(13,Identity);
        CHECK(f.reads==reads&&f.value==1&&!p.Snapshot().closed&&p.Snapshot().failure==PresentationExperimentFailure::Owner);
        f.thread=7;p.Close(13,Identity);CHECK(f.value==3&&p.Snapshot().restored&&p.Snapshot().closed);}
    {Fake f;Policy p(f);ReachC(f,p);auto changed=Identity;++changed.context;auto b=Boundary();b.current=changed;
        p.Tick(13,changed,b,RenderQueueMode::Observe);CHECK(p.Snapshot().failure==PresentationExperimentFailure::Owner&&f.value==1);
        p.Close(13,Identity);CHECK(f.value==3&&p.Snapshot().restored);}
    {Fake f;Policy p(f);ReachC(f,p);f.value=2;Request(f,p);
        CHECK(p.Phase()==3&&f.acks==3&&p.Snapshot().failure==PresentationExperimentFailure::QueueChanged);
        p.Close(13,Identity);CHECK(f.value==2&&f.writes==1&&!p.Snapshot().restored&&p.Snapshot().closed);}
    return 0;
}
int FailurePaths(){
    {Fake f;f.openResult=E_NOINTERFACE;Policy p(f);Configure(p);Request(f,p);CHECK(p.Snapshot().failure==PresentationExperimentFailure::Device&&!f.writes&&!f.acks);p.Close(13,Identity);}
    {Fake f;f.readResult=E_FAIL;Policy p(f);Configure(p);Request(f,p);CHECK(p.Snapshot().failure==PresentationExperimentFailure::Read&&!f.writes&&!f.acks);f.readResult=S_OK;p.Close(13,Identity);}
    {Fake f;Policy p(f);Configure(p);Request(f,p);Request(f,p);f.writeResult=E_FAIL;f.writeOnError=true;Request(f,p);
        CHECK(p.Phase()==2&&f.value==1&&f.acks==2&&p.Snapshot().failure==PresentationExperimentFailure::Apply);
        f.writeResult=S_OK;p.Close(13,Identity);CHECK(f.value==3&&p.Snapshot().restored);}
    {Fake f;Policy p(f);ReachC(f,p);f.writeResult=E_FAIL;Request(f,p);
        CHECK(p.Phase()==3&&f.acks==3&&f.value==1&&p.Snapshot().failure==PresentationExperimentFailure::Restore);
        f.writeResult=S_OK;p.Close(13,Identity);CHECK(f.value==3&&p.Snapshot().restored);}
    {Fake f;Policy p(f);Configure(p);Request(f,p);Request(f,p);f.changeOnRead=f.reads+2;Request(f,p);
        CHECK(p.Snapshot().failure==PresentationExperimentFailure::Apply&&f.value==2&&!f.writes&&f.acks==2);p.Close(13,Identity);CHECK(f.value==2&&!f.writes);}
    {Fake f;f.pollOkay=false;Policy p(f);Configure(p);Request(f,p);CHECK(p.Snapshot().failure==PresentationExperimentFailure::Poll&&!p.Snapshot().rows&&!f.acks);p.Close(13,Identity);}
    {Fake f;f.now=0;Policy p(f);Configure(p);Request(f,p);CHECK(p.Snapshot().failure==PresentationExperimentFailure::Clock&&!p.Phase()&&!f.acks);p.Close(13,Identity);}
    {Fake f;f.ackOkay=false;Policy p(f);Configure(p);Request(f,p);auto s=p.Snapshot();
        CHECK(s.failure==PresentationExperimentFailure::Ack&&s.phase==1&&s.phases[0].applied&&!s.phases[0].acknowledged&&s.acks==0);p.Close(13,Identity);}
    {PrivatePresentationExperiment p;std::ostringstream unavailable;p.Write(unavailable,false);
        CHECK(unavailable.str().find("\"available\":false")!=std::string::npos&&unavailable.str().find("identity")==std::string::npos);
        std::ostringstream empty;p.Write(empty,true);CHECK(empty.str().find("\"enabled\":false")!=std::string::npos&&empty.str().find("\"rows\":[]")!=std::string::npos);}
    return 0;
}
}
int main(){CHECK(!OptInAndAdmission());CHECK(!CompleteSequence());CHECK(!OwnershipAndCleanup());CHECK(!FailurePaths());return 0;}
