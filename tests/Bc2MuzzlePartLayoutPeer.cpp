#include "Bc2RigPublication.h"
#include "Bc2MuzzlePartProfiles294.h"
#include <cstddef>
const fvr::bc2::muzzle_part_observation::Profile* MuzzlePartProfilePeer() {
    return &fvr::bc2::muzzle_part_observation::Profiles294[0];
}
std::size_t MuzzlePartLayoutPeer() {return sizeof(fvr::bc2::rigPublication::WeaponShotFrame);}
