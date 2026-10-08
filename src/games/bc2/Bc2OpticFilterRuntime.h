#pragma once
#include "Bc2OpticFilter.h"
#include "Bc2OpticDrawState.h"
#include "Bc2OpticFilterSession.h"
#include <memory>
#include <ostream>
namespace fvr::bc2::opticFilterRuntime {
struct Source {ReloadStateOwner owner{};std::shared_ptr<const SelectedMeshesSnapshot> selected;};
using SourceResolver=std::optional<Source>(*)()noexcept;
struct View {std::uint64_t request=0,view=0,frame=0;std::int32_t eye=-1;};
// Return no view unless the caller already owns the exact current draw scope.
using ViewResolver=std::optional<View>(*)()noexcept;
bool Install(std::span<const std::byte>,const engine::PeImage&,std::uint32_t base,
    SourceResolver,ViewResolver,bool diagnostic=false);
bool BeginGlobalStart()noexcept;
bool CompleteGlobalStart(bool globalEnableSucceeded)noexcept;
// Call on focus/tracking/owner cancellation. No old scope survives a reset.
// Reconnection requires an explicit true and a new genuinely fresh source.
void SetConnected(bool connected)noexcept;
void Disable()noexcept;
bool Quiescent()noexcept;
// Invoke inside existing D3D Draw/DrawIndexed observers immediately BEFORE the
// original draw. No hook is created here. Captures only while the exact native
// lens-filter callback owns this thread's scope; nested rejected calls mask it.
void CaptureDraw(ID3D11DeviceContext*,bool indexed,std::uint32_t count,std::uint32_t start,std::int32_t base=0)noexcept;
void Report(std::ostream&);
}
