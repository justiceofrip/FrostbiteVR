#include "Test.h"
#include "fvr/interaction/VehicleTracking.h"
#include "Bc2VehicleRoutes.h"
#include "Bc2VehicleCommit.h"
#include "Bc2BoatProfile.h"
#include <vector>
#include <bit>
#include <iostream>
#include <map>
#include <string>
using namespace fvr;using namespace fvr::bc2;using namespace fvr::interaction;
namespace {
math::Matrix4 Identity(){math::Matrix4 m{};for(unsigned n=0;n<4;++n)m.values[n][n]=1;return m;}
bool Same(const math::Matrix4& a,const math::Matrix4& b){for(unsigned r=0;r<4;++r)for(unsigned c=0;c<4;++c)if(!Near(a.values[r][c],b.values[r][c],.0003f))return false;return true;}
int SeatReferenceAndMotion(){
    VehicleCameraAnchor anchor;VehicleTrackingOwner owner{1,2,3,4};auto native=Identity();native.values[3]={10,20,30,1};
    math::Pose reference{},head{};head.position={1.2f,.3f,-1.7f};head.orientation={0,std::sin(.4f),0,std::cos(.4f)};
    const auto base=anchor.Base(owner,native,reference,head,1,1);CHECK(base);
    const auto centered=math::ComposeRuntimeHeadWithLhCamera(*base,reference,head,1);CHECK(centered&&Same(*centered,native));
    const auto broken=math::ComposeRuntimeHeadWithLhCamera(native,reference,head,1);CHECK(broken&&!Same(*broken,native));
    auto movedNative=Identity();movedNative.values[0]={0,0,-1,0};movedNative.values[2]={1,0,0,0};movedNative.values[3]={50,60,70,1};
    const auto moved=anchor.Base(owner,movedNative,reference,head,1,1);CHECK(moved);
    const auto follows=math::ComposeRuntimeHeadWithLhCamera(*moved,reference,head,1);CHECK(follows&&Same(*follows,movedNative));
    auto leaned=head;leaned.position.x+=.12f;const auto leanBase=anchor.Base(owner,movedNative,reference,leaned,1,1);CHECK(leanBase&&Same(*leanBase,*moved));
    auto left=leaned,right=leaned;left.position.x-=.032f;right.position.x+=.032f;
    const auto l=math::ComposeRuntimeHeadWithLhCamera(*leanBase,reference,left,1),r=math::ComposeRuntimeHeadWithLhCamera(*leanBase,reference,right,1);CHECK(l&&r);
    float squared=0;for(unsigned a=0;a<3;++a)squared+=(l->values[3][a]-r->values[3][a])*(l->values[3][a]-r->values[3][a]);CHECK(Near(std::sqrt(squared),.064f));
    // Recenter and switching seats discard the preceding seat reference.
    const auto recentered=anchor.Base(owner,native,reference,leaned,2,1);CHECK(recentered&&Same(*math::ComposeRuntimeHeadWithLhCamera(*recentered,reference,leaned,1),native));
    ++owner.seat;const auto switched=anchor.Base(owner,native,reference,head,2,1);CHECK(switched&&Same(*math::ComposeRuntimeHeadWithLhCamera(*switched,reference,head,1),native));return 0;
}
int KnownSeatNeverFallsThroughOnInputGap(){
    VehicleCameraSample previous;VehicleTrackingOwner owner{1,2,3,4};
    VehicleCameraSample current{owner,1,true,true,1000000000,1100000000};
    CHECK(PublishVehicleCameraSample(previous,current));
    CHECK(AdmitVehicleCamera(previous,1,3,4,1,1000000000)==VehicleCameraAdmission::Ready);
    auto absent=current;absent.live=false;absent.space=0;absent.observedNs=absent.deadlineNs=0;
    CHECK(!PublishVehicleCameraSample(previous,absent)&&previous.space==1);
    CHECK(AdmitVehicleCamera(previous,1,3,4,1,1000000001)==VehicleCameraAdmission::Wait);
    CHECK(previous.observedNs==0&&previous.deadlineNs==0); // Identity continuity is not a lease renewal.
    CHECK(PublishVehicleCameraSample(previous,{}));
    CHECK(AdmitVehicleCamera(previous,1,3,4,1,1000000001)==VehicleCameraAdmission::Unsupported);
    return 0;
}
int TemporaryInputGapPreservesSeatedLean(){
    VehicleCameraAnchor anchor;VehicleCameraSample previous;
    const VehicleTrackingOwner owner{1,2,3,4};
    VehicleCameraSample input{owner,1,true,true,1000000000,1100000000};
    if(PublishVehicleCameraSample(previous,input))anchor.Reset();
    auto native=Identity();native.values[3]={10,20,30,1};math::Pose reference{},head{};head.position={.6f,.2f,0};
    const auto initial=anchor.Base(owner,native,reference,head,1,1);CHECK(initial&&anchor.Ready());
    auto missing=input;missing.live=false;missing.space=0;missing.observedNs=missing.deadlineNs=0;
    if(PublishVehicleCameraSample(previous,missing))anchor.Reset();CHECK(anchor.Ready());
    CHECK(AdmitVehicleCamera(previous,1,3,4,1,1010000000)==VehicleCameraAdmission::Wait);
    input.observedNs=1020000000;input.deadlineNs=1120000000;
    if(PublishVehicleCameraSample(previous,input))anchor.Reset();CHECK(anchor.Ready());
    head.position.x+=.12f;const auto base=anchor.Base(owner,native,reference,head,1,1);CHECK(base&&Same(*base,*initial));
    const auto view=math::ComposeRuntimeHeadWithLhCamera(*base,reference,head,1);CHECK(view);
    CHECK(Near(view->values[3][0]-native.values[3][0],.12f)); // Recovery must not silently recenter the lean.
    const auto unanchored=math::ComposeRuntimeHeadWithLhCamera(native,reference,head,1);CHECK(unanchored);
    CHECK(std::abs(unanchored->values[3][0]-view->values[3][0])>.5f); // Old fallback would jump by the inherited offset.
    return 0;
}
int VehicleCameraDeadlineAndOwnerChanges(){
    VehicleCameraSample input{{1,2,3,4},1,true,true,1000000000,1100000000};
    for(unsigned bad=0;bad<7;++bad){auto value=input;auto now=1000000001ll;std::uint64_t actor=1,vehicle=3,seat=4,space=1;
        if(bad==0)now=value.deadlineNs;if(bad==1)now=value.observedNs-1;if(bad==2)++actor;
        if(bad==3)++vehicle;if(bad==4)++seat;if(bad==5)++space;if(bad==6)value.deadlineNs=value.observedNs+150000001;
        CHECK(AdmitVehicleCamera(value,actor,vehicle,seat,space,now)==VehicleCameraAdmission::Wait);
    }
    auto previous=input;auto recentered=input;++recentered.space;CHECK(PublishVehicleCameraSample(previous,recentered));
    auto changed=recentered;++changed.owner.seat;CHECK(PublishVehicleCameraSample(previous,changed));
    auto lostProfile=changed;lostProfile.supported=false;CHECK(PublishVehicleCameraSample(previous,lostProfile));
    CHECK(AdmitVehicleCamera(previous,1,3,5,2,1000000001)==VehicleCameraAdmission::Unsupported);
    return 0;
}
InputFrame Frame(){InputFrame f;f.generation=1;f.spaceGeneration=1;f.predictedNs=1000000000;f.focused=f.headValid=true;for(auto& h:f.hands){h.gripTracked=h.aimTracked=true;h.active=Stick|Trigger|Primary;}return f;}
int ContinuousAxesAndRecovery(){
    VehicleControllerActions policy;auto f=Frame();VehicleControlOwner owner{{1,2,3,4},1};
    f.hands[0].stickY=1;f.hands[1].stickX=.75f;f.hands[1].trigger=.8f;
    CHECK(!policy.Update(f,owner,true,true).active); // Must neutralize after entering.
    ++f.generation;for(auto& h:f.hands){h.stickX=h.stickY=h.trigger=0;}auto neutral=policy.Update(f,owner,true,true);CHECK(neutral.armedLeft&&neutral.armedRight);
    ++f.generation;f.hands[0].stickY=1;f.hands[0].stickX=-.5f;f.hands[1].stickY=.6f;f.hands[1].stickX=.75f;f.hands[1].trigger=.8f;f.hands[0].held=Primary;
    f.head.orientation={0,std::sin(.7f),0,std::cos(.7f)};auto active=policy.Update(f,owner,true,true);
    CHECK(active.active&&Near(active.axes.throttle,1)&&active.axes.steer<0&&active.axes.lookYaw>0&&active.axes.lookPitch>0&&Near(active.axes.fire,.8f)&&active.axes.exit);
    ++f.generation;auto held=policy.Update(f,owner,true,true);CHECK(Near(held.axes.lookYaw,active.axes.lookYaw)); // no one-shot snap/flick
    f.hands[0].gripTracked=false;auto loss=policy.Update(f,owner,true,true);CHECK(loss.axes.throttle==0&&!loss.axes.exit&&Near(loss.axes.fire,.8f));
    ++f.generation;f.hands[0].gripTracked=true;auto heldReturn=policy.Update(f,owner,true,true);CHECK(heldReturn.axes.throttle==0&&Near(heldReturn.axes.fire,.8f));
    auto stale=policy.Update(f,owner,true,false);CHECK(!stale.active&&stale.axes.fire==0);
    ++f.generation;CHECK(!policy.Update(f,owner,true,true).active);return 0;
}
int DuplicateActionValidityLoss(){
    VehicleControllerActions policy;auto f=Frame();VehicleControlOwner owner{{1,2,3,4},1};policy.Update(f,owner,true,true);
    ++f.generation;f.hands[0].stickY=.8f;f.hands[0].stickX=.5f;f.hands[0].held=Primary;
    f.hands[1].stickX=.6f;f.hands[1].stickY=.4f;f.hands[1].trigger=.9f;
    const auto active=policy.Update(f,owner,true,true);CHECK(active.axes.throttle>0&&active.axes.steer>0&&active.axes.exit&&active.axes.lookYaw>0&&active.axes.fire>0);
    f.hands[0].active&=~Stick;f.hands[0].stickX=f.hands[0].stickY=0;auto lost=policy.Update(f,owner,true,true);CHECK(lost.axes.throttle==0&&lost.axes.steer==0&&lost.axes.exit&&lost.axes.fire>0);
    f.hands[0].active&=~Primary;f.hands[0].held&=~Primary;lost=policy.Update(f,owner,true,true);CHECK(!lost.axes.exit&&lost.axes.throttle==0);
    f.hands[1].active&=~Stick;f.hands[1].stickX=f.hands[1].stickY=0;lost=policy.Update(f,owner,true,true);CHECK(lost.axes.lookYaw==0&&lost.axes.lookPitch==0&&lost.axes.fire>0);
    f.hands[1].active&=~Trigger;f.hands[1].trigger=0;lost=policy.Update(f,owner,true,true);CHECK(lost.axes.fire==0);
    // Re-enabling an action on the same sequence cannot resurrect a cancelled
    // cached value. A subsequent fresh sample supplies its new value.
    for(auto& h:f.hands)h.active=Stick|Trigger|Primary;
    const auto repeated=policy.Update(f,owner,true,true);CHECK(repeated.axes.throttle==0&&!repeated.axes.exit&&repeated.axes.fire==0);
    ++f.generation;f.hands[0].stickY=.8f;f.hands[0].held=Primary;f.hands[1].trigger=.9f;const auto renewed=policy.Update(f,owner,true,true);CHECK(renewed.axes.throttle>0&&renewed.axes.exit&&renewed.axes.fire>0);return 0;
}
struct Memory {
    static constexpr unsigned Base=0x10000;std::vector<std::byte> bytes=std::vector<std::byte>(0x20000);
    VehicleRouteOwner owner{0x11000,0x12000,0x14000,0x16000,1,2};VehicleRouteBinding binding{0x500000,0x500100,0x500200,true};
    unsigned entity=0x17000,entry=0x18000,router=0x19000,cache=0x1a000,buckets=0x1b000,entries=0x1c000;bool mutate=false;unsigned reads=0;
    void Put(unsigned at,unsigned value){std::memcpy(bytes.data()+at-Base,&value,4);}
    Memory(){Put(owner.manager,binding.managerVtable);Put(owner.manager+0xb4,owner.player);Put(owner.player+0xc54,owner.weak);Put(owner.weak,owner.soldier+4);Put(owner.soldier+0x220,owner.player);bytes[owner.player+0xccd-Base]=std::byte{8};Put(owner.player+0xc68,entity);Put(owner.player+0xc74,cache);Put(entity+0x7c,entries);Put(entity+0x80,entries+4);Put(entries,entry);Put(entry+0x198,router);Put(router,binding.routerVtable);Put(cache,binding.cacheVtable);Put(cache+0x98,0x711);Put(router+0x14,buckets);Put(router+0x18,53);
        unsigned n=0;for(auto pair:{std::pair{0u,0u},std::pair{4u,6u},std::pair{8u,9u},std::pair{9u,51u},std::pair{10u,52u},std::pair{16u,36u},std::pair{49u,53u}}){const auto address=0x20000+n++*16;Put(buckets+4*pair.first,address);Put(address,pair.first);Put(address+4,pair.second);}}
    VehicleRouteMemory Access(){return {this,[](void* ctx,unsigned at,void* dst,std::size_t n){auto& m=*static_cast<Memory*>(ctx);if(at<Base||std::uint64_t(at)+n>Base+m.bytes.size())return false;if(m.mutate&&at==0x20000&&++m.reads==2)m.Put(0x20004,1);std::memcpy(dst,m.bytes.data()+at-Base,n);return true;},[](void* ctx,unsigned at,const char* type){auto& m=*static_cast<Memory*>(ctx);return (at==m.entity&&!std::strcmp(type,"ClientVehicleEntity"))||(at==m.owner.soldier&&!std::strcmp(type,"ClientSoldierEntity"));}};}
};
int ExactNativeRouteReader(){Memory m;auto result=ReadVehicleRoutes(m.Access(),m.binding,m.owner);CHECK(result.status==VehicleRouteStatus::Okay&&result.snapshot.count==7&&result.snapshot.routes[6].action==49&&result.snapshot.permissionMask==0x711);
    Memory same;same.Put(same.buckets,0x22000);same.Put(0x22000,0);same.Put(0x22004,0);CHECK(ReadVehicleRoutes(same.Access(),same.binding,same.owner).snapshot.fingerprint==result.snapshot.fingerprint);
    for(unsigned variant=0;variant<6;++variant){Memory bad;
        if(variant==0)bad.Put(0x20008,0x20000);if(variant==1)bad.Put(0x20000,1);if(variant==2)bad.Put(0x20004,77);if(variant==3)bad.Put(bad.router+0x18,257);if(variant==4)bad.Put(bad.owner.player+0xc68,bad.owner.soldier);if(variant==5)bad.mutate=true;
        CHECK(ReadVehicleRoutes(bad.Access(),bad.binding,bad.owner).status!=VehicleRouteStatus::Okay);}
    return 0;
}
void MakeFoot(Memory& m){m.Put(m.owner.player+0xc68,m.owner.soldier);m.Put(m.owner.soldier+0x7c,m.entries);m.Put(m.owner.soldier+0x80,m.entries+4);
    m.Put(m.buckets+27*4,0x22000);m.Put(0x22000,27);m.Put(0x22004,36);}
int ExactOnFootSharedConcept(){Memory m;MakeFoot(m);const auto r=ReadOnFootRoutes(m.Access(),m.binding,m.owner);
    CHECK(r.status==VehicleRouteStatus::Okay&&HasOnFootContextUseVehicleAlias(r.snapshot));
    CHECK(r.snapshot.identity.controlled==m.owner.soldier&&r.snapshot.identity.entry==m.entry&&r.snapshot.identity.cache==m.cache);
    CHECK(ReadVehicleRoutes(m.Access(),m.binding,m.owner).status==VehicleRouteStatus::NotVehicle);
    for(unsigned variant=0;variant<9;++variant){Memory bad;MakeFoot(bad);
        if(variant==0)bad.Put(0x22004,28); // A distinct interaction concept is not an alias.
        if(variant==1)bad.Put(0x20000+5*16+4,28); // ChangeVehicle changed independently.
        if(variant==2)bad.Put(bad.buckets+27*4,0);
        if(variant==3)bad.Put(bad.owner.player+0xc60,bad.entity);
        if(variant==4)bad.Put(bad.owner.player+0xc68,bad.entity);
        if(variant==5)bad.Put(bad.router,bad.binding.routerVtable+4);
        if(variant==6)bad.binding.gatherCodeVerified=false;
        if(variant==7)bad.mutate=true;
        if(variant==8)bad.Put(bad.owner.weak,bad.owner.soldier+8);
        const auto x=ReadOnFootRoutes(bad.Access(),bad.binding,bad.owner);
        CHECK(x.status!=VehicleRouteStatus::Okay||!HasOnFootContextUseVehicleAlias(x.snapshot));}
    auto malformed=r.snapshot;malformed.routes[malformed.count++]=VehicleRoute{27,36};CHECK(!HasOnFootContextUseVehicleAlias(malformed));
    return 0;
}
int CommitExactPermissions(){
    std::array<std::byte,InputBytes> cache{};auto put=[&](unsigned at,unsigned v){std::memcpy(cache.data()+at,&v,4);};auto get=[&](unsigned at){unsigned v;std::memcpy(&v,cache.data()+at,4);return v;};
    put(0x98,0x711);const auto original=cache;VehicleInputPlan plan;plan.status=VehicleInputPlanStatus::Ready;plan.count=3;plan.edits[0]={8,0,std::bit_cast<unsigned>(.5f)};plan.edits[1]={24,0,std::bit_cast<unsigned>(-.3f)};plan.edits[2]={0x98,0x711,0x10711};
    {VehicleInputOverride edit;CHECK(edit.Apply(cache,plan));CHECK(get(8)==plan.edits[0].after);CHECK(edit.Restore());CHECK(cache==original);}
    {VehicleInputOverride edit;auto bad=plan;bad.edits[1].before=1;CHECK(!edit.Apply(cache,bad)&&cache==original);bad=plan;bad.edits[1].offset=12;CHECK(!edit.Apply(cache,bad)&&cache==original);}
    {VehicleInputOverride edit;CHECK(edit.Apply(cache,plan));put(0x98,0x10710);put(8,std::bit_cast<unsigned>(.9f));CHECK(!edit.Restore());CHECK(get(8)==std::bit_cast<unsigned>(.9f)&&get(0x98)==0x710);}
    cache=original;{VehicleInputOverride edit;CHECK(edit.Apply(cache,plan));edit.Commit();}CHECK(get(8)==plan.edits[0].after&&get(0x98)==0x10711);return 0;
}
// Synthetic reflected ownership graph, not captured proprietary object bytes.
struct BoatMemory {
    std::vector<std::byte> bytes=std::vector<std::byte>(0x100000);unsigned cursor=0x20000;
    std::map<std::string,unsigned> kinds;std::map<unsigned,std::string> objectTypes;
    VehicleRouteSnapshot seat{};unsigned data=0,entry=0,mesh=0,mapping=0,mapRows=0,meshName=0;
    unsigned Alloc(unsigned n){const auto at=cursor;cursor+=(n+15)&~15u;return at;}
    void Put(unsigned at,unsigned value){std::memcpy(bytes.data()+at,&value,4);}
    unsigned Text(const char* name){const auto at=Alloc(unsigned(std::strlen(name))+1);std::memcpy(bytes.data()+at,name,std::strlen(name)+1);return at;}
    struct Field {const char* name;unsigned offset;const char* kind;};
    unsigned Kind(const char* name,unsigned parent=0,unsigned size=1024,std::initializer_list<Field> fields={}){
        if(auto found=kinds.find(name);found!=kinds.end())return found->second;
        const auto info=Alloc(64),meta=Alloc(32),rows=Alloc(unsigned(fields.size())*24+4);kinds[name]=info;
        Put(info+4,meta);Put(info+20,parent);Put(info+36,rows);Put(meta,Text(name));Put(meta+4,0x35|(size<<16));bytes[meta+13]=std::byte(fields.size());
        unsigned n=0;for(const auto& f:fields){Put(rows+n*24,Text(f.name));Put(rows+n*24+8,Kind(f.kind));Put(rows+n*24+16,f.offset);++n;}return info;
    }
    unsigned Object(const char* name,unsigned info){const auto object=Alloc(1024),table=Alloc(16),getter=Alloc(16);Put(object,table);Put(table+8,getter);bytes[getter]=std::byte{0xb8};Put(getter+1,info);bytes[getter+5]=std::byte{0xc3};objectTypes[object]=name;return object;}
    BoatMemory(){
        const auto common=Kind("GameObjectData",0,64,{{"Name",12,"String"}});
        const auto vehicle=Kind("VehicleEntityData",common,608,{{"Mesh",500,"CompositeMeshAsset"}});
        const auto entryBase=Kind("EntryComponentData",common,240,{{"EntryOrderNumber",176,"Int32"},{"EntryClass",184,"EntryClass"},{"InputMapping",192,"InputActionMappingsData"}});
        const auto playerEntry=Kind("PlayerEntryComponentData",entryBase,256);
        // The class names above occur as field types first; replace their
        // placeholder metadata with concrete reflected layout for objects.
        kinds.erase("InputActionMappingsData");const auto mappingType=Kind("InputActionMappingsData",0,32,{{"Mappings",12,"ArrayBase"}});
        const auto rowType=Kind("EntryInputActionMappingData",0,20,{{"ConceptIdentifier",12,"InputConceptIdentifiers"},{"ActionIdentifier",16,"EntryInputActionEnum"}});
        data=Object("VehicleEntityData",vehicle);entry=Object("PlayerEntryComponentData",playerEntry);mesh=Object("CompositeMeshAsset",kinds["CompositeMeshAsset"]);mapping=Object("InputActionMappingsData",mappingType);
        seat.identity.controlled=Alloc(1024);seat.identity.entry=Alloc(1024);Put(seat.identity.controlled+12,data);Put(seat.identity.entry+12,entry);
        Put(data+12,Text("PBLB"));Put(data+500,mesh);Put(entry+12,Text("Entry Driver"));Put(entry+192,mapping);meshName=Text("Objects/Vehicles/Sea/Pbl/Pbl_Mesh");Put(mesh+12,meshName);
        constexpr std::array<VehicleRoute,30> rows{{{0,0},{1,6},{2,37},{4,1},{5,7},{6,8},{8,9},{9,51},{10,52},{12,13},{13,35},{14,13},{16,36},{17,42},{18,43},{19,44},{20,45},{21,46},{22,47},{23,48},{30,14},{39,59},{40,56},{41,75},{42,74},{43,61},{44,62},{45,68},{46,69},{47,70}}};
        seat.count=unsigned(rows.size());seat.fingerprint=1;std::copy(rows.begin(),rows.end(),seat.routes.begin());mapRows=Alloc(120);Put(mapping+24,30);Put(mapping+28,mapRows);
        for(unsigned n=0;n<rows.size();++n){const auto row=Object("EntryInputActionMappingData",rowType);Put(mapRows+4*n,row);Put(row+12,rows[n].conceptId);Put(row+16,rows[n].action);}
    }
    VehicleRouteMemory Access(){return {this,[](void* p,unsigned at,void* out,std::size_t n){auto& m=*static_cast<BoatMemory*>(p);if(std::uint64_t(at)+n>m.bytes.size())return false;std::memcpy(out,m.bytes.data()+at,n);return true;},[](void* p,unsigned at,const char* name){auto& m=*static_cast<BoatMemory*>(p);const auto found=m.objectTypes.find(at);return found!=m.objectTypes.end()&&found->second==name;}};}
};
int ExactBoatProfile(){
    BoatMemory good;const auto profile=ReadPblDriverProfile(good.Access(),good.seat);CHECK(profile&&profile->role==VehicleSeatRole::Driver&&profile->axes[0]==EntryAction::Throttle&&profile->axes[1]==EntryAction::Yaw&&profile->exitVerified);
    CHECK(!profile->axes[2]&&!profile->axes[3]&&!profile->axes[4]);
    for(unsigned variant=0;variant<7;++variant){BoatMemory bad;
        if(variant==0)bad.seat.slot=1;if(variant==1)bad.seat.routes[3].conceptId=6;if(variant==2)bad.Put(bad.entry+176,1);if(variant==3)bad.bytes[bad.meshName]=std::byte{'X'};
        if(variant==4)bad.Put(bad.mapping+24,29);if(variant==5){unsigned row=0;std::memcpy(&row,bad.bytes.data()+bad.mapRows,4);bad.Put(row+12,1);}
        if(variant==6)bad.Put(bad.entry+184,1);CHECK(!ReadPblDriverProfile(bad.Access(),bad.seat));
    }
    return 0;
}
}
int main(){if(KnownSeatNeverFallsThroughOnInputGap()||TemporaryInputGapPreservesSeatedLean()||VehicleCameraDeadlineAndOwnerChanges()||SeatReferenceAndMotion()||ContinuousAxesAndRecovery()||DuplicateActionValidityLoss()||ExactNativeRouteReader()||ExactOnFootSharedConcept()||CommitExactPermissions()||ExactBoatProfile())return 1;std::cout<<"10 vehicle runtime cases passed\n";}
