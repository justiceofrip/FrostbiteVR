#pragma once
#include "Bc2ReloadFlowRuntime.h"
namespace spas_progression251 {
// Actual251 trace SHA256: 58f02ca2147e0475ce1b3aa037aae093f8ad7bdc0ee3c2e84f2387bb864fa662
// Trace row11834 maps to native invocation16150; IDs below use native invocations.
inline fvr::bc2::ReloadHoldIdentity Identity(){return {{1010233360,1007884960,132269776,127057040,1,2,8589934593},{108769808,108769632,107340512},411356656,449711472,108343344};}
inline fvr::bc2::ReloadFlowRecord Record(){using namespace fvr::bc2;ReloadFlowRecord r{};
r.id=16150;r.entry.kind=ReloadFlowEvent::Restore;r.entry.nativeInvocation=16150;
r.entry.thread=r.exit.thread=45640;r.entry.depth=1;r.entry.caller=9155640;r.entry.context=410889296;
r.entry.nowNs=79380718624600ll;r.exit.nowNs=79380718758500ll;r.finished=true;r.identityRetained=true;
r.exit.boundary=ReloadFlowBoundary{};
r.entry.boundary.owner={1010233360,1007884960,132269776,127057040,1,2,8589934593};
r.entry.boundary.snapshotSequence=1736;
r.entry.boundary.firing=108769632;
r.entry.boundary.branch=1;
r.entry.boundary.wrapperOffset=64;
r.entry.boundary.soldierFlags=11;
r.entry.boundary.current=6;
r.entry.boundary.previous=6;
r.entry.boundary.next=7;
r.entry.boundary.timer=0.000529684f;
r.entry.boundary.loaded=7;
r.entry.boundary.reserve=24;
r.entry.boundary.flagsA8=2;
r.exit.boundary->owner={1010233360,1007884960,132269776,127057040,1,2,8589934593};
r.exit.boundary->snapshotSequence=1736;
r.exit.boundary->firing=108769632;
r.exit.boundary->branch=1;
r.exit.boundary->wrapperOffset=64;
r.exit.boundary->soldierFlags=11;
r.exit.boundary->current=7;
r.exit.boundary->previous=6;
r.exit.boundary->next=8;
r.exit.boundary->timer=0.498921f;
r.exit.boundary->loaded=7;
r.exit.boundary->reserve=24;
r.exit.boundary->flagsA8=2;
r.entry.snapshotCopied=true;r.entry.copiedSnapshot={std::byte{7},std::byte{0},std::byte{0},std::byte{0},std::byte{8},std::byte{0},std::byte{0},std::byte{0},std::byte{163},std::byte{114},std::byte{255},std::byte{62},std::byte{0},std::byte{0},std::byte{0},std::byte{0},std::byte{0},std::byte{0},std::byte{0},std::byte{0},std::byte{0},std::byte{0},std::byte{0},std::byte{0},std::byte{7},std::byte{0},std::byte{0},std::byte{0},std::byte{24},std::byte{0},std::byte{0},std::byte{0},std::byte{0},std::byte{0},std::byte{0},std::byte{0},std::byte{0},std::byte{0},std::byte{0},std::byte{0},std::byte{0},std::byte{0},std::byte{0},std::byte{0},std::byte{255},std::byte{255},std::byte{255},std::byte{255},std::byte{0},std::byte{0},std::byte{0},std::byte{0},std::byte{0},std::byte{0},std::byte{0},std::byte{0},std::byte{0},std::byte{1},std::byte{1},std::byte{0},std::byte{0},std::byte{0},std::byte{0},std::byte{0}};
r.exit.snapshotCopied=true;r.exit.copiedSnapshot={std::byte{7},std::byte{0},std::byte{0},std::byte{0},std::byte{8},std::byte{0},std::byte{0},std::byte{0},std::byte{163},std::byte{114},std::byte{255},std::byte{62},std::byte{0},std::byte{0},std::byte{0},std::byte{0},std::byte{0},std::byte{0},std::byte{0},std::byte{0},std::byte{0},std::byte{0},std::byte{0},std::byte{0},std::byte{7},std::byte{0},std::byte{0},std::byte{0},std::byte{24},std::byte{0},std::byte{0},std::byte{0},std::byte{0},std::byte{0},std::byte{0},std::byte{0},std::byte{0},std::byte{0},std::byte{0},std::byte{0},std::byte{0},std::byte{0},std::byte{0},std::byte{0},std::byte{255},std::byte{255},std::byte{255},std::byte{255},std::byte{0},std::byte{0},std::byte{0},std::byte{0},std::byte{0},std::byte{0},std::byte{0},std::byte{0},std::byte{0},std::byte{1},std::byte{1},std::byte{0},std::byte{0},std::byte{0},std::byte{0},std::byte{0}};
// The JSON float is rounded; the unchanged native snapshot retains the exact timer bits.
r.exit.boundary->timer=DecodeReloadFiringSnapshot(r.entry.copiedSnapshot)->phaseTimer;
return r;}
}
