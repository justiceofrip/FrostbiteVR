#include "Bc2CampaignProvisioning.h"
#include "Bc2CampaignProvisioningVariants.h"
#include <algorithm>

namespace fvr::bc2::campaignProvisioning {
WireReceipt ToWire(const Receipt& r)noexcept {
    return {0x31524342,1,sizeof(WireReceipt),std::uint32_t(r.status),r.process,r.nonce,r.job,r.preflight,r.retirements,
        r.native.dispatchSequence,r.observedNs,r.native.dispatchNs,r.previous,r.current,std::uint32_t(r.native.phase),
        r.native.nativeCalls,r.native.returnedItem,r.native.thread,std::uint32_t(r.boundary),
        r.native.serverItemObserved,r.native.clientServerConverged,r.inputExcluded,r.oldItemRetired,0};
}
policy::Owner ProvisionOwner(const ReloadServerSnapshot& s,std::uint64_t session)noexcept {
    return {session,s.client.owner.actorGeneration,s.client.owner.space,s.links.player,s.links.soldier,
        s.links.inventory,s.links.slot,s.links.item,s.client.config.weaponData,
        {s.client.branches[0].address,s.client.branches[1].address,s.links.firing}};
}
bool Service::Configure(const ProcessIdentity& process,const policy::Memory& memory,const policy::Host& host,
                        const LifetimeHost& lifetime,bool explicitRequest)noexcept {
    if(configured_||stopped_||!explicitRequest||!process.pid||process.reserved||!process.creation||!process.session||
       !process.executable[0]||process.executable.back()!=0||
       std::all_of(process.image.begin(),process.image.end(),[](auto b){return b==0;})||
       !memory.read||!memory.type||!host.owner||!host.assign||!lifetime.exclude||!lifetime.retire||!lifetime.resume)
        return false;
    process_=process;memory_=memory;host_=host;lifetime_=lifetime;configured_=true;
    receipt_.bytes=sizeof(Receipt);receipt_.process=process;receipt_.status=Status::Ready;return true;
}
bool Service::Submit(const Request& request,std::int64_t now)noexcept {
    if(!configured_||stopped_||receipt_.status==Status::Quarantined||request_||
       request.magic!=0x31504342||request.version!=1||request.bytes!=sizeof(Request)||request.reserved||
       request.process!=process_||!request.nonce||request.nonce<=lastNonce_||!request.job||
       (job_&&request.job!=job_)||request.issuedNs<=0||request.issuedNs>now||request.deadlineNs<=now||
       request.deadlineNs-request.issuedNs>2000000000ll||request.source.session!=process_.session||
       !request.sourceSequence||request.sourceObservedNs<=0||request.sourceObservedNs>request.issuedNs||
       request.issuedNs-request.sourceObservedNs>200000000||
       (request.operation!=Operation::Observe&&request.operation!=Operation::Equip&&request.operation!=Operation::Restore))return false;
    if(request.operation==Operation::Restore){
        if(restoreUsed_||policy_.Receipt().phase!=policy::ProvisionPhase::Equipped||request.targetAsset||request.targetData)return false;
    }else if(policy_.Receipt().phase!=policy::ProvisionPhase::Preflight||armed_)return false;
    if(request.operation==Operation::Equip&&!policy::FindProvisionVariant(request.variant))return false;
    if(request.operation==Operation::Observe&&(request.targetAsset||request.targetData))return false;
    request_=request;dispatchArmed_=false;lastNonce_=request.nonce;job_=request.job;receipt_.nonce=request.nonce;receipt_.job=request.job;
    receipt_.status=request.operation==Operation::Observe?Status::Observing:Status::Preflight;
    receipt_.observedNs=now;
    admissionReceipt_={};admissionReceipt_.bytes=sizeof(AdmissionReceipt);admissionReceipt_.process=process_;
    admissionReceipt_.nonce=request.nonce;admissionReceipt_.job=request.job;
    admissionReceipt_.operation=std::uint32_t(request.operation);admissionReceipt_.acceptedNs=now;
    admissionReceipt_.issuedNs=request.issuedNs;admissionReceipt_.deadlineNs=request.deadlineNs;
    admissionReceipt_.status=std::uint32_t(receipt_.status);admissionTerminal_=false;
    admission_.Begin(now,request.deadlineNs);controlRefusals_.Reset();return true;
}
AdmissionReceipt Service::ReadAdmission()const noexcept {
    auto out=admissionReceipt_;const auto counters=admission_.Read();
    out.scope=counters.scope;out.frozen=counters.frozen;out.saturatedMask=counters.saturatedMask;out.counts=counters.counts;
    if(!admissionTerminal_)out.status=std::uint32_t(receipt_.status);
    return out;
}
void Service::RecordControlRefusal(std::uint64_t token,const policy::Boundary& boundary,const ControlAcquisition& acquisition)noexcept {
    // Existing runtime gate serializes submit, this write, freeze and reads.
    // Reuse the captured Boundary clock; no timer/lease extension or new read.
    if(admissionTerminal_||!token||token!=admission_.Token()||
       boundary.nowNs<admissionReceipt_.acceptedNs||boundary.nowNs>=admissionReceipt_.deadlineNs)return;
    controlRefusals_.Record({boundary.nowNs,boundary.sequence,boundary.thread,boundary.returnAddress,
        boundary.seat,boundary.nativeFiringDepth,boundary.seatDepth,acquisition,0});
}
ControlRefusalReceipt Service::ReadControlRefusals()const noexcept {
    ControlRefusalReceipt out;out.bytes=sizeof(out);out.admission=ReadAdmission();out.details=controlRefusals_.Read();return out;
}
void Service::FreezeAdmission(std::int64_t now)noexcept {
    admission_.Freeze();
    if(!admissionTerminal_){admissionTerminal_=true;
        admissionReceipt_.status=std::uint32_t(receipt_.status);admissionReceipt_.frozenNs=now;}
}
void Service::ObserveServer(const ReloadServerSnapshot& source,std::uint32_t firing,std::uint32_t thread,
                            std::uint64_t sequence,std::int64_t now)noexcept {
    if(!configured_||stopped_||receipt_.status==Status::Quarantined||firing!=source.links.firing||
       (!request_&&policy_.Receipt().phase==policy::ProvisionPhase::Preflight))return;
    const auto owner=ProvisionOwner(source,process_.session);
    if(!host_.owner(host_.context,owner,now))return;
    policy_.ObserveServerUpdate(owner,thread,sequence,now);
}
bool Service::Source(const Request& request,const ReloadServerSnapshot& source,const policy::Boundary& b)const noexcept {
    return b.owner==ProvisionOwner(source,process_.session)&&b.owner==request.source&&source.client.owner==request.client&&
        source.client.sequence>=request.sourceSequence&&source.client.observedNs>=request.sourceObservedNs&&
        b.ownerObservedNs==source.client.observedNs;
}
void Service::Quarantine(std::int64_t now)noexcept {
    receipt_.status=Status::Quarantined;receipt_.inputExcluded=true;receipt_.observedNs=now;request_.reset();FreezeAdmission(now);
}
void Service::FinishUnsent(Status status,std::int64_t now,const ReloadServerSnapshot* source)noexcept {
    policy_.CancelUnsent();
    if(receipt_.inputExcluded&&(!source||!lifetime_.resume(lifetime_.context,*source,receipt_.nonce))){Quarantine(now);return;}
    receipt_.inputExcluded=false;receipt_.status=status;receipt_.observedNs=now;request_.reset();FreezeAdmission(now);
}
void Service::SeatEntry(const ReloadServerSnapshot& source,const policy::Boundary& b)noexcept {
    // A nested native seat call cannot expire or clear the outer in-flight
    // request. Only the verified top-level seat boundary owns this transition.
    // A completed request owns an immutable receipt until a fresh Submit.
    // In particular, a later SeatEntry cannot overwrite Expired current/age.
    if(!configured_||stopped_||receipt_.status==Status::Quarantined||!request_)return;
    admission_.Record(admission_.Token(),b.seatDepth==1?AdmissionEvent::ServiceEntered:AdmissionEvent::NestedDepth,b.nowNs);
    if(b.seatDepth!=1)return;
    receipt_.observedNs=b.nowNs;receipt_.current=source.client.owner;
    // Seat callbacks enforce the same terminal deadline as worker expiry.
    // In particular, late convergence cannot reopen excluded input.
    if(request_&&b.nowNs>=request_->deadlineNs){Expire(b.nowNs);return;}
    if(b.owner!=ProvisionOwner(source,process_.session)||b.ownerObservedNs!=source.client.observedNs)return;
    receipt_.boundary=policy_.Diagnose(memory_,host_,b);
    const auto phase=policy_.Receipt().phase;
    if(phase==policy::ProvisionPhase::AwaitingReplication||phase==policy::ProvisionPhase::AwaitingRestore){
        if(policy_.ObserveConvergence(memory_,host_,b)){
            receipt_.native=policy_.Receipt();
            if(!receipt_.oldItemRetired||!lifetime_.resume(lifetime_.context,source,receipt_.nonce)){Quarantine(b.nowNs);return;}
            receipt_.inputExcluded=false;receipt_.status=phase==policy::ProvisionPhase::AwaitingRestore?Status::Restored:Status::Equipped;
            request_.reset();FreezeAdmission(b.nowNs);
        }
        return;
    }
    if(!request_)return;
    if(!Source(*request_,source,b)){FinishUnsent(Status::Refused,b.nowNs,&source);return;}
    if(receipt_.boundary!=policy::BoundaryFailure::None)return;
    if(phase==policy::ProvisionPhase::Preflight){
        if(policy_.Preflight(memory_,host_,b))++receipt_.preflight;
        if(receipt_.preflight<8)return; // Retained bench requirement, not policy's lower minimum.
        if(request_->operation==Operation::Observe){receipt_.status=Status::Ready;request_.reset();FreezeAdmission(b.nowNs);return;}
        target_=policy::SelectExactProvisionResource(memory_,request_->targetData,request_->targetAsset,request_->variant);
        if(!target_){FinishUnsent(Status::Refused,b.nowNs,&source);return;}
    }
    if(!dispatchArmed_){
        const auto exclusion=lifetime_.exclude(lifetime_.context,source,request_->nonce);
        if(exclusion==LifetimeHost::Exclusion::Refused){FinishUnsent(Status::Refused,b.nowNs,&source);return;}
        receipt_.inputExcluded=true;receipt_.status=Status::Excluded;receipt_.previous=source.client.owner;
        retiredSource_=source;receipt_.oldItemRetired=false;
        if(exclusion==LifetimeHost::Exclusion::Pending)return;
        const bool restore=request_->operation==Operation::Restore;
        const bool armed=restore?policy_.RequestRestore(b,request_->deadlineNs):
            policy_.Arm(memory_,host_,b,*target_,request_->deadlineNs);
        if(!armed){FinishUnsent(Status::Refused,b.nowNs,&source);return;}
        armed_=dispatchArmed_=true;if(restore)restoreUsed_=true;
    }
    // The retained policy disarms BEFORE Host.assign. No server Update path
    // calls this method; no reentrant or ambiguous dispatch is retried.
    if(!policy_.AtSeatEntry(memory_,host_,b))return;
    receipt_.native=policy_.Receipt();
    if(receipt_.native.phase==policy::ProvisionPhase::Indeterminate){Quarantine(b.nowNs);return;}
    const auto desired=request_->operation==Operation::Restore?policy_.Original().data:target_->data;
    if(!receipt_.native.serverItemObserved||!retiredSource_||
       !lifetime_.retire(lifetime_.context,*retiredSource_,receipt_.native.returnedItem,desired,request_->nonce,b.nowNs)){
        Quarantine(b.nowNs);return;
    }
    ++receipt_.retirements;receipt_.oldItemRetired=true;
    receipt_.status=request_->operation==Operation::Restore?Status::AwaitingRestore:Status::AwaitingReplication;
}
void Service::Stop()noexcept {
    if(stopped_)return;stopped_=true;policy_.CancelUnsent();request_.reset();
    // Terminal session cleanup owns release. Stop never reopens an exclusion.
    receipt_.status=Status::Stopped;FreezeAdmission(0); // Stop has no observation timestamp.
}
void Service::Expire(std::int64_t now)noexcept {
    if(!request_||stopped_||now<request_->deadlineNs)return;
    if(receipt_.inputExcluded)Quarantine(now);
    else FinishUnsent(Status::Expired,now,nullptr);
}
}
