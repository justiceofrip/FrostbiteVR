namespace {
std::optional<KnifeCommand> StockJab(Fixture& f,MeleePoint axis){
    for(unsigned n=0;n<9;++n){
        f.s.tip.x+=axis.x*.012f;f.s.tip.y+=axis.y*.012f;f.s.tip.z+=axis.z*.012f;
        if(const auto command=f.Step())return command;
    }
    return {};
}
int StockJabCases(){
    CHECK(NormalKnifeMotion(KnifeStockTrigger)&&NormalKnifeStockTrigger(KnifeStockTrigger));
    CHECK(!ChestKnifeTrackedStrikeAvailable&&!NormalKnifeMotion(KnifeHolsterOnly));
    // The current knife axis may point anywhere in LOCAL space. These tests
    // use the same tip/axis semantic boundary supplied by measured grip math.
    for(const auto axis:{MeleePoint{1,0,0},MeleePoint{0,1,0},MeleePoint{0,0,1},MeleePoint{-.6f,0,-.8f}}){
        Fixture f;CHECK(f.Held());f.s.direction=axis;
        f.s.nativeAttackReady=NormalKnifeMotion(KnifeStockTrigger);
        f.s.stockTriggerReady=f.s.stockTriggerInputCurrent=true;CHECK(f.s.stockTrigger==0);
        CHECK(f.Rest());const auto attack=StockJab(f,axis);
        CHECK(attack&&attack->kind==KnifeCommandKind::Attack&&attack->stroke&&!attack->stockTriggerHold);
        CHECK(f.s.stockTrigger==0&&attack->stroke->direction==axis&&f.knife.CanCommit(*attack,f.s,f.hands));
        private_knife::StockKnifeMotionSourceRevocation source;CHECK(!source.Advance(f.s));
        const auto stamp=source.Stamp();private_knife::KnifeAttackGate native;
        ++f.s.source.nowNs;CHECK(!source.Advance(f.s)&&source.Current(stamp));
        CHECK(!f.knife.Prepare(f.s,f.hands)&&f.knife.CanCommit(*attack,f.s,f.hands));
        CHECK(native.Stage(*attack,f.s.source.nowNs));
        CHECK(!native.Consume(0,f.s.source.nowNs,true));
        CHECK(native.Consume(1,f.s.source.nowNs,source.Current(stamp)));
        CHECK(!native.Consume(1,f.s.source.nowNs,source.Current(stamp)));
        CHECK(native.ResolveBeforeSource(f.s.source.sequence,f.s.source.observedNs,f.s.source.nowNs)==KnifeCommit::Accepted);
        f.knife.Committed(*attack,KnifeCommit::Accepted);
        CHECK(native.Consume(2,attack->source.deadlineNs+1,false));
        CHECK(!native.Consume(2,attack->source.deadlineNs+1,false));
        for(unsigned n=0;n<3;++n)native.Observe(n,2,5,6);
        CHECK(native.Complete(attack->serial,true,true));CHECK(f.knife.Observe(f.Receipt(*attack)));
        // Continuous motion cannot become another jab after native completion.
        for(unsigned n=0;n<20;++n){f.s.tip.x+=axis.x*.012f;f.s.tip.y+=axis.y*.012f;f.s.tip.z+=axis.z*.012f;CHECK(!f.Step());}
        CHECK(f.Rest());const auto next=StockJab(f,axis);CHECK(next&&next->serial>attack->serial&&next->stroke&&!next->stockTriggerHold);
    }
    // Idle/jitter, short motion, slow drift, sideways motion and rotation do
    // not substitute for deliberate forward travel along the current blade.
    for(unsigned kind=0;kind<5;++kind){Fixture f;CHECK(f.Held());CHECK(f.Rest());
        for(unsigned n=0;n<24;++n){
            if(kind==0)f.s.tip.z=(n%2?-.001f:.001f);
            if(kind==1)f.s.tip.z=(n<2?.012f*float(n+1):.024f);
            if(kind==2)f.s.tip.z+=.001f;
            if(kind==3)f.s.tip.x+=.012f;
            if(kind==4){const float angle=.1f*float(n);f.s.tip={.15f*std::sin(angle),0,.15f*std::cos(angle)};
                f.s.direction={std::sin(angle),0,std::cos(angle)};}
            CHECK(!f.Step());
        }
    }
    {Fixture f;const auto draw=f.Draw();CHECK(draw&&draw->kind==KnifeCommandKind::Draw);
        f.knife.Committed(*draw,KnifeCommit::Accepted);
        for(unsigned n=0;n<20;++n){f.s.tip.z+=.02f;CHECK(!f.Step());}
        CHECK(f.knife.Observe(f.Receipt(*draw)));f.s.knifeSelected=f.s.trackedKnifeOwned=true;
        ++f.s.source.owner.equipGeneration;CHECK(!f.Step());
        for(unsigned n=0;n<12;++n){f.s.tip.z+=.02f;CHECK(!f.Step());} // Drawing travel never arms a jab.
        CHECK(f.Rest());f.Grip(false);f.s.tip.z+=.1f;
        const auto returned=f.Step();CHECK(returned&&returned->kind==KnifeCommandKind::Return&&!returned->stroke&&!returned->stockTriggerHold);
    }
    // A missing measured geometry sample keeps only the stock hand lease;
    // it clears a partial physical stroke and cannot be completed by recovery.
    {Fixture f;CHECK(f.Held());f.s.stockTriggerReady=f.s.stockTriggerInputCurrent=true;CHECK(f.Rest());
        f.s.tip.z+=.02f;CHECK(!f.Step());f.s.nativeAttackReady=false;CHECK(!StockGapFrame(f,false));
        f.s.nativeAttackReady=true;f.s.tip.z+=.08f;CHECK(!StockGapFrame(f,true));
        CHECK(!StockJab(f,{0,0,1}));CHECK(f.Rest());CHECK(StockJab(f,{0,0,1}));}
    for(unsigned loss=0;loss<20;++loss){Fixture f;CHECK(f.Held());CHECK(f.Rest());
        const auto attack=f.Stab();CHECK(attack&&attack->stroke&&!attack->stockTriggerHold);
        private_knife::StockKnifeMotionSourceRevocation source;CHECK(!source.Advance(f.s));
        const auto stamp=source.Stamp();const auto original=f.s;
        auto lost=original;bool cleanup=false;
        if(loss==0)lost.nativeAttackReady=false;if(loss==1)lost.trackedKnifeOwned=false;
        if(loss==2)lost.knifeSelected=false;if(loss==3)lost.fireSuppressed=false;if(loss==4)lost.mechanicsSettled=false;
        if(loss==5)lost.tip.x+=.001f;if(loss==6)lost.direction={1,0,0};if(loss==7)lost.stockTrigger=1;
        if(loss==8)lost.source.focused=false;if(loss==9)lost.source.tracked[1]=false;if(loss==10)lost.gripHeld[1]=false;
        if(loss==11)++lost.source.owner.actor;if(loss==12)++lost.source.owner.equipGeneration;if(loss==13)++lost.source.owner.space;
        if(loss==14)lost.source.nowNs=lost.source.deadlineNs;if(loss==15)lost.menu.owner=InputMenuOwner::Menu;
        if(loss==16)lost.heldContact.eligible=false;if(loss==17)lost.source.nowNs--;
        if(loss==18)lost.controlsNeutral=lost.actionInputCurrent=false;if(loss==19)cleanup=true;
        private_knife::KnifeAttackGate pending,accepted;CHECK(pending.Stage(*attack,f.s.source.nowNs)&&accepted.Stage(*attack,f.s.source.nowNs));
        CHECK(accepted.Consume(1,f.s.source.nowNs,true));
        CHECK(source.Advance(lost,cleanup)&&!source.Current(stamp));
        CHECK(!pending.Consume(1,original.source.nowNs,source.Current(stamp)));
        source.Advance(original);CHECK(!source.Current(stamp));CHECK(!pending.Consume(2,original.source.nowNs,source.Current(stamp)));
        CHECK(accepted.ResolveBeforeSource(original.source.sequence,original.source.observedNs,original.source.nowNs)==KnifeCommit::Accepted);
        CHECK(accepted.Consume(2,attack->source.deadlineNs+1,false));CHECK(!accepted.Consume(2,attack->source.deadlineNs+1,false));
    }
    std::puts("Normal stock jab: neutral trigger, authored-axis travel, one native input per consumer, quiet rearm, no jitter/draw/return/gap hit, immutable motion revocation.");
    return 0;
}
}
