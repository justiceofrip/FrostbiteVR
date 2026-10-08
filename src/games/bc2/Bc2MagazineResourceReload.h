#pragma once
#include "Bc2MagazinePhysicalReload.h"
#include "Bc2MagazineBodyAmmo.h"

namespace fvr::bc2 {
struct MagazineResourceProbeState;
struct MagazineResourceApi {
    void* context=nullptr;
    std::optional<AmmoResourceView> (*read)(void*,const ReloadStateOwner&,std::int64_t)noexcept=nullptr;
    bool (*submit)(void*,const AmmoResourceRequest&)noexcept=nullptr;
    // Durable outcome only: never current ammunition or hand authority.
    std::optional<AmmoResourceOutcome> (*outcome)(void*,std::uint64_t,const interaction::AmmoResourceContext&)noexcept=nullptr;
};
// Resource-backed BC2 hand adapter. Uses the same measured profiles, hand
// arbiter, chest supply, rail and native rig presentation as ordinary gameplay.
// It never starts a stock reload animation or waits for its timer.
class Bc2MagazineResourceReload {
public:
    explicit Bc2MagazineResourceReload(MagazineResourceApi,
        interaction::AmmoSupplyConfig=Bc2MagazinePhysicalReload::DefaultPouch())noexcept;
    MagazinePhysicalResult Tick(const MagazinePhysicalSample&,interaction::HandInteraction&,std::uint64_t& sharedIntent)noexcept;
    void Cancel(const interaction::HandInteractionSample&,interaction::HandInteraction&)noexcept;
    bool BlocksEquipment()const noexcept{return busy_;}
    void EnableBodyAmmo(bool enabled=true,interaction::SupplyAnchorFrame frame=interaction::SupplyAnchorFrame::HeadYaw)noexcept {bodyAmmo_=enabled;frame_=frame;}
    void Report(std::ostream&)const;
    MagazineResourceProbeState ProbeState(std::int64_t now)const noexcept;
private:
    enum class CancelCause:unsigned {Other,Input,Api,View,Mapping,Gun,Terminal,Reconciliation,Restore,Owner,Receipt,Supply,Counter,Interaction,Submit,Count};
    struct CancelEvent {CancelCause cause{};std::int64_t now=0;std::uint64_t input=0,request=0,cycle=0;
        unsigned phase=0,reason=0,nativeCheck=0;bool pending=false;};
    void NoteCancel(CancelCause,const interaction::HandInteractionSample&)noexcept;
    std::array<std::uint64_t,unsigned(CancelCause::Count)> cancelCounts_{};
    std::optional<CancelEvent> firstCancel_,firstActiveCancel_;
    MagazinePhysicalResult TickImpl(const MagazinePhysicalSample&,interaction::HandInteraction&,std::uint64_t&)noexcept;
    bool RecoverTerminal(const AmmoResourceOutcome&,const interaction::HandInteractionSample&,interaction::HandInteraction&)noexcept;
    bool Send(const AmmoResourceView&,const MagazinePhysicalSample&,interaction::AmmunitionOperation,bool,std::uint64_t&)noexcept;
    void Carry(MagazineTracking&,const MagazinePhysicalSample&)noexcept;
    MagazineResourceApi api_;interaction::AmmoSupplyConfig pouch_;interaction::AmmoSupply supply_;
    interaction::DetachableMagazine interaction_;
    const MagazineEquipmentProfile* profile_=nullptr;
    std::optional<interaction::MagazineResourceOwnerBinding> owner_;
    interaction::DetachableMagazineResult last_{};
    std::optional<interaction::ManualReloadRequest> physical_;
    std::optional<interaction::AmmoSupplyReservation> reservation_;
    std::optional<MagazineCarryFrame> carry_;
    std::optional<interaction::OriginalMagazine> original_;
    std::optional<AmmoResourceRequest> submittedSeat_;
    std::optional<AmmoResourceRequest> pendingRequest_;
    std::optional<AmmoResourceView> latestView_;
    MagazinePhysicalResult latestResult_;
    struct InputEvidence {interaction::AmmoSupplySample supply;};
    std::array<std::optional<InputEvidence>,32> history_{};std::size_t historyNext_=0;
    std::uint64_t event_=0,cycle_=0,sequence_=0,seatedEvent_=0;
    interaction::AmmunitionOperation operation_{};bool discard_=false,pending_=false,busy_=false,bodyAmmo_=false;
    bool needRestore_=false;
    interaction::SupplyAnchorFrame frame_=interaction::SupplyAnchorFrame::HeadYaw;
    unsigned submitted_=0,completed_=0,acquired_=0,rejected_=0;
};
}
