// Included in chestKnifeNative after its existing private helpers. No hooks or
// writes are installed here: selectedKnife's verified dispatcher calls these.
namespace {
using BridgeNative=Service::AttackNative;
struct BridgeScope {
    BridgeNative native{};
    std::optional<std::array<std::byte,64>> geometryCopy;
    std::array<std::byte,64> original{};
    unsigned searches=0;
};
thread_local BridgeScope* bridgeAcquiring=nullptr;
thread_local BridgeScope* bridgeAttacking=nullptr;
struct BridgeRestore {BridgeScope*& slot;BridgeScope* previous;~BridgeRestore(){slot=previous;}};
bool BridgeOwner(const BridgeNative& a)noexcept {
    const auto& k=a.key;const pk::detail::Reader r{service.memory};
    if(pk::ReadAcceptedKnifeOwnership(service.memory,a.anchor,service.base+service.state.firingVtableRva)!=pk::AcceptedKnifeOwnership::Same)return false;
    // Same verified implementation, plus the chest item's own relationships.
    // A common RTTI name alone never joins the selected and chest routes.
    for(unsigned pass=0;pass<2;++pass)if(
        !r.Eq(std::uint64_t(a.anchor.knifeWeapon)+0x38,k.clientEffects)||
        !r.Eq(std::uint64_t(k.serverItem)+0xc,k.effects)||!r.Eq(k.effects,0x1417cd0)||
        !r.Eq(std::uint64_t(k.effects)+0xd0,0x1417c70)||
        !r.Eq(std::uint64_t(k.effects)+0x128,k.serverPlayer)||
        !r.Eq(std::uint64_t(k.effects)+0x10,k.config.firingData)||
        !r.Eq(std::uint64_t(k.config.firingData)+0x40,k.config.primaryFire)||
        !r.Eq(std::uint64_t(k.config.primaryFire)+0x24,0)||
        !r.Eq(std::uint64_t(k.config.primaryFire)+0x4c,8))return false;
    return true;
}
bool BridgeUpdate(const BridgeNative& a,bool copied)noexcept {
    const auto* scope=OriginalFiringScope::Current();if(!scope||scope->ObserverContext()!=&service)return false;
    const auto& call=scope->Call();const auto& t=scope->Token();
    const pk::detail::Reader r{service.memory};unsigned begin=0,end=0;std::array<unsigned,64> listeners{};
    if(!r.Get(std::uint64_t(call.firing)+0x18,begin)||!r.Get(std::uint64_t(call.firing)+0x1c,end)||
       begin<0x10000||end<=begin||(end-begin)%4||end-begin>sizeof(listeners)||
       !r.Read(begin,listeners.data(),end-begin)||!r.Eq(std::uint64_t(call.firing)+0x18,begin)||
       !r.Eq(std::uint64_t(call.firing)+0x1c,end)||std::count(listeners.begin(),listeners.begin()+(end-begin)/4,a.key.effects+0xd0)!=1)return false;
    return call.firing==a.key.firing[2]&&call.caller==service.base+service.flow.code[1].rva+0x2f&&
        r.Eq(std::uint64_t(call.firing)+0x3c,copied?2u:6u)&&
        t.words[0]==a.key.lifetime&&t.words[1]==3&&t.words[6]==a.key.serial&&
        (copied?(scope->Copied()&&scope->NativeContext()&&t.words[3]==2&&r.Eq(scope->NativeContext()+0x2c,1)):!(t.words[3]&1));
}
bool BridgeGeometryCurrent(const BridgeNative& a,std::int64_t now)noexcept {
    pk::Selection selected;if(service.bad||!NativeSelection(a.anchor,selected))return false;
    for(auto slot:selected.current)if(slot!=a.anchor.knifeSlot)return false;
    return a.poseRevocation==service.poseRevocation.load(std::memory_order_acquire)&&service.source.trackedKnifeOwned&&
        service.bridge.Same(a.key)&&!service.bridge.Pending()->failed&&service.attacks.Pending()&&service.attacks.Pending()->serial==a.key.serial&&
        a.key.sourceEpoch==service.sourceEpoch.load(std::memory_order_acquire)&&
        SourceCurrent(*service.attacks.Pending(),now)&&pk::ChestAttackTransaction::Fresh(a.key,now)&&BridgeOwner(a);
}
std::optional<pk::ChestAttackTarget> BridgeTarget(unsigned effects)noexcept {
    const auto value=selectedKnife::ReadTarget(service.memory,effects);
    return value?std::optional(pk::ChestAttackTarget{value->handle,value->soldier}):std::nullopt;
}
bool BridgeHealth(const pk::ChestAttackTarget& target,float& value)noexcept {
    const pk::detail::Reader r{service.memory};unsigned before=0,after=0;
    return target.handle&&target.soldier&&r.Get(target.handle,before)&&before==target.soldier+4&&
        r.Eq(target.soldier,0x1424850)&&r.Eq(std::uint64_t(target.soldier)+0x2c,0x1424824)&&
        r.Get(std::uint64_t(target.soldier)+0x34,value)&&std::isfinite(value)&&
        r.Get(target.handle,after)&&after==before;
}
void BridgeFail(const pk::ChestAttackIdentity& key)noexcept {std::lock_guard lock(service.gate);service.bridge.Fail(key);}
}
void AttackHooksAvailable(bool available)noexcept {service.attackHooksAvailable.store(available,std::memory_order_release);}
ChestHookIsolation::ChestHookIsolation()noexcept:acquiring_(bridgeAcquiring),attacking_(bridgeAttacking){bridgeAcquiring=bridgeAttacking=nullptr;}
ChestHookIsolation::~ChestHookIsolation(){bridgeAcquiring=static_cast<BridgeScope*>(acquiring_);bridgeAttacking=static_cast<BridgeScope*>(attacking_);}
std::optional<std::uintptr_t> BridgeAcquire(void* self,unsigned caller,ChestAcquireOriginal original)noexcept {
    if(!service.enabled.load(std::memory_order_acquire)||!service.attackBridgeEnabled)return {};
    Active active;BridgeScope local;
    {
        std::lock_guard lock(service.gate);
        if(!service.enabled.load()||!service.attackBridgeEnabled||!service.attackHooksAvailable.load()||
           !service.attackNative||caller!=selectedKnife::AcquireCaller||unsigned(self)!=service.attackNative->key.effects+0xd0)return {};
        local.native=*service.attackNative;
        if(!service.bridge.BeginAcquire(local.native.key,BridgeUpdate(local.native,true),BridgeOwner(local.native))){
            service.bridge.Fail(local.native.key);return {};
        }
    }
    auto* previous=bridgeAcquiring;bridgeAcquiring=&local;BridgeRestore restore{bridgeAcquiring,previous};
    const auto result=original(self); // Always forward, including absent/revoked geometry.
    const auto target=BridgeTarget(local.native.key.effects);const bool owner=BridgeOwner(local.native);
    {
        std::lock_guard lock(service.gate);
        if(!owner||!local.geometryCopy)service.bridge.Fail(local.native.key);
        service.bridge.EndAcquire(local.native.key,target);
    }
    return result;
}
std::optional<unsigned> BridgeSearch(void* self,unsigned soldier,const void* transform,unsigned shot,float scalar,
    unsigned caller,bool retry,ChestSearchOriginal original)noexcept {
    auto* scope=bridgeAcquiring;
    if(!scope||caller!=(retry?0x578573u:0x578552u))return {};
    Active active;
    const auto& a=scope->native;const auto& k=a.key;const unsigned bit=retry?2u:1u;
    const auto fail=[&]()->std::optional<unsigned>{BridgeFail(k);return {};};
    if((scope->searches&bit)||(!retry&&scope->searches)||(retry&&scope->searches!=1)||
       unsigned(self)!=k.effects+0x130||soldier!=k.serverActor||unsigned(transform)!=k.effects+0x30||
       shot!=k.config.primaryFire+0xa0)return fail();
    scope->searches|=bit;
    const pk::detail::Reader reader{service.memory};std::array<float,3> direction{},position{};
    alignas(16) std::array<std::byte,64> before{};
    if(!reader.Read(std::uint64_t(shot)+0x10,direction.data(),12)||
       !reader.Read(std::uint64_t(shot)+0x20,position.data(),12)||
       !selectedKnife::NeutralShotOffsets(direction,position)||!reader.Read(unsigned(transform),before.data(),64))return fail();
    {
        std::lock_guard lock(service.gate);const auto now=Now();
        if(!BridgeUpdate(a,true)||!BridgeGeometryCurrent(a,now)){service.bridge.Fail(k);return {};}
        if(!scope->geometryCopy){
            const auto blade=service.bridge.TakeGeometry(k,now,true);
            if(blade)scope->geometryCopy=selectedKnife::AcquisitionCopy(before,*blade,a.units);
            if(!scope->geometryCopy){service.bridge.Fail(k);return {};}
            scope->original=before;
        }else if(before!=scope->original){service.bridge.Fail(k);return {};}
    }
    // Retry reuses this acquisition's one immutable mapping. Its internal nested
    // Search calls have different callers and keep the native adjusted copy.
    alignas(16) auto copy=*scope->geometryCopy;
    if(k.sourceEpoch!=service.sourceEpoch.load(std::memory_order_acquire)||a.poseRevocation!=service.poseRevocation.load(std::memory_order_acquire)||
       !pk::ChestAttackTransaction::Fresh(k,Now()))return fail();
    const auto result=original(self,soldier,copy.data(),shot,scalar);
    std::array<std::byte,64> after{};
    const bool unchanged=reader.Read(unsigned(transform),after.data(),64)&&after==before;
    {std::lock_guard lock(service.gate);service.bridge.GeometryReturned(k,retry,unchanged);}
    return result;
}
std::optional<std::uintptr_t> BridgeAttack(void* self,float dt,unsigned primary,void* context,
    unsigned caller,ChestAttackOriginal original)noexcept {
    if(!service.enabled.load(std::memory_order_acquire)||!service.attackBridgeEnabled)return {};
    Active active;BridgeScope local;
    {
        std::lock_guard lock(service.gate);
        if(!service.enabled.load()||!service.attackBridgeEnabled||!service.attackNative||
           caller!=0x6dd9e5||unsigned(self)!=service.attackNative->key.effects+0xd0)return {};
        local.native=*service.attackNative;
        const auto* update=OriginalFiringScope::Current();
        if(!update||unsigned(context)!=update->NativeContext()||!service.bridge.BeginAttack(local.native.key,BridgeTarget(local.native.key.effects),
            BridgeUpdate(local.native,false),BridgeOwner(local.native))){service.bridge.Fail(local.native.key);return {};}
    }
    auto* previous=bridgeAttacking;bridgeAttacking=&local;BridgeRestore restore{bridgeAttacking,previous};
    const auto result=original(self,dt,primary,context);
    {
        std::lock_guard lock(service.gate);
        if(!BridgeOwner(local.native)||(service.bridge.Same(local.native.key)&&!service.bridge.Pending()->target.handle&&
           BridgeTarget(local.native.key.effects)!=std::optional(service.bridge.Pending()->target)))service.bridge.Fail(local.native.key);
        service.bridge.EndAttack(local.native.key);
    }
    return result;
}
std::optional<std::uintptr_t> BridgeDamage(void* self,void* damage,void* player,
    unsigned caller,ChestDamageOriginal original)noexcept {
    auto* scope=bridgeAttacking;if(!scope||caller!=0x57af15)return {};Active active;
    const auto& a=scope->native;std::optional<pk::ChestAttackTarget> target;
    {
        std::lock_guard lock(service.gate);
        if(!service.bridge.Same(a.key))return {};
        target=service.bridge.Pending()->target;
        if(target->soldier!=unsigned(self)||BridgeTarget(a.key.effects)!=target||!BridgeOwner(a)){
            service.bridge.Fail(a.key);return {};
        }
    }
    float before=0,after=0;const bool known=BridgeHealth(*target,before);
    const auto result=original(self,damage,player);
    const bool complete=known&&BridgeHealth(*target,after)&&BridgeTarget(a.key.effects)==target&&BridgeOwner(a);
    {std::lock_guard lock(service.gate);service.bridge.Damage(a.key,complete,before,after);}
    return result;
}
