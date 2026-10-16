#pragma once
#include <cstdint>
#include <cmath>
#include <optional>
namespace fvr::bc2 {
// Diagnostic observation only. No native consumer can accept this as authority.
struct PumpReceiverGapWitness {
    std::uint64_t space=0,cycle=0,shot=0,input=0;
    std::int64_t observedNs=0,deadlineNs=0;
    int loaded=0,reserve=0,capacity=0;float travel=0;
};
inline bool ValidPumpReceiverGapWitness(const PumpReceiverGapWitness& w,std::int64_t now)noexcept {
    return w.space&&w.space<UINT64_MAX&&w.cycle&&w.shot&&w.input&&w.observedNs>0&&
        w.observedNs<=now&&now<w.deadlineNs&&w.deadlineNs-w.observedNs<=100000000&&
        w.capacity>0&&w.loaded>=0&&w.loaded<w.capacity&&w.reserve>=0&&std::isfinite(w.travel)&&w.travel>=.03f&&w.travel<=.055f;
}
// Receiver-only scheduling. Withhold all input publications AND new tracking
// requests, keeping the host connection and native observers alive. An extra
// 100 ms covers the last packet's original maximum lease; stale time is >=5 s.
class PumpReceiverGapProbe {
public:
    static constexpr std::int64_t MissingNs=5000000000ll,OriginalLeaseNs=100000000;
    bool Begin(const PumpReceiverGapWitness& w,std::int64_t now)noexcept {
        if(began_||failed_||!ValidPumpReceiverGapWitness(w,now)||now>INT64_MAX-MissingNs-OriginalLeaseNs)return false;
        witness_=w;began_=lastNow_=now;resumeAt_=now+MissingNs+OriginalLeaseNs;return true;
    }
    bool Withhold(std::int64_t now)noexcept {
        if(!began_||returned_)return false;
        if(now<lastNow_){failed_=true;return true;}lastNow_=now;
        if(now<resumeAt_)return true;returned_=now;return false;
    }
    bool Published(std::uint64_t sequence,std::uint64_t space,std::int64_t now,bool neutral)noexcept {
        if(!began_)return true;
        if(!returned_||failed_||now<returned_||space!=witness_.space+1||sequence<=witness_.input||!neutral){failed_=true;return false;}
        if(!firstReturnInput_){firstReturnInput_=sequence;firstReturnNs_=now;}return true;
    }
    bool Completed()const noexcept{return began_&&returned_&&firstReturnInput_&&!failed_;}
    bool Began()const noexcept{return began_!=0;}
    bool Returned()const noexcept{return returned_!=0;}
    bool Failed()const noexcept{return failed_;}
    std::uint64_t Space(std::uint64_t old)const noexcept{return returned_?witness_.space+1:old;}
    const PumpReceiverGapWitness& Witness()const noexcept{return witness_;}
    std::int64_t BeganNs()const noexcept{return began_;}
    std::int64_t ReturnedNs()const noexcept{return returned_;}
    std::int64_t FirstReturnNs()const noexcept{return firstReturnNs_;}
    std::uint64_t FirstReturnInput()const noexcept{return firstReturnInput_;}
private:
    PumpReceiverGapWitness witness_{};bool failed_=false;
    std::int64_t began_=0,resumeAt_=0,lastNow_=0,returned_=0,firstReturnNs_=0;
    std::uint64_t firstReturnInput_=0;
};
}
