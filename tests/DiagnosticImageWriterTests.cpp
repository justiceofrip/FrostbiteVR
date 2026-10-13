#include "DiagnosticImageWriter.h"
#include <chrono>
#include <iostream>
#include <stdexcept>
using fvr::probe::DiagnosticImageWriter;
#define CHECK(x) do{if(!(x)){std::cerr<<__LINE__<<": " #x "\n";return 1;}}while(false)
int main(){
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
