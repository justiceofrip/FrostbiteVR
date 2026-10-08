#pragma once
#include <array>
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <ostream>
#include "Bc2WeaponDrawCatalog.h"
#include "Bc2SelectedMeshes1p.h"
#include "fvr/math/StereoMath.h"
struct ID3D11DeviceContext;
namespace fvr::bc2 {
// Optional producer correlation supplied by its verified native owner. Default
// values make no association claim. A nearby pose/time alone is not a join.
struct ReloadDrawProducerEvidence {
    std::uint64_t request=0,rigPose=0,selectedMeshes1p=0,rigFingerprint=0;
    std::uint64_t actor=0,weak=0,weapon=0,ownerGeneration=0,inputGeneration=0,space=0;
    std::uint32_t nativeFrame=0;std::int64_t observedNs=0,deadlineNs=0;
    std::uint64_t holdCycle=0;std::int64_t holdBeginNs=0,holdDeadlineNs=0;
    bool exactRequestAssociation=false,selectedMeshIdentityVerified=false;
    bool shellHidden=false,packedShellValid=false,packedOpticValid=false;
    std::array<std::byte,48> packedShell{},packedOptic{}; // Exact native column-packed source.
    std::shared_ptr<const SelectedMeshesSnapshot> opticSelected; // Original configured metadata; NOT submitted-mesh proof.
    std::uint64_t physicalEquipmentGeneration=0;
};
struct ReloadDrawFrameEvidence {
    std::uint64_t world=0,request=0,view=0;
    std::uint32_t nativeFrame=0;unsigned eye=0;
    std::int64_t nowNs=0;std::uint64_t tickMs=0;
    ReloadDrawProducerEvidence producer{};
};
struct ReloadReticleDrawCurrent {
    std::uint64_t world=0,request=0,view=0,physicalEquipmentGeneration=0;
    std::uint64_t actor=0,weak=0,weapon=0,ownerGeneration=0,space=0;
    std::uint32_t nativeFrame=0;unsigned eye=0;std::int64_t nowNs=0;
    math::Vec3 eyeCanonicalLh{};bool eyeWorldValid=false;
    std::shared_ptr<const SelectedMeshesSnapshot> selected;
};
using ReloadReticleDrawResolver=ReloadReticleDrawCurrent(*)(const ReloadDrawFrameEvidence&)noexcept;
// Diagnostic only. No pipeline changes, flush/wait, original-call invocation,
// memory patch, or section/visibility override. One render-thread owner only.
// Caller brackets every native eye draw (not only one evidence frame), observes
// DrawIndexed before original, and polls from that same immediate-context thread.
class Bc2ReloadDrawCapture {
public:
    static constexpr unsigned MaxRecords=1024,MaxPending=8,MaxPerFrame=32;
    static constexpr std::int64_t WindowNs=15000000000ll,SamplePeriodNs=75000000;
    Bc2ReloadDrawCapture();~Bc2ReloadDrawCapture();
    Bc2ReloadDrawCapture(const Bc2ReloadDrawCapture&)=delete;
    Bc2ReloadDrawCapture& operator=(const Bc2ReloadDrawCapture&)=delete;
    bool Enable(bool explicitDiagnostic,std::int64_t nowNs)noexcept;
    // Set before Enable/hooks. Called only for bounded count12/stride68 candidates.
    bool SetReticleResolver(ReloadReticleDrawResolver)noexcept;
    // Copies validated metadata before callbacks begin; no borrowed catalog lifetime.
    bool Enable(bool explicitDiagnostic,std::int64_t nowNs,std::span<const WeaponDrawSection>)noexcept;
    void BeginEye(ID3D11DeviceContext*,const ReloadDrawFrameEvidence&)noexcept;
    void ObserveIndexed(ID3D11DeviceContext*,unsigned count,unsigned startIndex,std::int32_t baseVertex)noexcept;
    void EndEye()noexcept;
    void Poll(ID3D11DeviceContext*)noexcept;
    // Closes admission. False means an admitted callback still runs: retain the
    // module/object and retry later. No spinning/sleeping on a render thread.
    // On true, pending copies are abandoned; Report may then read full records.
    bool Stop()noexcept;
    void Report(std::ostream&)const;
private:
    struct Admission {
        Bc2ReloadDrawCapture& owner;bool entered=false;
        explicit Admission(Bc2ReloadDrawCapture&)noexcept;~Admission();
    };
    struct Impl;std::unique_ptr<Impl> impl_;
    ReloadReticleDrawResolver reticleResolver_=nullptr;
    std::atomic<bool> accepting_=false;std::atomic<unsigned> callbacks_=0;
};
}
