#define main ExistingSelectedMeshesMain
#include "Bc2SelectedMeshes1pTests.cpp"
#undef main
#include "Bc2SelectedMeshesObservation.h"
static_assert(!std::is_convertible_v<CarriedMeshConfiguration,SelectedMeshesSnapshot>);
namespace {
CarriedMeshesResult Carry(Fixture& f,bool enabled=true){return ReadCarriedMeshes1p({&f,Fixture::Read,nullptr},f.binding,Base,f.owner,
    f.inventory,1,Heap+0x4200,f.data,19,2,Now,Now+100000000,enabled);}
void Prepare(Fixture& f){f.Put(Heap+0x4204,f.data);f.Put(f.data+0x64,19u);}
int ActualCarriedReader(){Fixture f;Prepare(f);const auto s=Carry(f);CHECK(s.snapshot&&s.snapshot->weapon==Heap+0x4200);
    CHECK(s.snapshot->configured.owner==f.owner&&s.snapshot->configured.owner.weapon!=s.snapshot->weapon);
    CHECK(s.snapshot->configured.weaponData==f.data&&s.snapshot->nativeSlot==1&&s.snapshot->persistence==19);
    CHECK(!s.snapshot->configured.renderSuppressionAllowed&&!s.snapshot->configured.activeStateVerified);return 0;}
int MembershipAndIdentity(){for(unsigned n=0;n<6;++n){Fixture f;Prepare(f);
    if(n==0)f.Put(f.items+4,Heap+0x4300);if(n==1)f.Put(Heap+0x4204,f.data+0x100);if(n==2)f.Put(f.data+0x64,20u);
    if(n==3)f.Put(f.inventory+0x14c,1u);if(n==4)f.Put(f.owner.player+0xc68,Heap+0x1234);if(n==5)f.Put(Base+0x1300,std::uint8_t{0x90});
    CHECK(!Carry(f).snapshot);}return 0;}
int OriginalWindowAndUnknown(){Fixture f;Prepare(f);const auto off=Carry(f,false);CHECK(!off.snapshot&&f.callbackCalls==0);
    const auto result=Carry(f);CHECK(result.snapshot&&result.snapshot->configured.observedNs==Now&&result.snapshot->configured.deadlineNs==Now+100000000);
    f.Put(Heap+0xb000+12,f.Text("Objects/Unknown/Mesh"));const auto unknown=Carry(f);CHECK(unknown.snapshot&&unknown.snapshot->configured.states[0].meshes[0].kind==SelectedMeshKind::Unknown);return 0;}
struct Changing {Fixture f;unsigned reads=0;bool config=false;
    static bool Read(void* x,unsigned at,void* out,std::size_t n){auto& c=*static_cast<Changing*>(x);
        if(at==Heap+0x4204&&++c.reads==2){if(c.config)c.f.Put(c.f.data+0x64,20u);else c.f.Put(c.f.items+4,Heap+0x4300);}
        return Fixture::Read(&c.f,at,out,n);}
};
int RepeatedCohortRejects(){for(bool config:{false,true}){Changing c;Prepare(c.f);c.config=config;
    const auto r=ReadCarriedMeshes1p({&c,Changing::Read,nullptr},c.f.binding,Base,c.f.owner,c.f.inventory,1,Heap+0x4200,c.f.data,19,2,Now,Now+100000000,true);
    CHECK(!r.snapshot&&r.status==SelectedMeshesStatus::ChangedDuringRead);}return 0;}
int OriginalCachedLease(){Fixture f;Prepare(f);SelectedMeshesObservation observer;
    CHECK(observer.Install(f.file,f.pe,Base,{&f,Fixture::Read,nullptr},true));
    const auto read=[&](std::uint64_t sequence,std::int64_t observed,std::int64_t now){return observer.ReadCarried(f.owner,f.inventory,1,Heap+0x4200,f.data,19,sequence,observed,now);};
    const auto first=read(1,Now,Now);CHECK(first&&first->configured.deadlineNs==Now+200000000);
    const auto cached=read(2,Now+10000000,Now+10000000);CHECK(cached==first&&cached->configured.observedNs==Now);
    observer.Clear();const auto second=read(3,Now+20000000,Now+20000000);CHECK(second&&second!=first&&first->configured.deadlineNs==Now+200000000);
    CHECK(!read(4,Now,Now+100000000));return 0;}
}
int main(){for(auto fn:{ActualCarriedReader,MembershipAndIdentity,OriginalWindowAndUnknown,RepeatedCohortRejects,OriginalCachedLease})if(fn())return 1;
    std::cout<<"5 carried configured-mesh reader/cache groups passed\n";return 0;}
