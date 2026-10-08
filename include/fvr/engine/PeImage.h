#pragma once
#include <cstddef>
#include <cstdint>
#include <span>
#include <string>
#include <vector>
namespace fvr::engine {
struct PeSection {std::string name;std::uint32_t rva=0,virtualSize=0,rawOffset=0,rawSize=0,flags=0;};
struct PeImage {std::uint16_t machine=0;std::uint32_t timestamp=0,imageSize=0;bool largeAddressAware=false;
    std::vector<PeSection> sections;std::vector<std::string> imports;};
struct PeResult {bool valid=false;PeImage image;std::string error;};
PeResult InspectPe(std::span<const std::byte> bytes);
}