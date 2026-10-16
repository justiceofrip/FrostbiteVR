#pragma once
#include <cstdint>
#include <type_traits>
#include <array>
namespace fvr::bc2 {
// Private fixed-size read-only witness files. No pointer in this receipt is
// dereferenced by the receiver. All native action still uses normal input.
struct TwoGrenadeNativeWitness {
    std::uint64_t version=1,pid=0,creation=0,observedNs=0,deadlineNs=0,input=0;
    std::uint64_t player=0,soldier=0,weak=0,actorGeneration=0,space=0,weapon=0;
    std::uint64_t attempts=0,edges=0,completed=0,returnInput=0,returnObservedNs=0;
    std::uint64_t neutralInput=0,neutralObservedNs=0,gaps=0,returnNoGaps=0,phase=0,fault=0,tickMs=0;
};
struct TwoGrenadeHealthWitness {
    std::uint64_t version=1,pid=0,creation=0,tickMs=0,index=0,valid=0;
    std::uint64_t player=0,soldier=0,weak=0,serverPlayer=0,serverSoldier=0;
    std::uint64_t initialClient=0,initialServer=0,client=0,server=0,maximum=0;
};
static_assert(sizeof(TwoGrenadeNativeWitness)==192&&sizeof(TwoGrenadeHealthWitness)==128);
static_assert(std::is_trivially_copyable_v<TwoGrenadeNativeWitness>&&std::is_trivially_copyable_v<TwoGrenadeHealthWitness>);
class TwoGrenadeLifetimeGuard {
public:
    bool Allows(std::uint64_t player,std::uint64_t soldier,std::uint64_t weak,std::uint64_t generation,std::uint64_t space,unsigned edges)noexcept {
        if(!player||!soldier||!weak||!generation||!space)return false;
        const std::array<std::uint64_t,5> current{player,soldier,weak,generation,space};
        if(!Pinned())pin_=current;
        if(current!=pin_)lost_=true;
        if(edges<lastEdges_)lost_=true;lastEdges_=edges;
        return !lost_&&edges<2;
    }
    bool Pinned()const noexcept{return pin_[0]!=0;}
    bool Lost()const noexcept{return lost_;}
    const auto& Pin()const noexcept{return pin_;}
private:
    std::array<std::uint64_t,5> pin_{};unsigned lastEdges_=0;bool lost_=false;
};
}
