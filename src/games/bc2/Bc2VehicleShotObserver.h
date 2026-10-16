#pragma once
#include "Bc2CampaignVehicleProfile.h"
#include <array>
#include <atomic>
#include <cstdint>
#include <iosfwd>
#include <mutex>

namespace fvr::bc2 {
struct VehicleShotBinding {unsigned image=0;bool verified=false;};
VehicleShotBinding VerifyVehicleShotCode(const VehicleRouteMemory&,unsigned image)noexcept;
struct VehicleShotSource {
    CampaignVehicleSnapshot profile{};
    VehicleRouteOwner owner{};
    std::uint64_t input=0,space=0;
    std::int64_t observedNs=0,deadlineNs=0;
    float fire=0;
    std::uint64_t observationEpoch=0;
};
struct VehicleShotCall {unsigned effects=0,config=0,caller=0,context=0;};
enum class VehicleShotReason : unsigned {Okay,Unverified,Stale,Owner,Caller,Read,Foreign,Changed,Queue};
enum class VehicleShotLeaf : unsigned {None,CandidateRead,CandidateCount,EffectsExtent,
    ComponentType,EffectsType,FiringType,PrimaryType,MountedUpdate,MountedInput,
    MountedFiring,EffectsFiring,FireAction,ShotConfig,EffectsShoot,ListenerCallback,
    ListenerShape,ListenerMembership};
struct VehicleShotDiagnostic {
    struct Candidate {unsigned component=0,table=0,mounted=0,effects=0;};
    VehicleShotLeaf leaf=VehicleShotLeaf::None;
    std::uint64_t address=0;
    unsigned expected=0,observed=0;
    std::uint64_t firstReadFailure=0;
    unsigned associated=0,weaponCandidates=0,matches=0,candidateCount=0;
    std::array<Candidate,4> candidates{};
    unsigned componentData=0,mountedTable=0,effectsTable=0,listenerTable=0;
};
struct VehicleShotSample {
    VehicleShotReason reason=VehicleShotReason::Unverified;
    VehicleSeatIdentity owner{};
    unsigned component=0,mounted=0,effects=0,firing=0,primary=0,config=0;
    unsigned queueBegin=0,queueEnd=0,queueCount=0,queueTarget=0;
    std::int64_t nowNs=0;
    VehicleShotDiagnostic diagnostic{};
};
VehicleShotSample ReadVehicleShot(const VehicleRouteMemory&,const VehicleShotBinding&,
    const VehicleShotSource&,const VehicleRouteSnapshot&,const VehicleShotCall&,std::int64_t nowNs)noexcept;
bool VehicleShotSame(const VehicleShotSample&,const VehicleShotSample&)noexcept;
bool VehicleShotQueueGrowth(const VehicleRouteMemory&,const VehicleShotSample&,const VehicleShotSample&)noexcept;
// One immutable current source and32 scalar event rows. Observation never
// grants input authority and never changes the original callback or its result.
class VehicleShotObserver {
public:
    // Input-owner/space transitions revoke retained sources without waiting
    // for the journal mutex. The callback never reads producer-owned epochs.
    void Invalidate()noexcept;
    bool Current(const VehicleShotSource&)const noexcept;
    void Publish(const VehicleShotSource&);
    std::optional<VehicleShotSource> Source();
    void Record(const VehicleShotSource&,const VehicleShotCall&,const VehicleShotSample&,
        const VehicleShotSample&,bool queueGrowth);
    void Report(std::ostream&,bool bindingVerified=false)const;
private:
    friend struct VehicleShotObserverTestAccess;
    struct Row {std::uint64_t input=0,space=0;std::int64_t observed=0,deadline=0;
        float fire=0;VehicleShotCall call{};VehicleShotSample before{},after{};bool queueGrowth=false;};
    struct Snapshot {
        std::array<Row,32> rows{};unsigned count=0,total=0,matched=0,queueGrowth=0,dropped=0;
        std::array<unsigned,9> rejectedBefore{},rejectedAfter{};
        // Four first failures each for neutral and held source, independent
        // of the positive journal. No allocation, deduplication or retry.
        std::array<Row,8> firstRejected{};
        std::array<unsigned,2> rejectionSeen{},rejectionKept{};
    };
    mutable std::mutex mutex_;
    std::atomic<std::uint64_t> observationEpoch_{1};std::atomic<bool> epochExhausted_{false};
    std::atomic<std::uint64_t> publishContention_{0},sourceContention_{0},recordContention_{0},invalidatedRecords_{0};
    mutable std::atomic<std::uint64_t> reportContention_{0};
    VehicleShotSource source_{};bool hasSource_=false;
    Snapshot journal_{};
};
}
