#pragma once
#include "Bc2SelectedKnifeNativeReader.h"
#include "Bc2SelectedKnifeMotion.h"
#include "Bc2OriginalFiringObserver.h"
#include <ostream>

namespace fvr::bc2::selectedKnife {
// Independent measured melee listeners; never registers a second original
// firing observer or hooks the existing Step/Update/Compose dispatcher.
bool Install(std::span<const std::byte>,const engine::PeImage&,std::uintptr_t,
    const ReloadStateMemory&)noexcept;
void Start()noexcept;
bool Stop()noexcept;
// Gather thread: one shared SelectedMeleeAttack policy owns trigger and jab.
// handled=false preserves the existing verified action37 trigger fallback when
// the optional adapter was not installed. Once handled, fire is one intent only.
struct Request {bool handled=false,fire=false;std::uint64_t serial=0;};
Request Prepare(const SelectedKnifeRenderSource&,const interaction::InputFrame&,
    const interaction::HandInteraction&,const SelectedMeshesSnapshot*,
    const std::optional<SelectedKnifeRenderReceipt>&,const std::optional<math::Matrix4>& currentBody,
    bool actionsAvailable,std::uint64_t inputEpoch)noexcept;
// Call immediately after the SAME input-buffer transaction. A cache publication
// is not native completion. Acquisition and original attack return supply that.
void Published(const Request&,bool applied)noexcept;
// New continuous packets retain an already published intent. An actual source
// interruption revokes only further blade substitution, never native outcome.
void SourceAdvanced(const interaction::HandInteractionSample&,std::uint64_t inputEpoch)noexcept;
Request Missing()noexcept;
// Known current actor observation only; missing reads do not retire anything.
void ObserveOwner(const ReloadStateOwner&)noexcept;
// Nonblocking observation only. Counters remain readable when the transaction
// lock is busy; stateKnown then stays false. No native memory is read here.
enum class HookOutcome : unsigned {
    Admitted,Disabled,Caller,NoAction,NotPublished,AlreadyAcquired,Self,
    Pending,BeforeSource,Owner,Policy,NotAcquired,AcquireNotReturned,
    TargetUnreadable,TargetChanged,Overlap,Count
};
inline constexpr std::array<const char*,unsigned(HookOutcome::Count)> HookOutcomeNames{
    "admitted","disabled","caller","no_action","not_published","already_acquired","self",
    "pending","before_source","owner","policy","not_acquired","acquire_not_returned",
    "target_unreadable","target_changed","overlap"};
struct HookTelemetry {
    std::uint64_t entries=0;
    // One atomic boundary pair, including disabled/rejected callbacks. Counter
    // totals are independent snapshots and need not describe this same entry.
    unsigned rawLastCaller=0,rawLastSelf=0;
    std::array<std::uint64_t,unsigned(HookOutcome::Count)> outcomes{};
};
struct UpdateTelemetry {
    std::uint64_t calls=0,ownerRejected=0,contextRejected=0,stateRejected=0,fireZero=0,fireOne=0,notificationsZero=0,notificationsOne=0;
    std::uint64_t serial=0,lastFireSerial=0,fireForPublishedIntent=0;std::int64_t lastNs=0,firstFireNs=0,lastFireNs=0;
    unsigned firing=0,caller=0,inputFlags=0,contextFlags=0,current=0,next=0,listenerCount=0,expectedListenerMatches=0;
    unsigned lastFireContextFlags=0,lastFireCurrent=0,lastFireNext=0;
    bool listenerKnown=false;
    bool lastFireListenerKnown=false;unsigned lastFireListenerCount=0,lastFireExpectedListenerMatches=0;
    std::uint64_t fireWithExpectedListener=0;
};
enum class OwnerBoundary:unsigned {AcquireRetained,AcquireCurrent,AcquireReturned,SearchCurrent,AttackRetained,Count};
inline constexpr std::array<const char*,unsigned(OwnerBoundary::Count)> OwnerBoundaryNames{
    "acquire_retained","acquire_current","acquire_returned","search_current","attack_retained"};
struct OwnerFailure {
    OwnerBoundary boundary=OwnerBoundary::AcquireRetained;
    std::uint64_t serial=0;std::int64_t observedNs=0;OwnerDiagnostic proof{};
};
struct Telemetry {
    bool available=false,installed=false,enabled=false,stateKnown=false;
    HookTelemetry acquire{},attack{};
    unsigned prepared=0,rejected=0,readGaps=0,geometryGaps=0,unknown=0,pending=0;
    std::int64_t sampleNs=0,nativeObservedNs=0;
    std::array<unsigned,3> nativeCurrent{},nativeNext{};
    std::uint64_t serial=0,input=0,admissionEpoch=0,currentEpoch=0;
    bool published=false,acquired=false,acquisitionReturned=false,attackReturned=false,geometry=false;
    unsigned effects=0,serverFiring=0,attacker=0,target=0,searches=0,copies=0,damageCalls=0,failure=0;
    unsigned acquisitionFailure=0,acquisitionPriorFailure=0;
    std::int64_t observedNs=0,deadlineNs=0,publishedNs=0,revokedNs=0,acquiredNs=0,attackedNs=0;
    unsigned lastHook=0,lastOutcome=0,lastSelf=0,lastCaller=0;
    std::uint64_t lastSerial=0;std::int64_t lastHookNs=0;
    unsigned completed=0,lastCompletedTarget=0,lastCompletedDamageCalls=0;
    unsigned lastAcquisitionFailure=0,lastAcquisitionPriorFailure=0;
    std::uint64_t lastCompletedSerial=0;
    bool lastCompletedHealthKnown=false;float lastCompletedHealthBefore=0,lastCompletedHealthAfter=0;
    bool updateVerified=false;std::uint64_t updateEntries=0,updateContention=0;
    std::array<UpdateTelemetry,3> updates{}; // client+3c, client+40, server
    std::array<std::uint64_t,unsigned(OwnerBoundary::Count)> ownerFailures{};
    std::array<OwnerFailure,4> ownerFailureRecords{};unsigned ownerFailureCount=0,ownerFailureDropped=0;
};
// Observation from the existing Update dispatcher. Never supplies copied input,
// invokes native code, resolves an intent, or changes native/policy state.
void ObserveUpdate(const OriginalFiringUpdate&)noexcept;
Telemetry ReadTelemetry()noexcept;
void WriteTelemetry(std::ostream&,const Telemetry&);
void Report(std::ostream&);
}
