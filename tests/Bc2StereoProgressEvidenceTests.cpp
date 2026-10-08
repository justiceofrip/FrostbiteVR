#include "Bc2StereoProgressEvidence.h"
#include <array>
#include <cstdlib>
#include <iostream>
#include <sstream>
#include <thread>
#include <vector>
using namespace fvr::bc2;
void Check(bool ok){if(!ok){std::cerr<<"stereo progress assertion failed\n";std::abort();}}
StereoProgressSample Sample(){StereoProgressSample s;s.world=s.expectedWorld=1000;
    s.request=s.expectedRequest=2000;s.eye=3000;s.expectedEyeType=4000;return s;}
void GuardShortCircuit(){
    const std::array<std::uintptr_t,4> addresses{2008,2196,3000,3112};
    const std::array<std::uint32_t,4> valid{1000,3,4000,2000};
    for(unsigned failure=0;failure<7;++failure){
        auto s=Sample();auto values=valid;unsigned reads=0;
        if(failure==0)++s.world;else if(failure==1)++s.request;
        else if(failure<6)++values[failure-2];
        const auto reason=CheckStereoProgressOwner(s,[&](std::uintptr_t at){
            Check(reads<4&&at==addresses[reads]);return values[reads++];
        });
        const auto expected=failure==6?StereoProgressReason::OwnerMatched:
            StereoProgressReason(unsigned(StereoProgressReason::WorldChanged)+failure);
        Check(reason==expected);
        const unsigned expectedReads=failure<2?0:(failure==6?4:failure-1);
        Check(reads==expectedReads&&s.checked==((1u<<expectedReads)-1));
    }
}
void OriginalGuardEquivalence(){
    // Simultaneous corrupt operands still stop at the same first predicate/read.
    for(unsigned mask=0;mask<64;++mask){
        auto a=Sample(),b=a;
        if(mask&1)++a.world;if(mask&2)++a.request;b=a;
        std::array<std::uint32_t,4> values{1000u+((mask>>2)&1),3u+((mask>>3)&1),
            4000u+((mask>>4)&1),2000u+((mask>>5)&1)};
        unsigned oldReads=0,newReads=0;
        const auto oldRead=[&](std::uintptr_t){return values[oldReads++];};
        const bool originalRejected=a.world!=a.expectedWorld||a.request!=a.expectedRequest||
            oldRead(a.request+8)!=a.world||oldRead(a.request+0xc4)!=3||
            oldRead(a.eye)!=a.expectedEyeType||oldRead(a.eye+0x70)!=a.request;
        const auto reason=CheckStereoProgressOwner(b,[&](std::uintptr_t){return values[newReads++];});
        Check(originalRejected==(reason!=StereoProgressReason::OwnerMatched)&&oldReads==newReads);
    }
}
void PersistentCounts(){
    StereoProgressEvidence evidence;auto s=Sample();s.tickMs=1;
    evidence.Record(StereoProgressReason::BridgeNoLease,s);
    for(unsigned i=0;i<50000;++i){s.tickMs=60000+i;s.incomingFrame=i;
        evidence.Record(StereoProgressReason::BridgeNoLease,s);}
    const auto snap=evidence.Read();const auto i=unsigned(StereoProgressReason::BridgeNoLease);
    Check(snap.samplesAvailable&&snap.counts[i]==50001&&!snap.droppedSamples);
    Check(snap.rows[i].first.tickMs==1&&snap.rows[i].last.tickMs==109999);
    std::ostringstream out;StereoProgressEvidence::WriteJson(out,snap);
    Check(out.str().find("\"reason\":\"bridge_no_lease\",\"count\":50001")!=std::string::npos);
    Check(out.str().find("\"checked\":0")!=std::string::npos);
}
void ConcurrentSnapshot(){
    StereoProgressEvidence evidence;std::atomic<bool> begin=false;std::atomic<unsigned> done=0;
    std::vector<std::thread> writers;
    for(unsigned t=0;t<4;++t)writers.emplace_back([&,t]{
        while(!begin.load(std::memory_order_acquire))std::this_thread::yield();
        for(unsigned n=0;n<20000;++n){auto s=Sample();s.world=100000*t+n;s.request=s.world^0x55aa;
            evidence.Record(StereoProgressReason::WorldChanged,s);}
        done.fetch_add(1,std::memory_order_release);
    });
    begin.store(true,std::memory_order_release);
    do {
        const auto snap=evidence.Read();const auto& row=snap.rows[unsigned(StereoProgressReason::WorldChanged)];
        if(snap.samplesAvailable&&row.present){Check(row.first.request==(row.first.world^0x55aa));
            Check(row.last.request==(row.last.world^0x55aa));}
    }while(done.load(std::memory_order_acquire)!=4);
    for(auto& thread:writers)thread.join();
    const auto snap=evidence.Read();Check(snap.samplesAvailable);
    Check(snap.counts[unsigned(StereoProgressReason::WorldChanged)]==80000);
}
int main(){GuardShortCircuit();OriginalGuardEquivalence();PersistentCounts();ConcurrentSnapshot();
    std::cout<<"4 stereo progress groups passed\n";}
