#pragma once
#include <array>
#include <cstdint>
#include <cstddef>

namespace fvr::bc2::selectedKnife {
enum class KnifeSoundKind:unsigned { Unknown,Attack,AttackKill };
// Animation asset primary-instance GUIDs, in native little-endian GUID layout.
// These identify authored sound events, not damage or a successful native hit.
inline constexpr std::array<unsigned char,16> KnifeFireSoundGuid{
    0x3e,0xa2,0x20,0x1d,0x50,0xcc,0xc9,0x47,0xa9,0x80,0x4a,0xd6,0xd7,0x20,0xf2,0x7e};
inline constexpr std::array<unsigned char,16> KnifeFireKillSoundGuid{
    0x7e,0xab,0x75,0x93,0x30,0x7a,0x3c,0x49,0x96,0x61,0xdc,0x14,0xdf,0x52,0x3d,0xe9};
inline KnifeSoundKind MatchKnifeSound(const std::array<unsigned char,16>& guid)noexcept {
    return guid==KnifeFireSoundGuid?KnifeSoundKind::Attack:
        guid==KnifeFireKillSoundGuid?KnifeSoundKind::AttackKill:KnifeSoundKind::Unknown;
}
// Same-owner temporal candidates only. An animation callback carries no native
// melee transaction token, so even one candidate is not causal/hit authority.
struct KnifeSoundCandidate {
    std::uint64_t serial=0,actorGeneration=0,itemGeneration=0;
    std::int64_t stagedNs=0,publishedNs=0,acquiredNs=0,attackedNs=0;
    unsigned damageCalls=0;
    bool completed=false;
};
struct KnifeSoundObservation {
    std::uint64_t ordinal=0;
    std::int64_t observedNs=0;
    unsigned receiver=0,owner=0,weapon=0,thread=0,unused=0;
    KnifeSoundKind kind=KnifeSoundKind::Unknown;
    bool start=false,ownerVerified=false;
    std::array<KnifeSoundCandidate,2> candidates{};
    unsigned candidateCount=0;
};
inline bool KnifeSoundLocalBoundary(bool readable,unsigned owner,unsigned expectedOwner,
    unsigned char firstPerson,unsigned char ownerFlags,bool retainedOwner)noexcept {
    return readable&&owner>=0x10000&&owner==expectedOwner&&firstPerson==1&&(ownerFlags&1)&&retainedOwner;
}
inline bool AddKnifeSoundCandidate(KnifeSoundObservation& row,const KnifeSoundCandidate& candidate)noexcept {
    constexpr std::int64_t horizon=2000000000;
    if(!row.ownerVerified||!candidate.serial||!candidate.actorGeneration||!candidate.itemGeneration||
       candidate.publishedNs<=0||candidate.publishedNs<candidate.stagedNs||
       row.observedNs<candidate.publishedNs||row.observedNs-candidate.publishedNs>horizon)return false;
    for(unsigned n=0;n<row.candidateCount;++n)if(row.candidates[n].serial==candidate.serial)return false;
    if(row.candidateCount==row.candidates.size())return false;
    row.candidates[row.candidateCount++]=candidate;return true;
}
template<std::size_t Capacity=64> class KnifeSoundJournal {
    static_assert(Capacity>=2&&Capacity%2==0);
public:
    bool Observe(KnifeSoundObservation row)noexcept {
        if(!row.ownerVerified||row.kind==KnifeSoundKind::Unknown||row.observedNs<=0||total_==UINT64_MAX)return false;
        row.ordinal=++total_;constexpr auto half=Capacity/2;
        // Keep the opening and newest evidence; storage never grows per attack.
        const auto slot=total_<=half?total_-1:half+(total_-half-1)%half;
        rows_[std::size_t(slot)]=row;return true;
    }
    const auto& Rows()const noexcept{return rows_;}
    std::uint64_t Total()const noexcept{return total_;}
private:
    std::array<KnifeSoundObservation,Capacity> rows_{};
    std::uint64_t total_=0;
};
}
