// Included by the existing physical-container fixture; no separate runner.
// The parent translation unit also includes interaction/SpentCaseEjection.h.
int SpentCaseConsumptionDoesNotInventOrTransferRounds(){
    const AmmoResourceIdentity owner{11,12,13,14};
    const auto snapshot=[&](std::uint64_t sequence,int loaded,int reserve=7){
        const auto now=1000000000+std::int64_t(sequence)*10000000;
        return AmmunitionSnapshot{{owner,15,16},sequence,now,now+100000000,{loaded,reserve,1},true};};
    for(unsigned invalid=0;invalid<5;++invalid){
        SpentCaseEjection state(owner);auto first=snapshot(1,invalid==0?0:1),second=snapshot(2,0);
        CHECK(state.Observe(first,first.observedNs,invalid==1));
        if(invalid==2)second.counts.reserve=8;
        if(invalid==3)++second.context.resource.weaponGeneration;
        if(invalid==4)second.coherentIdleVerified=false;
        const auto before=second.counts;
        (void)state.Observe(second,second.observedNs,false);
        CHECK(!state.Present()&&state.Epoch()==0&&second.counts==before);
    }
    SpentCaseEjection state(owner);auto loaded=snapshot(1,1),empty=snapshot(2,0);
    CHECK(state.Observe(loaded,loaded.observedNs,false));
    CHECK(state.Observe(empty,empty.observedNs,false)&&state.Present()&&state.Epoch()==1);
    state.Suspend();CHECK(state.Present());
    empty=snapshot(3,0);++empty.context.equipGeneration;++empty.context.space;
    CHECK(state.Observe(empty,empty.observedNs,false)&&state.Present()&&state.Epoch()==1);
    CHECK(!state.Observe(empty,empty.deadlineNs,false)&&state.Present());
    return 0;
}
int SpentCaseTiltNeedsFreshNeutralOpenPackets(){
    const AmmoResourceIdentity resource{11,12,13,14};
    const AmmoResourceContext context{resource,15,16};
    const HandInteractionOwner hand{20,12,21,16};
    SpentCaseEjection state(resource);std::uint64_t sequence=0;std::int64_t now=1000000000;
    const auto observe=[&](int loaded){++sequence;now+=10000000;
        const AmmunitionSnapshot s{context,sequence,now,now+100000000,{loaded,7,1},true};
        return state.Observe(s,now,false);};
    CHECK(observe(1)&&observe(0)&&state.Present());
    auto down=Identity();down.values[1]={0,0,1,0};down.values[2]={0,-1,0,0};
    const auto sample=[&](){const HandInteractionSample i{hand,sequence,now,now+100000000,now,true,{true,true},{false,false}};
        return SpentCaseTiltSample{context,i,i,down,true,true,true};};
    for(unsigned rejected=0;rejected<4;++rejected){CHECK(observe(0));auto s=sample();
        if(rejected==0)s.open=false;if(rejected==1)s.leftNeutral=false;
        if(rejected==2)s.exactHands=false;if(rejected==3)s.caseWorldMeters=Identity();
        CHECK(!state.Tilt(s)&&state.Present());}
    CHECK(observe(0));const auto first=sample();CHECK(!state.Tilt(first));
    for(unsigned n=0;n<8;++n){CHECK(observe(0));auto repeated=first;repeated.raw.nowNs=now;repeated.safety=sample().safety;
        CHECK(!state.Tilt(repeated)&&state.Present());}
    // Suspension discards dwell but retains the exact spent object.
    state.Suspend();CHECK(state.Present());
    for(unsigned n=0;n<10;++n){CHECK(observe(0));CHECK(!state.Tilt(sample()));}
    CHECK(observe(0));const auto event=state.Tilt(sample());CHECK(event&&state.Present());
    auto altered=*event;++altered.epoch;CHECK(!state.Commit(altered,now)&&state.Present());
    CHECK(state.Commit(*event,now)&&!state.Present()&&state.Ejected());
    CHECK(!state.Commit(*event,now));
    // The dropped visual is reference-space local; the debt/epoch is not.
    auto changed=AmmunitionSnapshot{context,++sequence,now+10000000,now+110000000,{0,7,1},true};
    ++changed.context.space;CHECK(state.Observe(changed,changed.observedNs,false));
    CHECK(!state.Ejected()&&!state.Present()&&state.Epoch()==1);
    return 0;
}
int SpentCaseMeasuredAnchorAndBoundedFall(){
    unsigned opening=0,muzzle=0;
    for(const auto& e:generated::UnderbarrelFixedContainerEquipment){
        std::array<Matrix,16> parts{};
        for(unsigned n=0;n<e.parts.size();++n)parts[n]=e.parts[n].weaponFromOpen;
        const auto pose=FixedContainerSpentCasePose(e,std::span(parts).first(e.parts.size()));
        if(e.interaction.kind==FixedContainerKind::SingleRoundMuzzle){++muzzle;CHECK(!pose);continue;}
        ++opening;CHECK(pose&&InverseRigid(*pose));
        const auto root=std::find_if(e.parts.begin(),e.parts.end(),[&](const auto& p){return p.bone==e.supplyRoot;});
        CHECK(root!=e.parts.end());const auto expected=Multiply(root->centerInBone,root->weaponFromOpen);
        CHECK(pose->values==expected.values);
        CHECK(!FixedContainerSpentCasePose(e,std::span(parts).first(e.parts.size()-1)));
    }
    CHECK(opening&&muzzle);
    auto start=Identity();start.values[3]={1,2,3,1};
    const auto first=fvr::graphics::SpentCaseFallWorld(start,1000000000,1000000000);
    const auto later=fvr::graphics::SpentCaseFallWorld(start,1000000000,1200000000);
    CHECK(first&&first->values==start.values&&later);
    CHECK(Near(later->values[3][0],1)&&Near(later->values[3][1],1.8038f,.0001f)&&Near(later->values[3][2],3.05f,.0001f));
    CHECK(!fvr::graphics::SpentCaseFallWorld(start,1000000000,999999999));
    CHECK(!fvr::graphics::SpentCaseFallWorld(start,1000000000,1600000000));
    CHECK(!fvr::graphics::SpentCaseFallWorld(Matrix{},1000000000,1100000000));
    return 0;
}
int SpentCaseRetainedRenderingRejectsDifferentCase(){
    const auto it=std::find_if(generated::UnderbarrelFixedContainerEquipment.begin(),generated::UnderbarrelFixedContainerEquipment.end(),
        [](const auto& e){return e.interaction.kind==FixedContainerKind::SingleRoundBreech;});
    CHECK(it!=generated::UnderbarrelFixedContainerEquipment.end());
    Fixture f(&*it,0,5);f.Send(At(it->interaction.geometry[0].motion,0),false);
    auto a=f.result.tracking;a.spentCasePresent=true;a.spentCaseEpoch=1;
    CHECK(FixedContainerTrackingRetained(a,a,f.now));
    for(unsigned change=0;change<6;++change){auto b=a;
        if(change==0)b.spentCasePresent=false;if(change==1)++b.spentCaseEpoch;
        if(change==2)b.spentCaseEjectedNs=f.now;if(change==3)b.spentCaseEjectionWorldMeters=Identity();
        if(change==4)++b.source.binding.context.resource.weaponGeneration;
        if(change==5)++b.source.binding.context.resource.weapon;
        CHECK(!FixedContainerTrackingRetained(a,b,f.now));
    }
    CHECK(!FixedContainerTrackingRetained(a,a,a.input.deadlineNs));return 0;
}
int SpentCaseOpeningFamiliesEjectBeforeExactInsertion(){
    unsigned opening=0,muzzle=0;
    for(const auto& e:generated::UnderbarrelFixedContainerEquipment){
        Fixture f(&e,1,7);f.observe=true;
        const auto& insertion=e.interaction.geometry[2].motion;
        const auto start=e.interaction.kind==FixedContainerKind::SingleRoundBreech?At(e.interaction.geometry[0].motion,0):At(insertion,0);
        // Coherent loaded observation must survive a cancelled manual path
        // with no GunHold. Its counts do not grant physical hand authority.
        f.Prepare(start,false);CHECK(f.hands.Release(f.sample.input,f.gun).accepted);
        f.gun={};f.sample.cancel=true;f.Consume();CHECK(!f.result.tracking.enabled);
        f.counts.loaded=0;f.Send(start,false);
        if(e.interaction.kind==FixedContainerKind::SingleRoundMuzzle){
            ++muzzle;CHECK(!f.result.tracking.spentCasePresent&&f.result.tracking.spentCaseEpoch==0&&
                f.result.tracking.step==FixedContainerStep::Insert);continue;
        }
        ++opening;CHECK(f.result.tracking.spentCasePresent&&f.result.tracking.spentCaseEpoch==1);
        f.Hinge(0);CHECK(f.result.tracking.step==FixedContainerStep::EjectSpent&&f.result.blocksFire&&f.calls==0);
        const auto lifetime=f.result.tracking.lifetime,epoch=f.result.tracking.spentCaseEpoch;
        const auto local=FixedContainerSpentCasePose(e,std::span(f.result.tracking.parts).first(e.parts.size()));CHECK(local);
        const auto inverse=InverseRigid(*local);CHECK(inverse);
        auto down=Identity();down.values[1]={0,0,1,0};down.values[2]={0,-1,0,0};
        const auto world=Multiply(*inverse,down);
        const auto endpoint=At(e.interaction.geometry[0].motion,1);
        // Cancel/drop hand authority as a holster/tracking interruption does;
        // the open mechanism and exact spent object must remain owed.
        f.physical->Cancel(f.sample.input,f.hands);
        for(unsigned n=0;n<11;++n){
            f.Prepare(endpoint,false);f.sample.raw.weaponWorldMeters=world;f.Consume();
            CHECK(f.calls==0&&f.service.View()->snapshot.counts==f.counts&&f.result.tracking.lifetime==lifetime);
            if(n<10)CHECK(f.result.tracking.step==FixedContainerStep::EjectSpent&&f.result.tracking.spentCasePresent);
        }
        CHECK(f.result.tracking.step==FixedContainerStep::Insert&&!f.result.tracking.spentCasePresent&&
            f.result.tracking.spentCaseEpoch==epoch&&f.result.tracking.spentCaseEjectionWorldMeters&&f.result.blocksFire);
        const auto ejected=f.result.tracking.spentCaseEjectedNs;CHECK(ejected==f.sample.raw.input.observedNs);
        std::ostringstream observation;WriteFixedContainerObservation(observation,&f.observation,f.native.owner,
            f.sample.input,f.sample.raw.rig,&e,f.now);
        CHECK(observation.str().find("\"spentCasePoseWeaponMeters\":")!=std::string::npos&&
              observation.str().find("\"modOwned\":true")!=std::string::npos);
        CHECK(!InsertFixedWithOfflineReceipt(f));
        CHECK(f.result.tracking.step==FixedContainerStep::Close&&!f.result.tracking.spentCasePresent&&f.counts.loaded==1&&f.counts.reserve==6);
        f.Hinge(3);CHECK(f.result.tracking.step==FixedContainerStep::Complete&&!f.result.blocksFire&&f.calls==1);
    }
    CHECK(opening&&muzzle);return 0;
}
