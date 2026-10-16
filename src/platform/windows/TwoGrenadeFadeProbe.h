#pragma once
#include "OrdinaryGrenadeFadeProbe.h"
#include "fvr/ipc/FrameChannel.h"
#include "../../games/bc2/Bc2TwoGrenadeDeathWitness.h"
#include <bit>
#include <limits>
#include <optional>

namespace fvr::probe {
constexpr unsigned TwoGrenadeFadeDurationMs=12000,TwoGrenadeFadeIntervalMs=250,TwoGrenadeFadePairLimit=48;
constexpr bool TwoGrenadeFadeOption(std::wstring_view s)noexcept {
    return s==L"--two-grenade-fade-probe"||s==L"--fade-window"||s==L"--pairs"||s==L"--static-pose"||s==L"--async";
}
constexpr bool TwoGrenadeFadeWindow(std::uint64_t start,unsigned duration,unsigned interval)noexcept {
    return start&&start<=UINT64_MAX-TwoGrenadeFadeDurationMs&&duration==TwoGrenadeFadeDurationMs&&interval==TwoGrenadeFadeIntervalMs;
}
constexpr bool TwoGrenadeFadeRequest(std::uint64_t now,std::uint64_t start,unsigned consumed)noexcept {
    return consumed<(now<start?240-TwoGrenadeFadePairLimit:240);
}
inline std::optional<float> DeathHealth(std::uint64_t bits)noexcept {
    if(bits>UINT32_MAX)return {};
    const auto f=std::bit_cast<float>(static_cast<std::uint32_t>(bits));
    return std::isfinite(f)&&f>=0?std::optional<float>{f}:std::nullopt;
}
inline bool ValidDeathHealth(const bc2::TwoGrenadeHealthWitness& h,std::uint64_t pid,std::uint64_t creation,std::uint64_t now)noexcept {
    const auto initial=DeathHealth(h.initialClient),client=DeathHealth(h.client),server=DeathHealth(h.server),maximum=DeathHealth(h.maximum);
    return h.version==1&&h.pid==pid&&h.creation==creation&&h.valid==1&&h.index<400&&h.tickMs<=now&&now-h.tickMs<=100&&
        h.player&&h.soldier&&h.weak&&h.serverPlayer&&h.serverSoldier&&h.initialClient==h.initialServer&&initial&&*initial>0&&
        client&&server&&maximum&&*maximum>0&&*initial<=*maximum&&*client<=*maximum&&*server<=*maximum&&h.client==h.server;
}
inline bool SameDeathHealthOwner(const bc2::TwoGrenadeHealthWitness& a,const bc2::TwoGrenadeHealthWitness& b)noexcept {
    return a.pid==b.pid&&a.creation==b.creation&&a.player==b.player&&a.soldier==b.soldier&&a.weak==b.weak&&
        a.serverPlayer==b.serverPlayer&&a.serverSoldier==b.serverSoldier&&a.initialClient==b.initialClient&&a.initialServer==b.initialServer&&a.maximum==b.maximum;
}
inline bool FreshSecondGrenade(const bc2::TwoGrenadeNativeWitness& n,const bc2::TwoGrenadeHealthWitness& h,
    std::uint64_t pid,std::uint64_t creation,std::uint64_t nowMs,std::int64_t nowNs)noexcept {
    return nowNs>0&&n.version==1&&n.pid==pid&&n.creation==creation&&n.player==h.player&&n.soldier==h.soldier&&n.weak==h.weak&&
        n.actorGeneration&&n.space==0x200000001ull&&n.weapon&&n.attempts==1&&n.edges==1&&n.completed==1&&n.phase==0&&n.fault==0&&
        n.input&&n.returnInput&&n.neutralInput>n.returnInput&&n.input>=n.neutralInput&&
        n.returnObservedNs&&n.neutralObservedNs>n.returnObservedNs&&n.observedNs>=n.neutralObservedNs&&
        n.observedNs<=std::uint64_t(nowNs)&&n.deadlineNs>std::uint64_t(nowNs)&&n.deadlineNs>n.observedNs&&
        n.deadlineNs-n.observedNs<=100000000&&n.tickMs<=nowMs&&nowMs-n.tickMs<=100;
}
// One scheduled initial hold, followed by at most one evidence-gated hold.
// Missing observations never become death or a delayed retry of either hold.
class TwoGrenadeFadeGesture {
public:
    bool Held(std::uint64_t nowMs,std::int64_t nowNs,std::uint64_t start,bool pair,
        std::uint64_t pid,std::uint64_t creation,const bc2::TwoGrenadeHealthWitness* health,
        const bc2::TwoGrenadeNativeWitness* native)noexcept {
        if(aborted_)return false;
        if(lastMs_&&nowMs<lastMs_){Abort();return false;}lastMs_=nowMs;
        if(nowMs<start)return false;
        const auto elapsed=nowMs-start;
        if(health&&health->version==1&&health->pid==pid&&health->creation==creation&&health->player&&health->soldier&&health->weak&&pin_&&!SameDeathHealthOwner(*pin_,*health)){Abort();return false;}
        const bool valid=health&&ValidDeathHealth(*health,pid,creation,nowMs);
        if(valid&&pin_&&(!DeathHealth(health->client)||*DeathHealth(health->client)==0)){zeroObserved_=true;return false;}
        if(!firstDecided_&&elapsed>=1000){
            if(!pair||elapsed>1100){firstDecided_=true;Abort();return false;}
            // A missed health slot supplies no action authority. Keep this one
            // admission undecided only within its existing absolute window.
            if(!valid)return false;
            firstDecided_=true;
            if(health->client!=health->initialClient){Abort();return false;}
            pin_=*health;firstAdmitted_=true;
        }
        if(zeroObserved_)return false;
        if(firstAdmitted_&&elapsed<1650)return true;
        if(!firstAdmitted_||zeroObserved_)return false;
        if(secondAt_)return nowMs-secondAt_<650;
        if(elapsed>=6500)return false;
        if(!valid||!pin_||!SameDeathHealthOwner(*pin_,*health))return false;
        const auto initial=DeathHealth(health->initialClient),current=DeathHealth(health->client);
        if(!initial||!current||*current<=0||*current>=*initial)return false;
        if(!firstDamageAt_)firstDamageAt_=nowMs;
        // Do not defer a second attempt until regeneration or later damage.
        if(nowMs-firstDamageAt_>500){Abort();return false;}
        if(!native||!FreshSecondGrenade(*native,*health,pid,creation,nowMs,nowNs))return false;
        secondAt_=nowMs;secondAtNs_=nowNs;secondNative_=*native;secondHealth_=*health;return true;
    }
    void ObservePublished(bool requested,bool held,bool known,bool success,unsigned code)noexcept {
        if(requested!=held){Abort();return;}
        // Busy failed before the channel wrote any packet. Keep the delivered
        // edge baseline and this recipe's original absolute deadlines; the next
        // loop evaluates its current input normally, without replaying a packet.
        if(known&&!success&&code==unsigned(ipc::ChannelResult::Busy))return;
        if(!known||!success||code!=unsigned(ipc::ChannelResult::Ok)){
            if(requested||lastHeld_)Abort();return;
        }
        if(held&&!lastHeld_){if(++presses_>2)Abort();}
        if(!held&&lastHeld_)++releases_;
        lastHeld_=held;
    }
    void Abort()noexcept{aborted_=true;}
    bool Aborted()const noexcept{return aborted_;}
    bool FirstAdmitted()const noexcept{return firstAdmitted_;}
    bool ZeroObserved()const noexcept{return zeroObserved_;}
    unsigned Presses()const noexcept{return presses_;}
    unsigned Releases()const noexcept{return releases_;}
    std::uint64_t SecondAt()const noexcept{return secondAt_;}
    std::int64_t SecondAtNs()const noexcept{return secondAtNs_;}
    const auto& SecondNative()const noexcept{return secondNative_;}
    const auto& SecondHealth()const noexcept{return secondHealth_;}
private:
    std::optional<bc2::TwoGrenadeHealthWitness> pin_;
    bc2::TwoGrenadeNativeWitness secondNative_{};bc2::TwoGrenadeHealthWitness secondHealth_{};
    std::uint64_t lastMs_=0,firstDamageAt_=0,secondAt_=0;
    std::int64_t secondAtNs_=0;
    unsigned presses_=0,releases_=0;bool firstDecided_=false,firstAdmitted_=false,zeroObserved_=false,aborted_=false,lastHeld_=false;
};
}
