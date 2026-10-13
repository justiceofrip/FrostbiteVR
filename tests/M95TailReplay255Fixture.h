#pragma once
#include "Bc2ReloadFlowRuntime.h"
namespace m95_tail255 {
// Actual255 trace SHA256: c81fbe9b36390f1c4a88aefb580efccb38b658c3f0c9ca36a8646c07e73a0d92
// Recorder row3472 is native invocation3474; service IDs use native invocations.
inline fvr::bc2::ReloadHoldIdentity Identity(){return {{1008094800,1007517328,125684392,126399328,1,1,8589934593},{111664304,111664480,110982368},409672400,449723376,108408496};}
inline fvr::bc2::ReloadFlowRecord Record(){using namespace fvr::bc2;ReloadFlowRecord r{};
r.id=3474;r.entry.kind=ReloadFlowEvent::Restore;r.entry.nativeInvocation=3474;
r.entry.thread=r.exit.thread=45592;r.entry.depth=1;r.entry.caller=9155640;r.entry.context=1001847344;
r.entry.nowNs=82886093895600ll;r.exit.nowNs=82886093995700ll;r.finished=true;r.identityRetained=true;
r.exit.boundary=ReloadFlowBoundary{};
r.entry.boundary.owner={1008094800,1007517328,125684392,126399328,1,1,8589934593};
r.entry.boundary.snapshotSequence=612;
r.entry.boundary.firing=111664480;
r.entry.boundary.branch=1;
r.entry.boundary.wrapperOffset=64;
r.entry.boundary.soldierFlags=3;
r.entry.boundary.current=2;
r.entry.boundary.previous=1;
r.entry.boundary.next=2;
r.entry.boundary.timer=0.0f;
r.entry.boundary.loaded=4;
r.entry.boundary.reserve=45;
r.entry.boundary.flagsA8=96;
r.exit.boundary->owner={1008094800,1007517328,125684392,126399328,1,1,8589934593};
r.exit.boundary->snapshotSequence=612;
r.exit.boundary->firing=111664480;
r.exit.boundary->branch=1;
r.exit.boundary->wrapperOffset=64;
r.exit.boundary->soldierFlags=3;
r.exit.boundary->current=8;
r.exit.boundary->previous=2;
r.exit.boundary->next=1;
r.exit.boundary->timer=0.0241789f;
r.exit.boundary->loaded=4;
r.exit.boundary->reserve=45;
r.exit.boundary->flagsA8=96;
r.entry.snapshotCopied=true;r.entry.copiedSnapshot={std::byte{8},std::byte{0},std::byte{0},std::byte{0},std::byte{1},std::byte{0},std::byte{0},std::byte{0},std::byte{236},std::byte{18},std::byte{198},std::byte{60},std::byte{0},std::byte{0},std::byte{0},std::byte{0},std::byte{0},std::byte{0},std::byte{0},std::byte{0},std::byte{0},std::byte{0},std::byte{0},std::byte{0},std::byte{4},std::byte{0},std::byte{0},std::byte{0},std::byte{45},std::byte{0},std::byte{0},std::byte{0},std::byte{0},std::byte{0},std::byte{0},std::byte{0},std::byte{0},std::byte{0},std::byte{0},std::byte{0},std::byte{0},std::byte{0},std::byte{0},std::byte{0},std::byte{255},std::byte{255},std::byte{255},std::byte{255},std::byte{0},std::byte{0},std::byte{0},std::byte{0},std::byte{0},std::byte{0},std::byte{0},std::byte{0},std::byte{0},std::byte{1},std::byte{1},std::byte{0},std::byte{0},std::byte{0},std::byte{0},std::byte{0}};
r.exit.snapshotCopied=true;r.exit.copiedSnapshot={std::byte{8},std::byte{0},std::byte{0},std::byte{0},std::byte{1},std::byte{0},std::byte{0},std::byte{0},std::byte{236},std::byte{18},std::byte{198},std::byte{60},std::byte{0},std::byte{0},std::byte{0},std::byte{0},std::byte{0},std::byte{0},std::byte{0},std::byte{0},std::byte{0},std::byte{0},std::byte{0},std::byte{0},std::byte{4},std::byte{0},std::byte{0},std::byte{0},std::byte{45},std::byte{0},std::byte{0},std::byte{0},std::byte{0},std::byte{0},std::byte{0},std::byte{0},std::byte{0},std::byte{0},std::byte{0},std::byte{0},std::byte{0},std::byte{0},std::byte{0},std::byte{0},std::byte{255},std::byte{255},std::byte{255},std::byte{255},std::byte{0},std::byte{0},std::byte{0},std::byte{0},std::byte{0},std::byte{0},std::byte{0},std::byte{0},std::byte{0},std::byte{1},std::byte{1},std::byte{0},std::byte{0},std::byte{0},std::byte{0},std::byte{0}};
// Preserve exact original timer bits instead of the rounded JSON number.
r.exit.boundary->timer=DecodeReloadFiringSnapshot(r.entry.copiedSnapshot)->phaseTimer;
return r;}
}
