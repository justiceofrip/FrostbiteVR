// Included after Resolve. Observation only; no gameplay authority is changed.
enum class AttachedPhase : unsigned {Gather,UpdateBefore,UpdateAfter,WorldPose,Animation,RigOwner,ClientFire,ServerFire,FireOrigin,ViewBase,Count};
constexpr unsigned AttachedPhaseCount=unsigned(AttachedPhase::Count),AttachedPhaseLimit=24;
struct AttachedObservation {
    ULONGLONG ms=0;unsigned serial=0,phase=0,thread=0,object=0,arg=0,caller=0,update=0;
    unsigned player=0,soldier=0,weak=0,vehicle=0,entry=0,router=0,cache=0,permissions=0;
    unsigned slot=0,weapon=0,data=0,persistence=0,aim=0,aimer=0,constraints=0;
    unsigned camera=0,provider=0,cameraState=0,cameraNode=0,cameraIndex=0;
    unsigned effects=0,firing=0,serverPlayer=0,serverSoldier=0,serverControlled=0;
    std::array<float,4> limits{};float yaw=0,pitch=0;
    bool route=false,joined=false,callbackMatched=false,readback=false,anglesRead=false;
};
AttachedSoldierBinding attachedBinding{};
std::atomic_flag attachedGate=ATOMIC_FLAG_INIT;
std::array<std::array<AttachedObservation,AttachedPhaseLimit>,AttachedPhaseCount> attachedRecords{};
std::array<unsigned,AttachedPhaseCount> attachedCounts{},attachedLimited{},attachedThrottled{},attachedUnmatchedPrechecks{};
std::array<ULONGLONG,AttachedPhaseCount> attachedLast{};
std::atomic<unsigned> attachedOverlaps=0;
unsigned attachedSerial=0;
std::atomic<unsigned> attachedUpdateSerial=0;
thread_local unsigned attachedCurrentUpdate=0;
const char* AttachedPhaseName(unsigned n)noexcept {
    constexpr const char* names[]{"gather","native_update_before","native_update_after","world_pose","animation","rig_owner","client_fire","server_fire","fire_origin","view_base"};
    return n<AttachedPhaseCount?names[n]:"invalid";
}
void ObserveAttached(const Owner& owner,AttachedPhase phase,unsigned object,unsigned arg=0,unsigned caller=0)noexcept {
    if(owner.role!=OwnerRole::AttachedSoldier||!attachedBinding.verified)return;
    if(attachedGate.test_and_set(std::memory_order_acquire)){++attachedOverlaps;return;}
    struct Release {~Release(){attachedGate.clear(std::memory_order_release);}} release;
    const auto kind=unsigned(phase);if(kind>=AttachedPhaseCount)return;
    if(attachedCounts[kind]>=AttachedPhaseLimit){++attachedLimited[kind];return;}
    // Broad existing hooks also see other soldiers. A cheap conservative filter
    // reserves the bounded full snapshots for the local callback candidate.
    // This is not an ownership proof: failures are counted, and every retained
    // row still needs the complete typed join and its final readback below.
    bool candidate=true;
    if(phase==AttachedPhase::WorldPose||phase==AttachedPhase::RigOwner)candidate=object==owner.soldier;
    else if(phase==AttachedPhase::Animation)candidate=std::uint64_t(owner.soldier)+0x214==object;
    else if(phase==AttachedPhase::ClientFire){
        bool okay=true;const auto word=[&](std::uint64_t at){unsigned value=0;if(at>UINT32_MAX-3u||!Read(static_cast<unsigned>(at),&value,4))okay=false;return value;};
        const auto inventory=word(std::uint64_t(owner.soldier)+((word(std::uint64_t(owner.soldier)+0x114)&1)?0x24c:0x248));
        const auto begin=word(std::uint64_t(owner.soldier)+0x260),end=word(std::uint64_t(owner.soldier)+0x264),slot=word(std::uint64_t(inventory)+0x14c);
        candidate=okay&&begin&&end>=begin&&end-begin<=256&&(end-begin)%4==0&&slot<(end-begin)/4;
        if(candidate){const auto weapon=word(std::uint64_t(begin)+slot*4);candidate=weapon&&word(std::uint64_t(weapon)+0x38)==object&&okay;}
    }else if(phase==AttachedPhase::ServerFire||phase==AttachedPhase::FireOrigin){
        unsigned serverPlayer=0;
        const PlayerPairMemory players{nullptr,[](void*,unsigned at,void* dst,std::size_t n){return Read(at,dst,n);}};
        candidate=std::uint64_t(object)+0x128<=UINT32_MAX-3u&&Read(object+0x128,&serverPlayer,4)&&
            bool(MatchServerPlayer(players,unsigned(image)+fireProfile.serverContext,unsigned(image)+fireProfile.serverManagerVtable,owner.player,serverPlayer));
    }
    if(!candidate){++attachedUnmatchedPrechecks[kind];return;}
    const auto now=GetTickCount64();const bool fire=phase==AttachedPhase::ClientFire||phase==AttachedPhase::ServerFire||phase==AttachedPhase::FireOrigin;
    if(!fire&&attachedCounts[kind]&&now-attachedLast[kind]<1000){++attachedThrottled[kind];return;}
    attachedLast[kind]=now;
    AttachedObservation row;row.ms=now;row.serial=++attachedSerial;row.phase=kind;row.thread=GetCurrentThreadId();row.object=object;row.arg=arg;row.caller=caller;row.update=attachedCurrentUpdate;
    row.player=owner.player;row.soldier=owner.soldier;row.weak=owner.weak;row.vehicle=owner.controlled;row.entry=owner.entry;row.router=owner.router;row.cache=owner.cache;
    const VehicleRouteMemory memory{nullptr,[](void*,unsigned at,void* dst,std::size_t n){return Read(at,dst,n);},[](void*,unsigned at,const char* name){return Type(at,name);}};
    // Observer-local epoch values are never supplied to input/rig publications.
    const VehicleRouteOwner observedOwner{owner.manager,owner.player,owner.soldier,owner.weak,1,1};
    const VehicleRouteBinding routeBinding{unsigned(image)+profile.gameplay.managerVtable,unsigned(image)+profile.gameplay.inputRouterVtable,unsigned(image)+profile.cacheVtable,true};
    const auto route=ReadVehicleRoutes(memory,routeBinding,observedOwner);row.route=route.status==VehicleRouteStatus::Okay;
    if(row.route){
        row.slot=route.snapshot.slot;row.permissions=route.snapshot.permissionMask;
        const auto attached=ReadAttachedSoldierOwnership(memory,attachedBinding,route.snapshot);row.joined=bool(attached);
        if(attached){const auto& a=*attached;
            row.weapon=a.weapon;row.data=a.equipment.data;row.persistence=a.equipment.persistence;row.aim=a.aim;row.aimer=a.aimer;row.constraints=a.constraints;row.limits=a.nativeLimits;
            row.camera=a.camera.camera;row.provider=a.camera.wrapper;row.cameraState=a.camera.state;row.cameraNode=a.camera.node;row.cameraIndex=a.camera.selected;
            row.anglesRead=Type(U32(a.aimer),"HaloAimerData")&&U32(U32(a.aim)+0x28)==0&&Read(a.aimer+0xc,&row.yaw,4)&&Read(a.aimer+0x10,&row.pitch,4)&&std::isfinite(row.yaw)&&std::isfinite(row.pitch);
            if(!row.anglesRead){row.yaw=row.pitch=0;}
            switch(phase){
            case AttachedPhase::Gather:row.callbackMatched=object==owner.router&&arg==owner.cache;break;
            case AttachedPhase::UpdateBefore:case AttachedPhase::UpdateAfter:row.callbackMatched=object==owner.player;break;
            case AttachedPhase::WorldPose:case AttachedPhase::RigOwner:row.callbackMatched=object==owner.soldier;break;
            case AttachedPhase::Animation:row.callbackMatched=object==owner.soldier+0x214&&U32(U32(owner.soldier)+0x160)==image+rigProfile.animationGetter;break;
            case AttachedPhase::ViewBase:row.callbackMatched=object==owner.entry;break;
            case AttachedPhase::ClientFire:
                row.effects=object;row.firing=U32(object+0x10);
                row.callbackMatched=U32(a.weapon+0x38)==object&&Type(object,"ClientWeaponFiringEffects")&&U32(U32(object)+0x4c)==image+fireProfile.clientShoot&&Type(row.firing,"WeaponFiringData")&&arg==U32(row.firing+0x40)+0xa0;break;
            case AttachedPhase::ServerFire:case AttachedPhase::FireOrigin:{
                row.effects=object;row.firing=U32(object+0x10);row.serverPlayer=U32(object+0x128);row.serverSoldier=U32(row.serverPlayer+0xc3c);row.serverControlled=U32(row.serverPlayer+0xc6c);
                const PlayerPairMemory players{nullptr,[](void*,unsigned at,void* dst,std::size_t n){return Read(at,dst,n);}};
                const auto paired=MatchServerPlayer(players,unsigned(image)+fireProfile.serverContext,unsigned(image)+fireProfile.serverManagerVtable,owner.player,row.serverPlayer);
                row.callbackMatched=Type(object,"ServerWeaponFiringEffects")&&Type(row.firing,"WeaponFiringData")&&row.firing==U32(U32(a.weapon+0x38)+0x10)&&paired&&Type(row.serverSoldier,"ServerSoldierEntity")&&U32(row.serverSoldier+0xc)==U32(owner.soldier+0xc)&&row.serverControlled==row.serverSoldier&&U32(U32(object+0xd4)+4)==row.firing;
                if(phase==AttachedPhase::ServerFire)row.callbackMatched&=arg==U32(row.firing+0x40)+0xa0;
                else row.callbackMatched&=caller==image+fireProfile.callerA||caller==image+fireProfile.callerB;
                break;}
            default:break;
            }
            const auto after=ReadVehicleRoutes(memory,routeBinding,observedOwner);
            row.readback=after.status==VehicleRouteStatus::Okay&&RevalidateAttachedSoldierOwnership(memory,a,after.snapshot);
        }
    }
    attachedRecords[kind][attachedCounts[kind]++]=row;
}
void ReportAttached(std::ostream& out){
    if(attachedGate.test_and_set(std::memory_order_acquire)){out<<",\"attached_soldier_observation\":{\"snapshot_busy\":true,\"production_enabled\":false}";return;}
    struct Release {~Release(){attachedGate.clear(std::memory_order_release);}} release;
    out<<",\"attached_soldier_observation\":{\"native_code_verified\":"<<(attachedBinding.verified?"true":"false")<<",\"read_only\":true,\"production_enabled\":false,\"input_writes\":0,\"observer_local_epochs\":true,\"direct_aim_hook\":false,\"overlap_drops\":"<<attachedOverlaps.load()<<",\"limit_per_phase\":"<<AttachedPhaseLimit<<",\"sample_interval_ms\":1000,\"phases\":[";
    for(unsigned n=0;n<AttachedPhaseCount;++n){if(n)out<<',';out<<"{\"phase\":\""<<AttachedPhaseName(n)<<"\",\"count\":"<<attachedCounts[n]<<",\"limited\":"<<attachedLimited[n]<<",\"throttled\":"<<attachedThrottled[n]<<",\"unmatched_prechecks\":"<<attachedUnmatchedPrechecks[n]<<",\"records\":[";
        for(unsigned i=0;i<attachedCounts[n];++i){if(i)out<<',';const auto& r=attachedRecords[n][i];
            out<<"{\"serial\":"<<r.serial<<",\"native_update_sequence\":"<<r.update<<",\"ms\":"<<r.ms<<",\"thread\":"<<r.thread<<",\"object\":"<<r.object<<",\"arg\":"<<r.arg<<",\"caller\":"<<r.caller<<",\"player\":"<<r.player<<",\"soldier\":"<<r.soldier<<",\"weak\":"<<r.weak<<",\"vehicle\":"<<r.vehicle<<",\"entry\":"<<r.entry<<",\"router\":"<<r.router<<",\"cache\":"<<r.cache<<",\"slot\":"<<r.slot<<",\"permissions\":"<<r.permissions<<",\"weapon\":"<<r.weapon<<",\"data\":"<<r.data<<",\"persistence\":"<<r.persistence<<",\"aim\":"<<r.aim<<",\"aimer\":"<<r.aimer<<",\"constraints\":"<<r.constraints<<",\"camera\":"<<r.camera<<",\"provider\":"<<r.provider<<",\"camera_state\":"<<r.cameraState<<",\"camera_node\":"<<r.cameraNode<<",\"camera_index\":"<<r.cameraIndex<<",\"effects\":"<<r.effects<<",\"firing\":"<<r.firing<<",\"server_player\":"<<r.serverPlayer<<",\"server_soldier\":"<<r.serverSoldier<<",\"server_controlled\":"<<r.serverControlled<<",\"route\":"<<(r.route?"true":"false")<<",\"joined\":"<<(r.joined?"true":"false")<<",\"callback_matched\":"<<(r.callbackMatched?"true":"false")<<",\"readback\":"<<(r.readback?"true":"false")<<",\"angles_read\":"<<(r.anglesRead?"true":"false")<<",\"yaw\":"<<r.yaw<<",\"pitch\":"<<r.pitch<<",\"limits\":[";
            for(unsigned j=0;j<4;++j){if(j)out<<',';out<<r.limits[j];}out<<"]}";
        }out<<"]}";
    }out<<"]}";
}
