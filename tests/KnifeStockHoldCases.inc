namespace {
// Model the coordinator's publication choice, then use the real custody and
// native one-shot policies. No receipt or native acceptance is fabricated by
// the production helper; this fixture supplies its explicit test receipts.
std::optional<KnifeCommand> StockGapFrame(Fixture& f,bool rendered,std::int64_t elapsed=10*ms){
    const auto contact=f.knife.Hold()->contact;
    ++f.s.source.sequence;f.s.source.observedNs+=elapsed;f.s.source.nowNs=f.s.source.observedNs;
    f.s.source.deadlineNs=f.s.source.nowNs+100*ms;f.s.claimIntent=f.s.source.sequence;
    f.s.menu.deadlineNs=f.s.source.deadlineNs;++f.s.menu.sequence;f.hands.Update(f.s.source);
    f.s.trackedKnifeOwned=rendered;f.s.heldContact={};
    if(rendered)f.s.heldContact={contact,f.s.source.sequence,f.s.source.deadlineNs,true};
    else if(const auto retained=ChestKnifeRetainedStockContact(f.knife,f.hands,f.s,true,true)){
        f.s.trackedKnifeOwned=true;f.s.heldContact=*retained;
    }
    f.s.stockTriggerReady=f.s.trackedKnifeOwned&&f.s.mechanicsSettled;
    return f.knife.Prepare(f.s,f.hands);
}
int StockHoldPublicationGapCases(){
    Fixture f;CHECK(f.Held());f.s.nativeAttackReady=false;
    f.s.stockTriggerReady=f.s.stockTriggerInputCurrent=true;CHECK(!f.Step()&&!f.Step());
    CHECK(f.knife.StockTriggerArmedFor(f.s,f.hands));
    const auto original=*f.hands.Current(InteractionHand::Right);
    CHECK(!StockGapFrame(f,false));CHECK(f.knife.StockTriggerArmedFor(f.s,f.hands));
    CHECK(f.hands.Current(InteractionHand::Right)->token==original.token);
    CHECK(f.hands.Current(InteractionHand::Right)->deadlineNs==original.deadlineNs);
    CHECK(!f.s.nativeAttackReady&&!f.knife.RecognitionReady(f.s.source));
    f.s.controlsNeutral=f.s.actionInputCurrent=false;f.s.holdInputCurrent=true;f.s.stockTrigger=1;
    const auto attack=StockGapFrame(f,false);
    CHECK(attack&&attack->stockTriggerHold==f.knife.Hold()&&!attack->stroke);
    CHECK(f.knife.CanCommit(*attack,f.s,f.hands));
    CHECK(f.hands.Current(InteractionHand::Right)->deadlineNs==original.deadlineNs);
    private_knife::StockKnifeSourceRevocation source;CHECK(!source.Advance(f.s));const auto stamp=source.Stamp();
    const auto retained=ChestKnifeRetainedStockContact(f.knife,f.hands,f.s,true,true);CHECK(retained);
    f.s.heldContact=*retained;CHECK(!source.Advance(f.s)&&source.Current(stamp));
    CHECK(!f.knife.Prepare(f.s,f.hands)&&f.knife.CanCommit(*attack,f.s,f.hands));
    private_knife::KnifeAttackGate native;CHECK(native.Stage(*attack,f.s.source.nowNs));
    CHECK(!native.Consume(0,f.s.source.nowNs,true));
    CHECK(native.Consume(1,f.s.source.nowNs,source.Current(stamp)));
    CHECK(!native.Consume(1,f.s.source.nowNs,source.Current(stamp)));
    CHECK(native.ResolveBeforeSource(f.s.source.sequence,f.s.source.observedNs,f.s.source.nowNs)==KnifeCommit::Accepted);
    f.knife.Committed(*attack,KnifeCommit::Accepted);
    // Busy native work retains only custody. It cannot arm another command.
    f.s.mechanicsSettled=false;CHECK(!StockGapFrame(f,false));
    CHECK(f.hands.Current(InteractionHand::Right)->deadlineNs==original.deadlineNs);
    CHECK(!f.knife.StockTriggerArmedFor(f.s,f.hands));
    CHECK(native.Consume(2,attack->source.deadlineNs+1,false));
    CHECK(!native.Consume(2,attack->source.deadlineNs+1,false));
    for(unsigned branch=0;branch<3;++branch)native.Observe(branch,2,5,6);
    CHECK(native.Complete(attack->serial,true,true));
    CHECK(f.knife.Observe(f.Receipt(*attack)));f.s.mechanicsSettled=true;
    for(unsigned n=0;n<3;++n)CHECK(!StockGapFrame(f,true)); // Held trigger never repeats.
    f.s.stockTrigger=0;CHECK(!StockGapFrame(f,true));f.s.stockTrigger=1;
    const auto next=StockGapFrame(f,false);CHECK(next&&next->serial>attack->serial&&next->stockTriggerHold&&!next->stroke);

    {Fixture x;CHECK(x.Held());x.s.stockTriggerReady=x.s.stockTriggerInputCurrent=true;
        CHECK(!x.Step()&&!x.Step());const auto before=*x.hands.Current(InteractionHand::Right);
        for(unsigned n=0;n<9;++n){CHECK(!StockGapFrame(x,false));CHECK(x.hands.Current(InteractionHand::Right)->deadlineNs==before.deadlineNs);}
        CHECK(!StockGapFrame(x,false));CHECK(!x.hands.Current(InteractionHand::Right));
        CHECK(!ChestKnifeRetainedStockContact(x.knife,x.hands,x.s,true,true));
        CHECK(!StockGapFrame(x,true));CHECK(!x.hands.Current(InteractionHand::Right));} // No expired reacquire.

    for(unsigned fault=0;fault<19;++fault){Fixture x;CHECK(x.Held());
        auto sample=x.s;bool stock=true,nativeHold=true;
        if(fault==0)stock=false;if(fault==1)nativeHold=false;if(fault==2)sample.fireSuppressed=false;
        if(fault==3)sample.onFoot=false;if(fault==4)sample.knifeSelected=false;if(fault==5)sample.gripHeld[1]=false;
        if(fault==6)sample.source.released[1]=true;if(fault==7)sample.source.focused=false;
        if(fault==8)sample.source.tracked[0]=false;if(fault==9)sample.source.tracked[1]=false;
        if(fault==10)++sample.source.owner.actor;if(fault==11)++sample.source.owner.actorGeneration;
        if(fault==12)++sample.source.owner.equipGeneration;if(fault==13)++sample.source.owner.space;
        if(fault==14)sample.source.nowNs=sample.source.deadlineNs;if(fault==15)sample.menu.owner=InputMenuOwner::Menu;
        if(fault==16)++sample.knife.id;if(fault==17)sample.controlsNeutral=sample.holdInputCurrent=false;
        if(fault==18)--sample.source.sequence;
        const auto before=*x.hands.Current(InteractionHand::Right);
        CHECK(!ChestKnifeRetainedStockContact(x.knife,x.hands,sample,stock,nativeHold));
        const auto after=*x.hands.Current(InteractionHand::Right);
        CHECK(after.token==before.token&&after.inputSequence==before.inputSequence&&after.deadlineNs==before.deadlineNs);
    }
    {Fixture x;CHECK(x.Draw());CHECK(!ChestKnifeRetainedStockContact(x.knife,x.hands,x.s,true,true));} // Never initial attachment.
    {Fixture x;CHECK(x.Held());const auto old=*x.knife.Hold();
        const auto other=x.hands.Transfer(x.s.source,old,{x.s.source.owner,InteractionHand::Right,
            HandClaimKind::GunHold,{999,1},x.s.heldContact,x.s.claimIntent+1,0});CHECK(other.claim);
        CHECK(!ChestKnifeRetainedStockContact(x.knife,x.hands,x.s,true,true));}
    std::puts("Stock held publication gap: original lease retained, trigger edge once, accepted siblings drained, tracked stroke disabled, expiry/owner/native negatives.");
    return 0;
}
}
