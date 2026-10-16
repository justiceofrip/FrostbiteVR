#include "DiagnosticImageWriter.h"
#include "PumpSideCaptureWindow.h"
#include <string_view>
#include <sstream>
#include <chrono>
#include <iostream>
#include <stdexcept>
using fvr::probe::DiagnosticImageWriter;
#define CHECK(x) do{if(!(x)){std::cerr<<__LINE__<<": " #x "\n";return 1;}}while(false)
int CaptureWindow(){
    using Window=fvr::probe::PumpSideCaptureWindow;
    Window empty;CHECK(!empty.Due(0,0)&&!empty.Pixels(22000)&&!empty.Complete());
    for(const auto bounds:std::array<std::array<std::uint64_t,3>,6>{{{0,6000,50},{45001,6000,50},{22000,6001,50},{22000,6000,49},{22000,6000,251},{45000,6000,50}}}){
        Window bad;CHECK(!bad.Configure(bounds[0],bounds[1],bounds[2])&&!bad.Enabled());}
    Window window;CHECK(window.Configure(22000,6000,50));CHECK(!window.Configure(22000,6000,50));
    unsigned delivered=0,pixels=0;std::uint64_t first=0,last=0;
    for(std::uint64_t ms=0;ms<60000;++ms){if(!window.Due(ms,delivered))continue;
        if(window.Pixels(ms)){
            if(!pixels)first=ms;else CHECK(ms-last==50);
            last=ms;++pixels;CHECK(window.Observe(ms,1000000000+std::int64_t(ms)*1000000,true));
        }
        ++delivered;
    }
    CHECK(delivered==240&&pixels==120&&first==22000&&last==27950&&window.Complete());
    CHECK(!window.Pixels(27999)&&!window.Due(60000,240));
    std::ostringstream report;window.Report(report);CHECK(report.str().find("\"max_gap_ns\":50000000")!=std::string::npos);
    Window late;CHECK(late.Configure(22000,6000,50));
    CHECK(late.Observe(27990,1,true));CHECK(!late.Pixels(28000));CHECK(!late.Complete());
    CHECK(late.Due(50000,0)); // Late requests do not fabricate image delivery or extend the window.
    Window rejected;CHECK(rejected.Configure(22000,500,50));
    for(unsigned n=0;n<10;++n)CHECK(rejected.Observe(22000+n*50,1+n,n!=4));
    CHECK(!rejected.Complete()&&rejected.Attempted()==10&&!rejected.Pixels(22499));
    Window reversed;CHECK(reversed.Configure(22000,500,50));CHECK(reversed.Observe(22000,2,true));
    CHECK(!reversed.Observe(22050,1,true)&&!reversed.Complete());
    CHECK(!reversed.Pixels(22100)&&!reversed.Observe(22100,3,true)&&reversed.Attempted()==1);
    std::cout<<"Pump capture schedule PASS:240 requests,120/6s pixel bound, actual gaps, late/missed/rejected accounting. No input/native actions\n";
    return 0;
}
int main(int argc,char** argv){
    if(argc==2&&std::string_view(argv[1])=="--capture-window")return CaptureWindow();
    if(CaptureWindow())return 1;
    auto pixels=std::make_shared<DiagnosticImageWriter::Pixels>();(*pixels)[0]={1,2,3,4};(*pixels)[1]={5,6,7,8};
    std::mutex gate;std::condition_variable event;bool started=false,release=false;
    std::vector<std::uint64_t> order;const auto caller=std::this_thread::get_id();bool offThread=true;
    DiagnosticImageWriter blocked({},[&](const auto& capture){
        std::unique_lock lock(gate);offThread&=std::this_thread::get_id()!=caller;started=true;event.notify_one();
        event.wait(lock,[&]{return release;});order.push_back(capture.pair);return true;
    });
    CHECK(blocked.Queue(0,pixels));
    {std::unique_lock lock(gate);if(!event.wait_for(lock,std::chrono::seconds(5),[&]{return started;})){
        release=true;event.notify_one();throw std::runtime_error("Writer did not start");}}
    bool accepted=true;for(unsigned n=1;n<=4;++n)accepted&=blocked.Queue(n,pixels);
    const bool overflow=blocked.Queue(5,pixels);
    {std::lock_guard lock(gate);release=true;event.notify_one();}
    const auto result=blocked.Finish();
    CHECK(accepted&&!overflow&&offThread&&order==std::vector<std::uint64_t>({0,1,2,3,4}));
    CHECK(result.queued==5&&result.written==5&&result.rejected==1&&result.failed==0);
    CHECK(!blocked.Queue(6,pixels));
    DiagnosticImageWriter failures({},[](const auto& capture){
        if(capture.pair==1)throw std::runtime_error("Injected write failure");return capture.pair!=0;
    });
    for(unsigned n=0;n<3;++n)CHECK(failures.Queue(n,pixels));
    const auto failed=failures.Finish();CHECK(failed.failed==2&&failed.written==1&&failed.queued==3);
    const auto folder=std::filesystem::temp_directory_path()/("fvr-image-writer-"+std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
    CHECK(std::filesystem::create_directory(folder));
    DiagnosticImageWriter files(folder);CHECK(files.Queue(7,pixels));CHECK(files.Finish().written==1);
    for(unsigned eye=0;eye<2;++eye){const auto name=folder/("pair-7-eye-"+std::to_string(eye)+".rgba");
        std::ifstream in(name,std::ios::binary);std::vector<unsigned char> read{std::istreambuf_iterator<char>(in),{}};
        CHECK(read==(*pixels)[eye]);in.close();CHECK(std::filesystem::remove(name));}
    CHECK(std::filesystem::remove(folder));
    std::cout<<"Bounded queue, off-thread writes, ordered drain, failure accounting and exact files PASS\n";
}
