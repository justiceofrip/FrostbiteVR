#pragma once
#include <cstdint>
namespace fvr::bc2 {
struct ReloadStartOrigin {
 bool flagsKnown=false,gripPressed=false,ejectPressed=false;
 std::uint64_t inputSequence=0,held=0,pressed=0;
};
}
