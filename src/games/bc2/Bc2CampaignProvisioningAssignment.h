#pragma once
#include <cstdint>
namespace fvr::bc2::provisioning_candidate {
bool InvokeOriginalAssignment(void*,std::uint32_t actor,std::uint32_t data,std::uint32_t slot,std::uint32_t& item)noexcept;
}
