#pragma once
#include "Bc2ProjectileTarget302.h"
#include <span>

namespace fvr::bc2::smoke402 {
using Word=projectile302::Word;
using Key=projectile302::OriginKey;
using Registers=projectile302::Registers;
enum class Boundary : Word { EarlyEffect, CameraReset, Physical };
struct Entry {
    Boundary boundary=Boundary::EarlyEffect;
    Registers registers{};
    Word primary=0;
    const projectile302::Entry* physical=nullptr; // borrowed for this synchronous call only
};
using Admission=projectile302::OriginAdmission;
using Prepare=bool(__cdecl*)(void*,const Key&,const Entry&,Admission&)noexcept;
using Validate=bool(__cdecl*)(void*,const Key&,const Entry&,const Admission&)noexcept;
using ValidateReset=bool(__cdecl*)(void*,const Key&,const Entry&)noexcept;
struct Api {void* context=nullptr;Prepare prepare=nullptr;Validate validate=nullptr;ValidateReset reset=nullptr;};
struct Session {
    Api api{}; // immutable before global hook enable; retained until process exit
    std::atomic<bool> enabled{false},closed{false};
    volatile LONG active=0;
    std::atomic<std::uint64_t> earlyCommitted{0},physicalCommitted{0},partial{0},refused{0},poisoned{0};
    void Close()noexcept {closed.store(true,std::memory_order_release);enabled.store(false,std::memory_order_release);}
};
extern Session Runtime;
enum class Outcome : Word { Unseen, Refused, EarlyCommitted, ResetObserved, PhysicalCommitted, Partial, Poisoned };
// Created for EVERY server-shot invocation before native entry, even when
// disabled or missing geometry. Closed explicitly in the host's SEH __finally.
// No destructor/Close/retirement path restores any native write.
class Scope final {
public:
    explicit Scope(const Key&)noexcept;
    ~Scope()noexcept {Close();}
    void Close()noexcept;
    Scope(const Scope&)=delete;Scope& operator=(const Scope&)=delete;
    const Key& Identity()const noexcept {return key_;}
    Outcome Status()const noexcept {return outcome_;}
    bool EarlyCommitted()const noexcept {return earlyCommitted_;}
    bool CrossedEarly()const noexcept {return earlySeen_;}
    Word Primary()const noexcept {return primary_;}
private:
    friend struct Access;
    Key key_{};Scope* previous_=nullptr;
    Word ownerThread_=0,primary_=0,stackLow_=0,stackHigh_=0;
    bool earlySeen_=false,earlyCommitted_=false,resetSeen_=false,physicalSeen_=false,ambiguous_=false,closed_=false;
    Outcome outcome_=Outcome::Unseen;
};
inline constexpr Word EarlyRva=0x17a106,ResetRva=0x17a176;
inline constexpr std::array<unsigned char,5> EarlyBytes{0x8d,0x54,0x24,0x74,0x52};
inline constexpr std::array<unsigned char,5> ResetBytes{0xe8,0xe5,0xa2,0xfb,0xff};
// Exact continuation addresses are published before MH_CreateHook. Retained
// trampolines are kept by the host; these seams reproduce the displaced bytes
// directly so the original effect CALL and marker return address remain exact.
extern Word EarlyResume,ResetResume,ResetTarget;
void __cdecl EarlyThunk();
void __cdecl ResetThunk();
void __cdecl EarlyDispatch(const Registers*);
void __cdecl ResetDispatch(const Registers*)noexcept;
// Called from the existing7155b0 Dispatch, before its original native call.
// True means this shot crossed the earlier effect seam; ordinary-origin must
// not fall through after ANY early refusal or later partial application.
bool BeforePhysical(const projectile302::Entry&);
bool SuppressOrdinaryEntry()noexcept;
bool StackExtent(Word primary)noexcept;
bool VerifySeams(std::span<const std::byte> earlyFile,std::span<const std::byte> resetFile,
                 std::span<const std::byte> earlyLive,std::span<const std::byte> resetLive)noexcept;
} // namespace fvr::bc2::smoke402
