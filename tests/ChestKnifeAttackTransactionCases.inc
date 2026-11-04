int ChestAttackTransactionCases(){
    using Tx=pk::ChestAttackTransaction;using Outcome=pk::ChestAttackOutcome;using Target=pk::ChestAttackTarget;
    pk::ChestAttackIdentity k;k.serial=2;k.lifetime=3;k.operation=4;k.sourceEpoch=9;k.input=497;k.strokeSerial=2;k.holdId=16;
    k.originalOwner={0x10000,0x10200,0x10400,0x10600,7,8,11};k.inputOwner={0x1040000010200,7,12,11};k.knife={0x20000,6};
    // The hand allocator's handEquip and the native equipment epoch are distinct clocks.
    k.selectedOwner=k.originalOwner;k.selectedOwner.weapon=unsigned(k.knife.id);k.selectedOwner.equipGeneration=9;
    k.observedNs=1000000000;k.deadlineNs=k.observedNs+100000000;k.geometryDeadlineNs=k.deadlineNs-15000000;
    k.serverActor=0x30000;k.serverItem=0x31000;k.effects=0x32000;k.firing={0x33000,0x34000,0x35000};
    k.config.primaryFire=0x36000;k.config.fireInputAction=8;
    fvr::math::Matrix4 blade{};for(unsigned i=0;i<4;++i)blade.values[i][i]=1;blade.values[3][0]=.25f;
    const auto now=k.observedNs+1;const Target hit{0x50000,0x51000};
    {Tx tx;CHECK(tx.Stage(k,blade,now));CHECK(!tx.BeginAcquire(k,true,true));CHECK(!tx.Consume(k,0));
        CHECK(tx.Consume(k,1));CHECK(!tx.BeginAcquire(k,true,true));CHECK(tx.Consume(k,2));CHECK(!tx.Consume(k,2));
        for(unsigned i=0;i<6;++i){auto other=k;
            if(i==0)++other.serial;if(i==1)++other.lifetime;if(i==2)++other.inputOwner.actorGeneration;
            if(i==3)++other.knife.generation;if(i==4)++other.config.primaryFire;if(i==5)++other.effects;
            CHECK(!tx.BeginAcquire(other,true,true));}
        CHECK(!tx.BeginAcquire(k,false,true)&&!tx.BeginAcquire(k,true,false));CHECK(tx.BeginAcquire(k,true,true));
        CHECK(!tx.BeginAcquire(k,true,true)&&!tx.Settle(k));const auto geometry=tx.TakeGeometry(k,now,true);
        CHECK(geometry&&geometry->values==blade.values&&!tx.TakeGeometry(k,now,true));
        // Padded native transform is an immutable input; only the private XYZ
        // words in the caller-local copy change. A native mutation of that copy
        // cannot alias the engine argument or the retained stroke geometry.
        auto stock=blade;stock.values[3][0]=0;stock.values[0][3]=42;stock.values[1][3]=19;
        alignas(16) std::array<std::byte,64> original{};std::memcpy(original.data(),&stock,64);const auto saved=original;
        auto copy=selectedKnife::AcquisitionCopy(original,*geometry,1);CHECK(copy&&*copy!=original);
        for(unsigned row=0;row<4;++row)CHECK(!std::memcmp(copy->data()+row*16+12,original.data()+row*16+12,4));
        (*copy)[0]=std::byte{99};CHECK(original==saved&&tx.Pending()->geometry.values==blade.values);
        tx.GeometryReturned(k,false,true);tx.GeometryReturned(k,true,true);CHECK(tx.EndAcquire(k,hit));
        tx.RevokeGeometry();CHECK(!tx.TakeGeometry(k,k.deadlineNs+500000000,true));
        CHECK(!tx.BeginAttack(k,Target{hit.handle,hit.soldier+4},true,true));CHECK(!tx.BeginAttack(k,hit,false,true));
        CHECK(!tx.BeginAttack(k,hit,true,false));CHECK(tx.BeginAttack(k,hit,true,true));CHECK(!tx.BeginAttack(k,hit,true,true));
        CHECK(!tx.Settle(k));tx.Damage(k,true,100,75);CHECK(tx.EndAttack(k));CHECK(!tx.EndAttack(k));
        const auto receipt=tx.Settle(k);CHECK(receipt&&receipt->outcome==Outcome::Hit&&receipt->consumed==6&&
            receipt->geometryClaims==1&&receipt->geometryForwardMask==3&&receipt->damageCalls==1&&receipt->healthAfter==75);
        CHECK(!tx.Stage(k,blade,now)&&!tx.Settle(k));}
    // Occluded source, stale render lease, prediction-only callback and returned
    // native attack without a health delta are never reported as a tracked hit.
    for(unsigned control=0;control<6;++control){Tx tx;CHECK(tx.Stage(k,blade,now));CHECK(tx.Consume(k,2));
        if(control==0){CHECK(!tx.Settle(k)&&tx.Consume(k,1));const auto r=tx.Settle(k);CHECK(r&&r->outcome==Outcome::Unknown);continue;}
        CHECK(tx.BeginAcquire(k,true,true));
        if(control==1)tx.RevokeGeometry();
        auto geom=tx.TakeGeometry(k,control==2?k.geometryDeadlineNs:now,true);CHECK(bool(geom)==(control>=3));
        if(geom)tx.GeometryReturned(k,false,true);
        const Target target=control==3?Target{}:control==4?Target{hit.handle,0}:hit;
        CHECK(tx.EndAcquire(k,target)&&tx.BeginAttack(k,target,true,true));if(target.soldier)tx.Damage(k,true,75,75);
        CHECK(tx.EndAttack(k));CHECK(!tx.Settle(k)&&tx.Consume(k,1));const auto r=tx.Settle(k);CHECK(r&&r->outcome==(control==3?Outcome::Miss:Outcome::Unknown));}
    {Tx tx;CHECK(!tx.Stage(k,blade,k.geometryDeadlineNs));CHECK(tx.Stage(k,blade,now));CHECK(tx.Consume(k,2));
        tx.LifetimeEnded();CHECK(!tx.BeginAcquire(k,true,true)&&!tx.Stage(k,blade,now));auto next=k;++next.serial;++next.lifetime;
        CHECK(tx.Stage(next,blade,now));CHECK(!tx.Consume(k,2)&&!tx.EndAcquire(k,hit)&&!tx.Settle(k));}
    {Fixture f;f.s.nativeAttackReady=false;CHECK(f.Held()&&f.Rest());const auto hold=f.knife.Hold();
        CHECK(hold&&!f.Stab()&&f.knife.Hold()==hold&&f.knife.Phase()==KnifePhase::Held&&!f.knife.NativeAttackPending());
        f.Grip(false);const auto back=f.Step();CHECK(back&&back->kind==KnifeCommandKind::Return&&f.knife.CanCommit(*back,f.s,f.hands));}
    {OriginalFiringObserver observer{};unsigned marker=0;observer.context=&marker;
        observer.enter=[](void*,const OriginalFiringUpdate&)noexcept{OriginalFiringToken t;t.words[6]=2;return t;};
        observer.leave=[](void*,const OriginalFiringUpdate&,const OriginalFiringToken&)noexcept{};
        observer.copyInput=[](void*,const OriginalFiringUpdate&,OriginalFiringToken&)noexcept->std::optional<std::array<std::byte,0x30>>{return std::array<std::byte,0x30>{};};
        CHECK(!OriginalFiringScope::Current());
        {OriginalFiringScope outer(observer,{k.firing[2],0x41000,0x42000,0});CHECK(outer.InputCopy());
            outer.BindCopiedContext(0x43000);CHECK(OriginalFiringScope::Current()==&outer&&outer.Copied()&&outer.NativeContext()==0x43000);
            {OriginalFiringScope inner({},{});CHECK(OriginalFiringScope::Current()==&inner&&!inner.Copied()&&!inner.ObserverContext());}
            bool foreignSawParent=true;std::thread foreign([&]{foreignSawParent=OriginalFiringScope::Current()!=nullptr;});foreign.join();
            CHECK(!foreignSawParent&&OriginalFiringScope::Current()==&outer&&outer.Token().words[6]==2);}
        CHECK(!OriginalFiringScope::Current());}
    return 0;
}
