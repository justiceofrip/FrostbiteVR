#pragma once
#include "Bc2ReloadFlowRuntime.h"
#include "fvr/interaction/ControllerInput.h"

namespace fvr::bc2 {
// Explicit, bounded diagnostic only. Scripted reservation; no physical contact,
// mesh, held-state, authority or acknowledgement evidence is fabricated.
struct MagazineReloadProbeApi {
    decltype(&reloadFlowRuntime::RequestIdentity) identity=nullptr;
    decltype(&reloadFlowRuntime::StartMagazineRequestCycle) start=nullptr;
    decltype(&reloadFlowRuntime::KeepAliveRequestCycle) keep=nullptr;
    decltype(&reloadFlowRuntime::RequestMagazineLease) lease=nullptr;
    decltype(&reloadFlowRuntime::SubmitMagazineRequest) submit=nullptr;
    decltype(&reloadFlowRuntime::TakeMagazineAcknowledgement) ack=nullptr;
    decltype(&reloadFlowRuntime::CancelRequestCycle) cancel=nullptr;
    decltype(&reloadFlowRuntime::ReadRequestProbeSnapshot) snapshot=nullptr;
    std::int64_t (*clock)()noexcept=nullptr;
    decltype(&reloadFlowRuntime::RetireRequestCycle) retire=nullptr;
    decltype(&reloadFlowRuntime::ReadReserve) reserve=nullptr;
    decltype(&reloadFlowRuntime::TakeMagazineUnseatAcknowledgement) unseatAck=nullptr;
};
class Bc2MagazineReloadProbe {
public:
    explicit Bc2MagazineReloadProbe(bool enabled=false,MagazineReloadProbeApi api={},bool cancelOnly=false)noexcept:enabled_(enabled),api_(api),cancelOnly_(cancelOnly){}
    void Tick(const interaction::InputFrame&,const interaction::HandInteractionOwner&,
        std::int64_t observedNs,std::int64_t deadlineNs,std::int64_t nowNs)noexcept;
    void Stop(std::int64_t nowNs)noexcept;
    void Report(std::ostream&)const;
private:
    enum Event:unsigned {Sample,Started,Held,Submitted,Acknowledged,Cancelled,Failed,Unseated};
    struct Row {
        unsigned event=0,reason=0;std::int64_t now=0,source=0,deadline=0;
        std::uint64_t sequence=0,cycle=0,request=0,serverInvocation=0;
        std::optional<ReloadMagazineLease> lease;
        std::optional<reloadFlowRuntime::RequestProbeSnapshot> native;
    };
    void Record(Event,unsigned,std::int64_t,const std::optional<ReloadMagazineLease>& = {})noexcept;
    void Fail(unsigned,std::int64_t)noexcept;
    void PollRetirement(std::int64_t nowNs)noexcept;
    bool enabled_=false,started_=false,stopped_=false,submitted_=false,acknowledged_=false;
    MagazineReloadProbeApi api_{};
    bool cancelOnly_=false,unseatAcknowledged_=false;
    ReloadHoldIdentity identity_{};interaction::HandInteractionOwner physical_{};
    std::uint64_t sequence_=0,cycle_=0,request_=0,serverInvocation_=0;
    std::int64_t source_=0,deadline_=0,first_=0,last_=0,kept_=0,polled_=0,sampled_=0,ackAt_=0,heldAt_=0;
    int loaded_=-1,reserve_=-1;float marker_=0;
    std::array<Row,160> rows_{};unsigned count_=0,dropped_=0,reason_=0;
    std::optional<ReloadCycleRetirement> retirement_;
    std::optional<Bc2AmmoReserveLease> postRetirementReserve_;
    std::int64_t stoppedAt_=0,retirementPoll_=0,retirementChecked_=0,reserveChecked_=0;
    unsigned retirementAttempts_=0,reserveAttempts_=0,retirementReason_=0,retirementExpired_=0;
};
// Private fixture markers: no firing, equipment change, use or grasp actions.
struct MagazineReloadProbeSchedule {float marker=0;bool reload=false;};
inline MagazineReloadProbeSchedule MagazineProbeSchedule(std::uint64_t elapsedMs)noexcept {
    return {elapsedMs>=5800&&elapsedMs<14000?(elapsedMs>=9000&&elapsedMs<9200?1.f:.25f):0.f,
        elapsedMs>=6000&&elapsedMs<6200};
}
}
