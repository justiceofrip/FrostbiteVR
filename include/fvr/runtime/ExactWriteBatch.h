#pragma once
#include <array>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <span>
namespace fvr::runtime {
// Caller establishes address validity and exclusive access for the whole scope.
// No allocation, partial application, or restoration over newer owner writes.
template<std::size_t Capacity,std::size_t MaxBytes=64> class ExactWriteBatch {
    struct Entry {std::byte* address=nullptr;std::size_t size=0;std::array<std::byte,MaxBytes> before{},written{};};
    std::array<Entry,Capacity> entries_{};std::size_t count_=0;bool active_=false,finished_=false;
public:
    ExactWriteBatch()=default;ExactWriteBatch(const ExactWriteBatch&)=delete;ExactWriteBatch& operator=(const ExactWriteBatch&)=delete;
    ~ExactWriteBatch(){Restore();}
    bool Stage(std::span<std::byte> destination,std::span<const std::byte> expected,std::span<const std::byte> replacement)noexcept {
        if(active_||finished_||count_==Capacity||destination.empty()||!destination.data()||destination.size()>MaxBytes||
           expected.size()!=destination.size()||replacement.size()!=destination.size())return false;
        const auto begin=reinterpret_cast<std::uintptr_t>(destination.data()),size=destination.size();
        if(begin>UINTPTR_MAX-size)return false;
        for(std::size_t i=0;i<count_;++i){const auto other=reinterpret_cast<std::uintptr_t>(entries_[i].address);
            if(begin<other+entries_[i].size&&other<begin+size)return false;}
        if(std::memcmp(destination.data(),expected.data(),size))return false;
        auto& entry=entries_[count_++];entry.address=destination.data();entry.size=size;
        std::memcpy(entry.before.data(),expected.data(),size);std::memcpy(entry.written.data(),replacement.data(),size);return true;
    }
    bool Apply()noexcept {
        if(active_||finished_||!count_)return false;
        for(std::size_t i=0;i<count_;++i)if(std::memcmp(entries_[i].address,entries_[i].before.data(),entries_[i].size))return false;
        for(std::size_t i=0;i<count_;++i)std::memcpy(entries_[i].address,entries_[i].written.data(),entries_[i].size);
        active_=true;return true;
    }
    bool Restore()noexcept {
        if(!active_)return true;bool exact=true;
        for(std::size_t i=0;i<count_;++i){auto& entry=entries_[i];
            if(std::memcmp(entry.address,entry.written.data(),entry.size))exact=false;
            else std::memcpy(entry.address,entry.before.data(),entry.size);}
        active_=false;finished_=true;return exact;
    }
};
}
