#pragma once
#include <array>
#include <atomic>
#include <cstdint>
#include <type_traits>

namespace fvr::runtime {
enum class SceneMilestoneKind : std::uint32_t {
    Rebuild, WorldUpdate, GameplayUpdate, LocalGameplayUpdate,
    MainVisibility, EyeVisibility, MainDraw, EyeDraw, FinishTrackedFrame, OwnerTransition
};
enum class SceneMilestoneStage : std::uint32_t { Enter, CallingOriginal, OriginalReturned, Exit };
enum class SceneOwnerOutcome : std::uint32_t { Unknown, Valid, Disabled, Unresolved, PlayerMismatch, WrongThread };
struct SceneMilestoneScene {
    std::uint64_t world=0, request=0, main=0, eye=0, frame=0;
};
struct SceneMilestoneContext {
    std::uint64_t self=0, player=0, soldier=0, weak=0, controlled=0, manager=0, entry=0;
    std::uint64_t actorGeneration=0, inputEpoch=0;
    std::uint32_t inputThread=0, outcome=0;
};
struct SceneMilestoneStamp { std::uint64_t qpc=0, tickMs=0; std::uint32_t thread=0; };
struct SceneMilestoneToken {
    std::uint32_t epoch=0, invocation=0;
    SceneMilestoneKind kind{};
    explicit operator bool() const noexcept { return invocation!=0; }
};
struct SceneMilestoneRow {
    std::uint32_t id=0, epoch=0, invocation=0;
    SceneMilestoneKind kind{};
    SceneMilestoneStage stage{};
    SceneMilestoneStamp stamp{};
    SceneMilestoneScene scene{};
    SceneMilestoneContext context{};
};
static_assert(std::is_trivially_copyable_v<SceneMilestoneRow>);

// Append-only diagnostics. Published slots never change, and no producer waits
// for a reader or another producer. Full/contended admission loses evidence,
// explicitly counted, rather than overwriting an earlier unfinished callback.
template<std::uint32_t RowCapacity=1024, std::uint32_t EpochCapacity=32>
class SceneMilestones {
    static_assert(RowCapacity>0 && EpochCapacity>0);
    static_assert(std::atomic<std::uint32_t>::is_always_lock_free);
    static_assert(std::atomic<std::uint64_t>::is_always_lock_free);
    struct Slot { SceneMilestoneRow row{}; std::atomic<bool> ready{false}; };
    struct Epoch {
        SceneMilestoneScene scene{};
        std::atomic<bool> ready{false}, completed{false};
        std::atomic<std::uint32_t> first{0};
    };
    std::array<Slot,RowCapacity> rows_{};
    std::array<Epoch,EpochCapacity> epochs_{};
    std::atomic<std::uint32_t> rowCount_{0}, epochCount_{0}, current_{0};
    std::atomic<std::uint32_t> overflow_{0}, contention_{0}, epochOverflow_{0};
    // BC2 bridge uses 32-bit native player identities; the tuple is diagnostic,
    // never an actor-lifetime authority. Atomic pairing avoids mixed transitions.
    std::atomic<std::uint64_t> owner_{0};
    std::uint32_t Reserve(std::atomic<std::uint32_t>& count, std::uint32_t limit,
                          std::atomic<std::uint32_t>& overflow) noexcept {
        auto next=count.load(std::memory_order_relaxed);
        for(unsigned attempt=0;attempt<8;++attempt){
            if(next>=limit){overflow.fetch_add(1,std::memory_order_relaxed);return 0;}
            if(count.compare_exchange_weak(next,next+1,std::memory_order_acq_rel,std::memory_order_relaxed))return next+1;
        }
        contention_.fetch_add(1,std::memory_order_relaxed);return 0;
    }
    template<class Stamp>
    SceneMilestoneToken Append(std::uint32_t epoch, SceneMilestoneKind kind,
        SceneMilestoneStage stage, std::uint32_t invocation, const SceneMilestoneContext& context, Stamp&& stamp) noexcept {
        const auto id=Reserve(rowCount_,RowCapacity,overflow_);if(!id)return {};
        auto& slot=rows_[id-1];
        SceneMilestoneScene scene{};
        if(epoch && epoch<=EpochCapacity && epochs_[epoch-1].ready.load(std::memory_order_acquire))scene=epochs_[epoch-1].scene;
        slot.row={id,epoch,invocation?invocation:id,kind,stage,stamp(),scene,context};
        slot.ready.store(true,std::memory_order_release);
        return {epoch,invocation?invocation:id,kind};
    }
public:
    static constexpr auto Capacity=RowCapacity;
    template<class Stamp>
    SceneMilestoneToken BeginRebuild(const SceneMilestoneScene& scene, Stamp&& stamp) noexcept {
        const auto epoch=Reserve(epochCount_,EpochCapacity,epochOverflow_);
        if(!epoch){current_.store(0,std::memory_order_release);return {};}
        auto& e=epochs_[epoch-1];e.scene=scene;e.ready.store(true,std::memory_order_release);
        current_.store(epoch,std::memory_order_release);
        return Append(epoch,SceneMilestoneKind::Rebuild,SceneMilestoneStage::Enter,0,{},stamp);
    }
    template<class Stamp>
    void CompleteRebuild(SceneMilestoneToken token, bool initialized, Stamp&& stamp) noexcept {
        if(!token || token.kind!=SceneMilestoneKind::Rebuild)return;
        SceneMilestoneContext context{};context.outcome=initialized?1u:0u;
        Mark(token,SceneMilestoneStage::Exit,context,stamp);
        // Callbacks inside native rebuild are deliberately not post-rebuild.
        epochs_[token.epoch-1].completed.store(initialized,std::memory_order_release);
    }
    template<class Match,class Stamp>
    SceneMilestoneToken Begin(SceneMilestoneKind kind, const SceneMilestoneContext& context, Match&& match, Stamp&& stamp) noexcept {
        const auto epoch=current_.load(std::memory_order_acquire);if(!epoch)return {};
        auto& e=epochs_[epoch-1];
        if(!e.ready.load(std::memory_order_acquire)||!e.completed.load(std::memory_order_acquire)||!match(e.scene))return {};
        const auto bit=std::uint32_t{1}<<static_cast<unsigned>(kind);
        // Avoid a locked RMW on every subsequent callback in this scene.
        if(e.first.load(std::memory_order_relaxed)&bit)return {};
        if(e.first.fetch_or(bit,std::memory_order_acq_rel)&bit)return {};
        return Append(epoch,kind,SceneMilestoneStage::Enter,0,context,stamp);
    }
    template<class Stamp>
    void Mark(SceneMilestoneToken token, SceneMilestoneStage stage,
              const SceneMilestoneContext& context, Stamp&& stamp) noexcept {
        if(token)Append(token.epoch,token.kind,stage,token.invocation,context,stamp);
    }
    template<class Stamp>
    SceneMilestoneToken Owner(std::uint32_t self, SceneOwnerOutcome outcome,
                              const SceneMilestoneContext& context, Stamp&& stamp) noexcept {
        if(!self || outcome==SceneOwnerOutcome::Unknown)return {};
        auto old=owner_.load(std::memory_order_acquire);
        const auto next=(std::uint64_t(self)<<32)|static_cast<std::uint32_t>(outcome);
        for(unsigned attempt=0;attempt<8;++attempt){
            if(old==next)return {};
            // An unresolved nonlocal callback cannot invalidate the remembered
            // local callback. No game reads are needed to make this distinction.
            if(outcome!=SceneOwnerOutcome::Valid && static_cast<std::uint32_t>(old>>32)!=self)return {};
            if(owner_.compare_exchange_weak(old,next,std::memory_order_acq_rel,std::memory_order_acquire)){
                auto copy=context;copy.outcome=static_cast<std::uint32_t>(outcome);
                return Append(current_.load(std::memory_order_acquire),SceneMilestoneKind::OwnerTransition,
                    SceneMilestoneStage::Enter,0,copy,stamp);
            }
        }
        contention_.fetch_add(1,std::memory_order_relaxed);return {};
    }
    bool Read(std::uint32_t index, SceneMilestoneRow& row) const noexcept {
        if(index>=RowCapacity || !rows_[index].ready.load(std::memory_order_acquire))return false;
        row=rows_[index].row;return true;
    }
    std::uint32_t Reserved() const noexcept {return rowCount_.load(std::memory_order_acquire);}
    std::uint32_t Overflow() const noexcept {return overflow_.load(std::memory_order_relaxed);}
    std::uint32_t Contention() const noexcept {return contention_.load(std::memory_order_relaxed);}
    std::uint32_t EpochOverflow() const noexcept {return epochOverflow_.load(std::memory_order_relaxed);}
};
} // namespace fvr::runtime
