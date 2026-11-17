#pragma once
#include "Bc2NativeCycleService.h"
#include "Bc2ManualPumpConfig.h"
#include <cstring>

namespace fvr::bc2 {
// This is the runtime's source predicate, exercised directly by offline tests.
// Native reads remain lazy and retain their original single evaluation order.
template<class Isolated,class Current,class Config>
bool EvaluateNativeCycleRecoverySource(NativeCycleRecoverySourceDiagnostic& d,bool lease,bool capacity,
    bool holdCode,bool contextCopied,Isolated isolated,Current current,Config config)noexcept {
    using P=NativeCycleSourcePredicate;
    return d.Test(P::Lease,lease)&&d.Test(P::Capacity,capacity)&&d.Test(P::Isolated,isolated())&&
        d.Test(P::HoldCode,holdCode)&&d.Test(P::Caller,d.caller==d.expectedCaller)&&
        d.Test(P::StackLow,d.context>=d.stackLow)&&d.Test(P::StackHigh,std::uint64_t(d.context)+0x30<=d.stackHigh)&&
        d.Test(P::Alignment,!(d.context&3))&&d.Test(P::ContextCopied,contextCopied)&&
        d.Test(P::Current,current())&&d.Test(P::ManualConfig,config());
}
inline bool ReadNativeCycleRecoveryConfig(const ReloadStateMemory& memory,const ReloadObservedConfig& config,
    bool bolt,NativeCycleRecoverySourceDiagnostic& diagnostic)noexcept {
    struct Capture {const ReloadStateMemory& memory;NativeCycleRecoverySourceDiagnostic& diagnostic;unsigned next=0;};
    Capture capture{memory,diagnostic};diagnostic.memoryReadAvailable=memory.read!=nullptr;
    ReloadStateMemory observed{&capture};
    if(memory.read)observed.read=[](void* context,std::uint32_t address,void* output,std::size_t size)noexcept {
        auto& c=*static_cast<Capture*>(context);const auto index=c.next++;
        const bool result=c.memory.read(c.memory.context,address,output,size);
        if(index<c.diagnostic.configWords.size()){
            c.diagnostic.configAttempted|=1u<<index;
            if(result){c.diagnostic.configRead|=1u<<index;
                std::memcpy(&c.diagnostic.configWords[index],output,std::min(size,sizeof(std::uint32_t)));}
        }return result;
    };
    return bolt?ReadManualBoltConfig(observed,config):ReadManualPumpConfig(observed,config);
}
// Caller prepares both applicable native proof sets outside the policy gate,
// then holds ONE service gate across route selection and decision reservation.
// No engine read, callback, original call or context write belongs in this API.
struct Bc2NativeCycleUpdateEvidence {
    const ReloadHoldInput* normal=nullptr;
    const NativeCycleRecoveryScope* recovery=nullptr;
    const ReloadFlowEventInput* entry=nullptr;
    std::uint32_t firing=0;
    std::uint64_t invocation=0;
    int capacity=0;
    std::int64_t deadlineNs=0;
    const NativeCycleRecoverySourceDiagnostic* sourceDiagnostic=nullptr;
    const NativeCycleRecoveryScope* observedScope=nullptr; // Diagnostic only; may have failed source admission.
};
struct Bc2NativeCycleUpdateRoute {
    bool recovering=false;
    Bc2NativeCycleDecision normal{};
    NativeCycleRecoveryDecision recovery{};
};
inline Bc2NativeCycleUpdateRoute EvaluateNativeCycleUpdateRoute(Bc2NativeCycleService& service,
    const Bc2NativeCycleUpdateEvidence& proof)noexcept {
    // Admit conversion before Evaluate opens an unheld Update. Preparing both
    // proof sets does not choose a route: conversion and this callback's guard
    // reservation share the same service gate. A controller action is not a
    // native mutation failure, and it cannot forgive the still-owed stroke.
    if(!service.Recovering()&&proof.normal&&proof.recovery&&proof.entry&&
       proof.recovery->identity==proof.normal->identity&&proof.recovery->config==proof.normal->config&&
       proof.entry->nativeInvocation==proof.invocation&&proof.entry->boundary.firing==proof.firing&&
       proof.entry->boundary.branch==proof.normal->branch)
        service.BeginInputRecovery(*proof.normal,proof.invocation);
    Bc2NativeCycleUpdateRoute out;out.recovering=service.Recovering();
    if(out.recovering){
        if(proof.recovery&&proof.entry)
            out.recovery=service.EvaluateRecovery(*proof.recovery,*proof.entry,proof.capacity,proof.deadlineNs,proof.sourceDiagnostic);
        else service.MissingRecovery(proof.firing,proof.invocation,proof.recovery?
            NativeCycleAdmissionReason::MissingRecoveryEntry:NativeCycleAdmissionReason::MissingRecoverySource,
            proof.sourceDiagnostic,proof.observedScope?proof.observedScope:proof.recovery,proof.capacity,proof.deadlineNs);
    }else {
        if(proof.normal)out.normal=service.Evaluate(*proof.normal,proof.invocation);
        else service.Missing(proof.firing,proof.invocation);
    }
    return out;
}
}
