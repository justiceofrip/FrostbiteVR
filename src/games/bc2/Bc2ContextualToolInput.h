#pragma once
#include "Bc2HolsterInput.h"
#include "Bc2ContextualTool.h"

namespace fvr::bc2 {
enum class ToolInputKind:unsigned {Equip,Controls,Return};
struct ToolInputRequest {
    HolsterSuppressionRequest scope{};
    std::uint64_t operation=0,serial=0;
    ToolInputKind kind=ToolInputKind::Controls;
    BodyNativeItem from{},target{};
    interaction::HandClaimToken custody{};
    bool zoom=false,fire=false;
};
// A cache commit, NOT native selection, EmptyHands, visibility, shot or count
// acknowledgement. Deliberately unrelated to HolsterSuppressionReceipt.
struct ToolInputReceipt {ToolInputRequest request{};};
struct ToolInputValidation {
    void* context=nullptr;
    bool(*current)(void*,const ToolInputRequest&)noexcept=nullptr;
};
class ContextualToolInputOverride {
public:
    ~ContextualToolInputOverride(){Restore();}
    ContextualToolInputOverride()=default;
    ContextualToolInputOverride(const ContextualToolInputOverride&)=delete;
    ContextualToolInputOverride& operator=(const ContextualToolInputOverride&)=delete;
    bool Apply(std::span<std::byte>,const ToolInputRequest&,ToolInputValidation)noexcept;
    std::optional<ToolInputReceipt> Commit()noexcept;
    bool Restore()noexcept;
private:
    std::span<std::byte> cache_{};ToolInputRequest request_{};ToolInputValidation validation_{};
    std::array<std::uint32_t,4> before_{},written_{};bool active_=false;
};
}
