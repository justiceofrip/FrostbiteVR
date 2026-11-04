#pragma once
#include <cstdint>
#include <optional>
#if defined(_WIN32) && defined(_M_IX86) && defined(FVR_BC2_CHEST_KNIFE_DRAW_RETURN)
namespace fvr::bc2::chestKnifeNative {
// Composes with the five existing verified selected-knife hooks. A null result
// leaves dispatch with that hook; a value means original was called exactly once.
using ChestAcquireOriginal=std::uintptr_t(__thiscall*)(void*);
using ChestSearchOriginal=unsigned(__thiscall*)(void*,unsigned,const void*,unsigned,float);
using ChestAttackOriginal=std::uintptr_t(__thiscall*)(void*,float,unsigned,void*);
using ChestDamageOriginal=std::uintptr_t(__thiscall*)(void*,void*,void*);
void AttackHooksAvailable(bool)noexcept;
// Hide an outer chest scope through an entire nested Acquire/Attack callback,
// including selected and passthrough routes; restore it on every return.
struct ChestHookIsolation {
    ChestHookIsolation()noexcept;~ChestHookIsolation();
    ChestHookIsolation(const ChestHookIsolation&)=delete;
private:void* acquiring_;void* attacking_;
};
std::optional<std::uintptr_t> BridgeAcquire(void*,unsigned,ChestAcquireOriginal);
std::optional<unsigned> BridgeSearch(void*,unsigned,const void*,unsigned,float,unsigned,bool,ChestSearchOriginal);
std::optional<std::uintptr_t> BridgeAttack(void*,float,unsigned,void*,unsigned,ChestAttackOriginal);
std::optional<std::uintptr_t> BridgeDamage(void*,void*,void*,unsigned,ChestDamageOriginal);
}
#endif
