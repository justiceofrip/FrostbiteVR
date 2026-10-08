#include "fvr/ipc/RemoteFrameProvider.h"
#include "fvr/ipc/FeedbackChannel.h"
#include "Test.h"
#include <Windows.h>
#include <iostream>
#include <string>
using namespace fvr;
std::int64_t Now(){LARGE_INTEGER q{},f{};QueryPerformanceCounter(&q);QueryPerformanceFrequency(&f);
    return q.QuadPart/f.QuadPart*1000000000+(q.QuadPart%f.QuadPart)*1000000000/f.QuadPart;}
interaction::FeedbackEvent Event(std::uint64_t id){const auto now=Now();return {id,15,7,now,now+100000000,interaction::FeedbackKind::ReloadCapture,0};}
int CrossProcess(const wchar_t* peer){
    ipc::RemoteFrameProvider host;CHECK(host.Create());
    const auto doneName=L"Local\\FrostbiteVR.Feedback.Test.Done."+host.Token();
    const auto done=CreateEventW(nullptr,TRUE,FALSE,doneName.c_str());CHECK(done);
    std::wstring command=L"\""+std::wstring(peer)+L"\" --producer "+host.Token();
    STARTUPINFOW startup{sizeof(startup)};PROCESS_INFORMATION child{};
    const auto launched=CreateProcessW(peer,command.data(),nullptr,nullptr,FALSE,CREATE_NO_WINDOW,nullptr,nullptr,&startup,&child);
    if(!launched){CloseHandle(done);CHECK(launched);}
    interaction::FeedbackEvent event;bool received=false;
    const auto deadline=GetTickCount64()+5000;
    while(GetTickCount64()<deadline&&WaitForSingleObject(child.hProcess,0)==WAIT_TIMEOUT){
        if(host.TakeFeedback(event)){received=true;break;}Sleep(1);
    }
    SetEvent(done);const auto stopped=WaitForSingleObject(child.hProcess,5000);
    DWORD code=999;GetExitCodeProcess(child.hProcess,&code);
    CloseHandle(child.hThread);CloseHandle(child.hProcess);CloseHandle(done);
    CHECK(stopped==WAIT_OBJECT_0&&code==0&&received&&event.id==1&&event.inputSequence==15&&event.space==7);
    CHECK(event.deadlineNs-event.observedNs==100000000);
    std::cout<<"Feedback cross-process transfer with original deadline passed\n";return 0;
}
int wmain(int argc,wchar_t** argv){
    if(argc==3&&std::wstring(argv[1])==L"--producer"){
        ipc::FrameChannel producer;CHECK(producer.ConnectProducer(argv[2]));
        CHECK(producer.PublishFeedback(Event(1)));
        const auto doneName=L"Local\\FrostbiteVR.Feedback.Test.Done."+std::wstring(argv[2]);
        const auto done=OpenEventW(SYNCHRONIZE,FALSE,doneName.c_str());CHECK(done);
        const auto result=WaitForSingleObject(done,5000);CloseHandle(done);CHECK(result==WAIT_OBJECT_0);return 0;
    }
    if(argc==3&&std::wstring(argv[1])==L"--peer")return CrossProcess(argv[2]);
    wchar_t self[32768]{};CHECK(GetModuleFileNameW(nullptr,self,32768));CHECK(CrossProcess(self)==0);
    ipc::RemoteFrameProvider host;CHECK(host.Create());ipc::FrameChannel producer;CHECK(producer.ConnectProducer(host.Token()));
    interaction::FeedbackEvent e;CHECK(!host.TakeFeedback(e));
    const auto sent=Event(1);CHECK(producer.PublishFeedback(sent));CHECK(!producer.PublishFeedback(sent));
    CHECK(host.TakeFeedback(e)&&e.id==sent.id&&e.observedNs==sent.observedNs&&e.deadlineNs==sent.deadlineNs);CHECK(!host.TakeFeedback(e));
    auto invalid=Event(2);invalid.kind=interaction::FeedbackKind(999);CHECK(!producer.PublishFeedback(invalid));
    for(unsigned n=2;n<10;++n)CHECK(producer.PublishFeedback(Event(n)));CHECK(!producer.PublishFeedback(Event(10)));
    for(unsigned n=2;n<10;++n){CHECK(host.TakeFeedback(e)&&e.id==n);}CHECK(!host.TakeFeedback(e));
    CHECK(producer.PublishFeedback(Event(10)));Sleep(110);CHECK(!host.TakeFeedback(e));
    auto stale=Event(11);stale.observedNs-=200000000;stale.deadlineNs-=200000000;CHECK(!producer.PublishFeedback(stale));
    const auto token=host.Token();ipc::FeedbackChannel duplicate;CHECK(!duplicate.ConnectProducer(token));
    host.Close();CHECK(!producer.PublishFeedback(Event(12)));producer.Close();CHECK(host.Create());CHECK(host.Token()!=token);
    CHECK(producer.ConnectProducer(host.Token()));CHECK(!host.TakeFeedback(e));CHECK(producer.PublishFeedback(Event(1)));CHECK(host.TakeFeedback(e)&&e.id==1);
    std::cout<<"Feedback side lane: exact deadline, FIFO/dedup, bounded overflow, expiry and session replacement passed\n";return 0;
}
