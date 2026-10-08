#pragma once
#include "Bc2AmmoReserve.h"
#include "Bc2HolsterInput.h"
#include "fvr/interaction/DetachableMagazine.h"
namespace fvr::bc2 {
// A committed ordinary action-cache suppression capability. It is explicitly
// NOT a native reload hold, an ammo reservation, or a transfer acknowledgement.
struct MagazineDetachAuthorization {
 interaction::OriginalMagazine original{};
 Bc2AmmoReserveLease baseline{},current{};
 HolsterSuppressionReceipt suppression{};
 std::uint64_t request=0;
 std::int64_t committedNs=0; // Actual cache Commit precedes current reserve read.
 bool restoring=false; // Only an attached presentation is permitted here.
};
struct MagazineDetachPairReceipt {
 MagazineDetachAuthorization authorization{};
 std::uint64_t drawSerial=0;
 std::int64_t observedNs=0,deadlineNs=0;
 unsigned copyMask=0;
 bool attached=false;
};
bool MagazineDetachAuthorizationFresh(const MagazineDetachAuthorization&,std::int64_t now)noexcept;
bool MagazineDetachAuthorizationRetained(const MagazineDetachAuthorization& old,
 const MagazineDetachAuthorization& current,std::int64_t now)noexcept;
// Called only after both verified native Pack destinations matched their
// private palettes and the original native source remained byte-identical.
std::optional<MagazineDetachPairReceipt> MagazineDetachPackedPair(
 const MagazineDetachAuthorization& source,const MagazineDetachAuthorization& current,
 bool attached,std::uint64_t drawSerial,unsigned copyMask,bool exactBytes,std::int64_t now)noexcept;
bool MagazineDetachPairCurrent(const MagazineDetachPairReceipt&,const MagazineDetachAuthorization&,
 bool attached,std::int64_t now)noexcept;
}
