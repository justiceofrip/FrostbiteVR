#pragma once
namespace fvr::bc2 {
// Ordering only; caller still needs all adapter owner/caller/stack/config/count
// and exact helper guards. This grants no native capability or held receipt.
// A populated branch/context is not enough: PrepareHold may fail AFTER those
// fields are filled. Only a successful validated own invocation can mint this
// separate observation receipt, with all later native evidence still required.
inline bool PreholdAuthoritativeIdleInvocation(bool ready,unsigned branch,bool helperCalled,bool retained,bool boundaryKnown)noexcept {
 return ready&&branch==2&&!helperCalled&&retained&&boundaryKnown;
}
inline bool PreholdServerFirstOrder(unsigned branch,unsigned called,unsigned exact,unsigned idle,bool serverCurrentlyIdle,bool authoritativeServerIdle=false)noexcept {
 if(branch>=3||(called&(1u<<branch)))return false;
 if(branch==2)return called==0; // First helper on its OWN server Update only.
 return serverCurrentlyIdle&&(((called&4)&&(exact&4)&&(idle&4))||authoritativeServerIdle);
}
}
