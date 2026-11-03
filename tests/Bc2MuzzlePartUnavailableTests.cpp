#include "Bc2RigPublication.h"
#include <iostream>
template<class T> concept HasMuzzlePartObservation=requires(T value){value.muzzlePartObservation;};
static_assert(!HasMuzzlePartObservation<fvr::bc2::rigPublication::WeaponShotFrame>);
int main(){std::cout<<"optional observer disabled: normal shot layout has no observer member\n";}
