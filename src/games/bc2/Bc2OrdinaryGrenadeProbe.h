#pragma once
#include "OrdinaryGrenadeDiagnostic.h"
#include "Bc2ReloadServer.h"
#include "Bc2BodyInventory.h"
#include <ostream>

namespace fvr::bc2::private_grenade {
struct Selector {
    std::uint32_t inventory=0,switching=0,callback=0,current=0,previous=0,primary=0;
    bool operator==(const Selector&)const=default;
};
struct Native {
    ReloadStateOwner owner{};BodyCarriedIdentity carried{};
    std::uint32_t originalSlot=0,targetSlot=0;
    ReloadServerInventoryItem original{},target{};
    std::array<Selector,3> selectors{}; // effective client, server, independent248
    std::array<std::uint8_t,5> targetFlags{}; // deploy, hide, primary-empty, previous-return, allow-empty
};
struct Facts {
    Sample source{};std::optional<BodyInventoryDisplay> display;
    interaction::HandInteractionSample hand{};interaction::HandInteractionKey handItem{};
    std::optional<interaction::HandClaim> left,right;
    unsigned holsterPhase=0;bool visible=false,cacheQuiet=false,custodyObserved=true;
    std::uint32_t bodySettledFailures=0; // Diagnostic only; never an admission input.
};
// Reads only; every observation retains the actual selected owner. The saved
// external metadata capture is never consulted by admission.
std::optional<Native> ReadNative(const ReloadStateMemory&,const ReloadStateBinding&,
    const ReloadServerBinding&,std::uint32_t base,const ReloadStateOwner&,std::uint64_t sequence,
    std::int64_t now,std::uint32_t original=0,std::uint32_t target=0)noexcept;
bool VerifyStockReturn(const ReloadStateMemory&,std::uint32_t base)noexcept;
Sample StockSample(const Native&,const Facts&)noexcept;
bool StockReady(const ReloadServerInventoryItem&)noexcept;
bool StockAmmoEqual(const ReloadServerInventoryItem&)noexcept;
bool StockItemSame(const ReloadServerInventoryItem&,const ReloadServerInventoryItem&)noexcept;
class Probe {
public:
    std::optional<Request> Prepare(const std::optional<Native>&,Facts)noexcept;
    bool CanStage(const Request&,const std::optional<Native>&,Facts)noexcept;
    void Commit(const Request&,Facts,bool bit38)noexcept;
    void Missing(std::int64_t now)noexcept;
    void Observe(const std::optional<Native>&,const Facts&)noexcept;
    std::uint32_t Original()const noexcept{return baseline_?baseline_->original.client.target:0;}
    std::uint32_t Target()const noexcept{return baseline_?baseline_->target.client.target:0;}
    bool Started()const noexcept{return baseline_.has_value();}
    bool NeedsNative()const noexcept{return !complete_;}
    void Report(std::ostream&)const;
private:
    enum EventKind:unsigned {Baseline=1,Gesture,RequestIssued,Staged,Committed,Rejected,NativeChange,ReadGap,Finished};
    struct Event {
        unsigned kind=0,phase=0,reason=0,selected=0,holster=0;
        std::uint64_t input=0,right=0,rightItem=0,rightGeneration=0,left=0,revision=0;
        std::int64_t now=0,observed=0,deadline=0;
        std::array<Selector,3> selectors{};
        std::array<std::int32_t,12> ammo{};
        std::array<unsigned,6> firing{};
        std::array<unsigned,3> originalNext{};
        std::array<float,3> originalTimers{};
        std::array<std::uint8_t,6> firingFlags{};
        std::uint32_t admissionFailures=0,bodySettledFailures=0;
        std::uint64_t rightInput=0;
        std::int64_t rightDeadline=0;
        bool admissionEvaluated=false;
        bool visible=false,cacheQuiet=false,custodyObserved=false;
    };
    Sample Fill(const Native&,const Facts&)const noexcept;
    void Add(unsigned,const Facts&,const Native* =nullptr,const Sample* evaluated=nullptr)noexcept;
    bool SamePlan(const Native&)const noexcept;
    OneGrenade semantic_;
    Sample staged_{};
    std::optional<Native> baseline_;
    std::optional<BodyInventoryDisplay> body_;
    interaction::HandClaimToken originalGun_{};
    interaction::HandClaimToken returnedGun_{};
    ReloadStateOwner returnedOwner_{};
    std::array<Selector,3> returnedSelectors_{};
    std::array<Event,64> events_{};unsigned count_=0,dropped_=0;
    std::optional<Event> lastNative_;
    std::int64_t commitNs_=0,lastGoodNs_=0,returnNs_=0;
    bool selectedGrenade_=false,selectedOriginal_=false,returnedHeld_=false,ammoConsumed_=false;
    bool returnSettled_=false,failed_=false,complete_=false,pass_=false,gap_=false;
};
}
