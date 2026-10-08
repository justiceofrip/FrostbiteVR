#pragma once
#include "Bc2ReloadProducerBinding.h"
#include "Bc2ReloadState.h"
namespace fvr::bc2 {
struct RigWorkerSceneTypes {
    // Absolute vtables derived by the existing view/request/world discoveries.
    std::uint32_t view=0,request=0,world=0;
};
struct RigWorkerBinding {
    std::uint32_t base=0,imageSize=0,rendererTable=0,prepare=0,workerReturn=0;
    RigWorkerSceneTypes scene{};
    std::array<ReloadCodeProof,6> code{};
    // Exact common/main/derived view constructors and table relationships.
    // Only the derived table is admitted for a child of the actual primary.
    std::uint32_t commonViewTable=0,derivedViewTable=0;
    std::array<ReloadCodeProof,3> viewCode{};
};
std::optional<RigWorkerBinding> DiscoverRigWorker(std::span<const std::byte>,const engine::PeImage&,RigWorkerSceneTypes);
bool ValidateRigWorkerLive(const ReloadStateMemory&,const RigWorkerBinding&,std::uint32_t actualBase)noexcept;
struct RigWorkerCall {
    std::uint32_t renderer=0,caller=0,rowCount=0,rows=0,unusedArg=0,workspace=0;
    bool operator==(const RigWorkerCall&)const=default;
};
struct RigWorkerIdentity {
    RigWorkerCall call{};ReloadProducerOwner owner{};
    std::uint32_t job=0,batch=0,arena=0,arenaBase=0;
    std::uint16_t arenaAlignment=0;
    std::array<std::uint32_t,4> jobHeader{};
    std::array<std::uint32_t,8> selectedRow{};
    std::uint32_t selectedRowIndex=0,view=0,request=0,world=0,nativeFrame=0,requestState=0;
    std::array<std::uint32_t,4> observedVtables{};
    std::uint16_t selectedViewMask=0;
    std::array<std::uint32_t,16> views{};
    std::array<std::uint32_t,16> viewTables{},viewRequests{},viewParents{};
    std::uint32_t rejectedViewIndex=UINT32_MAX;
    std::uint8_t viewCount=0;
    bool operator==(const RigWorkerIdentity&)const=default;
};
enum class RigWorkerStatus:std::uint8_t {
    Observed,InvalidArguments,WrongCaller,ReadFailure,OwnerMissing,OwnerAmbiguous,
    Bounds,WrongType,ChangedDuringRead,NoExactView,MultipleStereoViews
};
struct RigWorkerRead {
    RigWorkerStatus status=RigWorkerStatus::InvalidArguments;
    std::optional<RigWorkerIdentity> identity;
    // Diagnostic partial reads are NEVER admissible ownership evidence.
    RigWorkerIdentity partial{};
};
// Safe read callbacks only. No native method calls. A complete read is repeated;
// native arena used-byte counters are deliberately NOT required to stand still.
RigWorkerRead ReadRigWorker(const ReloadStateMemory&,const RigWorkerBinding&,const RigWorkerCall&,const ReloadProducerOwner&)noexcept;
using RigWorkerViewResolver=std::optional<ReloadProducerView>(*)(std::uint32_t world,std::uint32_t request,std::uint32_t view,std::uint32_t frame)noexcept;
struct RigWorkerAssociation {RigWorkerStatus status=RigWorkerStatus::NoExactView;std::optional<ReloadProducerView> key;};
// Only the observed primary view can select an eye. Enumerated views cannot
// replace it. If the batch also contains the other stereo eye, leave shared
// ownership unresolved rather than inventing two per-eye producers.
RigWorkerAssociation AssociateRigWorker(const RigWorkerIdentity&,RigWorkerViewResolver)noexcept;
struct RigWorkerViewLease {
    std::array<ReloadProducerView,2> eyes{};
    std::int64_t observedNs=0,deadlineNs=0;
};
// Publish this value immutably at the existing visibility boundary. A worker
// must never read the non-atomic stereo experiment control structure directly.
std::optional<ReloadProducerView> LookupRigWorkerViewLease(const RigWorkerViewLease&,
    std::uint32_t world,std::uint32_t request,std::uint32_t view,std::uint32_t frame,std::int64_t nowNs)noexcept;
// Duplicate visibility calls retain the FIRST observation/deadline, even after
// expiry. Same-frame conflicting eyes poison that frame until it advances.
// Older frames cannot replace the current record or resurrect expired evidence.
RigWorkerViewLease AdvanceRigWorkerViewLease(const RigWorkerViewLease* previous,const RigWorkerViewLease& candidate)noexcept;
}
