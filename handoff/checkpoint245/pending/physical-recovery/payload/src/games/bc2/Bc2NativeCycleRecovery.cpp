#include "Bc2NativeCycleRecovery.h"
namespace fvr::bc2::reloadFlowRuntime {
// Deliberately unavailable until original callback convergence and continuous
// native fire/ammunition guard ownership are integrated. Consumer/renderer
// tests supply their own explicitly mocked adapter channel.
std::optional<Bc2NativeCycleRecoveryView> ReadNativeCycleRecoveryView(std::int64_t)noexcept{return {};}
}
