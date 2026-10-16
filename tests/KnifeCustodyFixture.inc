namespace {
constexpr std::int64_t ms=1000000;
struct Fixture {
    ChestKnifeCustody knife;HandInteraction hands;KnifeSample s;
    Fixture(){
        s.source={{1,2,3,4},1,1000*ms,1100*ms,1000*ms,true,{true,true},{true,true}};
        s.knife={42,1};s.handSupported={false,true};
        s.emptyHands=s.fireSuppressed=s.mechanicsSettled=s.onFoot=s.controlsNeutral=true;
        s.nativeAttackReady=true;
    }
    std::optional<KnifeCommand> Step(std::int64_t elapsed=10*ms,bool keepHeldContact=false){
        ++s.source.sequence;s.source.observedNs+=elapsed;s.source.nowNs=s.source.observedNs;
        s.source.deadlineNs=s.source.nowNs+100*ms;
        s.claimIntent=s.source.sequence;
        if(s.menu.owner==InputMenuOwner::Unknown)s.menu={InputMenuOwner::Gameplay,1,1,0};
        s.menu.deadlineNs=s.source.deadlineNs;++s.menu.sequence;
        s.chest[0]={{101,1},s.source.sequence,s.source.deadlineNs,true};
        s.chest[1]={{102,1},s.source.sequence,s.source.deadlineNs,true};
        if(!keepHeldContact)s.heldContact={{103,1},s.source.sequence,s.source.deadlineNs,true};
        hands.Update(s.source);return knife.Prepare(s,hands);
    }
    void Grip(bool held){s.gripHeld[1]=held;s.source.released[1]=!held;}
    KnifeReceipt Receipt(const KnifeCommand& c)const {
        KnifeReceipt r;r.kind=c.kind;r.serial=c.serial;r.actor=c.source.owner.actor;
        r.actorGeneration=c.source.owner.actorGeneration;r.space=c.source.owner.space;r.knife=c.knife;
        r.observedNs=s.source.nowNs;r.nowNs=r.observedNs;r.deadlineNs=r.nowNs+100*ms;
        r.coherent=r.originalAmmoUnchanged=r.knifeSentinelUnchanged=true;
        r.selectedKnife=r.attackObserved=r.attackSettled=r.drawDidNotAttack=r.holdGatesOwned=true;
        r.originalSelectedHidden=r.fireSuppressed=true;return r;
    }
    std::optional<KnifeCommand> Draw(){Step();Grip(true);return Step();}
    bool Held(bool changeEquip=true){
        const auto c=Draw();if(!c||!knife.CanCommit(*c,s,hands))return false;
        knife.Committed(*c,KnifeCommit::Accepted);if(!knife.Observe(Receipt(*c)))return false;
        s.knifeSelected=s.trackedKnifeOwned=true;s.emptyHands=false;
        if(changeEquip)++s.source.owner.equipGeneration;
        return !Step()&&knife.Hold()&&knife.Hold()->kind==HandClaimKind::GunHold;
    }
    bool Rest(unsigned count=20){for(unsigned i=0;i<count;++i)if(Step())return false;return true;}
    std::optional<KnifeCommand> Stab(){
        for(unsigned i=0;i<8;++i){s.tip.z+=.012f;if(auto command=Step())return command;}return {};
    }
};
}
