#pragma once
#if defined(FVR_BC2_ORDINARY_RESOURCE_SCENE_PROBE)
#include <Windows.h>
#include <array>
#include <atomic>
#include <filesystem>
#include <fstream>
namespace fvr::bc2::graphicsThreadObservation {
enum class Boundary : unsigned { View, Indexed, Present, Count };
struct Row {
    unsigned boundary=0, thread=0, previous=0, concurrent=0;
    std::array<unsigned,3> active{};
    unsigned long long tick=0;
};
struct Slot { Row row{}; std::atomic<bool> ready=false; };
inline std::array<Slot,64> rows;
inline std::array<std::atomic<unsigned>,3> last{}, active{}, conflicts{};
inline std::atomic<unsigned> count=0, overflow=0;
// Private observation only. No lock, device call, disk I/O or policy change
// in a graphics callback. Captured overlap is evidence, not synchronization.
class Scope {
    unsigned index_=3;bool owned_=false;
public:
    explicit Scope(Boundary boundary,bool enabled=true)noexcept {
        if(!enabled)return;
        index_=unsigned(boundary);const auto thread=GetCurrentThreadId();
        unsigned empty=0;owned_=active[index_].compare_exchange_strong(empty,thread);
        const auto previous=last[index_].load(std::memory_order_relaxed);
        if(previous!=thread)last[index_].store(thread,std::memory_order_relaxed);
        Row row;row.boundary=index_;row.thread=thread;row.previous=previous;
        for(unsigned i=0;i<3;++i){row.active[i]=active[i].load();if(row.active[i]&&row.active[i]!=thread)row.concurrent|=1u<<i;}
        const bool conflict=row.concurrent && !(conflicts[index_].fetch_or(row.concurrent)&row.concurrent);
        if(previous==thread&&!conflict)return;
        row.tick=GetTickCount64();const auto n=count.fetch_add(1);
        if(n>=rows.size()){++overflow;return;}
        rows[n].row=row;rows[n].ready.store(true,std::memory_order_release);
    }
    Scope(const Scope&)=delete;
    ~Scope(){if(owned_)active[index_].store(0);}
};
// Called only by the existing reporting worker. A crash dump also retains
// published slots that were not yet flushed; unpublished slots are not valid.
inline void Flush(const std::filesystem::path& path){
    static std::array<bool,64> written{};
    std::ofstream out;
    for(unsigned n=0;n<rows.size();++n){
        if(written[n]||!rows[n].ready.load(std::memory_order_acquire))continue;
        if(!out.is_open())out.open(path,std::ios::app);
        if(!out)return;
        const auto& r=rows[n].row;
        out<<"{\"boundary\":"<<r.boundary<<",\"thread\":"<<r.thread<<",\"previous\":"<<r.previous
           <<",\"concurrent\":"<<r.concurrent<<",\"active\":["<<r.active[0]<<','<<r.active[1]<<','<<r.active[2]
           <<"],\"tick_ms\":"<<r.tick<<",\"overflow\":"<<overflow.load()<<"}\n";
        out.flush();if(out)written[n]=true;
    }
}
}
#endif
