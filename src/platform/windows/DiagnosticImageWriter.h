#pragma once
#include <array>
#include <condition_variable>
#include <cstdint>
#include <deque>
#include <filesystem>
#include <fstream>
#include <functional>
#include <memory>
#include <mutex>
#include <thread>
#include <vector>

namespace fvr::probe {
// The diagnostic input loop must not wait for full-resolution file writes.
// Four queued pairs plus one being written bound retained pixel memory.
class DiagnosticImageWriter {
public:
    using Pixels=std::array<std::vector<unsigned char>,2>;
    struct Capture {std::uint64_t pair=0;std::shared_ptr<const Pixels> pixels;};
    struct Counts {unsigned queued=0,written=0,rejected=0,failed=0;};
    using Write=std::function<bool(const Capture&)>;
    explicit DiagnosticImageWriter(std::filesystem::path folder,Write write={})
        :folder_(std::move(folder)),write_(std::move(write)),worker_([this]{Run();}){}
    ~DiagnosticImageWriter(){Finish();}
    DiagnosticImageWriter(const DiagnosticImageWriter&)=delete;
    DiagnosticImageWriter& operator=(const DiagnosticImageWriter&)=delete;
    bool Queue(std::uint64_t pair,std::shared_ptr<const Pixels> pixels){
        std::lock_guard lock(mutex_);
        if(closing_||pending_.size()>=4||!pixels||(*pixels)[0].empty()||(*pixels)[1].empty()){
            ++counts_.rejected;return false;
        }
        pending_.push_back({pair,std::move(pixels)});++counts_.queued;changed_.notify_one();return true;
    }
    // Call after controller publication ends. The worker never references the
    // graphics context, native game, IPC provider or producer-owned buffers.
    Counts Finish(){
        {std::lock_guard lock(mutex_);closing_=true;changed_.notify_one();}
        if(worker_.joinable())worker_.join();
        std::lock_guard lock(mutex_);return counts_;
    }
private:
    bool Save(const Capture& capture){
        for(unsigned eye=0;eye<2;++eye){
            std::ofstream file(folder_/("pair-"+std::to_string(capture.pair)+"-eye-"+std::to_string(eye)+".rgba"),std::ios::binary);
            const auto& bytes=(*capture.pixels)[eye];
            file.write(reinterpret_cast<const char*>(bytes.data()),std::streamsize(bytes.size()));file.close();
            if(!file)return false;
        }
        return true;
    }
    void Run(){
        for(;;){
            Capture capture;
            {std::unique_lock lock(mutex_);changed_.wait(lock,[this]{return closing_||!pending_.empty();});
                if(pending_.empty())return;
                capture=std::move(pending_.front());pending_.pop_front();}
            bool okay=false;try{okay=write_?write_(capture):Save(capture);}catch(...){okay=false;}
            {std::lock_guard lock(mutex_);if(okay)++counts_.written;else ++counts_.failed;}
        }
    }
    std::filesystem::path folder_;Write write_;
    std::mutex mutex_;std::condition_variable changed_;std::deque<Capture> pending_;
    Counts counts_{};bool closing_=false;std::thread worker_;
};
}
