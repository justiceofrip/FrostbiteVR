// Uses the existing selected-seat fixture and the same SEH dispatcher used
// by native Update. Every action counts the one synthetic original body.
namespace {
thread_local mounted419::Qualification* qual419=nullptr;
enum class Action419 {Plain,NestedSelected,NestedForeign,Dead,Changed,Switched,Cpp,Seh,Lost};
constexpr DWORD Synthetic419=0xe0419001;
struct Fixture419:Fixture {
    mounted419::Qualification qual;
    VehicleReticleProducerContext ctx{};
    unsigned calls=0,cleanups=0,faultCleanups=0;bool unexpectedTls=false;
    Fixture419(){
        qual419=&qual;
        source.profile.profile.axes[2]=static_cast<EntryAction>(2);
        source.profile.profile.axes[3]=static_cast<EntryAction>(3);seat.permissionMask=12;
        Put(effects,0x1432300);Put(0x143234c,0x81c170);
        Put(primary+0xa8,std::bit_cast<unsigned>(450.f));Put(primary+0xb8,0x3f800000);
        Put(primary+0xd4,1);Put(primary+0xe4,0x42000);types[0x42000]="BulletEntityData";
        for(unsigned i=0;i<16;++i)Put(effects+0x30+4*i,(i%5==0)?0x3f800000:0u);
        Put(effects+0x60,std::bit_cast<unsigned>(10.f));
        ctx.input={{seat.identity.actor,seat.identity.actorGeneration,seat.identity.controlled,seat.identity.entry},
            source.space,true,true,source.observedNs,source.deadlineNs};
        ctx.inputSequence=source.input;ctx.scene=2;ctx.weaponGeneration=3;ctx.focused=ctx.playing=true;
        auto api=Api();qual.Configure({binding},api);
        api.lost=[]()noexcept{qual419->Lost();};
        api.prefire=[](std::uint64_t id,const VehicleShotSource& s,const OriginalFiringUpdate& c,const VehicleMountedSample& m)noexcept{
            qual419->Enter(id,s,c,m);
        };
        api.returned=[](std::uint64_t id,const VehicleShotSource& s,const OriginalFiringUpdate& c,const VehicleMountedSample& m,bool same)noexcept{
            qual419->Leave(id,s,c,m,same);
        };
        observer=std::make_unique<VehicleMountedObserver>();
        if(!observer->Configure(binding,api))std::abort();
        observer->Registration(true);Gather();
    }
    void Gather(){qual.Invalidate();qual.Gather(source.observationEpoch,ctx);}
    void Run(Action419 action=Action419::Plain,bool foreign=false){
        auto native=call;if(foreign)++native.caller;
        struct Args{Fixture419* f;Action419 action;};
        Args args{this,action};
        const auto dispatch=observer->Callbacks();
        mounted419::RunUpdate(dispatch,native,&args,[](void* raw){
            auto& args=*static_cast<Args*>(raw);auto& f=*args.f;++f.calls;
            f.unexpectedTls|=OriginalFiringScope::Current()!=nullptr;
            switch(args.action){
            case Action419::Plain:break;
            case Action419::NestedSelected:f.Run();break;
            case Action419::NestedForeign:f.Run(Action419::Plain,true);break;
            case Action419::Dead:f.now=f.source.deadlineNs;break;
            case Action419::Changed:f.Put(f.effects+0x60,std::bit_cast<unsigned>(11.f));break;
            case Action419::Switched:++f.source.observationEpoch;break;
            case Action419::Cpp:throw 419;
            case Action419::Seh:RaiseException(Synthetic419,0,0,nullptr);break;
            case Action419::Lost:f.qual.Lost();break;
            }
        },[](void* raw,bool returned)noexcept{
            auto& f=*static_cast<Args*>(raw)->f;++f.cleanups;if(!returned)++f.faultCleanups;
        });
    }
    std::string Result(){std::ostringstream out;qual.Report(out);return out.str();}
};
bool CatchNative419(Fixture419* f){
    __try {f->Run(Action419::Seh);return false;}
    __except(GetExceptionCode()==Synthetic419?EXCEPTION_EXECUTE_HANDLER:EXCEPTION_CONTINUE_SEARCH){return true;}
}
int PrivatePrefire419Tests(){
    {Fixture419 f;
        VehicleMountedObserverTestAccess::Budget(*f.observer,0,4096);
        f.Run();const auto original=f.qual.Retained();CHECK(original);
        CHECK(original->visual.proof==fvr::graphics::VehicleReticleProof::Unproven);
        CHECK(original->visual.sightWorld.values[3][0]==10&&original->visual.sightWorld.values[2][2]==-1);
        CHECK(original->visual.deadlineNs==f.source.deadlineNs&&f.calls==1&&f.cleanups==1);
        f.now=f.source.deadlineNs;CHECK(!f.qual.Retained());
        CHECK(f.Result().find("\"retained_prefire\":1")!=std::string::npos);}
    for(bool foreignOuter:{false,true}){
        Fixture419 f;f.Run(foreignOuter?Action419::NestedSelected:Action419::NestedForeign,foreignOuter);
        CHECK(!f.qual.Retained());CHECK(f.calls==2&&f.cleanups==2&&!f.unexpectedTls);
        CHECK(mounted419::updateSequence.Depth()==0);CHECK(f.Report().find("\"in_flight\":0")!=std::string::npos);
        f.Run();CHECK(f.qual.Retained());}
    for(auto action:{Action419::Dead,Action419::Changed,Action419::Switched,Action419::Lost}){
        Fixture419 f;f.Run(action);CHECK(!f.qual.Retained());CHECK(f.calls==1&&f.cleanups==1);}
    for(bool hardware:{false,true}){
        Fixture419 f;
        if(hardware){CHECK(CatchNative419(&f));}
        else {bool caught=false;try{f.Run(Action419::Cpp);}catch(int value){caught=value==419;}CHECK(caught);}
        CHECK(f.calls==1&&f.cleanups==1&&f.faultCleanups==1&&!f.qual.Retained());
        CHECK(!f.unexpectedTls&&OriginalFiringScope::Current()==nullptr&&mounted419::updateSequence.Depth()==0);
        CHECK(f.Report().find("\"in_flight\":0")!=std::string::npos);
        const auto report=f.Result();CHECK(report.find("\"unwound_or_leave_owner_changed\":1")!=std::string::npos);
        f.Run();CHECK(f.qual.Retained()&&f.calls==2&&f.cleanups==2);}
    for(unsigned change=0;change<8;++change){
        Fixture419 f;f.Run();CHECK(f.qual.Retained());
        switch(change){
        case 0:++f.source.observationEpoch;break;
        case 1:++f.ctx.input.space;f.Gather();break;
        case 2:f.ctx.focused=false;f.Gather();break;
        case 3:f.seat.permissionMask=0;break;
        case 4:f.Put(f.component+0xd0,f.mounted+4);break;
        case 5:f.Run(Action419::Plain,true);break;
        case 6:f.qual.Close();break;
        case 7:++f.ctx.input.deadlineNs;f.Gather();f.Run();break;
        }
        CHECK(!f.qual.Retained());}
    return 0;
}
}
