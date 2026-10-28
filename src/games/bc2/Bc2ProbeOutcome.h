#pragma once
#include <cstdint>
#include <ostream>
namespace fvr::bc2 {
// A signaled target can kill its remote thread before the probe returns. Do not
// call that a successful probe return, nor infer why the game was closed.
struct NativeProbeRunOutcome {
    bool gameExited=false,gameExitCodeKnown=false;
    std::uint32_t gameExitCode=0,remoteThreadExitCode=0;
    bool reportBytesKnown=false;
    std::uint64_t reportBytes=0;
};
inline void WriteNativeProbeRunOutcomeJson(std::ostream& out,std::uint32_t pid,
    const NativeProbeRunOutcome& value) {
    out<<"{\"schema\":\"bc2vr.native-run-outcome.v1\",\"pid\":"<<pid
        <<",\"outcome\":\""<<(value.gameExited?"game_exited":"remote_thread_exited")
        <<"\",\"remote_thread_exit_observed\":"<<(value.gameExited?"false":"true")<<",\"probe_exit\":";
    if(value.gameExited)out<<"null";else out<<value.remoteThreadExitCode;
    out<<",\"game_exit_code\":";
    if(value.gameExited&&value.gameExitCodeKnown)out<<value.gameExitCode;else out<<"null";
    out<<",\"report_bytes\":";
    if(value.reportBytesKnown)out<<value.reportBytes;else out<<"null";
    out<<",\"report_nonempty\":";
    if(value.reportBytesKnown)out<<(value.reportBytes?"true":"false");else out<<"null";
    // A nonempty file is not proof of valid JSON or completed hook cleanup.
    out<<",\"module_retained_until_game_exit\":true,\"game_files_modified\":false}\n";
}
}
