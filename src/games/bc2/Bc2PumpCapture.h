#pragma once
#include "Bc2PumpPart.h"
#include "Bc2ReloadFlowRuntime.h"
#include "Bc2SelectedMeshes1p.h"
#include <ostream>
namespace fvr::bc2 {
// Finite diagnostic only. Brackets observe state around the original rig read;
// they never label a submitted mesh, select a closed stop or grant readiness.
struct PumpPartCaptureRow {
    reloadFlowRuntime::PumpPartNativeSample before{},after{};
    RigIdentity rig{};
    std::uint64_t rigFingerprint=0,inputSequence=0;
    std::int64_t inputObservedNs=0,inputDeadlineNs=0,selectedObservedNs=0,selectedDeadlineNs=0;
    math::Matrix4 partFromWeapon{},wristFromWeapon{};
    bool stableStateBracket=false;
};
class PumpPartCapture {
public:
    static constexpr unsigned Capacity=1024;
    bool Observe(const reloadFlowRuntime::PumpPartNativeSample& before,
        const reloadFlowRuntime::PumpPartNativeSample& after,const RigSnapshot&,
        const SelectedMeshesSnapshot&,std::uint64_t inputSequence,
        std::int64_t inputObservedNs,std::int64_t inputDeadlineNs,float units,std::int64_t now);
    void Report(std::ostream&)const;
    unsigned Count()const noexcept{return count_;}
    const PumpPartCaptureRow& Row(unsigned n)const{return rows_.at(n);}
private:
    std::array<PumpPartCaptureRow,Capacity> rows_{};
    unsigned count_=0,rejected_=0,dropped_=0;
};
}
