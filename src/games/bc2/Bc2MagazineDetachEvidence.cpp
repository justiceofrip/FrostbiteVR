#include "Bc2MagazineDetachEvidence.h"
#include <algorithm>
namespace fvr::bc2 {
namespace {
bool Fresh(const Bc2AmmoReserveLease& s,std::int64_t now)noexcept {
 return s.verified&&s.allThreeIdle&&s.sequence&&s.observedNs>0&&s.observedNs<=now&&s.deadlineNs>now&&
  s.deadlineNs-s.observedNs<=200000000&&s.capacity>0&&s.loaded>=0&&s.loaded<=s.capacity&&s.reserve>=0;
}
bool SameCounts(const Bc2AmmoReserveLease& a,const Bc2AmmoReserveLease& b)noexcept {
 return a.identity==b.identity&&a.loaded==b.loaded&&a.reserve==b.reserve&&a.capacity==b.capacity;
}
}
bool MagazineDetachAuthorizationFresh(const MagazineDetachAuthorization& a,std::int64_t now)noexcept {
 const auto& o=a.original;const auto& s=a.suppression;const auto& in=s.input;
 if(!a.request||s.request!=a.request||s.owner!=a.current.identity.owner||!Fresh(a.current,now)||
    a.committedNs<=0||a.committedNs>now||a.current.observedNs<a.committedNs||
    !a.baseline.verified||!a.baseline.allThreeIdle||!a.baseline.sequence||a.baseline.observedNs<=0||
    a.baseline.capacity<=0||a.baseline.loaded<=0||a.baseline.loaded>a.baseline.capacity||a.baseline.reserve<0||
    a.baseline.deadlineNs<=a.baseline.observedNs||a.baseline.deadlineNs-a.baseline.observedNs>200000000||
    a.current.sequence<a.baseline.sequence||a.current.observedNs<a.baseline.observedNs||
    a.current.identity!=a.baseline.identity||(!a.restoring&&!SameCounts(a.baseline,a.current))||
    o.owner!=in.owner||o.weapon.id==0||o.weapon.generation==0||o.item.id==0||o.item.generation==0||o.item==o.weapon||
    o.pool.id!=a.baseline.identity.serverItem||o.pool.generation!=a.baseline.identity.owner.equipGeneration||
    o.trackingEpoch!=s.owner.space||o.rounds!=unsigned(a.baseline.loaded)||o.capacity!=unsigned(a.baseline.capacity)||
    o.sourceSequence!=a.baseline.sequence||o.observedNs!=a.baseline.observedNs||o.deadlineNs!=a.baseline.deadlineNs||
    !o.profile.id||!o.profile.generation||in.nowNs>now||in.deadlineNs<=now||!in.tracked[0]||in.released[1])return false;
 auto expected=static_cast<const HolsterSuppressionRequest&>(s);expected.input.nowNs=now;
 return HolsterSuppressionCurrent(s,expected);
}
bool MagazineDetachAuthorizationRetained(const MagazineDetachAuthorization& a,const MagazineDetachAuthorization& b,std::int64_t now)noexcept {
 return MagazineDetachAuthorizationFresh(a,now)&&MagazineDetachAuthorizationFresh(b,now)&&a.request==b.request&&
  a.original==b.original&&SameCounts(a.baseline,b.baseline)&&a.baseline.sequence==b.baseline.sequence&&
  a.baseline.observedNs==b.baseline.observedNs&&a.baseline.deadlineNs==b.baseline.deadlineNs&&a.restoring==b.restoring&&
  a.suppression.owner==b.suppression.owner&&a.suppression.cache==b.suppression.cache&&
  a.suppression.nativeTick<=b.suppression.nativeTick&&a.suppression.input.sequence<=b.suppression.input.sequence&&
  a.suppression.input.observedNs<=b.suppression.input.observedNs;
}
std::optional<MagazineDetachPairReceipt> MagazineDetachPackedPair(const MagazineDetachAuthorization& a,
 const MagazineDetachAuthorization& b,bool attached,std::uint64_t draw,unsigned mask,bool exact,std::int64_t now)noexcept {
 if(!draw||mask!=3||!exact||(a.restoring&&!attached)||!MagazineDetachAuthorizationRetained(a,b,now))return {};
 return MagazineDetachPairReceipt{a,draw,now,std::min(a.current.deadlineNs,a.suppression.input.deadlineNs),mask,attached};
}
bool MagazineDetachPairCurrent(const MagazineDetachPairReceipt& p,const MagazineDetachAuthorization& current,bool attached,std::int64_t now)noexcept {
 return p.drawSerial&&p.copyMask==3&&p.attached==attached&&p.observedNs>0&&p.observedNs<=now&&p.deadlineNs>now&&
  p.deadlineNs==std::min(p.authorization.current.deadlineNs,p.authorization.suppression.input.deadlineNs)&&
  p.observedNs>=p.authorization.suppression.input.observedNs&&
  MagazineDetachAuthorizationRetained(p.authorization,current,now);
}
}
