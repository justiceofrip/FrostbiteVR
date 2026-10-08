#pragma once
#include <cstdint>
#include <limits>
namespace fvr::bc2 {
struct WorkPoolHeader {std::uint32_t begin=0,end=0,capacity=0;bool operator==(const WorkPoolHeader&)const=default;};
// Caller owns the render thread and backing storage until native reset empties it.
class WorkPoolLease {
public:
    bool Expand(WorkPoolHeader& live,std::uint32_t expectedBegin,std::uint32_t originalBytes,std::uint32_t replacement,std::uint32_t replacementBytes) noexcept {
        if(active_||!expectedBegin||!replacement||live.begin!=expectedBegin||live.end!=expectedBegin||
           expectedBegin>UINT32_MAX-originalBytes||live.capacity!=expectedBegin+originalBytes||replacementBytes<=originalBytes||
           originalBytes%40||replacementBytes%40||replacement%16||replacement>UINT32_MAX-replacementBytes)return false;
        saved_=live;written_={replacement,replacement,replacement+replacementBytes};live=written_;active_=true;return true;
    }
    bool RestoreEmpty(WorkPoolHeader& live) noexcept {
        if(!active_||live.begin!=written_.begin||live.capacity!=written_.capacity||live.end!=live.begin)return false;
        live=saved_;active_=false;return true;
    }
    bool Active()const noexcept{return active_;}
private:WorkPoolHeader saved_{},written_{};bool active_=false;
};
}
