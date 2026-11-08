#pragma once
#include "Bc2CampaignProvisioningPolicy.h"
#include "Bc2ReloadServer.h"
#include <array>
#include <cstdint>
#include <optional>
#include <type_traits>

namespace fvr::bc2::campaignProvisioning {
namespace policy=provisioning_candidate;
enum class Operation:std::uint32_t {Observe,Equip,Restore};
enum class Status:std::uint32_t {Disabled,Ready,Observing,Preflight,Excluded,AwaitingReplication,
    Equipped,AwaitingRestore,Restored,Refused,Expired,Quarantined,Stopped};
struct ProcessIdentity {
    std::uint32_t pid=0,reserved=0;
    std::uint64_t creation=0,session=0;
    std::array<std::uint8_t,32> image{};
    std::array<char16_t,512> executable{};
    bool operator==(const ProcessIdentity&)const=default;
};
// Fixed-width private wire message. Native pointers are identities, never RPC
// targets. The worker copies a complete message; only SeatEntry can dispatch.
struct Request {
    std::uint32_t magic=0x31504342,version=1,bytes=0;
    Operation operation=Operation::Observe;
    ProcessIdentity process{};
    std::uint64_t nonce=0,job=0,sourceSequence=0;
    std::int64_t issuedNs=0,deadlineNs=0,sourceObservedNs=0;
    ReloadStateOwner client{};
    policy::Owner source{};
    std::uint32_t targetAsset=0,targetData=0,variant=0,reserved=0;
};
static_assert(std::is_trivially_copyable_v<Request>);
static_assert(sizeof(ProcessIdentity)==1080&&sizeof(Request)==1264);
struct Receipt {
    std::uint32_t magic=0x31524342,version=1,bytes=0;
    Status status=Status::Disabled;
    ProcessIdentity process{};
    std::uint64_t nonce=0,job=0,preflight=0,retirements=0;
    std::int64_t observedNs=0;
    policy::ProvisionReceipt native{};
    policy::BoundaryFailure boundary=policy::BoundaryFailure::None;
    ReloadStateOwner previous{},current{};
    bool inputExcluded=false,oldItemRetired=false;
};
struct WireReceipt {
    std::uint32_t magic=0x31524342,version=1,bytes=0,status=0;
    ProcessIdentity process{};
    std::uint64_t nonce=0,job=0,preflight=0,retirements=0,dispatchSequence=0;
    std::int64_t observedNs=0,dispatchNs=0;
    ReloadStateOwner previous{},current{};
    std::uint32_t phase=0,nativeCalls=0,returnedItem=0,thread=0,boundary=0;
    std::uint32_t serverItemObserved=0,clientServerConverged=0,inputExcluded=0,oldItemRetired=0,reserved=0;
};
static_assert(std::is_trivially_copyable_v<WireReceipt>&&sizeof(WireReceipt)==1272);
WireReceipt ToWire(const Receipt&)noexcept;
struct LifetimeHost {
    enum class Exclusion:std::uint32_t {Refused,Pending,Ready};
    void* context=nullptr;
    // Nonblocking, exact selected item only. Refuse held original, owed cycle,
    // open native transaction, pending selection or callback overlap. No reset.
    Exclusion (*exclude)(void*,const ReloadServerSnapshot&,std::uint64_t)noexcept=nullptr;
    // Called ONLY after assign returned and the policy verified the replacement
    // server slot. Never runs for preparation or an ambiguous native outcome.
    bool (*retire)(void*,const ReloadServerSnapshot&,std::uint32_t,std::uint32_t,
                   std::uint64_t,std::int64_t)noexcept=nullptr;
    bool (*resume)(void*,const ReloadServerSnapshot&,std::uint64_t)noexcept=nullptr;
};
policy::Owner ProvisionOwner(const ReloadServerSnapshot&,std::uint64_t session)noexcept;
class Service {
public:
    // Process-retained object; never reconstruct to retry an uncertain job.
    bool Configure(const ProcessIdentity&,const policy::Memory&,const policy::Host&,
                   const LifetimeHost&,bool explicitRequest=false)noexcept;
    bool Submit(const Request&,std::int64_t now)noexcept;
    // All three methods are serialized by the adapter, independently of the
    // immutable firing observer slot and native original-call lifetime.
    void ObserveServer(const ReloadServerSnapshot&,std::uint32_t firing,
                       std::uint32_t thread,std::uint64_t sequence,std::int64_t now)noexcept;
    void SeatEntry(const ReloadServerSnapshot&,const policy::Boundary&)noexcept;
    void Expire(std::int64_t now)noexcept;
    void Stop()noexcept;
    const Receipt& Read()const noexcept{return receipt_;}
    bool BlocksInput()const noexcept{return receipt_.inputExcluded;}
private:
    bool Source(const Request&,const ReloadServerSnapshot&,const policy::Boundary&)const noexcept;
    void Quarantine(std::int64_t now)noexcept;
    void FinishUnsent(Status,std::int64_t now,const ReloadServerSnapshot*)noexcept;
    ProcessIdentity process_{};policy::Memory memory_{};policy::Host host_{};LifetimeHost lifetime_{};
    policy::WeaponProvisioner policy_{};Receipt receipt_{};
    std::optional<Request> request_;std::optional<policy::Resource> target_;
    std::optional<ReloadServerSnapshot> retiredSource_;
    std::uint64_t lastNonce_=0,job_=0;
    bool configured_=false,stopped_=false,armed_=false,dispatchArmed_=false,restoreUsed_=false;
};
}
