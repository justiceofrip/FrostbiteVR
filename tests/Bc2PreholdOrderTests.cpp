#include "Bc2PreholdOrder.h"
#include <cstdio>
using namespace fvr::bc2;
#define CHECK(x) do{if(!(x)){std::fprintf(stderr,"line%d %s\n",__LINE__,#x);return 1;}}while(false)
int main(){
 // Actual spas03 clients0/1 cleaned before overlapping server1822, then
 // snapshot1826 restored reload. This ordering policy rejects that route.
 CHECK(!PreholdServerFirstOrder(0,0,0,0,false));CHECK(!PreholdServerFirstOrder(1,0,0,0,false));
 CHECK(PreholdServerFirstOrder(2,0,0,0,false));CHECK(!PreholdServerFirstOrder(2,1,1,1,false));
 CHECK(!PreholdServerFirstOrder(0,4,0,0,true));CHECK(!PreholdServerFirstOrder(0,4,4,0,true));
 CHECK(!PreholdServerFirstOrder(0,4,4,4,false));CHECK(PreholdServerFirstOrder(0,4,4,4,true));
 CHECK(PreholdServerFirstOrder(1,5,5,5,true));CHECK(!PreholdServerFirstOrder(0,5,5,5,true));
 CHECK(!PreholdServerFirstOrder(2,4,4,4,true));CHECK(!PreholdServerFirstOrder(3,0,0,0,false));
 CHECK(PreholdServerFirstOrder(0,0,0,0,true,true));
 CHECK(PreholdServerFirstOrder(1,0,0,0,true,true));
 CHECK(!PreholdServerFirstOrder(0,0,0,0,false,true));
 CHECK(!PreholdServerFirstOrder(0,0,0,0,true,false));
 CHECK(!PreholdServerFirstOrder(0,1,0,0,true,true));
 CHECK(PreholdServerFirstOrder(2,0,0,0,true,true));
 // Real producer uses this predicate. Partially populated branch2 after
 // failed PrepareHold must never publish authoritative-idle evidence.
 CHECK(PreholdAuthoritativeIdleInvocation(true,2,false,true,true));
 CHECK(!PreholdAuthoritativeIdleInvocation(false,2,false,true,true));
 CHECK(!PreholdAuthoritativeIdleInvocation(true,1,false,true,true));
 CHECK(!PreholdAuthoritativeIdleInvocation(true,2,true,true,true));
 CHECK(!PreholdAuthoritativeIdleInvocation(true,2,false,false,true));
 CHECK(!PreholdAuthoritativeIdleInvocation(true,2,false,true,false));
 std::puts("Prehold server-first receipt ordering passed; no native authority claim");return 0;
}
