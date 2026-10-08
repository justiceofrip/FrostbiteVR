#pragma once
#include "Bc2Rig.h"
#include "Bc2M95StockShotConfig.h"
#include "Bc2SightContact.h"
#include "fvr/interaction/PhysicalWeaponCycle.h"
#include "Bc2ReloadFlowRuntime.h"
#include "Bc2SelectedMeshes1p.h"
#include <ostream>
namespace fvr::bc2 {
// Finite diagnostic only. Brackets observe state around the original rig read;
// they never label a submitted mesh, select a closed stop or grant readiness.
struct M95ShotPartCaptureRow {
    reloadFlowRuntime::PumpPartNativeSample before{},after{};
    RigIdentity rig{};
    std::uint64_t rigFingerprint=0,inputSequence=0;
    std::int64_t inputObservedNs=0,inputDeadlineNs=0,selectedObservedNs=0,selectedDeadlineNs=0;
    math::Matrix4 partFromWeapon{},wristFromWeapon{};
    bool stableStateBracket=false;unsigned fixturePhase=0;
    std::array<math::Matrix4,15> fingersFromWrist{};
};
class M95ShotPartCapture {
public:
    static constexpr unsigned PrefireCapacity=32,AfterFireCapacity=320,Capacity=PrefireCapacity+AfterFireCapacity;
    bool Observe(const reloadFlowRuntime::PumpPartNativeSample& before,
        const reloadFlowRuntime::PumpPartNativeSample& after,const RigSnapshot&,
        const SelectedMeshesSnapshot&,std::uint64_t inputSequence,
        std::int64_t inputObservedNs,std::int64_t inputDeadlineNs,float units,std::int64_t now,unsigned fixturePhase);
    void Report(std::ostream&)const;
    unsigned Count()const noexcept{return count_;}
    const M95ShotPartCaptureRow& Row(unsigned n)const{return rows_.at(n);}
private:
    std::array<M95ShotPartCaptureRow,Capacity> rows_{};
    unsigned count_=0,rejected_=0,dropped_=0,prefireNext_=0,postfireCount_=0;bool fired_=false;
};
}
