#pragma once
#include "fvr/runtime/SceneMilestones.h"
#include <Windows.h>

namespace fvr::bc2::sceneMilestones {
using namespace fvr::runtime;
// One inline instance across Gameplay and the renderer in the same probe DLL.
inline SceneMilestones<> journal;
inline SceneMilestoneStamp Stamp() noexcept {
    LARGE_INTEGER qpc{};QueryPerformanceCounter(&qpc);
    return {static_cast<std::uint64_t>(qpc.QuadPart),GetTickCount64(),GetCurrentThreadId()};
}
inline SceneMilestoneToken Begin(SceneMilestoneKind kind, const SceneMilestoneContext& context={}) noexcept {
    return journal.Begin(kind,context,[](const auto&){return true;},Stamp);
}
inline SceneMilestoneToken Native(SceneMilestoneKind mainKind, SceneMilestoneKind eyeKind,
    std::uintptr_t world,std::uintptr_t request,std::uintptr_t view=0) noexcept {
    SceneMilestoneContext context{};context.self=view?view:world;
    const auto main=journal.Begin(mainKind,context,[=](const auto& s){
        return s.world==world&&s.request==request&&(!view||s.main==view);},Stamp);
    if(main||!view)return main;
    return journal.Begin(eyeKind,context,[=](const auto& s){
        return s.world==world&&s.request==request&&s.eye==view;},Stamp);
}
inline void Mark(SceneMilestoneToken token,SceneMilestoneStage stage,const SceneMilestoneContext& context={}) noexcept {
    journal.Mark(token,stage,context,Stamp);
}
} // namespace fvr::bc2::sceneMilestones
