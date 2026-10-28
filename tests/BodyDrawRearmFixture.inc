// Same production policies as Gameplay, including its previously destructive
// failed-Apply branch. No fabricated visible ack or native cache commit.
int OrdinaryDrawSelectionRearmComposition(){
    for(bool repaired:{false,true})for(unsigned startSlot:{0u,1u})
    for(bool unequalGenerations:{false,true})for(unsigned movement:{0u,1u,2u})
    for(std::int64_t frameNs:{10000000ll,70000000ll}){
        HolsterRun h;h.r.f.owner.equipGeneration=99;
        h.r.f.Word(Fixture::ad+0x84,1);h.r.f.Word(Fixture::bd+0x84,0);
        if(startSlot)h.r.f.Select(startSlot);
        h.r.s.owner=h.r.f.owner;h.r.s.hand.owner.equipGeneration=unequalGenerations?17:h.r.s.owner.equipGeneration;
        h.r.s.gun={h.r.s.owner.weapon,h.r.s.hand.owner.equipGeneration};
        for(auto& hand:h.r.s.input.hands){hand.active=interaction::Components;hand.gripTracked=hand.aimTracked=true;}
        h.r.s.input.hands[1].grip.position={0,1.3f,-.4f};
        interaction::ControllerActions actions;std::optional<bc2::WeaponModeCommand> queued;
        unsigned observedCommits=0,rearmSamples=0;bool issued=false;std::int64_t requestAt=0;
        const auto step=[&](float squeeze)->int {
            // Publish each immutable sample once, at the requested cadence.
            // Run::Advance would already publish before a slow-cadence adjustment.
            ++h.r.s.input.generation;h.r.s.input.predictedNs+=frameNs;
            h.r.s.input.hands[1].squeeze=squeeze;h.r.s.hand.sequence=h.r.s.input.generation;
            h.r.s.hand.observedNs+=frameNs;h.r.s.hand.deadlineNs+=frameNs;
            h.r.s.hand.nowNs=h.r.s.hand.observedNs;h.r.Visible();
            CHECK(h.r.hands.Update(h.r.s.hand).inputValid);
            if(const auto hold=h.r.hands.Current(interaction::InteractionHand::Right))
                CHECK(h.r.hands.Renew(h.r.s.hand,hold->token,{hold->token.contact,h.r.s.hand.sequence,h.r.s.hand.deadlineNs,true}).accepted);
            else if(!h.holster.BlocksActions()){
                // Gameplay::ObserveHandOwnership reacquires the selected gun
                // on the NEW owner sample, before semantic input rearms.
                const auto& hand=h.r.s.hand;
                CHECK(h.r.hands.Acquire(hand,{hand.owner,interaction::InteractionHand::Right,
                    interaction::HandClaimKind::GunHold,h.r.s.gun,
                    {{1,hand.owner.equipGeneration},hand.sequence,hand.deadlineNs,true},++h.r.intent,0}).accepted);
            }
            auto action=actions.Update(h.r.s.input,{Fixture::soldier,h.r.s.owner.equipGeneration,true,true,4},h.r.s.input.predictedNs);
            if(!action.active||action.owner!=Fixture::soldier){action={};action.owner=Fixture::soldier;}
            bc2::SetBodyInteractionInput(h.r.s,true,action.active,true,action.held|action.pressed);
            const auto before=h.cache;bc2::InputOverride patch;std::optional<bool> cycle;
            std::optional<bc2::EntryAction> mode;
            if(queued){if(queued->action==7)cycle=true;else mode=static_cast<bc2::EntryAction>(queued->action);}
            const bool dispatchCurrent=!queued||h.r.adapter.OrdinaryDrawDispatchCurrent(*queued,h.r.s);
            if(dispatchCurrent&&patch.Apply(h.cache,action,std::nullopt,cycle,mode)){
                patch.Commit();std::optional<unsigned> selected;
                if(queued){CHECK(h.r.adapter.ObserveOrdinaryDrawIssued(*queued,h.r.s));
                    selected=queued->targetSlot;queued.reset();issued=true;}
                h.Tick();if(h.out.ordinaryDraw.committed){++observedCommits;
                    if(frameNs==70000000)CHECK(h.r.s.hand.nowNs-requestAt>=300000000);}
                if(h.out.ordinaryDraw.command){queued=h.out.ordinaryDraw.command;requestAt=h.r.s.hand.nowNs;}
                // The native fixture changes selection only after cache commit,
                // just as the engine consumes the ordinary selector afterward.
                if(selected){h.r.f.Select(*selected);h.r.s.owner=h.r.f.owner;
                    ++h.r.s.hand.owner.equipGeneration;
                    h.r.s.gun={h.r.s.owner.weapon,h.r.s.hand.owner.equipGeneration};}
            }else{
                CHECK(h.cache==before&&!action.active);
                const auto native=h.r.f.bytes;const auto hand=h.r.hands.Current(interaction::InteractionHand::Right);
                const bool retained=repaired&&h.r.adapter.RetainOrdinaryDrawRearm(h.r.f.Memory(),h.r.s,action,true);
                CHECK(native==h.r.f.bytes);const auto after=h.r.hands.Current(interaction::InteractionHand::Right);
                CHECK(bool(hand)==bool(after)&&(!hand||hand->token==after->token));
                h.holster.Invalidate(false);if(!retained)h.r.adapter.SuspendInteraction();
                if(issued){++rearmSamples;if(repaired)CHECK(retained&&h.r.adapter.Pending());}
            }
            return 0;
        };
        for(unsigned n=0;n<6;++n)CHECK(!step(0));
        if(movement==1)h.r.s.input.hands[0].stickY=.7f;
        if(movement==2)h.r.s.input.hands[1].stickX=.7f;
        const auto target=startSlot?Fixture::a:Fixture::b;
        const auto slot=h.r.adapter.AssignedSlot(target);CHECK(slot);
        const interaction::BodyAnchorConfig anchors{};
        const auto* anchor=interaction::FindBodyWeaponAnchor(anchors,slot->slot);CHECK(anchor);
        h.r.s.input.hands[1].grip.position={anchor->center.x,1.7f+anchor->center.y,-anchor->center.z};
        CHECK(!step(1));CHECK(queued&&queued->targetWeapon==target);
        for(unsigned n=0;n<8;++n)CHECK(!step(1));
        CHECK(issued&&rearmSamples>=2&&h.r.s.owner.weapon==target&&!h.r.adapter.Pending());
        CHECK(observedCommits==(repaired?1u:0u));
        std::array<unsigned,3> releasePhases{},releaseRequests{};
        for(unsigned n=0;n<3;++n){CHECK(!step(0));releasePhases[n]=unsigned(h.holster.Phase());releaseRequests[n]=bool(h.out.inventory.request);}
        if(repaired){CHECK(h.holster.Phase()==bc2::BodyHolsterPhase::Held&&!h.out.inventory.request&&!h.out.freeRight);}
        else {
            if(h.holster.Phase()==bc2::BodyHolsterPhase::Held){
                std::fprintf(stderr,"Historical draw variant: start_slot=%u unequal_generations=%u movement=%u frame_ns=%lld commits=%u rearm_samples=%u release_phases=%u,%u,%u release_requests=%u,%u,%u free_right=%u blocked=%u\n",
                    startSlot,unsigned(unequalGenerations),movement,static_cast<long long>(frameNs),observedCommits,rearmSamples,
                    releasePhases[0],releasePhases[1],releasePhases[2],releaseRequests[0],releaseRequests[1],releaseRequests[2],
                    unsigned(bool(h.out.freeRight)),unsigned(h.out.blockWeaponActions));
                std::ostringstream report;h.r.adapter.Report(report);std::fprintf(stderr,"%s\n",report.str().c_str());
            }
            CHECK(h.holster.Phase()!=bc2::BodyHolsterPhase::Held); // historical unexpected stow
        }
    }
    return 0;
}
int OrdinaryDrawRearmBoundaries(){
    for(unsigned fault=0;fault<19;++fault){
        Run r;r.f.owner.equipGeneration=17;r.s.owner=r.f.owner;r.Visible();
        for(auto& hand:r.s.input.hands){hand.active=interaction::Components;hand.gripTracked=hand.aimTracked=true;}
        r.s.input.hands[1].grip.position={.20f,1.58f,.18f};
        r.Tick();r.Advance();r.Tick();r.Advance(1);const auto request=r.Tick();CHECK(request.command);
        const auto dispatchDeadline=r.s.hand.deadlineNs;
        const auto deadline=r.s.hand.nowNs+interaction::BodyInventoryConfig{}.acknowledgementTimeoutNs;
        const auto oldOwner=r.s.owner;const auto oldPose=r.s.visible;
        bc2::InputOverride patch;std::array<std::byte,bc2::InputBytes> cache{};
        interaction::ActionOutput active;active.active=true;active.owner=Fixture::soldier;
        CHECK(patch.Apply(cache,active,std::nullopt,true));patch.Commit();
        if(fault!=1){auto expired=r.s;expired.hand.nowNs=dispatchDeadline;
            CHECK(!r.adapter.ObserveOrdinaryDrawIssued(*request.command,expired));
            CHECK(r.adapter.ObserveOrdinaryDrawIssued(*request.command,r.s));
            CHECK(!r.adapter.ObserveOrdinaryDrawIssued(*request.command,r.s));}
        r.f.Select(1);r.s.owner=r.f.owner;++r.s.hand.owner.equipGeneration;
        r.s.gun={Fixture::b,r.s.hand.owner.equipGeneration};r.Advance(1);r.Gun();
        interaction::ActionOutput neutral;neutral.owner=Fixture::soldier;bool onlyRearm=true;
        if(fault==2)++r.s.owner.actorGeneration;
        if(fault==3)++r.s.owner.weak;
        if(fault==4)++r.s.owner.space;
        if(fault==5)++r.s.owner.equipGeneration;
        if(fault==6)r.s.owner.weapon=Fixture::a;
        if(fault==7)r.s.input.focused=false;
        if(fault==8)r.s.hand.tracked[1]=false;
        if(fault==9)r.s.reloadBusy=true;
        if(fault==10)r.s.input.hands[1].trigger=1;
        if(fault==11)r.s.input.hands[0].held=interaction::Primary;
        if(fault==12)r.s.input.menu={interaction::InputMenuOwner::Menu,1,1,r.s.hand.deadlineNs};
        if(fault==13)onlyRearm=false;
        if(fault==14)r.f.Word(Fixture::bd+0x84,0);
        if(fault==15)neutral.active=true;
        if(fault==16)neutral.forward=.5f;
        const auto original=r.f.bytes;
        const bool retained=r.adapter.RetainOrdinaryDrawRearm(r.f.Memory(),r.s,neutral,onlyRearm);
        CHECK(original==r.f.bytes&&retained==(fault==0||fault>=17));
        if(fault&&fault<17){r.adapter.SuspendInteraction();CHECK(!r.adapter.Pending());continue;}
        const auto binding=r.adapter.ResourceBinding(r.s.owner,r.s.hand.nowNs);
        CHECK(binding&&binding->owner==r.s.owner&&!r.adapter.ResourceBinding(oldOwner,r.s.hand.nowNs));
        if(fault==18){const auto same=r.s.owner;++r.s.owner.equipGeneration;
            CHECK(!r.adapter.RetainOrdinaryDrawRearm(r.f.Memory(),r.s,neutral,true));
            r.s.owner=same;CHECK(!r.adapter.RetainOrdinaryDrawRearm(r.f.Memory(),r.s,neutral,true));
            r.adapter.SuspendInteraction();continue;}
        if(fault==17){
            while(r.s.hand.nowNs<=dispatchDeadline+50000000){r.Advance(1);
                CHECK(r.adapter.RetainOrdinaryDrawRearm(r.f.Memory(),r.s,neutral,true));}
            r.s.visible=oldPose;const auto stale=r.Tick();
            CHECK(stale.pending&&!stale.committed&&!stale.command); // retained transaction never makes old pose current
            r.Advance(1);const auto fresh=r.Tick();CHECK(fresh.committed&&!fresh.pending&&!fresh.command);continue;
        }
        // Fresh native metadata polls cannot renew the ORIGINAL policy deadline.
        while(r.s.hand.nowNs+10000000<deadline){r.Advance(1);CHECK(r.adapter.RetainOrdinaryDrawRearm(r.f.Memory(),r.s,neutral,true));}
        r.Advance(1);CHECK(r.s.hand.nowNs==deadline);
        const auto late=r.Tick();CHECK(late.cancelled&&!late.committed&&!late.command&&!r.adapter.Pending());
        CHECK(!r.adapter.RetainOrdinaryDrawRearm(r.f.Memory(),r.s,neutral,true));
    }
    return 0;
}
int OrdinaryDrawExpiredQueuedDispatch(){
    Run r;r.Tick();r.Advance();r.Tick();r.Advance(1);const auto draw=r.Tick();CHECK(draw.command&&r.adapter.Pending());
    // The current owner/controller sample is fresh, but the queued gesture's
    // original lease is over. The same production predicate precedes Apply.
    const auto expiredAt=r.s.hand.deadlineNs;
    while(r.s.hand.nowNs<expiredAt)r.Advance(1);
    std::array<std::byte,bc2::InputBytes> cache{};const auto original=cache;
    interaction::ActionOutput active;active.active=true;active.owner=Fixture::soldier;
    bc2::InputOverride patch;bool applied=false;
    if(r.adapter.OrdinaryDrawDispatchCurrent(*draw.command,r.s)){
        applied=patch.Apply(cache,active,std::nullopt,true);if(applied)patch.Commit();
    }else r.adapter.SuspendInteraction();
    CHECK(!applied&&cache==original&&!r.adapter.Pending());
    CHECK(!r.adapter.ObserveOrdinaryDrawIssued(*draw.command,r.s));
    r.f.Select(1);r.s.owner=r.f.owner;++r.s.hand.owner.equipGeneration;
    r.s.gun={Fixture::b,r.s.hand.owner.equipGeneration};r.Advance(1);r.Gun();
    interaction::ActionOutput neutral;neutral.owner=Fixture::soldier;
    CHECK(!r.adapter.RetainOrdinaryDrawRearm(r.f.Memory(),r.s,neutral,true));
    return 0;
}
