void ObserveShellStarts(Fixture& f){
    f.api.startObserved=[](void* p,const ReloadCycleControl& c)noexcept{
        auto& f=*static_cast<Fixture*>(p);++f.startAttempts;
        if(f.startReject)return ReloadStartResult::Rejected;
        if(f.startDefers){--f.startDefers;return ReloadStartResult::Deferred;}
        return f.api.start(p,c)?ReloadStartResult::Accepted:ReloadStartResult::Rejected;
    };
    f.policy.emplace(true,f.api,AmmoSupplyConfig{InteractionHand::Left,1000,{1001,1},{0,0,0},.15f,200000000});
}
int ShellStartBusyRetainsCustodyWithoutPulse(){
    Fixture f;ObserveShellStarts(f);f.startDefers=2;
    f.s.raw.inputEvidence=f.s.input;f.s.originalHandEvidence=f.s.input;f.Sync();f.Send(true);
    CHECK(f.startAttempts==1&&f.starts==0&&f.result.ammoOwnsHand&&!f.result.reloadHeld);
    const auto held=f.hands.Current(InteractionHand::Left);CHECK(held);
    const auto cycle=f.policy->ProbeState(f.now).cycle;
    f.Sync();CHECK(f.startAttempts==1&&f.hands.Current(InteractionHand::Left)->token==held->token);
    f.Send(true);CHECK(f.startAttempts==2&&f.starts==0&&f.result.ammoOwnsHand&&!f.result.reloadHeld);
    f.Send(true);CHECK(f.starts==1&&f.startAttempts==3&&f.result.ammoOwnsHand&&f.result.reloadHeld);
    CHECK(f.policy->ProbeState(f.now).cycle>cycle&&f.hands.Current(InteractionHand::Left)->token==held->token);
    CHECK(f.submits==0&&f.reserve.loaded==2&&f.reserve.reserve==8);
    CHECK(f.Report().find("\"acquired\":1")!=std::string::npos);
    CHECK(f.Report().find("\"start_deferrals\":2")!=std::string::npos);
    return 0;
}
int ShellStartAdmissionWaitRetainsLiveCustody(){
    Fixture f;ObserveShellStarts(f);f.startDefers=76;
    f.s.raw.inputEvidence=f.s.input;f.s.originalHandEvidence=f.s.input;f.Sync();f.Send(true);
    const auto original=f.hands.Current(InteractionHand::Left);CHECK(original);
    for(unsigned n=0;n<75;++n){f.Send(true);
        CHECK(f.starts==0&&f.result.ammoOwnsHand&&!f.result.reloadHeld&&f.submits==0);
        CHECK(f.hands.Current(InteractionHand::Left)->token==original->token);
        CHECK(f.reserve.loaded==2&&f.reserve.reserve==8);
    }
    CHECK(f.startAttempts==76);f.Send(true);
    CHECK(f.starts==1&&f.result.ammoOwnsHand&&f.result.reloadHeld&&f.submits==0);
    CHECK(f.hands.Current(InteractionHand::Left)->token==original->token);
    CHECK(f.reserve.loaded==2&&f.reserve.reserve==8);
    return 0;
}
int ShellStartRejectionAndReleaseStillCancel(){
    for(bool reject:{false,true}){
        Fixture f;ObserveShellStarts(f);f.startDefers=10;f.startReject=reject;
        f.s.raw.inputEvidence=f.s.input;f.s.originalHandEvidence=f.s.input;f.Sync();f.Send(true);
        CHECK(f.starts==0&&f.startAttempts==1&&!f.result.reloadHeld);
        if(!reject){CHECK(f.result.ammoOwnsHand);f.Send(false);}
        CHECK(!f.result.ammoOwnsHand&&!f.hands.Current(InteractionHand::Left)&&f.submits==0);
        CHECK(f.reserve.loaded==2&&f.reserve.reserve==8);
    }
    for(unsigned loss=0;loss<3;++loss){
        Fixture f;ObserveShellStarts(f);f.startDefers=100;
        f.s.raw.inputEvidence=f.s.input;f.s.originalHandEvidence=f.s.input;f.Sync();f.Send(true);
        CHECK(f.result.ammoOwnsHand);
        if(loss==0)++f.reserve.identity.firing[2];
        if(loss==1)f.s.input.focused=false;
        if(loss==2){f.now+=200000000;f.Sync();}
        else f.Send(true);
        CHECK(!f.result.ammoOwnsHand&&!f.hands.Current(InteractionHand::Left));
        CHECK(!f.starts&&!f.submits&&f.reserve.loaded==2&&f.reserve.reserve==8);
    }
    return 0;
}
