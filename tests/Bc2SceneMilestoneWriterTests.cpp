#include "Bc2SceneMilestoneWriter.h"
#include <cstdlib>
#include <fstream>
#include <iostream>
#include <thread>
using namespace fvr::bc2::sceneMilestones;
#define CHECK(x) do {if(!(x)){std::cerr<<__LINE__<<": " #x "\n";std::exit(1);}} while(false)
std::string ReadFileText(const std::filesystem::path& p){std::ifstream in(p);return {std::istreambuf_iterator<char>(in),{}};}
int main(int argc,char** argv){
    CHECK(argc==2);const std::filesystem::path output=argv[1];
    Writer writer(output);const auto r=journal.BeginRebuild({11,22,33,44,55},Stamp);journal.CompleteRebuild(r,true,Stamp);
    std::atomic<bool> inside=false,release=false;
    std::thread paused([&]{journal.Begin(SceneMilestoneKind::WorldUpdate,{99},[](const auto&){return true;},[&]{
        inside.store(true,std::memory_order_release);while(!release.load(std::memory_order_acquire))std::this_thread::yield();return Stamp();});});
    while(!inside.load(std::memory_order_acquire))std::this_thread::yield();
    const auto local=Begin(SceneMilestoneKind::LocalGameplayUpdate,{77});Mark(local,SceneMilestoneStage::Exit);
    writer.Flush();const auto before=ReadFileText(output);
    CHECK(before.find("local_gameplay_update")!=std::string::npos&&before.find("world_update")==std::string::npos);
    release.store(true,std::memory_order_release);paused.join();writer.Flush();
    const auto after=ReadFileText(output);CHECK(after.starts_with(before));CHECK(after.find("world_update")!=std::string::npos);
    writer.Flush();CHECK(ReadFileText(output)==after);
    std::ostringstream status;writer.Status(status);CHECK(status.str().find("\"write_error\":0")!=std::string::npos);
    CHECK(status.str().find("\"flushed_rows\":5")!=std::string::npos);
    Writer rejected(output/L"not-a-directory.jsonl");rejected.Flush();std::ostringstream failed;rejected.Status(failed);
    CHECK(failed.str().find("\"write_error\":0")==std::string::npos);
    std::cout<<"SceneMilestoneWriter: durable batches, delayed producer, no duplicate drain, open failure passed\n";
}
