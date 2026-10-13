#pragma once
#include "Bc2SceneMilestones.h"
#include <filesystem>
#include <ostream>
#include <sstream>

namespace fvr::bc2::sceneMilestones {
inline const char* KindName(SceneMilestoneKind kind) noexcept {
    switch(kind){
    case SceneMilestoneKind::Rebuild:return "rebuild";
    case SceneMilestoneKind::WorldUpdate:return "world_update";
    case SceneMilestoneKind::GameplayUpdate:return "gameplay_update";
    case SceneMilestoneKind::LocalGameplayUpdate:return "local_gameplay_update";
    case SceneMilestoneKind::MainVisibility:return "main_visibility";
    case SceneMilestoneKind::EyeVisibility:return "eye_visibility";
    case SceneMilestoneKind::MainDraw:return "main_draw";
    case SceneMilestoneKind::EyeDraw:return "eye_draw";
    case SceneMilestoneKind::FinishTrackedFrame:return "finish_tracked_frame";
    case SceneMilestoneKind::OwnerTransition:return "owner_transition";
    }
    return "unknown";
}
inline const char* StageName(SceneMilestoneStage stage) noexcept {
    switch(stage){case SceneMilestoneStage::Enter:return "enter";
    case SceneMilestoneStage::CallingOriginal:return "calling_original";
    case SceneMilestoneStage::OriginalReturned:return "original_returned";
    case SceneMilestoneStage::Exit:return "exit";}
    return "unknown";
}
inline void WriteRow(std::ostream& out,const SceneMilestoneRow& r){
    const auto& s=r.scene;const auto& c=r.context;
    out<<"{\"type\":\"milestone\",\"id\":"<<r.id<<",\"epoch\":"<<r.epoch
       <<",\"invocation\":"<<r.invocation<<",\"kind\":\""<<KindName(r.kind)<<"\",\"stage\":\""<<StageName(r.stage)
       <<"\",\"qpc\":"<<r.stamp.qpc<<",\"tick_ms\":"<<r.stamp.tickMs<<",\"thread\":"<<r.stamp.thread
       <<",\"world\":"<<s.world<<",\"request\":"<<s.request<<",\"main\":"<<s.main<<",\"eye\":"<<s.eye
       <<",\"rebuild_frame\":"<<s.frame<<",\"self\":"<<c.self<<",\"player\":"<<c.player
       <<",\"soldier\":"<<c.soldier<<",\"weak\":"<<c.weak<<",\"controlled\":"<<c.controlled
       <<",\"manager\":"<<c.manager<<",\"entry\":"<<c.entry<<",\"actor_generation\":"<<c.actorGeneration
       <<",\"input_epoch\":"<<c.inputEpoch<<",\"input_thread\":"<<c.inputThread<<",\"outcome\":"<<c.outcome<<"}\n";
}

// Construct and call only on the existing probe reporting thread. Hooks never
// touch this object or its file. Each nonempty batch has an OS flush boundary.
class Writer {
    HANDLE file_=INVALID_HANDLE_VALUE;
    std::array<bool,SceneMilestones<>::Capacity> emitted_{};
    std::uint32_t emittedCount_=0,error_=0;
    std::uint64_t lastFlushMs_=0;
public:
    explicit Writer(const std::filesystem::path& path){
        file_=CreateFileW(path.c_str(),GENERIC_WRITE,FILE_SHARE_READ|FILE_SHARE_WRITE,nullptr,
            CREATE_ALWAYS,FILE_ATTRIBUTE_NORMAL,nullptr);
        if(file_==INVALID_HANDLE_VALUE)error_=GetLastError();
    }
    Writer(const Writer&)=delete;
    Writer& operator=(const Writer&)=delete;
    ~Writer(){if(file_!=INVALID_HANDLE_VALUE)CloseHandle(file_);}
    void Status(std::ostream& out) const {
        out<<"{\"file\":\"scene-milestones.jsonl\",\"flushed_rows\":"<<emittedCount_
           <<",\"reserved_rows\":"<<journal.Reserved()<<",\"capacity\":"<<SceneMilestones<>::Capacity
           <<",\"overflow\":"<<journal.Overflow()<<",\"contention\":"<<journal.Contention()
           <<",\"epoch_overflow\":"<<journal.EpochOverflow()<<",\"write_error\":"<<error_<<'}';
    }
    void Flush(){
        if(error_ || file_==INVALID_HANDLE_VALUE)return;
        const auto now=GetTickCount64();const auto reserved=journal.Reserved();
        std::ostringstream batch;std::array<bool,SceneMilestones<>::Capacity> selected{};
        unsigned count=0;
        // Inspect every reserved slot: an interrupted producer cannot hide
        // completed later rows. Its original slot remains eligible next time.
        for(std::uint32_t i=0;i<reserved;++i){SceneMilestoneRow row{};
            if(!emitted_[i]&&journal.Read(i,row)){WriteRow(batch,row);selected[i]=true;++count;}}
        if(!count && now-lastFlushMs_<1000)return;
        LARGE_INTEGER frequency{};QueryPerformanceFrequency(&frequency);
        batch<<"{\"type\":\"flush\",\"tick_ms\":"<<now<<",\"qpc_frequency\":"<<frequency.QuadPart
             <<",\"reserved_snapshot\":"<<reserved<<",\"rows_through_batch\":"<<(emittedCount_+count)<<",\"status\":";
        Status(batch);batch<<"}\n";
        const auto bytes=batch.str();DWORD written=0;
        if(!WriteFile(file_,bytes.data(),static_cast<DWORD>(bytes.size()),&written,nullptr))error_=GetLastError();
        else if(written!=bytes.size())error_=ERROR_WRITE_FAULT;
        else if(!FlushFileBuffers(file_))error_=GetLastError();
        if(error_)return; // A partial write is never retried as a valid JSON batch.
        for(std::uint32_t i=0;i<reserved;++i)if(selected[i])emitted_[i]=true;
        emittedCount_+=count;lastFlushMs_=now;
    }
};
} // namespace fvr::bc2::sceneMilestones
