#include "Bc2SelectedMeshesObservation.h"
#include "Bc2SelectedMeshesFixture.h"
#include <future>
#include <sstream>
#include <thread>
namespace {
ReloadStateSnapshot Source(const Fixture& f,std::uint64_t sequence=1,std::int64_t observed=Now){
    ReloadStateSnapshot s;s.owner=f.owner;s.sequence=sequence;s.observedNs=observed;
    s.inventory=f.inventory;s.selectedSlot=0;s.config.weaponData=f.data;
    std::memcpy(s.config.assetName.data(),"SPAS12_sp",10);return s;
}
bool Install(SelectedMeshesObservation& cache,Fixture& f){return cache.Install(f.file,f.pe,Base,{&f,Fixture::Read,nullptr},true);}
int DisabledAndOwnedBinding(){
    Fixture f;SelectedMeshesObservation c;const auto s=Source(f);
    CHECK(!c.Install(f.file,f.pe,Base,{&f,Fixture::Read,nullptr}));CHECK(!c.Observe(s,Now)&&f.callbackCalls==0);
    CHECK(!c.Read(f.owner,Now));CHECK(Install(c,f));
    // Discover uses spans; caller may release both its executable and binding.
    f.binding={};f.file.clear();f.file.shrink_to_fit();
    CHECK(c.Observe(s,Now));const auto result=c.Read(f.owner,Now+1);CHECK(result&&result->soleConfiguredArray==f.states+0x80);
    CHECK(FindSelectedMesh(*result,f.owner,SelectedMeshKind::Spas12,Now+1));
    CHECK(!result->activeStateVerified&&!result->submittedSkinVerified&&!result->renderSuppressionAllowed);return 0;
}
int LeaseAndAllOwnerFields(){
    Fixture f;SelectedMeshesObservation c;CHECK(Install(c,f));auto s=Source(f);CHECK(c.Observe(s,Now));
    const auto p=c.Read(f.owner,Now+1);CHECK(p&&p->observedNs==Now&&p->deadlineNs==Now+200000000);
    CHECK(!c.Observe(s,Now+150000000));CHECK(c.Read(f.owner,Now+199999999)->deadlineNs==p->deadlineNs);
    CHECK(!c.Read(f.owner,Now-1)&&!c.Read(f.owner,Now+200000000));
    CHECK(c.ReadCurrent(Now+1)==p&&c.ReadCurrent(Now+199999999)==p);
    CHECK(!c.ReadCurrent(Now-1)&&!c.ReadCurrent(Now+200000000));
    for(unsigned n=0;n<7;++n){auto wrong=f.owner;switch(n){case 0:++wrong.player;break;case 1:++wrong.soldier;break;
        case 2:++wrong.weak;break;case 3:++wrong.weapon;break;case 4:++wrong.actorGeneration;break;
        case 5:++wrong.equipGeneration;break;case 6:++wrong.space;break;}CHECK(!c.Read(wrong,Now+1));}
    CHECK(c.Counters().attempts==1);return 0;
}
int ImmutablePublicationAndClear(){
    Fixture f;SelectedMeshesObservation c;CHECK(Install(c,f));CHECK(c.Observe(Source(f),Now));
    const auto original=c.Read(f.owner,Now+1);CHECK(original&&c.ReadCurrent(Now+1)==original);
    f.Put(f.meshNames[0],std::uint8_t{'o'});f.triggerCount=0;
    CHECK(c.Observe(Source(f,2,Now+100000000),Now+100000000));
    const auto newer=c.Read(f.owner,Now+100000001);CHECK(newer&&newer!=original);
    CHECK(!std::strcmp(original->states[0].meshes[0].assetPath.data(),Spas));
    CHECK(newer->states[0].meshes[0].kind==SelectedMeshKind::Unknown);
    c.Clear();CHECK(!c.Read(f.owner,Now+100000001)&&!c.ReadCurrent(Now+100000001));CHECK(original->owner==f.owner&&original->sequence==1);
    CHECK(!c.Observe(Source(f,2,Now+100000000),Now+100000001));return 0;
}
int CrossSourceAndResolverRejects(){
    for(unsigned n=0;n<6;++n){Fixture f;SelectedMeshesObservation c;CHECK(Install(c,f));auto s=Source(f);
        if(n==0)++s.config.weaponData;if(n==1)++s.inventory;if(n==2)++s.selectedSlot;if(n==3)s.config.assetName[0]='x';
        if(n==4)f.Put(f.owner.player+0xc68,0u);if(n==5)f.Put(Base+0x2900,0xc308418bu);
        CHECK(!c.Observe(s,Now)&&!c.Read(f.owner,Now+1));
        CHECK(c.Counters().attempts==1&&c.Counters().readCalls>0&&c.Counters().readBytes>0);
    }return 0;
}
int CadenceAndDurationBudget(){
    Fixture f;SelectedMeshesObservation c;CHECK(Install(c,f));CHECK(c.Observe(Source(f),Now));
    const auto reads=f.callbackCalls;
    CHECK(!c.Observe(Source(f,2,Now+50000000),Now+50000000)&&f.callbackCalls==reads);
    CHECK(c.Read(f.owner,Now+50000000));
    CHECK(!c.Observe(Source(f,2,Now+100000000),Now+299999999));
    CHECK(c.Counters().status==SelectedObservationStatus::ExpiredDuringRead&&!c.Read(f.owner,Now+150000000));
    const auto counters=c.Counters();CHECK(counters.attempts==2&&counters.published==1&&counters.throttled==1);
    CHECK(counters.readCalls>0&&counters.readBytes>0&&counters.lastDurationNs>0&&counters.maxDurationNs>=counters.lastDurationNs);
    std::ostringstream report;c.Report(report);CHECK(report.str().find("\"configured_only\":true")!=std::string::npos);
    CHECK(report.str().find("\"render_authority\":false")!=std::string::npos);return 0;
}
int ClearDuringConcurrentObservation(){
    Fixture f;SelectedMeshesObservation c;CHECK(Install(c,f));CHECK(c.Observe(Source(f),Now));
    const auto first=c.Read(f.owner,Now+1);CHECK(first);f.triggerCount=0;
    std::promise<void> entered,release;auto ready=entered.get_future();auto resume=release.get_future().share();
    f.mutation=[&](Fixture&){entered.set_value();resume.wait();};
    bool observed=true;std::thread worker([&]{observed=c.Observe(Source(f,2,Now+100000000),Now+100000000);});
    ready.wait();const bool retained=c.Read(f.owner,Now+100000001)==first;
    c.Clear();const bool cleared=!c.Read(f.owner,Now+100000001);release.set_value();worker.join();
    CHECK(retained&&cleared&&!observed&&!c.Read(f.owner,Now+100000001)&&!c.ReadCurrent(Now+100000001));CHECK(first->sequence==1);return 0;
}
int DiagnosticObservationLimit(){
    Fixture f;SelectedMeshesObservation c;CHECK(Install(c,f));
    for(std::uint64_t n=0;n<c.MaxObservations;++n){const auto at=Now+std::int64_t(n)*c.CadenceNs;
        CHECK(c.Observe(Source(f,n+1,at),at));}
    const auto reads=f.callbackCalls;c.Clear();const auto at=Now+std::int64_t(c.MaxObservations)*c.CadenceNs;
    CHECK(!c.Observe(Source(f,c.MaxObservations+1,at),at));
    CHECK(c.Counters().status==SelectedObservationStatus::BudgetExhausted&&f.callbackCalls==reads);return 0;
}
int ExplicitPersistentStillBoundsEveryRead(){
    Fixture f;SelectedMeshesObservation c;CHECK(c.Install(f.file,f.pe,Base,{&f,Fixture::Read,nullptr},true,true));
    for(std::uint64_t n=0;n<c.MaxObservations+2;++n){const auto at=Now+std::int64_t(n)*c.CadenceNs;
        CHECK(c.Observe(Source(f,n+1,at),at));}
    const auto at=Now+std::int64_t(c.MaxObservations+1)*c.CadenceNs;
    const auto s=c.Read(f.owner,at);CHECK(s&&s->deadlineNs==at+c.LeaseNs&&c.Counters().persistent);
    const auto calls=f.callbackCalls;CHECK(!c.Observe(Source(f,500,at+1),at+1)&&f.callbackCalls==calls);
    CHECK(!c.Read(f.owner,at+c.LeaseNs));
    CHECK(c.Counters().lastReadCalls<=131072&&c.Counters().lastReadBytes<=2097152);
    std::ostringstream report;c.Report(report);CHECK(report.str().find("\"persistent\":true")!=std::string::npos);return 0;
}
int SharedSourceCadenceAvoidsCoarseClockLeaseGap(){
    Fixture f;SelectedMeshesObservation c;CHECK(Install(c,f));
    constexpr auto initial=Now;std::int64_t producerLast=0;
    CHECK(c.SourceObservationDue(producerLast,initial));producerLast=initial;
    CHECK(c.Observe(Source(f,15,initial),initial));
    const auto old=c.Read(f.owner,initial);CHECK(old&&old->deadlineNs==initial+c.LeaseNs);
    // Actual224811 interval: coarse GetTickCount called the producer at100ms,
    // but original QPC source interval was only98.3179ms. Do not consume that
    // producer slot; a later Gather can publish at the true100ms boundary.
    constexpr auto early=initial+98317900;
    CHECK(!c.SourceObservationDue(producerLast,early));CHECK(c.Read(f.owner,early)==old);
    constexpr auto next=initial+100100000;CHECK(c.SourceObservationDue(producerLast,next));producerLast=next;
    CHECK(c.Observe(Source(f,21,next),next));const auto fresh=c.Read(f.owner,next);
    CHECK(fresh&&fresh->observedNs==next&&fresh->deadlineNs==next+c.LeaseNs);
    CHECK(old->observedNs==initial&&old->deadlineNs==initial+c.LeaseNs);
    CHECK(!c.Read(f.owner,next-1)); // processing now must follow the actual publication
    CHECK(c.Read(f.owner,initial+203053500)==fresh); // observed failure time in original gap
    CHECK(c.Counters().throttled==0&&c.Counters().published==2);
    CHECK(!c.SourceObservationDue(producerLast,next-1)&&!c.SourceObservationDue(-1,next));
    CHECK(!c.SourceObservationDue(0,0)&&!c.SourceObservationDue(producerLast,next+99999999));
    CHECK(c.SourceObservationDue(producerLast,next+100000000));return 0;
}
}
int NativeConfigurationTelemetry(){
    Fixture f;SelectedMeshesObservation c;const auto pointer=f.Text("Objects/Weapons/Configuration");f.Put(f.data+0x40,pointer);CHECK(Install(c,f));
    CHECK(c.Observe(Source(f),Now));const auto snapshot=c.Read(f.owner,Now+1);CHECK(snapshot&&snapshot->configurationPathVerified&&snapshot->configurationPathPointer==pointer);
    CHECK(c.Observe(Source(f,2,Now+100000000),Now+100000000));
    std::ostringstream out;c.Report(out);const auto text=out.str();
    CHECK(text.find("\"configuration_path_verified\":true")!=std::string::npos);
    CHECK(CompleteOperationBinding(snapshot->operationBinding));
    CHECK(text.find("\"operation_binding\":{\"content_fingerprint_fnv64\":"+std::to_string(snapshot->operationBinding.executableFingerprint))!=std::string::npos);
    CHECK(text.find("\"executable_bytes\":"+std::to_string(snapshot->operationBinding.executableBytes))!=std::string::npos);
    CHECK(text.find("\"configuration_path\":\"Objects/Weapons/Configuration\"")!=std::string::npos);
    CHECK(text.find("\"first_sequence\":1,\"observations\":2,\"sequence\":2")!=std::string::npos);
    CHECK(text.find("\"weapon_data\":"+std::to_string(f.data))!=std::string::npos);
    c.Clear();std::ostringstream retired;c.Report(retired);CHECK(retired.str().find("Objects/Weapons/Configuration")!=std::string::npos);CHECK(!c.ReadCurrent(Now+100000001));
    for(unsigned n=0;n<10;++n){const auto value="Objects/Weapons/Config"+std::to_string(n);f.Put(f.data+0x40,f.Text(value.c_str()));const auto at=Now+(n+2)*100000000ll;CHECK(c.Observe(Source(f,n+3,at),at));}
    std::ostringstream bounded;c.Report(bounded);CHECK(bounded.str().find("\"configuration_identity_capacity\":8,\"configuration_identity_dropped\":3")!=std::string::npos);
    return 0;
}
int SourceConfigurationPathCohort(){
    for(unsigned variant=0;variant<4;++variant){
        Fixture f;SelectedMeshesObservation c;f.Put(f.data+0x40,f.Text("Objects/Weapons/Configuration"));CHECK(Install(c,f));
        auto source=Source(f);
        if(variant==0)std::strcpy(source.config.assetPath.data(),"Objects/Weapons/Configuration");
        if(variant==1)std::strcpy(source.config.assetPath.data(),"Objects/Weapons/OtherConfiguration");
        if(variant==2)source.config.assetPath.fill('x');
        const bool accepted=c.Observe(source,Now);
        CHECK(accepted==(variant==0||variant==3));
        if(!accepted){CHECK(c.Counters().status==SelectedObservationStatus::SourceMismatch);CHECK(!c.ReadCurrent(Now));}
    }
    return 0;
}
int main(){for(auto test:{SourceConfigurationPathCohort,NativeConfigurationTelemetry,DisabledAndOwnedBinding,LeaseAndAllOwnerFields,ImmutablePublicationAndClear,CrossSourceAndResolverRejects,
    CadenceAndDurationBudget,ClearDuringConcurrentObservation,DiagnosticObservationLimit,ExplicitPersistentStillBoundsEveryRead,SharedSourceCadenceAvoidsCoarseClockLeaseGap})if(test())return 1;
    std::puts("Eleven configured-mesh observer groups passed: source path cohort, native telemetry and original observer guards.");return 0;}
