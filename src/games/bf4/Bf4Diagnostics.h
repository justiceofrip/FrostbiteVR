#pragma once
// Developer-session diagnostics for the live game (Windows, in-process, read-only):
//  - a watchdog that notices when the game stops presenting frames and records where every
//    thread of the process is (unwound stacks as module+offset), so a hang can be traced to
//    the code that waits;
//  - an exception logger, armed for a feature under test, that records the faulting address
//    and stack of access violations and similar faults before the game's own handler runs.
// Nothing here changes game state: threads are suspended one at a time only to read their
// registers, and the exception logger always passes the exception on.
#include <cstdint>
#include <functional>
#include <string>

namespace fvr::bf4 {

class Diagnostics {
public:
    // `sink` receives whole lines (called on the watchdog thread, never while a thread is
    // suspended). `faultFile` gets exception records (written without the C runtime).
    static void Start(std::function<void(const std::string&)> sink, const std::string& faultFile);
    static void FramePresented();          // every Present
    static void ArmFaultLogging(bool armed); // e.g. while a risky feature is on
    // "foreground=<process> class=<class> own=<class>[(minimized)]...": who is in front and the
    // state of this process's visible top-level windows (stall reports, focus changes).
    static std::string DescribeForeground();
};

} // namespace fvr::bf4
