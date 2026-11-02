#pragma once
// Private, single-translation-unit x86 adapter for the existing context helper.
// The normal x64 target does not compile Bc2Gameplay.cpp or include this header.
#if !defined(_MSC_VER) || !defined(_M_IX86)
#error The BC2 projectile entry adapter requires the verified MSVC x86 ABI.
#endif
#include <cstddef>
#include <cstdint>

namespace fvr::bc2::gameplay {
namespace {
namespace projectile_entry {
using Word = std::uint32_t;
using Helper = std::uintptr_t(__thiscall*)(void*, void*);

// PUSHFD then PUSHAD, in increasing address order. PUSHAD's saved ESP points
// at the saved EFLAGS, not at the caller's return slot. POPAD ignores it.
struct Registers {
    Word edi, esi, ebp, pushadEsp, ebx, edx, ecx, eax, eflags;
};
struct Entry {
    Registers registers;
    Word returnAddress;
    Word output;
    std::uintptr_t ReturnSlot() const noexcept {
        return reinterpret_cast<std::uintptr_t>(&returnAddress);
    }
};
struct Invocation {
    Registers returned;
    Entry entry;
};
static_assert(sizeof(void*) == 4 && sizeof(Registers) == 36);
static_assert(offsetof(Registers, edi) == 0 && offsetof(Registers, esi) == 4);
static_assert(offsetof(Registers, ebp) == 8 && offsetof(Registers, pushadEsp) == 12);
static_assert(offsetof(Registers, ebx) == 16 && offsetof(Registers, eax) == 28);
static_assert(offsetof(Registers, edx) == 20 && offsetof(Registers, ecx) == 24);
static_assert(offsetof(Registers, eflags) == 32);
static_assert(offsetof(Invocation, entry) == 36);
static_assert(offsetof(Entry, returnAddress) == 36 && offsetof(Entry, output) == 40);
static_assert(sizeof(Invocation) == 80);
}

// Original is immutable while callbacks are active. Dispatch owns Callback
// RAII and calls Forward exactly once on every normal path. Keep C++ linkage:
// /EHsc must unwind Dispatch when the original throws through the naked frames.
extern projectile_entry::Helper ReceiverEntryOriginal;
void __cdecl ReceiverEntryDispatch(projectile_entry::Invocation* invocation);
std::uintptr_t __cdecl ReceiverEntryForward(projectile_entry::Invocation* invocation);

// Instruction-for-instruction copy of the reviewed receiver-entry120 adapter.
// All offsets describe OUR stack records, never compiler spills/game fields.
__declspec(naked) std::uintptr_t __fastcall
ReceiverEntryThunk(void*, void*, void*) {
    __asm {
        pushfd
        pushad
        sub esp, 36
        mov eax, esp
        push eax
        call ReceiverEntryDispatch
        lea esp, [esp + 4]
        popad
        popfd
        lea esp, [esp + 36]
        ret 4
    }
}

// Original receives the captured GPRs/flags and self/output. Its return slot is
// the gateway continuation; Entry retains the actual original caller slot.
__declspec(naked) std::uintptr_t __cdecl
ReceiverEntryForward(projectile_entry::Invocation*) {
    __asm {
        pushfd
        pushad
        mov eax, [esp + 40]
        push eax
        push dword ptr [eax + 76]
        push dword ptr [eax + 68]
        popfd
        mov edi, [eax + 36]
        mov esi, [eax + 40]
        mov ebp, [eax + 44]
        mov ebx, [eax + 52]
        mov edx, [eax + 56]
        mov ecx, [eax + 60]
        mov eax, [eax + 64]
        call dword ptr [ReceiverEntryOriginal]
        pushfd
        pushad
        mov edi, [esp + 36]
        mov esi, esp
        mov ecx, 9
        cld
        rep movsd
        mov eax, [esp + 28]
        mov [esp + 68], eax
        lea esp, [esp + 40]
        popad
        popfd
        ret
    }
}
}
} // namespace fvr::bc2::gameplay
