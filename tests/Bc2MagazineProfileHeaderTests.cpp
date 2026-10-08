// This executable deliberately does not link BC2Camera or registry.cpp.
// Merely consuming profile constants/types must not add an eager registry call.
#include "Bc2MagazineNativeProfile.h"
#include <cstdio>
static_assert(sizeof(fvr::bc2::NativeMagazineProfileId)==8);
static_assert(fvr::bc2::Xm8MagazineNativeProfile.Reviewed());
static_assert(fvr::bc2::AekMagazineNativeProfile.Reviewed());
int main(){std::puts("Type-only magazine profile consumer links without runtime registry");return 0;}
