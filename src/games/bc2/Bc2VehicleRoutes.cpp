#include "Bc2VehicleRoutes.h"
#include <algorithm>
#include <cstring>
#include <limits>
namespace fvr::bc2 {
namespace {
VehicleRouteResult ReadEntryRoutes(const VehicleRouteMemory& memory,const VehicleRouteBinding& binding,const VehicleRouteOwner& owner,bool onFoot)noexcept {
    VehicleRouteResult result;
    if(!memory.read||!memory.type||!binding.gatherCodeVerified||!binding.managerVtable||!binding.routerVtable||!binding.cacheVtable)return result;
    const auto fail=[&](VehicleRouteStatus why){VehicleRouteResult out;out.status=why;return out;};
    const auto read=[&](std::uint32_t at,void* out,std::size_t bytes){return at>=0x10000&&bytes&&bytes<=4096&&std::uint64_t(at)+bytes<=UINT32_MAX&&memory.read(memory.context,at,out,bytes);};
    bool readable=true;const auto word=[&](std::uint32_t at){std::uint32_t out=0;if(!read(at,&out,4))readable=false;return out;};
    const auto pointer=[](unsigned at){return at>=0x10000&&at<=UINT32_MAX-0x1000;};
    if(!pointer(owner.manager)||!pointer(owner.player)||!pointer(owner.soldier)||!pointer(owner.weak)||!owner.actorGeneration||!owner.seatGeneration)return fail(VehicleRouteStatus::NoOwner);
    if(word(owner.manager)!=binding.managerVtable||word(owner.manager+0xb4)!=owner.player||word(owner.player+0xc54)!=owner.weak||word(owner.weak)!=owner.soldier+4||word(owner.soldier+0x220)!=owner.player)return fail(VehicleRouteStatus::NoOwner);
    unsigned char flags=0;if(!read(owner.player+0xccd,&flags,1)||!(flags&8))return fail(VehicleRouteStatus::NoOwner);
    const auto controlled=word(owner.player+0xc68),attached=word(owner.player+0xc60);
    const bool attachedSeat=controlled==owner.soldier&&attached;
    const auto entity=attachedSeat?attached:controlled,slot=word(owner.player+(attachedSeat?0xc64:0xc6c));
    if(onFoot){if(entity!=owner.soldier||attached||!memory.type(memory.context,entity,"ClientSoldierEntity"))return fail(VehicleRouteStatus::NotOnFoot);}
    else if(!pointer(entity)||entity==owner.soldier||!memory.type(memory.context,entity,"ClientVehicleEntity"))return fail(VehicleRouteStatus::NotVehicle);
    const auto begin=word(entity+0x7c),end=word(entity+0x80);
    if(!readable)return fail(VehicleRouteStatus::ReadFailed);
    if(!begin||end<begin||end-begin>256||(end-begin)%4||slot>=(end-begin)/4)return fail(VehicleRouteStatus::Malformed);
    const auto entry=word(begin+slot*4);if(!pointer(entry))return fail(VehicleRouteStatus::Malformed);
    const auto router=word(entry+0x198),cache=word(owner.player+0xc74);
    if(!pointer(router)||!pointer(cache))return fail(VehicleRouteStatus::Malformed);
    if(word(router)!=binding.routerVtable||word(cache)!=binding.cacheVtable)return fail(VehicleRouteStatus::Unverified);
    const auto buckets=word(router+0x14),bucketCount=word(router+0x18);
    if(!readable)return fail(VehicleRouteStatus::ReadFailed);
    if(!buckets||!bucketCount||bucketCount>256)return fail(VehicleRouteStatus::Malformed);
    std::array<std::uint32_t,257> heads{};
    if(!read(buckets,heads.data(),(bucketCount+1)*4))return fail(VehicleRouteStatus::ReadFailed);
    struct Node {std::uint32_t address=0;std::array<std::uint32_t,3> words{};};
    std::array<Node,50> nodes{};unsigned count=0;std::array<bool,50> keys{};
    const auto sentinel=heads[bucketCount];
    for(unsigned b=0;b<bucketCount;++b){auto address=heads[b];
        while(address&&address!=sentinel){
            if(count>=nodes.size()||std::find_if(nodes.begin(),nodes.begin()+count,[&](const auto& n){return n.address==address;})!=nodes.begin()+count)return fail(VehicleRouteStatus::Malformed);
            Node node;node.address=address;if(!read(address,node.words.data(),12))return fail(VehicleRouteStatus::ReadFailed);
            const auto action=node.words[0],conceptId=node.words[1];
            // EiaUndefined49 is present in ordinary native maps but gather
            // consumes only0..48. Preserve it in identity; never map a control.
            if(action>=50||conceptId>76||keys[action]||action%bucketCount!=b)return fail(VehicleRouteStatus::Malformed);
            keys[action]=true;nodes[count++]=node;address=node.words[2];
        }
    }
    for(unsigned n=0;n<count;++n){std::array<std::uint32_t,3> again{};if(!read(nodes[n].address,again.data(),12))return fail(VehicleRouteStatus::ReadFailed);if(again!=nodes[n].words)return fail(VehicleRouteStatus::Changed);}
    std::array<std::uint32_t,257> headsAgain{};if(!read(buckets,headsAgain.data(),(bucketCount+1)*4))return fail(VehicleRouteStatus::ReadFailed);
    if(headsAgain!=heads||word(router+0x14)!=buckets||word(router+0x18)!=bucketCount||word(entry+0x198)!=router||word(begin+slot*4)!=entry||word(entity+0x7c)!=begin||word(entity+0x80)!=end||word(owner.player+0xc74)!=cache||word(owner.player+0xc68)!=controlled||word(owner.player+0xc60)!=attached||word(owner.player+(attachedSeat?0xc64:0xc6c))!=slot||word(owner.manager+0xb4)!=owner.player||word(owner.player+0xc54)!=owner.weak||word(owner.weak)!=owner.soldier+4)return fail(VehicleRouteStatus::Changed);
    if(!readable)return fail(VehicleRouteStatus::ReadFailed);
    auto& s=result.snapshot;s.identity={(std::uint64_t(owner.weak)<<32)|owner.soldier,owner.actorGeneration,owner.seatGeneration,entity,entry,router,cache};s.player=owner.player;s.weak=owner.weak;s.slot=slot;s.count=count;s.permissionMask=word(cache+0x98)&0x7ff;
    if(!readable)return fail(VehicleRouteStatus::ReadFailed);
    for(unsigned n=0;n<count;++n)s.routes[n]={nodes[n].words[0],nodes[n].words[1]};
    std::sort(s.routes.begin(),s.routes.begin()+count,[](const auto& a,const auto& b){return a.action<b.action;});
    s.fingerprint=14695981039346656037ull;
    for(unsigned n=0;n<count;++n)for(auto value:{s.routes[n].action,s.routes[n].conceptId})for(unsigned byte=0;byte<4;++byte){s.fingerprint^=(value>>(byte*8))&255;s.fingerprint*=1099511628211ull;}
    result.status=VehicleRouteStatus::Okay;return result;
}
}
VehicleRouteResult ReadVehicleRoutes(const VehicleRouteMemory& m,const VehicleRouteBinding& b,const VehicleRouteOwner& o)noexcept{return ReadEntryRoutes(m,b,o,false);}
VehicleRouteResult ReadOnFootRoutes(const VehicleRouteMemory& m,const VehicleRouteBinding& b,const VehicleRouteOwner& o)noexcept{return ReadEntryRoutes(m,b,o,true);}
bool HasOnFootContextUseVehicleAlias(const VehicleRouteSnapshot& s)noexcept {
    if(!s.identity.actor||!s.identity.actorGeneration||!s.identity.seatGeneration||!s.identity.entry||!s.identity.router||!s.identity.cache||
       s.identity.controlled!=std::uint32_t(s.identity.actor)||!s.fingerprint||s.count>s.routes.size())return false;
    bool interact=false,change=false;unsigned seenInteract=0,seenChange=0;
    for(unsigned n=0;n<s.count;++n){const auto r=s.routes[n];
        if(r.action==27){++seenInteract;interact=r.conceptId==36;}
        if(r.action==16){++seenChange;change=r.conceptId==36;}}
    return interact&&change&&seenInteract==1&&seenChange==1;
}
}
