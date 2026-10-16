#pragma once
#include "Bc2OriginalFiringObserver.h"
#include "Bc2VehicleShotObserver.h"
#include <array>
#include <atomic>
#include <iosfwd>

namespace fvr::bc2 {
// Private SP_04 seat-1 observation. The runner proves the map independently.
// These records never grant input, substitute a context, or prove a discharge.
struct VehicleMountedBinding {unsigned image=0;bool verified=false;};
VehicleMountedBinding VerifyVehicleMountedCode(const VehicleRouteMemory&,unsigned image)noexcept;
enum class VehicleMountedReason:unsigned {Okay,Unverified,Source,Owner,Profile,Caller,
    CandidateRead,CandidateCount,Type,Join,Listener,ContextStack,ContextRead,ContextShape,
    StateRead,Changed,Count};
struct VehicleMountedSample {
    VehicleMountedReason reason=VehicleMountedReason::Unverified;
    std::int64_t nowNs=0;VehicleSeatIdentity owner{};
    unsigned component=0,componentData=0,mounted=0,effects=0,firing=0,data=0,primary=0,listener=0;
    unsigned candidates=0,matched=0,firstReadFailure=0,failedAddress=0,expected=0,observed=0;
    unsigned flags=0,rawToken=0,dtBits=0,scalarBits=0;
    unsigned state=0,next=0,timerBits=0,loadedRaw=0,heatBits=0,byte8a=0;
    std::array<unsigned,12> context{};
    bool contextRead=false,contextDecoded=false,stateRead=false;
    std::array<unsigned,5> contextBytes{}; // Raw +24..+28; no semantic renaming.
};
VehicleMountedSample ReadVehicleMounted(const VehicleRouteMemory&,const VehicleMountedBinding&,
    const VehicleShotSource&,const VehicleRouteSnapshot&,const OriginalFiringUpdate&,
    std::int64_t nowNs,bool contextOnCallingThreadStack)noexcept;
bool VehicleMountedSame(const VehicleMountedSample&,const VehicleMountedSample&)noexcept;

struct VehicleMountedApi {
    void* context=nullptr;VehicleRouteMemory memory{};
    bool (*source)(void*,VehicleShotSource&)noexcept=nullptr;
    bool (*current)(void*,const VehicleShotSource&)noexcept=nullptr;
    bool (*seat)(void*,const VehicleShotSource&,VehicleRouteSnapshot&)noexcept=nullptr;
    std::int64_t (*clock)(void*)noexcept=nullptr;
    bool (*stack)(void*,unsigned,std::size_t)noexcept=nullptr;
};
class VehicleMountedObserver {
public:
    // Configure before dispatcher registration. No reset/replacement/reuse.
    bool Configure(VehicleMountedBinding,VehicleMountedApi)noexcept;
    OriginalFiringObserver Callbacks()noexcept;
    void Registration(bool accepted)noexcept;
    void Removal(bool removed)noexcept;
    void Report(std::ostream&)const;
private:
    friend struct VehicleMountedObserverTestAccess;
    struct Row {
        std::uint64_t serial=0,input=0,space=0,epoch=0;
        std::int64_t sourceNs=0,deadlineNs=0;float sourceFire=0;
        VehicleRouteOwner sourceOwner{};VehicleSeatIdentity sourceSeat{};std::uint64_t routeFingerprint=0;
        OriginalFiringUpdate call{};VehicleMountedSample before{},after{};
        bool scopeReturned=false,same=false;
    };
    struct Slot {
        std::atomic<unsigned> state{0}; // 0 free,1 constructing,2 active,3 leaving
        VehicleShotSource source{};Row row{};int uncaught=0;
    };
    struct Journal {
        // Separate budgets: neutral traffic cannot consume held/rejected rows.
        std::array<std::array<Row,16>,3> rows{};
        std::array<std::uint64_t,3> seen{},collapsed{},dropped{};
        std::array<unsigned,3> kept{};
        std::array<std::uint64_t,unsigned(VehicleMountedReason::Count)> beforeReasons{},afterReasons{};
    };
    OriginalFiringToken Enter(const OriginalFiringUpdate&)noexcept;
    void Leave(const OriginalFiringUpdate&,const OriginalFiringToken&)noexcept;
    VehicleMountedSample Sample(const VehicleShotSource&,const OriginalFiringUpdate&)noexcept;
    void Record(const Row&)noexcept;
    VehicleMountedBinding binding_{};VehicleMountedApi api_{};bool configured_=false;
    std::atomic<bool> registered_{false},everRegistered_{false},registrationRejected_{false},removed_{false},exhausted_{false};
    std::array<Slot,8> slots_{};
    std::atomic<std::uint64_t> serial_{0},callbacks_{0},callerCalls_{0},noSource_{0},notPrivateSeat_{0};
    std::array<std::atomic<std::uint64_t>,2> sourceCalls_{},budgetDrops_{};
    std::atomic<std::uint64_t> slotDrops_{0},leaves_{0},badTokens_{0},inFlight_{0},matched_{0},unwound_{0};
    std::atomic<std::uint64_t> contextFireZero_{0},contextFireOne_{0},byte24Zero_{0},byte24Nonzero_{0};
    std::array<std::array<std::atomic<std::uint64_t>,2>,2> nativeFireBySource_{},flag24BySource_{};
    std::atomic<std::uint64_t> journalContention_{0};
    mutable std::atomic_flag journalLock_=ATOMIC_FLAG_INIT;
    Journal journal_{};
};
}
