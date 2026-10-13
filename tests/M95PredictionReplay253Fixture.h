#pragma once
#include "Bc2ReloadFlowRuntime.h"
namespace m95_replay253 {
// Actual253 trace SHA256: 7aa21a15c626003c380a23155cb5e54a0c787deae71bcdbcb5786ce82f9fc895
// Trace row1048 maps to native invocation1048; IDs below use native invocations.
inline fvr::bc2::ReloadHoldIdentity Identity(){return {{1009193120,1006126816,133245816,126915664,1,1,8589934593},{109173936,109174112,108426464},409672160,449725040,106180272};}
inline fvr::bc2::ReloadFlowRecord Record(){using namespace fvr::bc2;ReloadFlowRecord r{};
r.id=1048;r.entry.kind=ReloadFlowEvent::Restore;r.entry.nativeInvocation=1048;
r.entry.thread=r.exit.thread=34776;r.entry.depth=1;r.entry.caller=9155640;r.entry.context=410893216;
r.entry.nowNs=81416496696500ll;r.exit.nowNs=81416496790400ll;r.finished=true;r.identityRetained=true;
r.exit.boundary=ReloadFlowBoundary{};
r.entry.boundary.owner={1009193120,1006126816,133245816,126915664,1,1,8589934593};
r.entry.boundary.snapshotSequence=176;
r.entry.boundary.firing=109174112;
r.entry.boundary.branch=1;
r.entry.boundary.wrapperOffset=64;
r.entry.boundary.soldierFlags=11;
r.entry.boundary.current=6;
r.entry.boundary.previous=5;
r.entry.boundary.next=7;
r.entry.boundary.timer=0.116971f;
r.entry.boundary.loaded=4;
r.entry.boundary.reserve=45;
r.entry.boundary.flagsA8=98;
r.exit.boundary->owner={1009193120,1006126816,133245816,126915664,1,1,8589934593};
r.exit.boundary->snapshotSequence=176;
r.exit.boundary->firing=109174112;
r.exit.boundary->branch=1;
r.exit.boundary->wrapperOffset=64;
r.exit.boundary->soldierFlags=11;
r.exit.boundary->current=2;
r.exit.boundary->previous=6;
r.exit.boundary->next=2;
r.exit.boundary->timer=0.0f;
r.exit.boundary->loaded=5;
r.exit.boundary->reserve=45;
r.exit.boundary->flagsA8=98;
r.entry.snapshotCopied=true;r.entry.copiedSnapshot={std::byte{2},std::byte{0},std::byte{0},std::byte{0},std::byte{2},std::byte{0},std::byte{0},std::byte{0},std::byte{0},std::byte{0},std::byte{0},std::byte{0},std::byte{0},std::byte{0},std::byte{0},std::byte{0},std::byte{0},std::byte{0},std::byte{0},std::byte{0},std::byte{0},std::byte{0},std::byte{0},std::byte{0},std::byte{5},std::byte{0},std::byte{0},std::byte{0},std::byte{45},std::byte{0},std::byte{0},std::byte{0},std::byte{1},std::byte{0},std::byte{0},std::byte{0},std::byte{0},std::byte{0},std::byte{0},std::byte{0},std::byte{0},std::byte{0},std::byte{0},std::byte{0},std::byte{255},std::byte{255},std::byte{255},std::byte{255},std::byte{0},std::byte{0},std::byte{0},std::byte{0},std::byte{0},std::byte{0},std::byte{0},std::byte{0},std::byte{0},std::byte{1},std::byte{1},std::byte{0},std::byte{0},std::byte{0},std::byte{1},std::byte{0}};
r.exit.snapshotCopied=true;r.exit.copiedSnapshot={std::byte{2},std::byte{0},std::byte{0},std::byte{0},std::byte{2},std::byte{0},std::byte{0},std::byte{0},std::byte{0},std::byte{0},std::byte{0},std::byte{0},std::byte{0},std::byte{0},std::byte{0},std::byte{0},std::byte{0},std::byte{0},std::byte{0},std::byte{0},std::byte{0},std::byte{0},std::byte{0},std::byte{0},std::byte{5},std::byte{0},std::byte{0},std::byte{0},std::byte{45},std::byte{0},std::byte{0},std::byte{0},std::byte{1},std::byte{0},std::byte{0},std::byte{0},std::byte{0},std::byte{0},std::byte{0},std::byte{0},std::byte{0},std::byte{0},std::byte{0},std::byte{0},std::byte{255},std::byte{255},std::byte{255},std::byte{255},std::byte{0},std::byte{0},std::byte{0},std::byte{0},std::byte{0},std::byte{0},std::byte{0},std::byte{0},std::byte{0},std::byte{1},std::byte{1},std::byte{0},std::byte{0},std::byte{0},std::byte{1},std::byte{0}};
// The JSON float is rounded; the unchanged native snapshot retains the exact timer bits.
r.exit.boundary->timer=DecodeReloadFiringSnapshot(r.entry.copiedSnapshot)->phaseTimer;
return r;}
}
