#define main ExistingCustodyTests
#include "HandGunCustodyTests.cpp"
#undef main
namespace {
int ActualReleaseEdgeBothHands(){
    for(auto side:{left,right}){Fixture f(side);const auto original=f.sample;f.Next();f.sample.released[static_cast<unsigned>(side)]=true;
        auto t=f.Transfer(false);t.releaseDepartingGun=true;t.nextGun.evidence=original;
        t.nextGun.request.contact.inputSequence=original.sequence;t.nextGun.request.contact.deadlineNs=original.deadlineNs;
        const auto r=f.hands.TransferGunCustody(f.sample,t);CHECK(r.transaction.accepted&&r.transaction.claim&&Releases(r.transaction)==2);
        CHECK(f.hands.Current(Other(side))&&!f.hands.Current(side));const auto gun=r.transaction.claim->token;
        // The old released packet is not a new grip, nor a current gun claim.
        auto repeated=f.hands.TransferGunCustody(f.sample,t);CHECK(!repeated.transaction.accepted&&f.hands.Current(Other(side))->token==gun);
        f.Next();f.sample.released[static_cast<unsigned>(side)]=false;
        auto request=f.Request(side,HandClaimKind::Mechanism,40,gun.id);
        request.contact.inputSequence=original.sequence;request.contact.deadlineNs=original.deadlineNs;
        CHECK(!f.hands.AcquireFrom(f.sample,original,request).accepted);
    }return 0;
}
int FailedReleaseDestroysDepartingDependency(){
    for(unsigned n=0;n<6;++n){Fixture f;const auto source=f.sample;f.Next();f.sample.released[1]=true;
        auto t=f.Transfer(false);t.releaseDepartingGun=true;t.nextGun.evidence=source;
        t.nextGun.request.contact.inputSequence=source.sequence;t.nextGun.request.contact.deadlineNs=source.deadlineNs;
        if(n==0)t.nextGun.request.contact.eligible=false;if(n==1)t.nextGun.evidence.deadlineNs++;
        if(n==2)f.sample.focused=false;if(n==3)f.sample.tracked[0]=false;
        if(n==4){f.sample.observedNs+=200*ms;f.sample.nowNs=f.sample.observedNs;f.sample.deadlineNs=f.sample.nowNs+150*ms;}
        if(n==5)t.nextGun.request.intent=0;
        const auto r=f.hands.TransferGunCustody(f.sample,t);CHECK(!r.transaction.accepted);
        CHECK(!f.hands.Current(left)&&!f.hands.Current(right));
    }return 0;
}
int ReleaseMustBeUnconsumedAndCannotRollback(){
    Fixture f;f.Next();f.sample.released[1]=true;auto t=f.Transfer(false);t.releaseDepartingGun=true;
    f.hands.Update(f.sample);CHECK(!f.hands.TransferGunCustody(f.sample,t).transaction.accepted&&!f.hands.Current(left)&&!f.hands.Current(right));
    Fixture g;auto stale=g.sample;g.Next();g.hands.Update(g.sample);auto queued=g.Transfer(false);queued.releaseDepartingGun=true;
    stale.released[1]=true;CHECK(!g.hands.TransferGunCustody(stale,queued).transaction.accepted&&g.Unchanged());return 0;
}
}
int main(){if(ExistingCustodyTests()||ActualReleaseEdgeBothHands()||FailedReleaseDestroysDepartingDependency()||ReleaseMustBeUnconsumedAndCannotRollback())return 1;
    std::puts("3 release-edge groups plus9 existing custody groups passed.");return 0;}
