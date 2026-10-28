#pragma once
#include <cstdint>
#include <ostream>

namespace fvr::xr {
// Observations only. Neither a composed image nor successful xrEndFrame is a
// source-copy completion receipt; only D3D11PairConsumer::PollCopy grants that.
struct PrivateGpuOrderedPresentReport {
    bool configured=false,available=false;
    std::uint64_t composed=0,postEndPolls=0,postEndCompletions=0,preWaitRetirements=0;
    std::uint64_t postInputPolls=0,postInputRetirements=0;
    bool drainAttempted=false,drained=false,retainedUntilProcessExit=false;
    std::uint32_t drainError=0;
    std::uint64_t drainPolls=0,drainElapsedNs=0;
    void Write(std::ostream& out)const {
        out<<"{\"schema\":\"fvr.gpu-ordered-present.v1\",\"configured\":"<<(configured?"true":"false")
           <<",\"available\":"<<(available?"true":"false")<<",\"composed\":"<<composed
           <<",\"post_end_polls\":"<<postEndPolls<<",\"post_end_completions\":"<<postEndCompletions
           <<",\"pre_wait_retirements\":"<<preWaitRetirements
           <<",\"post_input_polls\":"<<postInputPolls<<",\"post_input_retirements\":"<<postInputRetirements
           <<",\"copy_completion_means\":\"actual_source_query_success\""
           <<",\"composition_means\":\"both_eye_commands_submitted_and_images_released\""
           <<",\"drain_attempted\":"<<(drainAttempted?"true":"false")
           <<",\"drained\":"<<(drained?"true":"false")<<",\"drain_error\":"<<drainError
           <<",\"drain_polls\":"<<drainPolls<<",\"drain_elapsed_ns\":"<<drainElapsedNs
           <<",\"retained_until_process_exit\":"<<(retainedUntilProcessExit?"true":"false")<<'}';
    }
};
}
