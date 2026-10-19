#include "Bf4Bindings.h"

#include <array>
#include <string>

namespace fvr::bf4 {

using frostbite::ScanStatus;

namespace {
// Fill patterns from the RE playbook, one per evidence note. Do NOT paste patterns from
// cheat forums: they target other builds and carry no validation; enroll only what you
// located and checked yourself in this bf4.exe. Each enrolled pattern was checked unique
// in the decrypted .text of build 179547 (research image, 2026-10-19).
const std::array<BindingSpec, 10> kSpecs{{
    // lea rcx,[rbp+0xF0]; call; mov rcx,[GameRenderer]; mov rdx,[rcx]; mov rax,[r12+0x40]:
    // the per-frame view submit in +0x828090 (normal path). Global +0x2672378.
    {names::kGameRenderer, "48 8D 8D F0 00 00 00 E8 ?? ?? ?? ?? 48 8B 0D ?? ?? ?? ?? 48 8B 11 49 8B 44 24 40",
     BindingKind::RipRelativeGlobal, 12, 3, 7,
     "R3 static: renderer global loaded for the slot-5 view submit; live object checked at install"},
    // GameRenderer vtable slot 5 (+0x83F0E0): prologue through `cmp byte [rcx+0x144],0`.
    // (renderer, float, float, viewCount, viewBlocks*, sync*, ptr); calls the view setup
    // +0x848B00 that copies the blocks. Must equal slot 5 of the live object to be used.
    {names::kRenderViewUpdate,
     "48 8B C4 55 53 56 57 41 54 41 55 41 56 41 57 48 8D A8 48 FE FF FF 48 81 EC 78 02 00 00 "
     "0F 29 70 A8 0F 29 78 98 48 8B 05 ?? ?? ?? ?? 48 33 C4 48 89 85 48 01 00 00 80 B9 44 01 00 00 00",
     BindingKind::Code, 0, 0, 0, "R3 static: renderer view submit (vtable slot 5), signature from call site + prologue"},
    {names::kStereoEyeLoop, "", BindingKind::Code, 0, 0, 0,
     "RE task R2: renderer loop that renders a frame twice when stereo is enabled"},
    {names::kStereoActiveEye, "", BindingKind::Code, 0, 0, 0,
     "RE task R2: where the active eye / eye offset is selected (NvAPI/HD3D path)"},
    {names::kClientGameContext, "", BindingKind::RipRelativeGlobal, 0, 3, 7,
     "RE task R5: client game context global; owns level + player manager"},
    {names::kTypeInfoHead, "", BindingKind::RipRelativeGlobal, 0, 3, 7,
     "RE task R4: head of the reflection TypeInfo list for the read-only dumper"},
    {names::kLevelName, "", BindingKind::Code, 0, 0, 0,
     "RE task R5: current level name source (campaign levels are Levels/SP_*)"},
    {names::kInputConceptCache, "", BindingKind::Code, 0, 0, 0,
     "RE task R6: per-tick input concept cache (reload/fire/use concepts)"},
    {names::kUiCursorRead, "", BindingKind::Code, 0, 0, 0,
     "RE task R7: where UI reads mouse position, if Win32 cursor injection is not enough"},
    {names::kSoldierWeaponTransform, "", BindingKind::Code, 0, 0, 0,
     "RE task R8: first-person weapon transform write (controller aim, roadmap M3)"},
}};
} // namespace

std::span<const BindingSpec> DefaultBindingSpecs() { return kSpecs; }

void BindingTable::Resolve(std::span<const std::uint8_t> code, std::uint64_t codeBase,
                           const std::function<BindingValidator(std::string_view)>& validatorFor) {
    resolved_.clear();
    resolved_.reserve(specs_.size());
    for (const auto& spec : specs_) {
        ResolvedBinding r;
        r.name = spec.name;
        if (spec.pattern.empty()) {
            r.status = ScanStatus::Unenrolled;
            resolved_.push_back(r);
            continue;
        }
        const auto pattern = frostbite::ParsePattern(spec.pattern);
        if (!pattern) {
            r.status = ScanStatus::InvalidPattern;
            resolved_.push_back(r);
            continue;
        }
        const auto scan = frostbite::FindUnique(code, *pattern);
        r.status = scan.status;
        if (scan.status == ScanStatus::Ok) {
            const std::size_t at = scan.offset + static_cast<std::size_t>(spec.adjust);
            switch (spec.kind) {
            case BindingKind::Code:
                r.address = codeBase + at;
                break;
            case BindingKind::RipRelativeGlobal: {
                const auto target = frostbite::ResolveRipRelative(code, codeBase, at,
                    static_cast<std::size_t>(spec.dispOffset), static_cast<std::size_t>(spec.instructionLength));
                if (target) r.address = *target; else r.status = ScanStatus::ValidationFailed;
                break;
            }
            case BindingKind::CallTarget: {
                const auto target = frostbite::ResolveRipRelative(code, codeBase, at, 1, 5);
                if (target) r.address = *target; else r.status = ScanStatus::ValidationFailed;
                break;
            }
            }
            if (r.status == ScanStatus::Ok && validatorFor) {
                if (auto validate = validatorFor(spec.name); validate && !validate(r))
                    r.status = ScanStatus::ValidationFailed;
            }
        }
        resolved_.push_back(r);
    }
}

const ResolvedBinding* BindingTable::Find(std::string_view name) const {
    for (const auto& r : resolved_)
        if (r.name == name) return &r;
    return nullptr;
}

bool BindingTable::IsOk(std::string_view name) const {
    const auto* r = Find(name);
    return r && r->status == ScanStatus::Ok;
}

namespace {
std::string Missing(const BindingTable& t, std::initializer_list<std::string_view> required) {
    std::string out;
    for (auto name : required) {
        const auto* r = t.Find(name);
        const auto status = r ? r->status : ScanStatus::Unenrolled;
        if (status == ScanStatus::Ok) continue;
        if (!out.empty()) out += ", ";
        out += std::string(name) + " " + std::string(frostbite::ScanStatusName(status));
    }
    return out;
}

void Gate(game::CapabilityRegistry& caps, game::Feature f, const BindingTable& t,
          std::initializer_list<std::string_view> required, std::string_view readyNote) {
    const std::string missing = Missing(t, required);
    if (missing.empty())
        caps.Set(f, game::FeatureState::Candidate, std::string(readyNote));
    else
        caps.Set(f, game::FeatureState::Disabled, "needs " + missing);
}
} // namespace

void ApplyBindingsToCapabilities(const BindingTable& t, game::CapabilityRegistry& caps, bool presentHookOk) {
    using game::Feature;
    using game::FeatureState;
    // M0 needs only the swapchain: no game-internal bindings at all.
    if (presentHookOk) {
        caps.Set(Feature::TheaterView, FeatureState::Candidate, "swapchain captured; headset check pending");
        caps.Set(Feature::ModMenuPointer, FeatureState::Candidate, "host overlay; headset check pending");
        // Evidence: 2026-10-18 the same PointerEvent -> Win32PointerSink path drove the real
        // BF4 menus (Campaign tab, campaign tile, Resume) on build 179547 (BF4-STEREO.md).
        caps.Set(Feature::GameMenuPointer, FeatureState::Enabled, "Win32 cursor injection verified on BF4 menus");
        caps.RecordEvidence(Feature::GameMenuPointer, game::Evidence::ActualGame);
        // Theater link: real swapchain capture on bf4.exe; panel to the real XR host on the
        // fake OpenXR runtime (tools/emulated/Run-Bf4TheaterXr.ps1). No headset yet.
        caps.RecordEvidence(Feature::TheaterView, game::Evidence::Emulated);
        caps.RecordEvidence(Feature::ModMenuPointer, game::Evidence::Emulated);
        // BF4 imports XINPUT9_1_0 (XInputGetState/SetState/GetCapabilities, research image).
        caps.Set(Feature::VirtualGamepad, FeatureState::Candidate, "XInput pad from VR controllers; in-game check pending");
    } else {
        for (auto f : {Feature::TheaterView, Feature::ModMenuPointer, Feature::GameMenuPointer, Feature::VirtualGamepad})
            caps.Set(f, FeatureState::Disabled, "no D3D11 swapchain captured (Mantle selected?)");
    }
    Gate(caps, Feature::HeadTrackedCamera, t, {names::kGameRenderer, names::kRenderViewUpdate},
         "camera override bound; headset check pending");
    Gate(caps, Feature::StereoNative, t,
         {names::kGameRenderer, names::kRenderViewUpdate, names::kStereoEyeLoop, names::kStereoActiveEye},
         "native stereo path bound; per-eye check pending");
    // The camera is consumed one or two frames after the request, so alternating eyes also
    // needs a signal for which eye a presented frame was rendered with.
    Gate(caps, Feature::StereoAlternateEye, t, {names::kGameRenderer, names::kRenderViewUpdate, names::kStereoActiveEye},
         "alternate-eye fallback bound; comfort check pending");
    Gate(caps, Feature::ButtonReload, t, {names::kInputConceptCache},
         "reload concept bound; in-game check pending");
    Gate(caps, Feature::ControllerAim, t, {names::kSoldierWeaponTransform, names::kGameRenderer},
         "weapon transform bound; aim check pending");
}

} // namespace fvr::bf4
