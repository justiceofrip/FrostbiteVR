#pragma once

#if defined(_MSC_VER) && defined(_WIN32)
namespace fvr::bc2 {
// One SEH frame per hook invocation. Both closures refer to objects in its
// still-live caller. A nested game handler can unwind a child independently.
template<class Body,class Abort>
__declspec(noinline) void RunOriginalFiringUnwind(Body& body,Abort& abort) {
    bool returned=false;
    __try {
        body();
        returned=true;
    } __finally {
        if(!returned)abort();
    }
}
}
#endif
