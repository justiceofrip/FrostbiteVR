#pragma once
// Named native bindings for bf4.exe (x64). Every pattern starts EMPTY = Unenrolled.
// A binding is enrolled only after RE evidence (Ghidra / RE MCP, see
// docs/BF4-RE-PLAYBOOK.md) and a validation callback that checks the resolved object
// looks like what we think it is. Features gate on bindings, never on hashes alone.
#include "../../engines/frostbite/PatternScan.h"
#include "fvr/game/Capability.h"

#include <cstdint>
#include <functional>
#include <span>
#include <string_view>
#include <vector>

namespace fvr::bf4 {

enum class BindingKind : std::uint8_t {
    Code,              // address of the match (+adjust): function start or hook site
    RipRelativeGlobal, // instruction at match+adjust references a global via disp32
    CallTarget         // E8 rel32 call at match+adjust -> callee
};

struct BindingSpec {
    std::string_view name;
    std::string_view pattern;   // "" until enrolled
    BindingKind kind = BindingKind::Code;
    int adjust = 0;             // bytes from match start to the instruction of interest
    int dispOffset = 3;         // RipRelativeGlobal: offset of disp32 inside the instruction
    int instructionLength = 7;  // RipRelativeGlobal: full instruction length
    std::string_view evidence;  // short note: where it came from, what validated it
};

struct ResolvedBinding {
    std::string_view name;
    frostbite::ScanStatus status = frostbite::ScanStatus::Unenrolled;
    std::uint64_t address = 0;
};

// Optional per-binding sanity check run after a successful scan (e.g. the global points
// into .data, the object's vtable points into .rdata, a float field is a plausible FOV).
using BindingValidator = std::function<bool(const ResolvedBinding&)>;

// Canonical binding names. The adapter refers to these, never to raw addresses.
namespace names {
inline constexpr std::string_view kGameRenderer = "GameRenderer.Instance";
inline constexpr std::string_view kRenderViewUpdate = "RenderView.Update";
inline constexpr std::string_view kStereoEyeLoop = "Renderer.StereoEyeLoop";
inline constexpr std::string_view kStereoActiveEye = "Renderer.StereoActiveEye";
inline constexpr std::string_view kClientGameContext = "ClientGameContext.Instance";
inline constexpr std::string_view kTypeInfoHead = "TypeInfo.FirstTypeInfo";
inline constexpr std::string_view kLevelName = "ClientLevel.Name";
inline constexpr std::string_view kInputConceptCache = "Input.ConceptCache";
inline constexpr std::string_view kUiCursorRead = "UI.CursorRead";
inline constexpr std::string_view kSoldierWeaponTransform = "Soldier.WeaponTransform";
inline constexpr std::string_view kIsXInputDevice = "Input.IsXInputDevice";
} // namespace names

std::span<const BindingSpec> DefaultBindingSpecs();

class BindingTable {
public:
    explicit BindingTable(std::span<const BindingSpec> specs) : specs_(specs.begin(), specs.end()) {}

    // Scan the executable's code region. validators may be empty.
    void Resolve(std::span<const std::uint8_t> code, std::uint64_t codeBase,
                 const std::function<BindingValidator(std::string_view)>& validatorFor = {});

    const ResolvedBinding* Find(std::string_view name) const;
    bool IsOk(std::string_view name) const;
    const std::vector<ResolvedBinding>& All() const { return resolved_; }

private:
    std::vector<BindingSpec> specs_;
    std::vector<ResolvedBinding> resolved_;
};

// Turns binding results into capability states. One place, so "why is X disabled?"
// always has a single readable answer.
void ApplyBindingsToCapabilities(const BindingTable& table, game::CapabilityRegistry& caps,
                                 bool presentHookOk);

} // namespace fvr::bf4
