#pragma once
#include <dxgi.h>
namespace fvr::graphics {
using SwapChainPresentFn=HRESULT(STDMETHODCALLTYPE*)(IDXGISwapChain*,UINT,UINT);
// The VR runtime controls headset cadence. Only the adapter's verified desktop
// swap chain may bypass its monitor wait; preserve probe/test flags and errors.
inline HRESULT PresentDesktopForVr(SwapChainPresentFn original,IDXGISwapChain* chain,
                                    UINT interval,UINT flags,bool active)noexcept {
    const auto effective=active&&!(flags&DXGI_PRESENT_TEST)&&interval<=4?0u:interval;
    return original(chain,effective,flags);
}
}
