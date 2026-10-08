#pragma once
#include "Bc2WeaponVisibility.h"
#include "Bc2WeaponVisibilityProbeSession.h"
#include <ostream>
#include <string_view>
namespace fvr::bc2 {
enum class WeaponVisibilityProbePhase:unsigned {Warmup,Baseline,Hidden,Restored,Done,Failed};
struct WeaponVisibilityProbeSample {
    WeaponVisibilityProbePhase phase=WeaponVisibilityProbePhase::Warmup;
    WeaponVisibilityRequest intent{};
    std::optional<WeaponVisibilityReceipt> receipt;
    std::int64_t sampledNs=0;
};
// Explicit native diagnostic only. It neither switches weapons nor grants an
// inventory acknowledgement. Graphics acceptance requires saved eye output.
class Bc2WeaponVisibilityProbe {
public:
    explicit Bc2WeaponVisibilityProbe(bool enabled=false)noexcept:enabled_(enabled){}
    WeaponVisibilityProbeSample Tick(const ReloadStateOwner&,const interaction::HandInteractionSample&,
        std::shared_ptr<const SelectedMeshesSnapshot>,std::string_view asset,
        const std::optional<WeaponVisibilityReceipt>&)noexcept;
    void Cancel(unsigned reason,std::int64_t nowNs)noexcept;
    std::uint64_t RequestId()const noexcept{return request_;}
    WeaponVisibilityProbePhase Phase()const noexcept{return phase_;}
    bool BlocksActions()const noexcept{return enabled_;}
    void Report(std::ostream&)const;
private:
    void Record(const WeaponVisibilityProbeSample&,unsigned reason=0)noexcept;
    bool enabled_=false;WeaponVisibilityProbePhase phase_=WeaponVisibilityProbePhase::Warmup;
    unsigned failure_=0;std::uint64_t request_=0;ReloadStateOwner owner_{};
    interaction::HandInteractionOwner physical_{};
    std::shared_ptr<const SelectedMeshesSnapshot> configuration_; // Exact bounded diagnostic set; never production admission.
    std::int64_t started_=0,phaseAt_=0,lastNow_=0,lastRow_=0;
    std::uint64_t lastSequence_=0;std::int64_t originalObserved_=0,originalDeadline_=0;
    std::array<unsigned,3> receipts_{};std::array<std::uint64_t,3> lastDraw_{};
    struct Row {unsigned phase=0,reason=0;std::int64_t now=0;std::uint64_t input=0,request=0,draw=0,receiptInput=0;
        std::int64_t observed=0,deadline=0,sourceObserved=0,sourceDeadline=0;unsigned weighted=0,preserved=0;bool hidden=false;};
    std::array<Row,64> rows_{};unsigned rowCount_=0;
};
}
