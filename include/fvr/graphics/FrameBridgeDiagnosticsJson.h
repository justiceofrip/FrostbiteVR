#pragma once
#include "fvr/graphics/FrameBridgeDiagnostics.h"
#include <ostream>

namespace fvr::graphics {
// Called only when writing the final report, from an owned journal snapshot.
inline void WriteFrameBridgeTiming(std::ostream& out,const FrameBridgeTimingSnapshot& s) {
    out<<"{\"schema\":\"fvr.native-frame-timing.v1\",\"clock\":\"qpc_ticks\",\"duration_kind\":\"cpu_observed_boundaries_not_gpu_execution\""
       <<",\"frequency\":"<<s.frequency<<",\"available\":"<<(s.available?"true":"false")
       <<",\"count\":"<<s.count<<",\"completed\":"<<s.completed<<",\"overwritten\":"<<s.overwritten
       <<",\"dropped\":"<<s.dropped<<",\"unmatched\":"<<s.unmatched<<",\"invalid_clock\":"<<s.invalidClock;
    const auto row=[&](const FrameBridgeTimingRow& r){
        const auto& k=r.key;const auto& t=r.ticket;
        out<<"{\"channel\":"<<k.channel<<",\"request\":"<<k.request<<",\"owner\":"<<k.owner
           <<",\"frame\":"<<k.frame<<",\"device\":"<<k.device<<",\"tracking\":"<<k.tracking
           <<",\"space\":"<<k.space<<",\"predicted_ns\":"<<k.predictedNs
           <<",\"ticket\":{\"resource_epoch\":"<<t.resourceEpoch<<",\"sequence\":"<<t.sequence
           <<",\"frame\":"<<t.frameId<<",\"tracking\":"<<t.trackingGeneration<<",\"space\":"<<t.spaceGeneration
           <<",\"predicted_ns\":"<<t.predictedNs<<",\"session\":[";
        for(unsigned i=0;i<t.session.size();++i){if(i)out<<',';out<<unsigned(t.session[i]);}
        out<<"]},\"width\":"<<r.width<<",\"height\":"<<r.height<<",\"sharing_mode\":"<<r.sharingMode
           <<",\"end\":\""<<FrameBridgeTimingEndNames[unsigned(r.end)]<<"\",\"qpc\":{";
        for(unsigned i=0;i<r.qpc.size();++i){if(i)out<<',';out<<'"'<<FrameBridgeTimingStageNames[i]<<"\":"<<r.qpc[i];}
        out<<"},\"depth_samples\":"<<r.depthSamples<<",\"depth_quality\":"<<r.depthQuality
           <<",\"depth_format\":"<<r.depthFormat<<",\"depth_mask\":"<<r.depthMask<<",\"depth_queued_qpc\":[";
        for(unsigned i=0;i<r.depthQueuedQpc.size();++i){if(i)out<<',';out<<r.depthQueuedQpc[i];}
        out<<"],\"export_attempts\":"<<r.exportAttempts<<",\"export_busy\":"<<r.exportBusy
           <<",\"last_export_busy_qpc\":"<<r.lastExportBusyQpc<<",\"ack_observed_qpc\":"<<r.ackObservedQpc
           <<",\"graphics_release_qpc\":"<<r.qpc[unsigned(FrameBridgeTimingStage::Released)]<<'}';
    };
    out<<",\"rows\":[";for(unsigned i=0;i<s.count;++i){if(i)out<<',';row(s.rows[i]);}
    out<<"],\"active\":";if(s.hasActive)row(s.active);else out<<"null";
    out<<",\"retiring\":";if(s.hasRetiring)row(s.retiring);else out<<"null";
    out<<",\"first_non_consumed\":";if(s.hasFirstNonConsumed)row(s.firstNonConsumed);else out<<"null";out<<'}';
}
}
