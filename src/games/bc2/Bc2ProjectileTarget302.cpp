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

OriginSession Origin;
struct OriginAccess {
    static thread_local OriginScope* current;
    static thread_local std::uint64_t lastScope, lastInvocation;
    static thread_local bool poisoned;
    static OriginScope* Enter(const Entry& entry) noexcept {
        auto* s = current;
        if (!s) return nullptr;
        if (Depth != 1) {
            // Nested execution before commit invalidates outstanding admission;
            // after native starts it must not undo the already committed result.
            s->ambiguous_ = true;
            if (!s->nativeStarted_) s->status_ = OriginStatus::Nested;
            s->attempted_ = true;
            return nullptr;
        }
        if (s->attempted_) return nullptr;
        s->attempted_ = true; // Even disabled/refused/faulted first entry is final.
        s->primary_ = entry.args[1];
        return s;
    }
    static bool Live(const OriginScope& s) noexcept {
        return !poisoned && current == &s && !s.ambiguous_ && Depth == 1 &&
            s.key_.thread == GetCurrentThreadId() && !s.nativeStarted_;
    }
    static bool MemoryExtent(Word primary) noexcept {
        const auto* tib = reinterpret_cast<const NT_TIB*>(NtCurrentTeb());
        const auto low = reinterpret_cast<Word>(tib->StackLimit), high = reinterpret_cast<Word>(tib->StackBase);
        if (primary < 0x10000 || (primary & 15) || primary < low ||
            std::uint64_t(primary) + 64 > high) return false;
        MEMORY_BASIC_INFORMATION region{};
        if (!VirtualQuery(reinterpret_cast<const void*>(primary), &region, sizeof(region)) ||
            region.State != MEM_COMMIT || (region.Protect & PAGE_GUARD)) return false;
        const auto protection = region.Protect & 0xff;
        return (protection == PAGE_READWRITE || protection == PAGE_WRITECOPY ||
                protection == PAGE_EXECUTE_READWRITE || protection == PAGE_EXECUTE_WRITECOPY) &&
            reinterpret_cast<std::uintptr_t>(region.BaseAddress) <= primary &&
            std::uint64_t(primary) + 64 <= reinterpret_cast<std::uintptr_t>(region.BaseAddress) + region.RegionSize;
    }
    static bool ReadWord(Word at, Word& value) noexcept {
        __try { value = *reinterpret_cast<const volatile Word*>(at); return true; }
        __except (EXCEPTION_EXECUTE_HANDLER) { return false; }
    }
    static bool WriteWord(Word at, Word value) noexcept {
        __try { *reinterpret_cast<volatile Word*>(at) = value; return true; }
        __except (EXCEPTION_EXECUTE_HANDLER) { return false; }
    }
    static bool Prepare(OriginScope& s, const Entry& entry, OriginAdmission& admission,
                        origin386::Bits& before) noexcept {
        __try {
            if (!Live(s) || s.status_ != OriginStatus::AwaitingEntry) return false;
            if (Origin.closed.load(std::memory_order_acquire) || !Origin.enabled.load(std::memory_order_acquire)) {
                s.status_ = OriginStatus::Disabled; return false;
            }
            s.status_ = OriginStatus::Refused;
            const auto& api = Origin.api;
            if (!api.prepare || !api.validate || !MemoryExtent(s.primary_) ||
                !api.prepare(api.context, s.key_, entry, admission) || !Live(s) ||
                admission.key != s.key_ || admission.primary != s.primary_ ||
                !admission.proof.evidence || !admission.proof.activeExit || !admission.proof.referenceSpace ||
                admission.proof.originalDeadline <= 0 ||
                !origin386::BoundedAbsolute(admission.absoluteNativeXyz, admission.positiveNativeBoundBits)) return false;
            for (unsigned i = 0; i != 3; ++i) {
                if (!ReadWord(s.primary_ + 0x30 + 4 * i, before[i])) {
                    s.status_ = OriginStatus::ReadFailure; return false;
                }
            }
            // The retained complete proof and original deadlines remain the
            // host's job. Recheck session, scope and exact stack extent after it.
            return api.validate(api.context, s.key_, entry, admission) && Live(s) &&
                !Origin.closed.load(std::memory_order_acquire) && Origin.enabled.load(std::memory_order_acquire) &&
                MemoryExtent(s.primary_);
        } __except (EXCEPTION_EXECUTE_HANDLER) {
            s.status_ = OriginStatus::AdmissionFault; return false;
        }
    }
    static void Commit(OriginScope* s, const Entry& entry) {
        if (!s) return;
        OriginAdmission admission{};
        origin386::Bits before{};
        if (!Prepare(*s, entry, admission, before)) return;
        // From here until commit/rollback completes: direct same-thread native
        // stack reads/stores only, no host callback, observer, wait, or allocation.
        const Word xyz = s->primary_ + 0x30;
        const auto result = origin386::Commit(before, admission.absoluteNativeXyz,
            [xyz](unsigned i, Word value) noexcept { return WriteWord(xyz + i * 4, value); },
            [xyz](unsigned i, Word& value) noexcept { return ReadWord(xyz + i * 4, value); });
        if (result == origin386::WriteResult::Committed) s->status_ = OriginStatus::Committed;
        else if (result == origin386::WriteResult::Restored) s->status_ = OriginStatus::WriteRestored;
        else {
            s->status_ = OriginStatus::WriteUnresolved;
            // Cannot forward partially corrupted input or pretend no change.
            // This distinct NONCONTINUABLE adapter failure occurs BEFORE native
            // execution; native exceptions after Forward are never caught here.
            RaiseException(0xe0386001u, EXCEPTION_NONCONTINUABLE, 0, nullptr);
            __fastfail(FAST_FAIL_INVALID_ARG); // Invalid handler tried to resume.
        }
    }
    static void Starting(OriginScope* s) noexcept {
        if (s) { s->nativeStarted_ = true; _ReadWriteBarrier(); }
    }
};
thread_local OriginScope* OriginAccess::current = nullptr;
thread_local std::uint64_t OriginAccess::lastScope = 0, OriginAccess::lastInvocation = 0;
thread_local bool OriginAccess::poisoned = false;

