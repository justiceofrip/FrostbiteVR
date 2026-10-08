#pragma once
#include "Bc2ReloadDrawCapture.h"
#include <array>
#include <mutex>
#include <optional>
#include <span>
#include <atomic>
namespace fvr::bc2 {
struct ReloadProducerView {
    std::uint64_t world=0,request=0,view=0;
    std::uint32_t nativeFrame=0;unsigned eye=0;
    bool operator==(const ReloadProducerView&)const=default;
};
struct ReloadProducerOwner {
    std::uint64_t actor=0,weak=0,weapon=0,ownerGeneration=0,space=0;
    bool operator==(const ReloadProducerOwner&)const=default;
};
struct ReloadPackedSource {
    ReloadProducerOwner owner{};
    std::uint64_t rigPose=0,rigFingerprint=0,inputGeneration=0,selectedMeshes1p=0;
    std::int64_t observedNs=0,deadlineNs=0;
    unsigned boneCount=0,shellIndex=0,opticIndex=0;
    bool shellNamed=false,opticNamed=false,shellHidden=false,selectedMeshIdentityVerified=false;
    std::uint64_t holdCycle=0;std::int64_t holdBeginNs=0,holdDeadlineNs=0;
    std::shared_ptr<const SelectedMeshesSnapshot> opticSelected;
    std::uint64_t physicalEquipmentGeneration=0;
    bool operator==(const ReloadPackedSource&)const=default;
};
struct ReloadProducerSnapshot {
    ReloadProducerView key{};ReloadDrawProducerEvidence evidence{};
    std::array<std::uint64_t,2> packDestinations{};
    std::uint64_t packedHash=0;
    unsigned boneCount=0;
};
// Diagnostic-only, no native memory access. Call around the VERIFIED original
// visibility/prepare invocation, then ObservePacked from its owned actor packer.
// Thread-local dynamic scope is required: an async worker gets NO guessed join.
// Source/output bytes are compared now and only derived immutable metadata is kept.
class Bc2ReloadProducerBinding {
public:
    static constexpr unsigned Slots=32;
    void Enable(bool value)noexcept {enabled_.store(value,std::memory_order_release);}
    bool Enabled()const noexcept{return enabled_.load(std::memory_order_acquire);}
    struct Counters {std::uint64_t outsideScope=0,invalidPack=0,pairs=0,ambiguous=0,scopeRejected=0,busy=0,published=0,readMisses=0;};
    class Scope {
    public:
        Scope(Bc2ReloadProducerBinding&,ReloadProducerView,bool nativeOwnerVerified,std::int64_t beginNs)noexcept;
        ~Scope();Scope(const Scope&)=delete;Scope& operator=(const Scope&)=delete;
        // Must be called after verifying the SAME native owner/frame after the
        // original call. Missing completion or exception unwinding publishes none.
        void Complete(bool nativeOwnerStillExact,std::int64_t endNs)noexcept;
    private:
        friend class Bc2ReloadProducerBinding;
        Bc2ReloadProducerBinding& recorder_;Scope* previous_=nullptr;
        ReloadProducerView key_{};std::int64_t beginNs_=0,lastPackNs_=0;
        bool admitted_=false,completed_=false,bad_=false,first_=false;
        unsigned pairs_=0;std::uint64_t pairSerial_=0;
        ReloadPackedSource source_{};ReloadProducerSnapshot result_{};
    };
    // Zero/first=false slot must follow the matching first slot in this scope.
    // actualInput64 is the source actually passed to the original packer (which
    // can be posed or preview-fallback data), NOT the preceding animation pose.
    void ObservePacked(std::uint64_t pairSerial,bool first,const ReloadPackedSource&,
        std::uint64_t destination,std::int64_t packNs,std::span<const std::byte> actualInput64,
        std::span<const std::byte> observedOutput48)noexcept;
    std::optional<ReloadProducerSnapshot> Read(const ReloadProducerView&,const ReloadProducerOwner& currentOwner,std::int64_t nowNs)noexcept;
    Counters Statistics()const noexcept;
    void Report(std::ostream&)noexcept;
private:
    static thread_local Scope* current_;
    struct Slot {ReloadProducerSnapshot value{};bool used=false,ambiguous=false;};
    void Publish(const ReloadProducerSnapshot&)noexcept;
    std::atomic<bool> enabled_=false;
    std::array<Slot,Slots> slots_{};unsigned next_=0;std::mutex mutex_;
    std::atomic<std::uint64_t> outsideScope_=0,invalidPack_=0,pairs_=0,ambiguous_=0,scopeRejected_=0,busy_=0,published_=0,readMisses_=0;
};
Bc2ReloadProducerBinding& ReloadProducerBinding()noexcept;
}
