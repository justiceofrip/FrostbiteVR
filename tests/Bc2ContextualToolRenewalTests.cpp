#include "Bc2ContextualToolSource.h"
#include <cstdio>
using namespace fvr::interaction;
using fvr::bc2::ContextualToolRenewalContact;
#define CHECK(v) do{if(!(v)){std::printf("FAIL %d: %s\n",__LINE__,#v);return 1;}}while(false)
int main(){
    HandInteraction hands;
    HandInteractionSample s{{0x1234567800010000ull,1,4,1},124,
        511303898419600,511303998419600,511303917304900,true,{true,true},{true,false}};
    const HandInteractionKey item{128563584,4},contactKey{0x4243325447524950ull,4};
    const auto acquired=hands.Acquire(s,{s.owner,InteractionHand::Right,HandClaimKind::GunHold,item,
        {contactKey,s.sequence,s.deadlineNs,true},1,0});
    CHECK(acquired.claim);const auto token=acquired.claim->token;
    s.sequence=126;s.observedNs=511303929182700;s.deadlineNs=511304029182700;s.nowNs=511303935000000;
    HandContactProof olderRender{contactKey,s.sequence,511304013774600,true};
    CHECK(hands.Renew(s,token,olderRender).accepted);
    s.nowNs=511303950753400;
    const HandContactProof newerRender{contactKey,s.sequence,s.deadlineNs,true};
    // Old adapter forwards the wider same-input deadline and is rejected by
    // the real shared policy. It then releases this otherwise-current hold.
    const auto oldResult=hands.Renew(s,token,newerRender);
    CHECK(!oldResult.accepted&&oldResult.reason==HandInteractionReason::ContactMismatch);
    const auto original=hands.Current(InteractionHand::Right);CHECK(original&&original->token==token);
    auto current=ContextualToolRenewalContact(newerRender,*original);
    CHECK(current.deadlineNs==olderRender.deadlineNs&&hands.Renew(s,token,current).accepted);
    CHECK(hands.Current(InteractionHand::Right)->deadlineNs==olderRender.deadlineNs);
    // Tightening remains effective and never moves back to the larger bound.
    auto shorter=newerRender;shorter.deadlineNs=olderRender.deadlineNs-1000;
    current=ContextualToolRenewalContact(shorter,*hands.Current(InteractionHand::Right));
    CHECK(hands.Renew(s,token,current).accepted);
    CHECK(ContextualToolRenewalContact(newerRender,*hands.Current(InteractionHand::Right)).deadlineNs==shorter.deadlineNs);
    // A genuinely new original packet can renew within its own lease.
    ++s.sequence;s.observedNs+=15000000;s.deadlineNs=s.observedNs+100000000;s.nowNs+=15000000;
    const HandContactProof next{contactKey,s.sequence,s.deadlineNs,true};
    current=ContextualToolRenewalContact(next,*hands.Current(InteractionHand::Right));
    CHECK(current.deadlineNs==s.deadlineNs&&hands.Renew(s,token,current).accepted);
    auto foreign=current;++foreign.key.generation;
    CHECK(!hands.Renew(s,token,ContextualToolRenewalContact(foreign,*hands.Current(InteractionHand::Right))).accepted);
    auto expired=current;expired.deadlineNs=s.nowNs;
    CHECK(!hands.Renew(s,token,ContextualToolRenewalContact(expired,*hands.Current(InteractionHand::Right))).accepted);
    s.nowNs=s.deadlineNs;hands.Update(s);
    CHECK(!hands.Current(InteractionHand::Right));CHECK(!hands.Renew(s,token,next).accepted);
    std::puts("One contextual renewal group PASS: real shared old rejection, bounded new renewal, newer-source renewal and unchanged foreign/expiry guards.");
    return 0;
}
