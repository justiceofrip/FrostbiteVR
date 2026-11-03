#include "Bc2ProjectileTarget302.h"
#include <intrin.h>

namespace fvr::bc2::projectile302 {
MachineEntry Original = nullptr;
Session Observer;
thread_local unsigned Depth = 0;
static const Word DefaultMxcsr = 0x1f80;

bool FloatingStateSupported() noexcept {
    int cpu[4]{}; __cpuid(cpu, 1);
    return (cpu[3] & (1 << 24)) && (cpu[3] & (1 << 25)) &&
        IsProcessorFeaturePresent(PF_XMMI64_INSTRUCTIONS_AVAILABLE);
}

// Original caller return slot and all eleven caller argument words stay intact.
// Allocations are each < one guard page; save areas are explicitly 16 aligned.
// POPFD restores application-visible flags; privileged/debug flag virtualization
// is not an ABI claim. No instruction modifies game data or original flags.
__declspec(naked) void __cdecl Thunk() {
    __asm {
        pushfd
        pushad
        mov eax, esp
        sub esp, 1088
        and esp, -16
        mov [esp + 1060], eax
        mov eax, fs:[34h] // Windows x86 TEB LastErrorValue, not a game offset.
        mov [esp + 1064], eax
        fxsave [esp]
        fninit
        ldmxcsr DefaultMxcsr
        cld
        mov eax, esp
        push eax
        call Dispatch
        lea esp, [esp + 4]
        fxrstor [esp + 512]
        mov eax, [esp + 1068]
        mov fs:[34h], eax
        mov eax, [esp + 1060]
        mov edi, eax
        lea esi, [esp + 1024]
        mov ecx, 9
        cld
        rep movsd
        mov esp, eax
        popad
        popfd
        ret 2ch
    }
}

// The same copied-argument gateway shape as Bc2ProjectileEntry.h, extended for
// eleven words and three independent floating environments (incoming, returned,
// and Dispatch). CALL supplies its own continuation; no return-slot rewriting.
__declspec(naked) void __cdecl Forward(Frame*) {
    __asm {
        pushfd
        pushad
        mov edx, esp
        mov eax, [edx + 40]
        sub esp, 544
        and esp, -16
        fxsave [esp]
        mov [esp + 512], edx
        mov [esp + 516], eax
        mov edx, [eax + 1064]
        mov fs:[34h], edx
        fxrstor [eax]
        mov edx, [eax + 1060]
        push dword ptr [edx + 80]
        push dword ptr [edx + 76]
        push dword ptr [edx + 72]
        push dword ptr [edx + 68]
        push dword ptr [edx + 64]
        push dword ptr [edx + 60]
        push dword ptr [edx + 56]
        push dword ptr [edx + 52]
        push dword ptr [edx + 48]
        push dword ptr [edx + 44]
        push dword ptr [edx + 40]
        push dword ptr [edx + 32]
        popfd
        mov edi, [edx]
        mov esi, [edx + 4]
        mov ebp, [edx + 8]
        mov ebx, [edx + 16]
        mov ecx, [edx + 24]
        mov eax, [edx + 28]
        mov edx, [edx + 20]
        call dword ptr [Original]
        pushfd
        pushad
        mov eax, [esp + 552] // 36 saved GPR/flags + local Frame* at 516.
        fxsave [eax + 512]
        mov edx, fs:[34h]
        mov [eax + 1068], edx
        lea edi, [eax + 1024]
        mov esi, esp
        mov ecx, 9
        cld
        rep movsd
        lea esp, [esp + 36]
        fxrstor [esp]
        mov esp, [esp + 512]
        popad
        popfd
        ret
    }
}

Sample CopyBoundary(const ObserverApi& api, const Grant& grant, const Sample* before) noexcept {
    Sample out; out.source = grant.source; out.identity.receiver = grant.receiver;
    out.status = SampleStatus::InvalidArguments;
    if (!api.read || !grant.receiver || grant.stackLow >= grant.stackHigh ||
        grant.source < grant.stackLow || std::uint64_t(grant.source) + 12 > grant.stackHigh)
        return out;
    const auto bytes = [&](std::uint64_t at, void* dst, std::size_t n) noexcept {
        return at >= 0x10000 && at <= UINT32_MAX && n <= UINT32_MAX - at &&
            api.read(api.context, Word(at), dst, n);
    };
    const auto word = [&](std::uint64_t at, Word& v) noexcept { return bytes(at, &v, 4); };
    const auto head = [&](Selection& s, std::uint16_t& flags) noexcept {
        s.receiver = grant.receiver;
        return word(s.receiver, s.vtable) &&
            word(std::uint64_t(s.vtable) + 0xe8, s.target) &&
            bytes(std::uint64_t(s.receiver) + 0x26, &flags, 2);
    };
    const auto select = [&](Selection& s, std::uint16_t flags) noexcept {
        if (!word(std::uint64_t(s.receiver) + 0x28, s.backing)) return false;
        s.indexed = (flags & 0x80) != 0;
        std::uint64_t selected = s.backing;
        if (s.indexed) {
            if (!bytes(std::uint64_t(s.backing) + 0xe, &s.index, 1)) return false;
            selected += 0x10 + std::uint64_t(s.index) * 0x50;
        }
        if (selected < 0x10000 || selected > UINT32_MAX - 0x3c) return false;
        s.selected = Word(selected); return true;
    };
    out.status = SampleStatus::ReadFailure;
    if (!head(out.identity, out.flags)) return out;
    out.flagsRead = true;
    if (out.identity.vtable != grant.vtable || out.identity.target != grant.target) {
        out.status = SampleStatus::IdentityChanged; return out;
    }
    // Bit 2 is evidence at THIS boundary, never a classification of 610d10.
    if (!select(out.identity, out.flags)) return out;
    if (before && out.identity != before->identity) {
        out.status = SampleStatus::IdentityChanged; return out;
    }
    if (!bytes(grant.source, out.sourceBits.data(), 12)) return out;
    out.sourceRead = true;
    if (!bytes(std::uint64_t(out.identity.selected) + 0x30, out.selectedBits.data(), 12)) return out;
    out.selectedRead = true;
    if (!bytes(std::uint64_t(grant.receiver) + 0x40, out.velocityCacheBits.data(), 12)) return out;
    out.velocityRead = true;
    Selection repeated{}; std::uint16_t flags = 0;
    if (!head(repeated, flags) || !select(repeated, flags)) return out;
    if (repeated != out.identity || flags != out.flags) { out.status = SampleStatus::Torn; return out; }
    out.status = SampleStatus::Complete;
    return out;
}

bool Session::Offer(const Grant& grant) noexcept {
    if (!enabled.load(std::memory_order_acquire)) return false;
    if (!TryAcquireSRWLockExclusive(&gate)) { ++busy; return false; }
    bool accepted = false;
    if (enabled.load(std::memory_order_acquire)) {
        if (offered == rows.size()) ++full;
        else if (grant.scopeSerial && grant.invocation && grant.ordinal && grant.thread &&
                 grant.receiver && grant.vtable && grant.target && grant.returnAddress && grant.source) {
            rows[offered++].grant = grant; accepted = true;
        }
    }
    ReleaseSRWLockExclusive(&gate);
    return accepted;
}

static Record* Claim(const Entry& entry) noexcept {
    auto& session = Observer;
    if (!session.enabled.load(std::memory_order_acquire)) { ++session.disabled; return nullptr; }
    if (Depth != 1) { ++session.nested; return nullptr; }
    if (!TryAcquireSRWLockExclusive(&session.gate)) { ++session.busy; return nullptr; }
    Record* row = nullptr;
    if (session.enabled.load(std::memory_order_acquire)) {
        const Word thread = GetCurrentThreadId();
        // The LATEST offered row on this thread is the only eligible row.
        // A rejected, consumed or expired latest row never falls back to an old
        // same-token row, including another invocation in a reused stack slot.
        for (Word n = session.offered; n; --n) {
            auto& candidate = session.rows[n - 1]; const auto& g = candidate.grant;
            if (g.thread != thread) continue;
            if (!candidate.claimed && g.receiver == entry.registers.ecx &&
                g.returnAddress == entry.returnAddress && g.token == entry.args[5] &&
                std::uint64_t(entry.args[1]) + 0x30 == g.source) {
                candidate.claimed = true; row = &candidate;
            }
            break;
        }
    }
    ReleaseSRWLockExclusive(&session.gate);
    if (!row) ++session.noGrant;
    return row;
}

// POD-only SEH functions: no C++ destructors mixed with __try (/EHsc C2712).
// Observer faults become value-only failure evidence and cannot skip Original.
static void Observe(Record* row, Boundary boundary) noexcept {
    if (!row) return;
    __try {
        const auto& api = Observer.api;
        if (!Observer.enabled.load(std::memory_order_acquire) || !api.validate ||
            !api.validate(api.context, row->grant, boundary)) {
            row->outcome = Outcome::RejectedIdentity; return;
        }
        const auto* tib = reinterpret_cast<const NT_TIB*>(NtCurrentTeb());
        Grant currentBounds = row->grant;
        currentBounds.stackLow = reinterpret_cast<Word>(tib->StackLimit);
        currentBounds.stackHigh = reinterpret_cast<Word>(tib->StackBase);
        if (currentBounds.stackHigh != row->grant.stackHigh ||
            currentBounds.source < row->grant.stackLow ||
            std::uint64_t(currentBounds.source) + 12 > row->grant.stackHigh) {
            row->outcome = Outcome::RejectedIdentity; return;
        }
        if (boundary == Boundary::Entry) {
            row->before = CopyBoundary(api, currentBounds);
            if (row->before.status != SampleStatus::Complete) row->outcome = Outcome::EntryReadFailure;
        } else if (row->outcome == Outcome::Offered && row->before.status == SampleStatus::Complete) {
            row->after = CopyBoundary(api, currentBounds, &row->before);
            switch (row->after.status) {
            case SampleStatus::IdentityChanged: row->outcome = Outcome::StorageIdentityChanged; break;
            case SampleStatus::Torn: row->outcome = Outcome::Torn; break;
            case SampleStatus::Complete:
                row->outcome = row->before.selectedBits == row->after.selectedBits ?
                    Outcome::ReturnedUnchanged : Outcome::ReturnedChanged; break;
            default: row->outcome = Outcome::ReturnReadFailure; break;
            }
        }
        // Even a copied complete sample is not admitted if full owner/config
        // identity or the optional window changed during its bounded reads.
        if (!api.validate(api.context, row->grant, boundary)) row->outcome = Outcome::RejectedIdentity;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        row->outcome = Outcome::ObserverFault;
    }
}

void __cdecl Dispatch(Frame* frame) {
    Record* row = nullptr;
    bool originalReturned = false;
    InterlockedIncrement(&Observer.active);
    ++Depth;
    __try {
        row = Claim(*frame->entry);
        Observe(row, Boundary::Entry);
        Forward(frame); // Exactly one call, including every refusal/full/busy path.
        originalReturned = true;
        if (row) row->originalReturned = true;
        Observe(row, Boundary::Return);
    } __finally {
        if (row) {
            if (!originalReturned) row->outcome = Outcome::NativeException;
            row->complete.store(true, std::memory_order_release);
        }
        --Depth;
        InterlockedDecrement(&Observer.active);
    }
}
} // namespace fvr::bc2::projectile302
