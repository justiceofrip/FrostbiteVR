#pragma once
#include "Bc2ReloadState.h"
#include "fvr/interaction/HandInteraction.h"
#include "fvr/interaction/RigPose.h"
#include <cmath>

namespace fvr::bc2::private_knife {
// One chest command, not a native shot token. The adapter must prove the first
// acquisition's ancestry in the exact copied server Update before joining it.
struct ChestAttackIdentity {
    std::uint64_t serial=0,lifetime=0,operation=0,sourceEpoch=0,input=0,strokeSerial=0,holdId=0;
    ReloadStateOwner originalOwner{},selectedOwner{};
    interaction::HandInteractionOwner inputOwner{};
    interaction::HandInteractionKey knife{};
    std::int64_t observedNs=0,deadlineNs=0,geometryDeadlineNs=0;
    unsigned serverPlayer=0,serverActor=0,serverItem=0,effects=0,clientEffects=0;
    std::array<unsigned,3> firing{};
    ReloadObservedConfig config{};
    bool operator==(const ChestAttackIdentity&)const=default;
};
struct ChestAttackTarget {
    unsigned handle=0,soldier=0;
    bool operator==(const ChestAttackTarget&)const=default;
};
enum class ChestAttackPhase {Staged,Consumed,Acquiring,Acquired,Attacking,Returned,Aborted};
enum class ChestAttackOutcome {Pending,Hit,Miss,Unknown};
struct ChestAttackReceipt {
    ChestAttackIdentity identity{};
    ChestAttackOutcome outcome=ChestAttackOutcome::Pending;
    ChestAttackTarget target{};
    math::Matrix4 geometry{};
    unsigned consumed=0,geometryClaims=0,geometryForwardMask=0,damageCalls=0;
    bool acquisitionReturned=false,attackReturned=false,failed=false,healthKnown=false;
    float healthBefore=0,healthAfter=0;
};
// Adapter gate owns all access. A rejected/expired geometry lease never cancels
// already accepted native work, and no method invokes or suppresses native code.
class ChestAttackTransaction {
public:
    bool Stage(const ChestAttackIdentity& key,const math::Matrix4& geometry,std::int64_t now)noexcept {
        if(pending_||!key.serial||key.serial<=lastSerial_||!key.lifetime||!key.operation||!key.sourceEpoch||
           !key.input||!key.strokeSerial||!key.holdId||!key.knife.id||!key.knife.generation||!key.originalOwner.actorGeneration||
           key.selectedOwner.weapon!=key.knife.id||key.selectedOwner.player!=key.originalOwner.player||
           key.selectedOwner.weak!=key.originalOwner.weak||key.selectedOwner.soldier!=key.originalOwner.soldier||
           key.inputOwner.actor!=((std::uint64_t(key.selectedOwner.weak)<<32)|key.selectedOwner.soldier)||
           key.inputOwner.actorGeneration!=key.originalOwner.actorGeneration||key.inputOwner.actorGeneration!=key.selectedOwner.actorGeneration||
           !key.inputOwner.equipGeneration||!key.selectedOwner.equipGeneration||key.inputOwner.space!=key.selectedOwner.space||
           !key.serverActor||!key.serverItem||!key.effects||!key.firing[2]||!key.config.primaryFire||
           key.config.fireInputAction!=8||key.config.fireLogicType!=0||!Fresh(key,now)||
           !interaction::InverseRigid(geometry))return false;
        pending_=ChestAttackReceipt{};pending_->identity=key;lastSerial_=key.serial;
        pending_->geometry=geometry;phase_=ChestAttackPhase::Staged;geometryRevoked_=false;targetKnown_=false;return true;
    }
    bool Consume(const ChestAttackIdentity& key,unsigned branch)noexcept {
        if(!Same(key)||(branch!=1&&branch!=2)||
           (pending_->consumed&(1u<<branch)))return false;
        pending_->consumed|=1u<<branch;
        if(phase_==ChestAttackPhase::Staged)phase_=ChestAttackPhase::Consumed;
        return true;
    }
    bool BeginAcquire(const ChestAttackIdentity& key,bool exactCopiedServerUpdate,bool ownerRetained)noexcept {
        if(!Same(key)||!exactCopiedServerUpdate||!ownerRetained||!(pending_->consumed&4)||
           phase_!=ChestAttackPhase::Consumed)return false;
        phase_=ChestAttackPhase::Acquiring;return true;
    }
    std::optional<math::Matrix4> TakeGeometry(const ChestAttackIdentity& key,std::int64_t now,bool sourceCurrent)noexcept {
        if(!Same(key)||phase_!=ChestAttackPhase::Acquiring||geometryRevoked_||!sourceCurrent||
           pending_->geometryClaims||!Fresh(key,now))return {};
        ++pending_->geometryClaims;return pending_->geometry;
    }
    void GeometryReturned(const ChestAttackIdentity& key,bool retry,bool originalUnchanged)noexcept {
        if(!Same(key)||phase_!=ChestAttackPhase::Acquiring)return;
        const auto bit=retry?2u:1u;
        if(!originalUnchanged||!pending_->geometryClaims||(pending_->geometryForwardMask&bit))pending_->failed=true;
        pending_->geometryForwardMask|=bit;
    }
    bool EndAcquire(const ChestAttackIdentity& key,const std::optional<ChestAttackTarget>& target)noexcept {
        if(!Same(key)||phase_!=ChestAttackPhase::Acquiring)return false;
        pending_->acquisitionReturned=true;targetKnown_=bool(target);
        if(target)pending_->target=*target;else pending_->failed=true;
        phase_=ChestAttackPhase::Acquired;return true;
    }
    bool BeginAttack(const ChestAttackIdentity& key,const std::optional<ChestAttackTarget>& target,
        bool exactServerUpdate,bool ownerRetained)noexcept {
        if(!Same(key)||phase_!=ChestAttackPhase::Acquired||!targetKnown_||!target||
           *target!=pending_->target||!exactServerUpdate||!ownerRetained)return false;
        phase_=ChestAttackPhase::Attacking;return true;
    }
    void Damage(const ChestAttackIdentity& key,bool known,float before,float after)noexcept {
        if(!Same(key)||phase_!=ChestAttackPhase::Attacking)return;
        ++pending_->damageCalls;
        pending_->healthKnown=known&&pending_->damageCalls==1&&std::isfinite(before)&&std::isfinite(after);
        if(pending_->healthKnown){pending_->healthBefore=before;pending_->healthAfter=after;}
    }
    bool EndAttack(const ChestAttackIdentity& key)noexcept {
        if(!Same(key)||phase_!=ChestAttackPhase::Attacking)return false;
        pending_->attackReturned=true;phase_=ChestAttackPhase::Returned;
        auto& r=*pending_;r.outcome=ChestAttackOutcome::Unknown;
        if(!r.failed&&r.geometryClaims==1&&r.geometryForwardMask){
            if(!r.target.handle&&!r.target.soldier&&!r.damageCalls)r.outcome=ChestAttackOutcome::Miss;
            else if(r.damageCalls==1&&r.healthKnown&&r.healthAfter<r.healthBefore)r.outcome=ChestAttackOutcome::Hit;
        }return true;
    }
    void Fail(const ChestAttackIdentity& key)noexcept {if(Same(key)){pending_->failed=true;geometryRevoked_=true;
        if(pending_->outcome!=ChestAttackOutcome::Pending)pending_->outcome=ChestAttackOutcome::Unknown;}}
    // The accepting native frame unwound. Existing idle/copy-return proof can
    // still settle an Unknown receipt; neither native return flag is fabricated.
    void AbortCallback(const ChestAttackIdentity& key,ChestAttackPhase executing)noexcept {
        if(!Same(key)||phase_!=executing||
           (executing!=ChestAttackPhase::Acquiring&&executing!=ChestAttackPhase::Attacking))return;
        Fail(key);
        phase_=ChestAttackPhase::Aborted;
    }
    void RevokeGeometry()noexcept {geometryRevoked_=true;}
    // Existing three-copy idle/return proof may retire an unobserved callback;
    // it yields Unknown, never Hit/Miss. No timeout completes native work.
    std::optional<ChestAttackReceipt> Settle(const ChestAttackIdentity& key)noexcept {
        if(!Same(key)||pending_->consumed!=6||phase_==ChestAttackPhase::Acquiring||phase_==ChestAttackPhase::Attacking)return {};
        auto receipt=*pending_;if(receipt.outcome==ChestAttackOutcome::Pending)receipt.outcome=ChestAttackOutcome::Unknown;
        pending_.reset();return receipt;
    }
    void LifetimeEnded()noexcept {pending_.reset();geometryRevoked_=true;}
    bool Same(const ChestAttackIdentity& key)const noexcept {return pending_&&pending_->identity==key;}
    const std::optional<ChestAttackReceipt>& Pending()const noexcept {return pending_;}
    ChestAttackPhase Phase()const noexcept {return phase_;}
    static bool Fresh(const ChestAttackIdentity& k,std::int64_t now)noexcept {
        return k.observedNs>0&&now>=k.observedNs&&now<k.geometryDeadlineNs&&k.geometryDeadlineNs<=k.deadlineNs&&
            k.deadlineNs-k.observedNs<=100000000;
    }
private:
    std::optional<ChestAttackReceipt> pending_;
    ChestAttackPhase phase_=ChestAttackPhase::Staged;std::uint64_t lastSerial_=0;
    bool geometryRevoked_=false,targetKnown_=false;
};
}
