namespace {
struct PhysicalKnifeHoldFixture {
    Fixture f;InputFrame input;ReloadStateOwner owner{};chestKnifeNative::View native;
    ChestKnifePhysicalHold custody;
    bool Held(){
        if(!f.Held())return false;
        custody={*f.knife.Hold(),5,8};
        owner={10,1,0,unsigned(f.s.knife.id),f.s.source.owner.actorGeneration,9,f.s.source.owner.space};
        native.owned=native.healthy=true;native.anchor.knifeWeapon=owner.weapon;native.anchor.knifeSlot=7;
        native.snapshot.selected.current={7,7,7};native.currentHoldWork.known=true;
        native.currentHoldWork.lifetime=custody.nativeLifetime;native.currentHoldWork.bodyOperation=custody.bodyOperation;
        native.currentHoldWork.drawSerial=3;
        for(auto* branch:{&native.snapshot.knife.client.branches[0],&native.snapshot.knife.client.branches[1],&native.snapshot.knife.server})
            branch->loaded=branch->reserve=-1;
        input.focused=input.headValid=true;input.trackingBasisGeneration=1;
        for(auto& hand:input.hands){hand.active=Stick|Trigger|Squeeze|Primary|Secondary|StickClick;hand.gripTracked=hand.aimTracked=true;}
        input.hands[1].squeeze=1;f.s.holdInputCurrent=true;f.s.nativeAttackReady=false;
        Current();return true;
    }
    void Current(){
        input.generation=f.s.source.sequence;input.spaceGeneration=f.s.source.owner.space;
        input.predictedNs=f.s.source.observedNs;f.s.menu.deadlineNs=f.s.source.deadlineNs;input.menu=f.s.menu;
        native.snapshot.selectedOwner=owner;native.snapshot.sequence=f.s.source.sequence;native.snapshot.observedNs=f.s.source.nowNs;
    }
    std::optional<HandContactProof> Contact()const {
        return ChestKnifePhysicalHoldContact(f.knife,f.hands,f.s.source,input,custody,owner,native);
    }
    HandGunHoldUpdateResult Advance(){
        const auto contact=Contact();
        if(contact)return f.hands.UpdateAndRenewGunHold(f.s.source,custody.token,*contact);
        return {f.hands.Update(f.s.source),{}};
    }
};
int PhysicalKnifeHoldContinuationCases(){
    constexpr std::int64_t oldDeadline=12166619080600;
    for(bool alreadyContinued:{false,true}){
        PhysicalKnifeHoldFixture x;x.f.s.source.sequence=10385;x.f.s.source.observedNs=oldDeadline-120*ms;
        x.f.s.source.nowNs=x.f.s.source.observedNs;x.f.s.source.deadlineNs=x.f.s.source.observedNs+100*ms;
        CHECK(x.Held());CHECK(x.f.s.source.sequence==10388);
        // Actual102's rendered10387 contact initially owns the physical lease.
        const HandContactProof measured{x.custody.token.contact,10388,oldDeadline,true};
        x.f.s.source.nowNs=12166563181000;x.Current();
        CHECK(x.f.hands.Renew(x.f.s.source,x.custody.token,measured).claim);
        ChestKnifeRenderSource render{x.owner,x.f.s.source,{x.f.s.knife.id,x.f.s.knife.generation},x.custody.bodyOperation};
        render.input.sequence=10387;render.input.observedNs=oldDeadline-100*ms;render.input.deadlineNs=oldDeadline;
        x.f.s.source.sequence=10391;x.f.s.source.observedNs=12166610000000;
        x.f.s.source.nowNs=12166612942000;x.f.s.source.deadlineNs=x.f.s.source.observedNs+100*ms;x.Current();
        if(alreadyContinued){
            CHECK(x.Advance().renewed);
            const auto physical=ChestKnifeRenewedPhysicalHoldContact(x.f.knife,x.f.hands,x.f.s.source,x.input,x.custody,x.owner,x.native);
            CHECK(physical&&physical->deadlineNs==x.f.s.source.deadlineNs&&physical->deadlineNs>render.input.deadlineNs);
            CHECK(ChestKnifeRenderCurrent(render,x.owner,x.f.s.source,x.f.s.source.nowNs));
            x.f.s.heldContact=*physical;x.f.s.claimIntent=x.f.s.source.sequence;
            CHECK(!x.f.knife.Prepare(x.f.s,x.f.hands));
            CHECK(x.f.hands.Current(InteractionHand::Right)->deadlineNs==x.f.s.source.deadlineNs);
        }else {
            x.f.hands.Update(x.f.s.source);
            CHECK(x.f.hands.Renew(x.f.s.source,x.custody.token,{measured.key,10391,oldDeadline,true}).claim);
            CHECK(x.f.hands.Current(InteractionHand::Right)->deadlineNs==oldDeadline);
        }
        x.f.s.source.sequence=10392;x.f.s.source.observedNs=12166612540600;
        x.f.s.source.nowNs=12166621468800;x.f.s.source.deadlineNs=12166712540600;x.Current();
        CHECK(x.f.s.source.observedNs<oldDeadline&&x.f.s.source.nowNs-oldDeadline==2388200);
        const auto update=x.Advance();CHECK(update.renewed&&update.lifecycle.inputValid);
        CHECK(!update.lifecycle.released[0]&&!update.lifecycle.released[1]);
        CHECK(update.renewed->token==x.custody.token&&update.renewed->inputSequence==10392);
        CHECK(update.renewed->deadlineNs==12166712540600);
        const auto contact=ChestKnifeRenewedPhysicalHoldContact(x.f.knife,x.f.hands,x.f.s.source,x.input,x.custody,x.owner,x.native);
        CHECK(contact);x.f.s.heldContact=*contact;x.f.s.claimIntent=10392;
        CHECK(!x.f.knife.Prepare(x.f.s,x.f.hands));
        CHECK(x.f.knife.Phase()==KnifePhase::Held&&!x.f.knife.NeedsNativeCleanup());
        CHECK(!ChestKnifeRenderCurrent(render,x.owner,x.f.s.source,x.f.s.source.nowNs));
        CHECK(render.input.sequence==10387&&render.input.deadlineNs==oldDeadline);
        CHECK(!x.f.s.nativeAttackReady&&!x.f.knife.RecognitionReady(x.f.s.source));
    }
    for(unsigned fault=0;fault<31;++fault){
        PhysicalKnifeHoldFixture x;CHECK(x.Held());const auto old=*x.f.hands.Current(InteractionHand::Right);
        ++x.f.s.source.sequence;x.f.s.source.observedNs+=50*ms;x.f.s.source.nowNs=old.deadlineNs+1;
        x.f.s.source.deadlineNs=x.f.s.source.observedNs+100*ms;x.Current();
        if(fault==0)x.f.s.source.released[1]=true;if(fault==1)x.input.hands[1].squeeze=0;
        if(fault==2)x.f.s.source.focused=false;if(fault==3)x.f.s.source.tracked[0]=false;
        if(fault==4)x.f.s.source.tracked[1]=false;if(fault==5)x.input.hands[1].active&=~unsigned(Squeeze);
        if(fault==6)x.input.hands[1].aimTracked=false;if(fault==7)x.input.menu.owner=InputMenuOwner::Menu;
        if(fault==8)++x.input.generation;if(fault==9)++x.input.spaceGeneration;
        if(fault==10)x.input.trackingBasisGeneration=0;if(fault==11)x.native.owned=false;
        if(fault==12)x.native.healthy=false;if(fault==13)x.native.currentHoldWork.known=false;
        if(fault==14)++x.native.currentHoldWork.lifetime;if(fault==15)++x.native.currentHoldWork.bodyOperation;
        if(fault==16)x.native.currentHoldWork.drawSerial=0;if(fault==17)x.native.currentHoldWork.returnReady=true;
        if(fault==18)x.native.currentHoldWork.closing=true;if(fault==19)x.native.currentHoldWork.commandPending=true;
        if(fault==20)++x.native.snapshot.selectedOwner.player;if(fault==21)--x.native.snapshot.sequence;
        if(fault==22)--x.native.snapshot.observedNs;if(fault==23)++x.native.anchor.knifeWeapon;
        if(fault==24)x.native.snapshot.selected.current[2]=0;if(fault==25)x.native.snapshot.knife.server.loaded=0;
        if(fault==26)++x.custody.token.id;if(fault==27)++x.f.s.source.owner.equipGeneration;
        if(fault==28)x.f.s.source.nowNs=x.f.s.source.deadlineNs;
        if(fault==29)x.custody.nativeLifetime=0;if(fault==30)x.custody.bodyOperation=0;
        CHECK(!x.Contact());CHECK(!x.Advance().renewed);CHECK(!x.f.hands.Current(InteractionHand::Right));
    }
    // The semantic proof does not bypass the arbiter's source-overlap rule.
    {PhysicalKnifeHoldFixture x;CHECK(x.Held());const auto deadline=x.f.hands.Current(InteractionHand::Right)->deadlineNs;
        ++x.f.s.source.sequence;x.f.s.source.observedNs=deadline;x.f.s.source.nowNs=deadline+1;
        x.f.s.source.deadlineNs=deadline+100*ms;x.Current();CHECK(x.Contact());
        CHECK(!x.Advance().renewed&&!x.f.hands.Current(InteractionHand::Right));}
    // Duplicate packets cannot restamp the old source or revive a lost hold.
    {PhysicalKnifeHoldFixture x;CHECK(x.Held());const auto old=*x.f.hands.Current(InteractionHand::Right);
        x.f.s.source.nowNs+=10*ms;x.Current();CHECK(!x.Advance().renewed);
        CHECK(x.f.hands.Current(InteractionHand::Right)->deadlineNs==old.deadlineNs);
        x.f.s.source.nowNs=old.deadlineNs;x.Current();CHECK(!x.Advance().renewed);
        ++x.f.s.source.sequence;x.f.s.source.observedNs=x.f.s.source.nowNs;
        x.f.s.source.deadlineNs=x.f.s.source.observedNs+100*ms;x.Current();CHECK(!x.Contact());}
    // A release without action authority requests cleanup but cannot stage Return.
    {PhysicalKnifeHoldFixture x;CHECK(x.Held());x.f.s.controlsNeutral=false;
        x.f.Grip(false);CHECK(!x.f.Step());CHECK(!x.f.hands.Current(InteractionHand::Right));
        CHECK(x.f.knife.NeedsNativeCleanup());x.f.Grip(true);x.Current();CHECK(!x.Contact());}
    // With current action authority, the same release stages Return immediately.
    {PhysicalKnifeHoldFixture x;CHECK(x.Held());x.f.Grip(false);
        const auto returned=x.f.Step();CHECK(returned&&returned->kind==KnifeCommandKind::Return);
        CHECK(x.f.knife.Phase()==KnifePhase::ReturnStaged&&!x.f.hands.Current(InteractionHand::Right));
        CHECK(x.f.knife.CanCommit(*returned,x.f.s,x.f.hands));
        x.f.knife.Committed(*returned,KnifeCommit::Accepted);CHECK(x.f.knife.Observe(x.f.Receipt(*returned)));
        CHECK(x.f.knife.Phase()==KnifePhase::NeedNeutral&&!x.f.knife.Hold());
        x.f.Grip(true);x.Current();CHECK(!x.Contact());}
    {PhysicalKnifeHoldFixture x;CHECK(x.Held());const auto other=x.f.hands.Transfer(x.f.s.source,x.custody.token,
        {x.f.s.source.owner,InteractionHand::Right,HandClaimKind::GunHold,{999,1},x.f.s.heldContact,x.f.s.claimIntent+1,0});
        CHECK(other.claim);CHECK(!x.Contact());CHECK(x.f.hands.Current(InteractionHand::Right)->token==other.claim->token);}
    {PhysicalKnifeHoldFixture x;CHECK(x.f.Draw());x.custody={*x.f.knife.Hold(),5,8};x.Current();CHECK(!x.Contact());}
    std::puts("Physical knife custody: overlapping 10391/10392 survives 10387 render expiry; measured render unchanged; release/return/lifetime/current-proof/source-gap/foreign-hold refusals.");
    return 0;
}
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
    CHECK(!PhysicalKnifeHoldContinuationCases());
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
