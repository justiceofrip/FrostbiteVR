#include "fvr/interaction/ShellReloadAudio.h"
#include "Bc2ShellReloadAudioProfile.h"
#include "Test.h"
#include <iostream>
using namespace fvr::interaction;
int main(){
    using namespace fvr::bc2;
    constexpr std::array<unsigned char,16> reload{0xf3,0x0a,0x49,0xaa,0x6d,0xe7,0xde,0x42,0x81,0xbb,0x2c,0x3d,0xce,0x52,0x0f,0x84};
    constexpr std::array<unsigned char,16> pump{0x1b,0xf4,0x9c,0x2d,0xba,0x3a,0x22,0x4a,0x82,0xa8,0x64,0x05,0x1c,0x18,0x04,0x32};
    CHECK(ShellReloadSoundFor("SPAS12_sp",reload)==ShellReloadSound::InsertLoop);
    CHECK(ShellReloadSoundFor("SPAS12_sp",pump)==ShellReloadSound::Unrelated);
    CHECK(ShellReloadSoundFor("TOZ194",reload)==ShellReloadSound::Unrelated);
    CHECK(ShellReloadSoundFor("SPAS12_sp",{})==ShellReloadSound::Unrelated);
    CHECK(ShellReloadSoundFor(static_cast<const ShotgunAudioProfile*>(nullptr),reload)==ShellReloadSound::Unrelated);
    auto missingFinish=LegacySpasAudio;missingFinish.finish={};
    CHECK(ShellReloadSoundFor(&missingFinish,LegacySpasAudio.finish)==ShellReloadSound::Unrelated);
    CHECK(ShellReloadSoundFor(&missingFinish,reload)==ShellReloadSound::InsertLoop);
    auto unproven=LegacySpasAudio;unproven.provenance={};
    CHECK(ShellReloadSoundFor(&unproven,reload)==ShellReloadSound::Unrelated);
    CHECK(ShellReloadLocalStart(true,ShellReloadSound::InsertLoop,true,1,1,true));
    CHECK(ShellReloadLocalStart(true,ShellReloadSound::Finish,true,1,1,true));
    CHECK(!ShellReloadLocalStart(false,ShellReloadSound::InsertLoop,true,1,1,true));
    CHECK(!ShellReloadLocalStart(true,ShellReloadSound::Unrelated,true,1,1,true));
    CHECK(!ShellReloadLocalStart(true,ShellReloadSound::InsertLoop,false,1,1,true));
    CHECK(!ShellReloadLocalStart(true,ShellReloadSound::InsertLoop,true,0,1,true));
    CHECK(!ShellReloadLocalStart(true,ShellReloadSound::InsertLoop,true,1,0,true));
    CHECK(!ShellReloadLocalStart(true,ShellReloadSound::InsertLoop,true,1,1,false));
    ShellReloadAudioLedger<> ledger;
    ShellReloadAudioEvent e{{1,2,3,4,5},6,7,8,1000000000,1100000000,1,true,true,true};
    CHECK(ledger.Accept(e,e.observedNs)==ShellReloadAudioAdmission::Accepted);
    CHECK(ledger.Accept(e,e.observedNs)==ShellReloadAudioAdmission::Duplicate);
    auto again=e;again.sequence++;again.observedNs++;
    CHECK(ledger.Accept(again,again.observedNs)==ShellReloadAudioAdmission::Duplicate);
    auto next=e;++next.request;++next.nativeEvent;++next.sequence;++next.observedNs;
    CHECK(ledger.Accept(next,next.observedNs)==ShellReloadAudioAdmission::Accepted);
    CHECK(ledger.Accept(e,e.observedNs)==ShellReloadAudioAdmission::Stale);
    for(unsigned invalid=0;invalid<8;++invalid){auto bad=next;++bad.request;++bad.nativeEvent;
        if(invalid==0)bad.applied=false;if(invalid==1)bad.units=2;if(invalid==2)bad.units=0;
        if(invalid==3)bad.focused=false;if(invalid==4)bad.tracked=false;if(invalid==5)bad.nativeEvent=0;
        if(invalid==6)bad.identity.itemGeneration=0;if(invalid==7)bad.request=0;
        CHECK(ledger.Accept(bad,bad.observedNs)==ShellReloadAudioAdmission::Invalid);
    }
    auto stale=next;++stale.request;++stale.nativeEvent;
    CHECK(ledger.Accept(stale,stale.deadlineNs)==ShellReloadAudioAdmission::Stale);
    CHECK(ledger.Accept(stale,stale.observedNs-1)==ShellReloadAudioAdmission::Stale);
    auto other=next;other.identity.item=100;
    CHECK(ledger.Accept(other,other.observedNs)==ShellReloadAudioAdmission::Accepted);
    ++other.identity.cycle;other.request=1;++other.nativeEvent;++other.sequence;++other.observedNs;
    CHECK(ledger.Accept(other,other.observedNs)==ShellReloadAudioAdmission::Accepted);
    // Selection/regrip does not reset either ledger; explicit actor retirement does.
    CHECK(ledger.Accept(other,other.observedNs)==ShellReloadAudioAdmission::Duplicate);
    ledger.RetireActor(99,2);CHECK(ledger.Accept(other,other.observedNs)==ShellReloadAudioAdmission::Duplicate);
    ledger.RetireActor(1,2);CHECK(ledger.Accept(other,other.observedNs)==ShellReloadAudioAdmission::Accepted);
    ShellReloadAudioLedger<1> bounded;
    CHECK(bounded.Accept(e,e.observedNs)==ShellReloadAudioAdmission::Accepted);
    CHECK(bounded.Accept(other,other.observedNs)==ShellReloadAudioAdmission::Capacity);
    ShellReloadAudioSuppression<> suppress;
    CHECK(!suppress.Allows(e.identity));CHECK(suppress.Start(e.identity,true));CHECK(suppress.Allows(e.identity));
    CHECK(suppress.Start(e.identity,true));CHECK(!suppress.Start(e.identity,false));CHECK(!suppress.Allows(e.identity));
    CHECK(!suppress.Start(e.identity,true)); // A stock loop cannot be undone by a late physical receipt.
    auto cycle=e.identity;++cycle.cycle;CHECK(suppress.Start(cycle,true));CHECK(suppress.Allows(cycle));
    CHECK(!suppress.Start(e.identity,true));CHECK(!suppress.Allows(e.identity));
    suppress.RetireActor(1,2);CHECK(!suppress.Allows(cycle));CHECK(!suppress.Start({},true));
    ShellReloadAudioSuppression<> nativeStarts;
    CHECK(!ShellReloadSuppressStart(nativeStarts,ShellReloadSound::Finish,e.identity,true));
    CHECK(!nativeStarts.Allows(e.identity)); // A finish/cocking graph cannot create insertion authority.
    CHECK(!ShellReloadSuppressStart(nativeStarts,ShellReloadSound::Unrelated,e.identity,true));
    CHECK(ShellReloadSuppressStart(nativeStarts,ShellReloadSound::InsertLoop,e.identity,true));
    CHECK(ShellReloadSuppressStart(nativeStarts,ShellReloadSound::Finish,e.identity,true));
    CHECK(!ShellReloadSuppressStart(nativeStarts,ShellReloadSound::Finish,e.identity,false));
    CHECK(!ShellReloadSuppressStart(nativeStarts,ShellReloadSound::Finish,cycle,true));
    CHECK(nativeStarts.Allows(e.identity)); // An unrelated cycle cannot revoke the legitimate cue.
    CHECK(!ShellReloadSuppressStart(nativeStarts,ShellReloadSound::InsertLoop,e.identity,false));
    CHECK(!ShellReloadSuppressStart(nativeStarts,ShellReloadSound::Finish,e.identity,true));
    std::cout<<"ShellReloadAudio: exact applied-shell dedupe, lifetime, freshness, bounds and isolated stock ownership passed\n";
}
