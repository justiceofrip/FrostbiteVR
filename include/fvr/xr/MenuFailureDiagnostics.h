#pragma once
#include "fvr/ipc/MenuChannel.h"
#include <ostream>

namespace fvr::xr {
// One value snapshot, retained for the presenter's lifetime. Times are host QPC,
// not GPU execution time. Stage/domain names point only to static literals.
struct OpenXrMenuFailure {
    bool available=false,errorCounted=false;
    const char* stage="none";const char* resultDomain="none";
    std::int64_t result=0,transferResult=-1,qpc=0,qpcFrequency=0,stageStartedQpc=0;
    std::uint64_t inputGeneration=0,inputSpace=0;std::int64_t inputPredictedNs=0;
    bool focused=false,headValid=false,stateKnown=false,active=false,hasSurface=false;
    ipc::ChannelResult stateRead=ipc::ChannelResult::Closed;
    ipc::MenuState state{};ipc::MenuSurface surface{};
    std::uint64_t swapchain=0;unsigned width=0,height=0,textureFormat=0;
    std::int64_t format=0;
    void Write(std::ostream& out)const {
        if(!available){out<<"null";return;}
        const auto boolean=[](bool value){return value?"true":"false";};
        out<<"{\"schema\":\"fvr.menu-first-failure.v1\",\"stage\":\""<<stage<<"\",\"result_domain\":\""<<resultDomain
           <<"\",\"result\":"<<result<<",\"transfer_result\":"<<transferResult<<",\"error_counted\":"<<boolean(errorCounted)
           <<",\"clock\":\"qpc\",\"elapsed_semantics\":\"host_stage_elapsed_not_gpu_time\",\"qpc\":"<<qpc
           <<",\"qpc_frequency\":"<<qpcFrequency<<",\"stage_started_qpc\":"<<stageStartedQpc
           <<",\"input_generation\":"<<inputGeneration<<",\"input_space\":"<<inputSpace<<",\"input_predicted_ns\":"<<inputPredictedNs
           <<",\"focused\":"<<boolean(focused)<<",\"head_valid\":"<<boolean(headValid)<<",\"state_known\":"<<boolean(stateKnown)
           <<",\"active\":"<<boolean(active)<<",\"state_read\":"<<std::uint32_t(stateRead)<<",\"state\":";
        const auto writeState=[&](const ipc::MenuState& value){
            out<<"{\"sequence\":"<<value.sequence<<",\"epoch\":"<<value.epoch<<",\"deadline_qpc\":"<<value.deadlineQpc
               <<",\"mode\":"<<std::uint32_t(value.mode)<<",\"logical_width\":"<<value.logicalWidth
               <<",\"logical_height\":"<<value.logicalHeight<<",\"input_ready\":"<<value.inputReady<<'}';
        };
        const auto session=[&](const auto& bytes){out<<'[';bool comma=false;for(const auto value:bytes){if(comma)out<<',';out<<unsigned(value);comma=true;}out<<']';};
        writeState(state);out<<",\"has_surface\":"<<boolean(hasSurface)<<",\"surface\":";
        if(!hasSurface)out<<"null";
        else {
            out<<"{\"state\":";writeState(surface.state);
            const auto& d=surface.descriptor;const auto& t=surface.ticket;
            out<<",\"descriptor\":{\"width\":"<<d.width<<",\"height\":"<<d.height<<",\"format\":"<<d.format
               <<",\"adapter_luid_low\":"<<d.adapterLow<<",\"adapter_luid_high\":"<<d.adapterHigh
               <<",\"resource_epoch\":"<<d.resourceEpoch<<",\"sharing\":"<<std::uint32_t(d.sharing)<<",\"session\":";session(d.session);
            out<<"},\"ticket\":{\"resource_epoch\":"<<t.resourceEpoch<<",\"sequence\":"<<t.sequence<<",\"frame\":"<<t.frameId
               <<",\"tracking\":"<<t.trackingGeneration<<",\"space\":"<<t.spaceGeneration<<",\"predicted_ns\":"<<t.predictedNs
               <<",\"session\":";session(t.session);out<<"}}";
        }
        out<<",\"swapchain\":"<<swapchain<<",\"width\":"<<width<<",\"height\":"<<height<<",\"format\":"<<format
           <<",\"texture_format\":"<<textureFormat<<'}';
    }
};
}
