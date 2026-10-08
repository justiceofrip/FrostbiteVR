#pragma once
#include "TrackingMath.h"
#include <array>
#include <cstdint>
namespace fvr::interaction {
struct FiringPoseKey {
 std::uint64_t actor=0,ownerGeneration=0,equipped=0,space=0;
 std::uint32_t event=0;
 bool operator==(const FiringPoseKey&)const=default;
};
struct FiringPoseSample {math::Matrix4 muzzle{};std::uint64_t generation=0;std::int64_t deadline=0;};
// Distinct native consumers of ONE verified event share its first muzzle pose.
// Times/deadlines use the caller's single monotonic clock. No deadline extension,
// matching by elapsed time, native addresses, allocation, or internal locking.
class FiringPoseHistory {
public:
 std::optional<FiringPoseSample> Resolve(const FiringPoseKey&,const FiringPoseSample&,std::int64_t now) noexcept;
 void Reset()noexcept {*this={};}
private:
 struct Entry {FiringPoseKey key{};FiringPoseSample sample{};std::int64_t created=0;};
 std::array<Entry,32> entries_{};unsigned next_=0;FiringPoseKey owner_{};
};
}
