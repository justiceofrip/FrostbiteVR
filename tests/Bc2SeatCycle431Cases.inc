int SeatCycleRequestPreservesNativeControls() {
    using namespace fvr::bc2::seatCycle;
    std::array<std::byte,InputBytes> cache{};
    const auto put=[&](unsigned w){std::memcpy(cache.data()+0x98,&w,4);};
    const auto get=[&](){unsigned w{};std::memcpy(&w,cache.data()+0x98,4);return w;};
    VehicleInputPlan p;p.status=VehicleInputPlanStatus::Ready;
    constexpr unsigned native=0x40000901;
    put(native); CHECK(Request(p,native));
    CHECK(p.count==1 && p.edits[0].after==(native|InputBit));
    VehicleInputOverride edit; CHECK(edit.Apply(cache,p));put(get()|0x08000000);
    CHECK(edit.Restore() && get()==(native|0x08000000));
    for (unsigned bit=16;bit<=25;++bit) {
        put(native|(1u<<bit));VehicleInputPlan other;other.status=VehicleInputPlanStatus::Ready;
        CHECK(!Request(other,get()) && !other.count);
    }
    VehicleInputPlan exitPlan;exitPlan.status=VehicleInputPlanStatus::Ready;
    exitPlan.edits[exitPlan.count++]={0x98,native,native|(1u<<16)};
    CHECK(!Request(exitPlan,native) && exitPlan.edits[0].after==(native|(1u<<16)));
    // A forged plan cannot clear native seat selection or broaden the change.
    put(native|InputBit);p.edits[0]={0x98,get(),native};CHECK(!edit.Apply(cache,p));
    p.edits[0]={0x98,get(),get()|(1u<<25)};CHECK(!edit.Apply(cache,p));

    Button button; VehicleSeatIdentity seat{1,2,3,4,5,6,7}; Binding binding{};binding.key=23;
    InputFrame in{};in.spaceGeneration=2;in.menu.epoch=3;in.focused=in.headValid=true;
    auto& right=in.hands[1];right.gripTracked=true;right.active=Primary|StickClick;
    unsigned tick=0,proofs=0;
    constexpr unsigned throttle=std::bit_cast<unsigned>(0.5f);
    const auto gather=[&](unsigned gen,unsigned down,unsigned stock=0,bool staleAxis=false) {
        // The native gather rebuilds its fields. A stock bit may appear between
        // the caller's initial flags read and BuildVehicleInputPlan's snapshot.
        put(native|stock);
        const unsigned zero=0;std::memcpy(cache.data()+8,&zero,4);
        VehicleInputPlan plan;plan.status=VehicleInputPlanStatus::Ready;
        plan.edits[plan.count++]={8,staleAxis?throttle:zero,throttle};
        plan.edits[plan.count++]={0x98,get(),get()};
        in.generation=gen;right.held=down;++tick;
        const auto checked=button.CheckBinding(in,true,[&]{++proofs;return binding;});
        button.Update(seat,checked,in,tick,tick,tick+10,true);
        if (!button.Apply(edit,cache,plan,native)) return false;
        edit.Commit();return true;
    };
    CHECK(gather(1,0) && get()==native && proofs==0);
    CHECK(gather(2,Primary) && get()==(native|InputBit) && proofs==1);
    CHECK(gather(2,Primary) && get()==(native|InputBit) && proofs==2);
    CHECK(gather(3,Primary,InputBit) && get()==(native|InputBit) && proofs==3);
    unsigned observed=0;std::memcpy(&observed,cache.data()+8,4);CHECK(observed==throttle);
    CHECK(gather(2,0) && gather(3,0) && gather(4,Primary) && gather(5,Primary) && get()==native && proofs==3);
    CHECK(gather(6,0) && gather(7,Primary) && get()==(native|InputBit) && proofs==4);
    CHECK(!gather(7,Primary,0,true) && get()==native && proofs==5);
    CHECK(gather(7,0) && gather(8,Primary) && gather(9,Primary) && get()==native && proofs==5);
    CHECK(gather(10,0) && gather(11,Primary) && get()==(native|InputBit) && proofs==6);

    // Unrelated driving/neutral gathers cannot exhaust the opt-in A journal.
    AttemptTrace trace;in.generation=100;right.held=0;
    for(unsigned n=0;n<100;++n)CHECK(!trace.Observe(true,seat,in));
    right.held=Primary;CHECK(!trace.Observe(false,seat,in));
    auto attempt=trace.Observe(true,seat,in);CHECK(attempt);
    attempt->gather=31;attempt->verified=false;
    CHECK(!Admit(false,Gate::Verification,&attempt->first));
    CHECK(!Admit(false,Gate::Binding,&attempt->first));
    trace.Publish(*attempt);
    for(unsigned n=0;n<100;++n){++in.generation;CHECK(!trace.Observe(true,seat,in));}
    CHECK(trace.Count()==1 && trace.Row(0).input==100 && trace.Row(0).gather==31);
    CHECK(trace.Row(0).first==Gate::Verification && !trace.Row(0).requested && !trace.Row(0).committed);
    for(unsigned n=1;n<17;++n){
        right.held=0;CHECK(!trace.Observe(true,seat,in));right.held=Primary;++in.generation;
        auto row=trace.Observe(true,seat,in);CHECK(bool(row)==(n<16));if(row)trace.Publish(*row);
    }
    CHECK(trace.Count()==16 && trace.Dropped()==1 && trace.Row(0).first==Gate::Verification);
    return 0;
}