OriginScope::OriginScope(const OriginKey& key) noexcept : key_(key), previous_(OriginAccess::current), ownerThread_(GetCurrentThreadId()) {
    const bool fresh = key.scopeSerial > OriginAccess::lastScope ||
        (key.scopeSerial == OriginAccess::lastScope && key.invocation > OriginAccess::lastInvocation);
    // Reserve even a refused invocation: correcting missing/thread/lifetime
    // evidence later cannot reconstruct a fresh scope for that same whole shot.
    if (fresh && key.scopeSerial && key.invocation) {
        OriginAccess::lastScope = key.scopeSerial; OriginAccess::lastInvocation = key.invocation;
    }
    if (OriginAccess::poisoned || !key.scopeSerial || !key.invocation || !key.primaryLifetime ||
        key.thread != ownerThread_ || !fresh) {
        ambiguous_ = true; status_ = OriginStatus::StaleScope;
    }
    if (previous_ || Depth) {
        ambiguous_ = true; status_ = OriginStatus::Nested;
        if (previous_) {
            previous_->ambiguous_ = true;
            if (!previous_->nativeStarted_) previous_->status_ = OriginStatus::Nested;
        }
    }
    OriginAccess::current = this; // Invalid/nested scopes block fallback to outer.
}
OriginScope::~OriginScope() noexcept { Close(); }
void OriginScope::Close() noexcept {
    // Lifecycle only: never restore P, even after exception, Close, or native
    // mutation. Bad scope ordering permanently refuses this thread's writes.
    if (closed_) return;
    closed_ = true;
    if (ownerThread_ != GetCurrentThreadId() || OriginAccess::current != this) {
        OriginAccess::poisoned = true; OriginAccess::current = nullptr; return;
    }
    OriginAccess::current = previous_;
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

static void CopyProducerValues(const ObserverApi& api, const Grant& grant, Boundary boundary,
    const ProducerSample* before, ProducerSample& out) noexcept {
    if (!api.producerDiagnostics) return;
    out.status = SampleStatus::InvalidArguments;
    const auto embedded = std::uint64_t(grant.receiver) + 0x2c;
    if (!api.read || !api.validate || grant.receiver < 0x10000 || embedded + 16 > UINT32_MAX ||
        grant.stackLow >= grant.stackHigh || grant.source < grant.stackLow ||
        std::uint64_t(grant.source) + 12 > grant.stackHigh) return;
    out.embedded = Word(embedded);
    const auto bytes = [&](std::uint64_t at, void* dst, std::size_t size) noexcept {
        out.status = SampleStatus::ReadFailure;
        if (at < 0x10000 || at > UINT32_MAX || size > UINT32_MAX - at) return false;
        // Full live owner/helper/thread/primary identity and the original deadline
        // must pass again immediately before EACH borrowed read, including repeats.
        if (!api.validate(api.context, grant, boundary)) {
            out.status = SampleStatus::IdentityChanged; return false;
        }
        return api.read(api.context, Word(at), dst, size);
    };
    if (!bytes(embedded, out.words.data(), sizeof(out.words))) return;
    out.wordsRead = true;
    if (!bytes(std::uint64_t(out.words[0]) + 4, &out.slot4Target, 4)) return;
    out.targetRead = true;
    std::array<Word, 4> repeated{}; Word repeatedTarget = 0;
    if (!bytes(embedded, repeated.data(), sizeof(repeated))) return;
    if (repeated != out.words) { out.status = SampleStatus::Torn; return; }
    if (!bytes(std::uint64_t(repeated[0]) + 4, &repeatedTarget, 4)) return;
    if (!api.validate(api.context, grant, boundary)) {
        out.status = SampleStatus::IdentityChanged; return;
    }
    if (repeatedTarget != out.slot4Target) {
        out.status = SampleStatus::Torn; return;
    }
    if (before && before->status == SampleStatus::Complete &&
        (before->embedded != out.embedded || before->words[0] != out.words[0] || before->slot4Target != out.slot4Target)) {
        out.status = SampleStatus::IdentityChanged; return;
    }
    out.status = SampleStatus::Complete;
}

ProducerSample CopyProducerBoundary(const ObserverApi& api, const Grant& grant, Boundary boundary,
    const ProducerSample* before) noexcept {
    ProducerSample out;
    // Diagnostic faults cannot escape into Observe's existing outcome handling.
    __try { CopyProducerValues(api, grant, boundary, before, out); }
    __except (EXCEPTION_EXECUTE_HANDLER) { out.status = SampleStatus::ReadFailure; }
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
        else if (api.producerDiagnostics) {
            auto& sample = boundary == Boundary::Entry ? row->before : row->after;
            if (sample.status == SampleStatus::Complete)
                sample.producer = CopyProducerBoundary(api, row->grant, boundary,
                    boundary == Boundary::Return ? &row->before.producer : nullptr);
        }
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        row->outcome = Outcome::ObserverFault;
    }
}

void __cdecl Dispatch(Frame* frame) {
    Record* row = nullptr;
    bool originalReturned = false, originalStarted = false;
    OriginScope* origin = nullptr;
    InterlockedIncrement(&Observer.active);
    ++Depth;
    __try {
        origin = OriginAccess::Enter(*frame->entry);
        row = Claim(*frame->entry);
        Observe(row, Boundary::Entry);
        OriginAccess::Commit(origin, *frame->entry);
        OriginAccess::Starting(origin);
        originalStarted = true;
        Forward(frame); // Exactly once after refusal/recovered rollback/commit; never unresolved rollback.
        originalReturned = true;
        if (row) row->originalReturned = true;
        Observe(row, Boundary::Return);
    } __finally {
        if (row) {
            if (!originalReturned) row->outcome = originalStarted ? Outcome::NativeException : Outcome::OriginCommitFailure;
            row->complete.store(true, std::memory_order_release);
        }
        --Depth;
        InterlockedDecrement(&Observer.active);
    }
}
} // namespace fvr::bc2::projectile302
