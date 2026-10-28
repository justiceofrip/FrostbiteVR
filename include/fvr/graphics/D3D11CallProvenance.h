#pragma once
#include "fvr/graphics/GraphicsCallProvenance.h"
#include <Windows.h>
#include <filesystem>
#include <fstream>
#include <ostream>
namespace fvr::graphics::callProvenance {
using Kind=GraphicsCallKind;
inline GraphicsCallProvenance recorder;
struct Bootstrap {unsigned flags=0;std::int32_t multithreadQuery=0;bool protectedKnown=false,protectedValue=false;};
inline Bootstrap bootstrap;
inline bool configured=false;
inline std::int64_t Qpc()noexcept{LARGE_INTEGER value{};return QueryPerformanceCounter(&value)?value.QuadPart:0;}
inline bool Configure(GraphicsCallIdentity identity,Bootstrap value)noexcept {
    // Worker only copies already captured values; it never queries an interface.
    bootstrap=value;configured=recorder.Configure(identity);return configured;
}
class Scope {
    GraphicsCallProvenance::Ticket ticket_{};GraphicsCallExit exit_{};
public:
    Scope(Kind kind,const void* device,const void* context,const void* chain,std::uintptr_t caller)noexcept {
        const auto d=reinterpret_cast<std::uintptr_t>(device),c=reinterpret_cast<std::uintptr_t>(context),s=reinterpret_cast<std::uintptr_t>(chain);
        if(!recorder.Matches(kind,d,c,s))return;const auto error=GetLastError();
        ticket_=recorder.Begin(kind,d,c,s,caller,GetCurrentThreadId(),Qpc());SetLastError(error);
    }
    ~Scope(){if(ticket_.admitted){const auto error=GetLastError();exit_.endQpc=Qpc();recorder.End(ticket_,exit_);SetLastError(error);}}
    void Result(std::int32_t value)noexcept{exit_.result=value;exit_.resultKnown=true;}
    Scope(const Scope&)=delete;
};
inline void Summary(std::ostream& out){
    const auto id=recorder.Identity();
    out<<"{\"schema\":\"fvr.graphics.call-provenance.v1\",\"enabled_for_session\":"<<configured
       <<",\"device\":"<<id.device<<",\"context\":"<<id.context<<",\"swapchain\":"<<id.chain<<",\"bootstrap_thread\":"<<id.ownerThread
       <<",\"device_flags\":"<<bootstrap.flags<<",\"multithread_query_hresult\":"<<bootstrap.multithreadQuery
       <<",\"multithread_protected_known\":"<<bootstrap.protectedKnown<<",\"multithread_protected\":"<<bootstrap.protectedValue
       <<",\"active\":"<<recorder.Active()<<",\"rejected_identity\":"<<recorder.RejectedIdentity()<<",\"thread_overflow\":"<<recorder.ThreadOverflow()
       <<",\"record_limit\":"<<GraphicsCallProvenance::Rows<<",\"protection_changed\":false,\"performance_measurement\":false,\"counts\":[";
    for(unsigned k=0;k<GraphicsCallProvenance::Kinds;++k){const auto c=recorder.CountsFor(k);if(k)out<<',';
        out<<"{\"kind\":"<<k<<",\"calls\":"<<c.calls<<",\"foreign\":"<<c.foreign<<",\"boundary_overlap\":"<<c.overlap<<",\"omitted_records\":"<<c.omitted<<'}';}
    out<<"]}";
}
inline void Flush(const std::filesystem::path& path){
    // Reporting worker only; each immutable entry/return is written once.
    // Maximum 1 header + 112 entry + 112 return rows; no periodic full dumps.
    static bool header=false;static std::array<bool,GraphicsCallProvenance::Rows> entries{},exits{};
    if(!configured)return;std::ofstream out;
    const auto open=[&]{if(!out.is_open())out.open(path,std::ios::app);return bool(out);};
    if(!header){if(!open())return;out<<"{\"event\":\"bootstrap\",\"value\":";Summary(out);out<<"}\n";out.flush();if(!out)return;header=true;}
    for(unsigned i=0;i<GraphicsCallProvenance::Rows;++i){
        if(!entries[i])if(const auto r=recorder.Entry(i)){if(!open())return;
            out<<"{\"event\":\"enter\",\"slot\":"<<i<<",\"kind\":"<<r->kind<<",\"thread\":"<<r->thread<<",\"bootstrap_thread\":"<<r->ownerThread
               <<",\"overlap_thread\":"<<r->overlapThread<<",\"bucket\":"<<r->bucket<<",\"device\":"<<r->device<<",\"context\":"<<r->context
               <<",\"chain\":"<<r->chain<<",\"caller\":"<<r->caller<<",\"ordinal\":"<<r->ordinal<<",\"begin_qpc\":"<<r->beginQpc<<"}\n";
            out.flush();if(!out)return;entries[i]=true;}
        if(entries[i]&&!exits[i])if(const auto r=recorder.Exit(i)){if(!open())return;
            out<<"{\"event\":\"return\",\"slot\":"<<i<<",\"end_qpc\":"<<r->endQpc<<",\"result_known\":"<<r->resultKnown<<",\"result\":"<<r->result<<"}\n";
            out.flush();if(!out)return;exits[i]=true;}
    }
}
}
