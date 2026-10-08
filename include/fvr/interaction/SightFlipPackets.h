#pragma once
#include "SightFlip.h"
#include <array>

namespace fvr::interaction {
// Raw button/eligibility evidence retained before any support-grip correction.
// Geometry is intentionally absent: it belongs to the separately published pose.
struct SightFlipInputPacket {
    SightFlipSample sample{};
    std::uint64_t weapon=0;
    std::int64_t deadline=0;
    bool eligible=false,awaitingAcknowledgement=false,latched=false;
};
struct SightFlipContactPacket {
    bool valid=false,preferred=false;
    std::uint64_t generation=0;
    std::int64_t deadline=0;
    math::Vec3 handLocalMeters{};
    float distanceMeters=0;
};
enum class SightFlipPacketReason : std::uint8_t {
    Paired,Duplicate,Released,InvalidInput,WaitingPublication,MissingHistory,ExpiredPublication,NativeAcknowledgement,LatchedTracking
};
struct SightFlipPacketResult {
    SightFlipSample sample{};
    bool paired=false,advanced=false;
    // Original contact proof, including when Latched samples use the current
    // raw sequence for safety/freshness rather than measuring gesture movement.
    std::uint64_t geometrySequence=0;
    SightFlipPacketReason reason=SightFlipPacketReason::WaitingPublication;
};
// A native gather can read XR packet N while the render publication still uses
// N-1. Consume a squeeze and geometry only when their generation and complete
// ownership agree. All clocks/deadlines retain the caller's original units.
class SightFlipPackets {
public:
    SightFlipPacketResult Update(const SightFlipInputPacket& current,
        const SightFlipContactPacket& contact,std::int64_t nowQpc) noexcept;
    void Reset() noexcept {*this={};}
private:
    std::array<SightFlipInputPacket,32> history_{};
    unsigned next_=0;
    SightFlipOwner owner_{};
    std::uint64_t weapon_=0,latestInput_=0,releaseBarrier_=0,geometryGeneration_=0;
    std::optional<SightFlipSample> retained_{};
    std::int64_t retainedDeadline_=0;
};
inline SightFlipPacketResult SightFlipPackets::Update(const SightFlipInputPacket& current,
    const SightFlipContactPacket& contact,std::int64_t nowQpc) noexcept {
    const auto& live=current.sample;
    SightFlipPacketResult out;
    const auto decorate=[&](SightFlipSample sample){
        sample.owner=live.owner;sample.nowNs=live.nowNs;
        sample.focused=sample.focused&&live.focused;sample.tracked=sample.tracked&&live.tracked;
        sample.nativeModeValid=live.nativeModeValid;sample.nativeMode=live.nativeMode;
        sample.acknowledgedRequest=live.acknowledgedRequest;return sample;
    };
    if(!live.owner.actor||!live.owner.generation||!live.owner.equipped||!live.owner.space||
       !current.weapon||!live.sequence||live.nowNs<=0||nowQpc<=0||current.deadline<=nowQpc||
       !live.focused||!live.tracked||!live.nativeModeValid||
       !std::isfinite(live.squeeze)||live.squeeze<0||live.squeeze>1){
        Reset();out.sample=live;out.sample.contactValid=false;out.sample.tracked=false;
        out.reason=SightFlipPacketReason::InvalidInput;return out;
    }
    if(owner_!=live.owner||(latestInput_&&live.sequence<latestInput_)){
        Reset();owner_=live.owner;
    }
    if(weapon_&&weapon_!=current.weapon&&
       !(current.awaitingAcknowledgement&&live.acknowledgedRequest)){
        Reset();out.sample=live;out.sample.contactValid=false;out.sample.tracked=false;
        out.reason=SightFlipPacketReason::InvalidInput;return out;
    }
    if(weapon_!=current.weapon){
        history_={};next_=0;weapon_=current.weapon;geometryGeneration_=0;
        // Preserve the semantic policy sequence for an expected native mode
        // handoff, but never retain geometry from the previous equipment slot.
        if(retained_)retained_->contactValid=false;
        retainedDeadline_=0;
    }
    if(live.sequence!=latestInput_){
        history_[next_]=current;next_=(next_+1)%history_.size();latestInput_=live.sequence;
    }
    // Release is current raw evidence and must win over every older publication.
    // The neutral packet needs no geometry; it can arm the subsequent press.
    if(live.squeeze<=.35f){
        releaseBarrier_=live.sequence;out.sample=live;out.sample.contactValid=false;
        retained_=out.sample;retainedDeadline_=0;out.reason=SightFlipPacketReason::Released;return out;
    }
    const SightFlipInputPacket* source=nullptr;
    const bool contactFresh=contact.valid&&contact.generation&&contact.deadline>nowQpc;
    if(contactFresh&&contact.generation>releaseBarrier_&&
       (!retained_||contact.generation>=retained_->sequence)){
        for(const auto& candidate:history_)if(candidate.sample.sequence==contact.generation&&
            candidate.weapon==current.weapon&&candidate.sample.owner==live.owner&&
            candidate.deadline>nowQpc){source=&candidate;break;}
    }
    if(current.latched){
        // The policy already committed its exact native request. Current input
        // may keep that reservation alive while the native mode settles; no
        // stale contact, pose motion, button edge or acknowledgement is invented.
        out.sample=live;out.sample.contactValid=false;
        out.reason=SightFlipPacketReason::LatchedTracking;
        if(source){
            out.sample.contactValid=contact.preferred&&source->eligible&&current.eligible;
            out.sample.handLocalMeters=contact.handLocalMeters;out.sample.contactDistanceMeters=contact.distanceMeters;
            out.paired=true;out.geometrySequence=contact.generation;out.reason=SightFlipPacketReason::Paired;
        }
        return out;
    }
    if(source&&contact.generation!=geometryGeneration_){
        out.sample=decorate(source->sample);
        out.sample.contactValid=contact.preferred&&source->eligible&&current.eligible;
        out.sample.handLocalMeters=contact.handLocalMeters;out.sample.contactDistanceMeters=contact.distanceMeters;
        out.paired=true;out.geometrySequence=contact.generation;out.advanced=true;out.reason=SightFlipPacketReason::Paired;
        geometryGeneration_=contact.generation;
        retained_=out.sample;retainedDeadline_=std::min(contact.deadline,source->deadline);
        return out;
    }
    if(source&&retained_&&contact.generation==geometryGeneration_){
        out.sample=decorate(*retained_);
        out.sample.contactValid=out.sample.contactValid&&contact.preferred&&current.eligible&&retainedDeadline_>nowQpc;
        out.paired=true;out.geometrySequence=contact.generation;out.reason=SightFlipPacketReason::Duplicate;return out;
    }
    // The policy itself validates the request id and native mode. Fresh raw
    // tracking may acknowledge that transition without borrowing old geometry.
    if(current.awaitingAcknowledgement&&live.acknowledgedRequest){
        out.sample=live;out.sample.contactValid=false;retained_=out.sample;retainedDeadline_=0;
        out.reason=SightFlipPacketReason::NativeAcknowledgement;return out;
    }
    out.sample=retained_?decorate(*retained_):live;
    if(!retained_)out.sample.sequence=0; // An unmatched press cannot initialize a gesture.
    out.sample.contactValid=false;
    out.reason=contact.valid&&!contactFresh?SightFlipPacketReason::ExpiredPublication:
        (contactFresh?SightFlipPacketReason::MissingHistory:SightFlipPacketReason::WaitingPublication);
    return out;
}
}