int SeatCycleRequiresFreshNeutralAndDistinctButton() {
    using namespace fvr::bc2::seatCycle;
    Button button;VehicleSeatIdentity seat{1,2,3,4,5,6,7}; Binding binding{};binding.key=23;
    InputFrame in{};in.spaceGeneration=2;in.menu.epoch=3;in.focused=in.headValid=true;
    auto& right=in.hands[1];right.gripTracked=true;right.active=Primary|StickClick;
    unsigned tick=0;
    const auto sample=[&](unsigned gen,unsigned down,bool eligible=true) {
        in.generation=gen;right.held=down;
        ++tick;
        return button.Update(seat,down?binding:Binding{},in,tick,tick,tick+10,eligible);
    };
    CHECK(!sample(1,Primary));CHECK(!sample(2,0));
    CHECK(sample(3,Primary));CHECK(sample(3,Primary));CHECK(sample(4,Primary));
    ++seat.seatGeneration;CHECK(!sample(4,Primary));CHECK(!sample(5,Primary));
    CHECK(!sample(6,0));CHECK(sample(7,Primary));
    CHECK(!sample(7,Primary,false));CHECK(!sample(8,Primary));
    CHECK(!sample(9,0));in.hands[0].held=Primary;CHECK(!sample(10,Primary));
    in.hands[0].held=0;CHECK(!sample(11,Primary));CHECK(!sample(12,0));
    CHECK(!sample(13,Primary|StickClick));CHECK(!sample(14,Primary));
    CHECK(!sample(15,0));CHECK(sample(16,Primary));
    ++binding.key;CHECK(!sample(17,Primary));CHECK(!sample(18,0));CHECK(sample(19,Primary));
    ++tick;CHECK(!sample(20,Primary));CHECK(!sample(21,0));CHECK(sample(22,Primary));
    right.gripTracked=false;CHECK(!sample(22,Primary));right.gripTracked=true;CHECK(!sample(23,Primary));
    CHECK(!sample(24,0));CHECK(sample(25,Primary));++in.menu.epoch;CHECK(!sample(26,Primary));
    CHECK(!sample(27,0));CHECK(sample(28,Primary));++in.spaceGeneration;CHECK(!sample(29,Primary));
    CHECK(!sample(30,0));CHECK(sample(31,Primary));++tick;in.generation=32;
    CHECK(!button.Update(seat,binding,in,tick,tick+20,tick+30,true));
    // Simple-controller select and menu cannot prove a distinct right A.
    right.active=Primary|MenuClick;
    CHECK(!sample(33,0));CHECK(!sample(34,Primary));
    right.active=Primary|StickClick;CHECK(!sample(35,Primary));
    CHECK(!sample(36,0));CHECK(sample(37,Primary));
    right.active=Primary|MenuClick;CHECK(!sample(37,Primary));
    right.active=Primary|StickClick;CHECK(!sample(38,Primary));

    Button diagnostic;Gate first=Gate::None;unsigned bindingCalls=0;
    in.generation=100;right.held=Primary;
    CHECK(!diagnostic.CheckBinding(in,true,[&]{++bindingCalls;return binding;},&first).key);
    CHECK(first==Gate::Rearm && bindingCalls==0);
    CHECK(!diagnostic.Update(seat,{},in,100,100,110,true,&first) && first==Gate::Rearm);
    right.held=0;++in.generation;CHECK(!diagnostic.Update(seat,{},in,101,101,111,true));
    right.held=Primary;right.active=Primary|MenuClick;++in.generation;first=Gate::None;
    CHECK(!diagnostic.CheckBinding(in,true,[&]{++bindingCalls;return binding;},&first).key);
    CHECK(first==Gate::Controller && bindingCalls==0);
    right.active=Primary|StickClick;first=Gate::None;
    CHECK(diagnostic.CheckBinding(in,true,[&]{++bindingCalls;return binding;},&first)==binding);
    CHECK(diagnostic.Update(seat,binding,in,102,102,112,true,&first) && first==Gate::None);
    first=Gate::None;++in.generation;
    CHECK(!diagnostic.Update(seat,binding,in,104,104,114,true,&first) && first==Gate::Continuity);
    first=Gate::None;--in.generation;
    CHECK(!diagnostic.Update(seat,binding,in,105,105,115,true,&first) && first==Gate::Replay);
    return 0;
}

int SeatCycleRequiresCurrentMappedKeyboardAndPermission() {
    using namespace fvr::bc2::seatCycle;
    struct Memory {
        std::map<unsigned,unsigned char> bytes;
        std::vector<std::pair<unsigned,unsigned>> reads;
        void word(unsigned at,unsigned value) {
            for (unsigned i=0;i<4;++i) bytes[at+i]=static_cast<unsigned char>(value>>(i*8));
        }
        VehicleRouteMemory access() {
            return {this,[](void* ctx,unsigned at,void* dst,std::size_t n){
                auto& self=*static_cast<Memory*>(ctx);
                self.reads.emplace_back(at,unsigned(n));
                for (std::size_t i=0;i<n;++i) {
                    auto it=self.bytes.find(at+unsigned(i));if(it==self.bytes.end())return false;
                    static_cast<unsigned char*>(dst)[i]=it->second;
                }return true;
            },nullptr};
        }
    } m;
    VehicleRouteSnapshot s{};s.player=0x18000;s.identity={1,2,3,0x20000,0x21000,0x22000,0x23000};
    s.count=1;s.routes[0]={17,42};
    const unsigned p=s.player,r=s.identity.router,device=0x30000,map=0x32000,table=0x40000;
    const unsigned rows=0x41000,key=0x42000,record=table+42*16;
    m.word(p+0xc74,s.identity.cache);m.word(s.identity.entry+0x198,r);m.word(p+0x160,64);
    m.word(0x1570e9c,0);m.word(0x1570ee0,0);m.word(0x1570ee4,0);
    m.word(r+0x14,0x50000);m.word(r+0x18,31);m.word(0x50000+31*4,0);
    m.word(0x50000+17*4,0x51000);m.word(0x51000,17);m.word(0x51004,42);
    m.word(r+4,device);m.word(r+8,map);m.word(device,0x14196f8);m.word(map,0x141ca1c);
    m.word(device+8,0x31000);m.word(device+0x20+42*4,0);
    m.word(map+4,table);m.word(map+8,table+76*16);
    m.word(record,rows);m.word(record+4,rows+8);m.word(rows,key);m.word(rows+4,42);
    m.word(key,0x14656f8);m.word(key+12,0);m.word(key+16,59);
    // Compare recorded/unrecorded results, gates and the complete callback order.
    // The explicit baseline below also protects the pre-observation read schedule.
    BindingObservation observed;
    Gate observedGate=Gate::None;
    const auto sameReads=[&](Memory& candidate,const VehicleRouteSnapshot& seat,bool proof=true) {
        auto plain=candidate;plain.reads.clear();candidate.reads.clear();observed={};observedGate=Gate::None;
        Gate plainGate=Gate::None;
        const auto before=ReadBinding(plain.access(),seat,proof,&plainGate);
        const auto after=ReadBinding(candidate.access(),seat,proof,&observedGate,&observed);
        return before==after && plainGate==observedGate && plain.reads==candidate.reads;
    };
    const auto fieldRead=[&](BindingField field){return (observed.readMask&(1u<<unsigned(field)))!=0;};
    const auto fieldSuccess=[&](BindingField field){return (observed.successMask&(1u<<unsigned(field)))!=0;};
    const auto value=[&](BindingField field){return observed.values[unsigned(field)];};
    const auto refused=[&](const char* predicate){return !std::strcmp(observed.refusal,predicate);};
    CHECK(sameReads(m,s) && observed.returned && refused("none") && observed.calls==1 && observed.call==1);
    std::vector<std::pair<unsigned,unsigned>> expectedReads{
        {p+0xc74,4},{s.identity.entry+0x198,4},{p+0x160,4},{0x1570e9c,1},{0x1570ee0,4},{0x1570ee4,4},
        {r+0x14,4},{r+0x18,4},{0x50000+31*4,4},{0x50000+17*4,4},{0x51000,4},{0x51000,4},{0x51004,4},
        {r+4,4},{r+8,4},{device,4},{map,4},{device+8,4},{map+4,4},{map+8,4},{device+0x20+42*4,4},
        {record,4},{record+4,4},{rows,4},{rows+4,4},{key,4},{key+12,2},{key+16,4}};
    // C++ leaves the two operands of the existing suppression comparison unordered.
    if(m.reads[4].first==0x1570ee4)std::swap(expectedReads[4],expectedReads[5]);
    CHECK(m.reads==expectedReads && observed.reads==28 && observed.terminalNode==0x51000 && observed.traversed==0);
    CHECK(observed.verified && observed.reader && observed.routeChecked && observed.routePresent && observed.routeCount==1);
    CHECK(value(BindingField::PlayerCache)==s.identity.cache && value(BindingField::EntryRouter)==r);
    CHECK(value(BindingField::Permission)==64 && value(BindingField::TableEnd)-value(BindingField::Table)==1216);
    CHECK(!fieldRead(BindingField::NodeNext) && !observed.failedReadIndex && observed.alternativeCount==1);
    CHECK(observed.alternatives[0].readMask==63 && observed.alternatives[0].successMask==63 && observed.alternatives[0].values[5]==59);
    CHECK(ReadBinding(m.access(),s,true));CHECK(!ReadBinding(m.access(),s,false));
    for (const auto [at,value]:{std::pair{p+0x160,0u},{p+0xc74,0u},{s.identity.entry+0x198,0u},
         {0x1570ee4u,4u},{map,0u},{device,0u},{device+8,0u},{device+0x20+42*4,1u},
         {0x51004u,76u},{record+4,rows},{record+4,rows+9},{rows+4,43u},{key,0u},{key+12,1u},{key+16,0u}}) {
        auto changed=m;changed.word(at,value);CHECK(sameReads(changed,s) && !observed.returned);
    }
    auto missing=m;missing.bytes.erase(key+16);CHECK(!ReadBinding(missing.access(),s,true));
    auto denied=m;denied.word(p+0x160,0);Gate first=Gate::None;
    CHECK(!ReadBinding(denied.access(),s,true,&first) && first==Gate::Permission);
    denied=m;denied.word(p+0xc74,0);first=Gate::None;
    CHECK(!ReadBinding(denied.access(),s,true,&first) && first==Gate::Owner);
    denied=m;denied.word(0x1570ee4,4);first=Gate::None;
    CHECK(!ReadBinding(denied.access(),s,true,&first) && first==Gate::Permission);
    denied=m;denied.bytes.erase(p+0x160);first=Gate::None;
    CHECK(!ReadBinding(denied.access(),s,true,&first) && first==Gate::Binding);

    CHECK(sameReads(m,s,false) && m.reads.empty() && refused("verified") && !observed.routeChecked);
    auto malformed=s;malformed.count=unsigned(malformed.routes.size())+1;
    CHECK(sameReads(m,malformed) && m.reads.empty() && refused("route_count") && observed.actual==51 && !observed.routeChecked);
    malformed=s;malformed.routes[0]={17,76};
    CHECK(sameReads(m,malformed) && m.reads.empty() && refused("route_17_42") && observed.routeChecked && !observed.routePresent);
    denied=m;denied.word(p+0xc74,0);
    CHECK(sameReads(denied,s) && refused("player_cache") && observed.actual==0 && observed.expected==s.identity.cache);
    CHECK(denied.reads.size()==1 && !fieldRead(BindingField::EntryRouter) && observedGate==Gate::Owner);
    denied=m;denied.bytes.erase(p+0xc74);
    CHECK(sameReads(denied,s) && refused("memory_read") && observed.failedReadAddress==p+0xc74);
    CHECK(observed.failedReadSize==4 && observed.failedReadIndex==1 && observed.failedReadCalled && !fieldSuccess(BindingField::PlayerCache));
    CHECK(denied.reads.size()==1 && !fieldRead(BindingField::EntryRouter) && observedGate==Gate::Binding);
    malformed=s;malformed.player=UINT32_MAX-0x100;
    CHECK(sameReads(m,malformed) && refused("read_address") && m.reads.empty() && observed.failedReadIndex==1);
    CHECK(observed.failedReadAddress==std::uint64_t(malformed.player)+0xc74 && !observed.failedReadCalled);
    denied=m;denied.word(p+0x160,0);
    CHECK(sameReads(denied,s) && refused("player_permission") && value(BindingField::Permission)==0);
    CHECK(denied.reads.size()==3 && !fieldRead(BindingField::Unrestricted) && observedGate==Gate::Permission);
    denied=m;denied.word(0x1570ee4,4);
    CHECK(sameReads(denied,s) && refused("concept_suppression") && value(BindingField::SuppressionBegin)==0 && value(BindingField::SuppressionEnd)==4);
    CHECK(denied.reads.size()==6 && !fieldRead(BindingField::Buckets) && observedGate==Gate::Permission);
    auto unrestricted=m;unrestricted.word(0x1570e9c,1);
    CHECK(sameReads(unrestricted,s) && observed.returned && !fieldRead(BindingField::SuppressionBegin) && !fieldRead(BindingField::SuppressionEnd));
    auto unrestrictedReads=expectedReads;unrestrictedReads.erase(unrestrictedReads.begin()+4,unrestrictedReads.begin()+6);
    CHECK(unrestricted.reads==unrestrictedReads);
    denied=m;denied.word(r+0x18,257);
    CHECK(sameReads(denied,s) && refused("bucket_count_limit") && observed.actual==257 && observed.expected==256);
    CHECK(denied.reads.size()==8 && !fieldRead(BindingField::Sentinel));
    denied=m;denied.bytes.erase(r+0x14);
    CHECK(sameReads(denied,s) && refused("memory_read") && observed.failedReadAddress==r+0x14 && observed.failedReadIndex==7);
    CHECK(denied.reads.size()==8 && fieldRead(BindingField::BucketCount) && !fieldRead(BindingField::Sentinel));
    denied=m;denied.word(0x51004,76);
    CHECK(sameReads(denied,s) && refused("node_concept") && observed.actual==76 && observed.expected==42);
    CHECK(denied.reads.size()==13 && !fieldRead(BindingField::Device));
    denied=m;denied.word(0x50000+31*4,0x53000);denied.word(0x50000+17*4,0x53000);
    CHECK(sameReads(denied,s) && refused("node_sentinel") && observed.terminalNode==0x53000 && observed.actual==0x53000);
    CHECK(denied.reads.size()==10 && !fieldRead(BindingField::NodeAction));
    auto traversal=m;traversal.word(0x51000,16);traversal.word(0x51008,0x52000);traversal.word(0x52000,17);traversal.word(0x52004,42);
    CHECK(sameReads(traversal,s) && observed.returned && observed.traversed==1 && observed.terminalNode==0x52000 && value(BindingField::NodeNext)==0x52000);
    auto traversalReads=expectedReads;traversalReads.insert(traversalReads.begin()+11,{{0x51008,4},{0x52000,4}});
    traversalReads[13]={0x52000,4};traversalReads[14]={0x52004,4};CHECK(traversal.reads==traversalReads);
    denied=m;denied.word(0x51000,16);denied.word(0x51008,0x51000);
    CHECK(sameReads(denied,s) && refused("node_action") && observed.actual==16 && observed.expected==17 && observed.traversed==50);
    CHECK(denied.reads.size()==111 && !fieldRead(BindingField::NodeConcept));
    denied=m;denied.word(device,0);
    CHECK(sameReads(denied,s) && refused("device_vtable") && observed.actual==0 && observed.expected==0x14196f8);
    CHECK(denied.reads.size()==16 && !fieldRead(BindingField::MapVtable));
    denied=m;denied.word(map+8,table+75*16);
    CHECK(sameReads(denied,s) && refused("table_span") && observed.actual==1200 && observed.expected==1216);
    CHECK(denied.reads.size()==20 && !fieldRead(BindingField::Override) && !fieldRead(BindingField::Actions));
    denied=m;denied.word(device+0x20+42*4,1);
    CHECK(sameReads(denied,s) && refused("concept_override") && observed.actual==1 && !fieldRead(BindingField::Actions));
    denied=m;denied.word(record+4,rows+9);
    CHECK(sameReads(denied,s) && refused("actions_stride") && observed.actual==1 && observed.alternativeCount==0);
    denied=m;denied.word(rows+4,43);
    CHECK(sameReads(denied,s) && refused("row_concept") && observed.actual==43 && observed.expected==42 && observed.failedAlternativeRow==rows);
    CHECK(observed.alternatives[0].readMask==3);
    CHECK(!std::strcmp(observed.alternatives[0].refusal,"row_concept") && observed.alternatives[0].values[1]==43 && denied.reads.size()==25);
    denied=m;denied.word(key+12,0x100);
    CHECK(sameReads(denied,s) && refused("flags13") && observed.actual==1 && observed.expected==0 && observed.alternatives[0].values[4]==1);
    CHECK(!std::strcmp(observed.alternatives[0].refusal,"flags13") && denied.reads==expectedReads);
    denied=m;denied.word(key+16,60);
    CHECK(sameReads(denied,s) && refused("scan16") && observed.actual==60 && observed.expected==59 && observed.alternatives[0].values[5]==60);
    CHECK(!std::strcmp(observed.alternatives[0].refusal,"scan16") && denied.reads==expectedReads);
    denied=m;denied.bytes.erase(key+16);
    CHECK(sameReads(denied,s) && refused("memory_read") && observed.failedReadAddress==key+16 && observed.failedReadIndex==28);
    CHECK(observed.alternatives[0].readMask==63 && observed.alternatives[0].successMask==31 && denied.reads==expectedReads);

    // All sixteen existing alternatives remain bounded, and a rejected earlier
    // alternative does not become a refusal when a later alternative succeeds.
    auto alternatives=m;alternatives.word(record+4,rows+16*8);
    for(unsigned n=0;n<16;++n)alternatives.word(rows+n*8,0);
    CHECK(sameReads(alternatives,s) && refused("key_pointer") && observed.alternativeCount==16 && observed.failedAlternativeRow==rows);
    for(unsigned n=0;n<16;++n)CHECK(observed.alternatives[n].row==rows+n*8 && observed.alternatives[n].readMask==1);
    alternatives.word(rows+15*8,key);alternatives.word(rows+15*8+4,42);
    CHECK(sameReads(alternatives,s) && observed.returned && refused("none") && observed.alternativeCount==16);
    CHECK(observed.alternatives[15].readMask==63 && alternatives.reads.size()==expectedReads.size()+15);
    // The second existing proof keeps its own failure and call index; no extra
    // proof is requested, and publishing an attempt copies its diagnostic facts.
    BindingObservation recheck;CHECK(ReadBinding(m.access(),s,true,nullptr,&recheck));
    denied=m;denied.word(map+8,table+75*16);
    CHECK(!ReadBinding(denied.access(),s,true,nullptr,&recheck));
    CHECK(recheck.calls==2 && recheck.call==2 && !recheck.returned && !std::strcmp(recheck.refusal,"table_span"));
    AttemptTrace journal;Attempt row;row.binding=recheck;journal.Publish(row);row.binding={};
    CHECK(journal.Row(0).binding.call==2 && journal.Row(0).binding.actual==1200 && journal.Row(0).binding.expected==1216);
    struct Output:std::streambuf {
        std::string text;
        int_type overflow(int_type c)override {
            if(!traits_type::eq_int_type(c,traits_type::eof()))text.push_back(traits_type::to_char_type(c));
            return traits_type::not_eof(c);
        }
    } output;
    std::ostream report(&output);journal.Report(report);
    CHECK(output.text.find("\"captured_call\":2")!=std::string::npos);
    CHECK(output.text.find("\"first_failed_predicate\":\"table_span\",\"actual\":1200,\"expected\":1216")!=std::string::npos);
    CHECK(output.text.find("\"concept_42_override\":{\"read\":0,\"success\":0,\"value\":0}")!=std::string::npos);
    CHECK(output.text.find("\"table_end\":{\"read\":1,\"success\":1,\"value\":263344}")!=std::string::npos);
    s.routes[0]={17,76};CHECK(!ReadBinding(m.access(),s,true));
    return 0;
}
