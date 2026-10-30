#include "Bc2WeaponFamilyAmmo.h"
#include "Bc2ReloadNativePolicy.h"
#include "Bc2ReloadFlowRuntime.h"
#include "Bc2AmmoCounterHost.h"
#include "Test.h"
#include <cstring>
#include <cstdio>

using namespace fvr::bc2;
namespace {
constexpr std::int64_t Now=1000000000;
struct Fixture {
    ReloadObservedConfig config{};
    std::array<std::uint32_t,1025> words{};
    unsigned reads=0;
    bool fail=false,change=false;
    explicit Fixture(const ReloadConfigDescriptor& descriptor){
        auto& c=config;const auto& v=descriptor.values;
        c.weaponData=0x10000;c.firingData=0x20000;c.primaryFire=0x30000;c.ammoAddress=c.primaryFire+0x170;
        std::copy(descriptor.assetName.begin(),descriptor.assetName.end(),c.assetName.begin());
        std::copy(descriptor.assetPath.begin(),descriptor.assetPath.end(),c.assetPath.begin());
        c.fireLogicType=v.fireLogicType;c.reloadType=v.reloadType;c.fireInputAction=v.fireInputAction;c.reloadInputAction=v.reloadInputAction;
        c.baseCapacity=v.baseCapacity;c.numberOfMagazines=v.numberOfMagazines;c.reloadDelay=v.reloadDelay;c.reloadTime=v.reloadTime;
        c.reloadThreshold=v.reloadThreshold;c.postReloadTime=v.postReloadTime;c.boltDelay=v.boltDelay;c.boltTime=v.boltTime;
        c.holdBoltUntilFireRelease=v.holdBoltUntilFireRelease;c.holdBoltUntilZoomRelease=v.holdBoltUntilZoomRelease;
        words[0x10/4]=std::bit_cast<std::uint32_t>(v.reloadThreshold);
        words[0x18/4]=std::bit_cast<std::uint32_t>(v.reloadTime);
        for(const auto& word:descriptor.timing)words[word.offset/4]=word.expected;
    }
    ReloadStateMemory Memory()noexcept{return {this,[](void* context,std::uint32_t at,void* destination,std::size_t count)noexcept{
        auto& fixture=*static_cast<Fixture*>(context);
        if(fixture.fail||count!=4||at<fixture.config.primaryFire||at>fixture.config.primaryFire+4096||(at&3))return false;
        auto word=fixture.words[(at-fixture.config.primaryFire)/4];
        if(fixture.change&&fixture.reads>0)word^=1;
        ++fixture.reads;std::memcpy(destination,&word,4);return true;
    },nullptr};}
};
ReloadHoldIdentity Identity(){return {{0x10000,0x20000,0x30000,0x40000,1,2,3},
    {0x50000,0x60000,0x70000},0x80000,0x90000,0xa0000};}
int RegisteredConfiguration(const ReloadConfigDescriptor& descriptor,WeaponFamilyAmmoSource expected){
    Fixture fixture(descriptor);
    CHECK(ReadWeaponFamilyAmmoConfig(fixture.Memory(),fixture.config)==expected);
    CHECK(fixture.reads>=4); // Every family still double-reads its native timing.
    for(unsigned fault=0;fault<7;++fault){
        Fixture bad(descriptor);
        if(fault==0)bad.config.assetName[0]='!';
        if(fault==1)bad.config.assetPath[0]='!';
        if(fault==2)++bad.config.baseCapacity;
        if(fault==3)++bad.config.ammoAddress;
        if(fault==4)bad.fail=true;
        if(fault==5)bad.change=true;
        if(fault==6)bad.words[0x18/4]^=1;
        CHECK(!ReadWeaponFamilyAmmoConfig(bad.Memory(),bad.config));
    }
    CHECK(!ReadWeaponFamilyAmmoConfig({},fixture.config));

    // A previous magazine operation can still own its native policy while the
    // newly selected family supplies display counts. Reading cannot hand off,
    // retire or replace that operation, even for another magazine profile.
    Bc2ReloadNativePolicy policy;CHECK(policy.ConfigureMagazine());
    const auto id=Identity();const ReloadCycleControl control{id,7,1,Now,Now+100000000,true};
    fvr::interaction::ManualReloadRequest request{9,{id.owner.soldier,id.owner.actorGeneration,id.owner.weapon,
        id.owner.equipGeneration,id.owner.space},fvr::interaction::ReloadOperation::UnseatMagazine,1,1};
    CHECK(policy.StartMagazine(control,request,Now));
    CHECK(ReadWeaponFamilyAmmoConfig(fixture.Memory(),fixture.config)==expected);
    CHECK(policy.Cycle()==control.cycle&&policy.Identity()==id&&policy.Phase()==ReloadRequestCyclePhase::Arming);
    CHECK(!policy.SelectFamily(ReloadNativeFamily::SpasTube,true,false));
    CHECK(policy.IsMagazine()&&policy.Cycle()==control.cycle);

    // Exercise the unchanged three-copy/count and passive HUD boundaries for
    // every admitted descriptor, including feed/fixed families at zero loaded.
    std::array<ReloadFlowBoundary,3> first{};
    std::array<int,3> capacities{};capacities.fill(descriptor.values.baseCapacity);
    for(unsigned n=0;n<3;++n){auto& b=first[n];b.owner=id.owner;b.firing=id.firing[n];b.branch=std::uint8_t(n);
        b.wrapperOffset=n==0?0x3c:n==1?0x40:0x10;b.current=b.next=2;b.reserve=19;
        if(n==2){b.serverPlayer=id.serverPlayer;b.serverSoldier=id.serverSoldier;b.serverItem=id.serverItem;}}
    CHECK(ReloadReserveCopiesAgree(id,first,first,capacities,capacities));
    CHECK(ReloadReserveInputReady(id,first,first,capacities,capacities));
    for(unsigned fault=0;fault<5;++fault){auto second=first;auto again=capacities;
        if(fault==0)++second[1].reserve;
        if(fault==1)++second[2].serverItem;
        if(fault==2)++second[0].owner.equipGeneration;
        if(fault==3)++again[2];
        if(fault==4)second[1].loaded=capacities[1]+1;
        CHECK(!ReloadReserveCopiesAgree(id,first,second,capacities,again));}
    Bc2AmmoReserveLease reserve{id,1,Now,Now+100000000,0,19,capacities[0],true,true,true};
    ReloadReservePublication publication;publication.Observe(publication.Begin(),{ReloadObservationResult::Available,reserve});
    AmmoCounterOwner owner{id.owner,Now,Now+100000000,true,true,true};
    CHECK(AmmoCounterHostSample(publication.Read(),owner,Now).reserve==19);
    CHECK(!AmmoCounterHostSample(publication.Read(),owner,reserve.deadlineNs).sequence);
    ++owner.owner.equipGeneration;CHECK(!AmmoCounterHostSample(publication.Read(),owner,Now).sequence);
    publication.Clear();CHECK(!publication.Read());
    return 0;
}
}
int main(){
    std::array<unsigned,4> counts{};
    for(const auto& row:RegisteredMagazineNativeProfiles())if(row.enabled&&row.profile&&ResolveMagazineNativeProfile(row.id)==row.profile){
        if(RegisteredConfiguration(row.profile->configuration,WeaponFamilyAmmoSource::Magazine))return 1;++counts[0];}
    Bc2ReloadNativePolicy tube;
    for(const auto& row:Bc2CycleNativeProfiles)if(row.family==Bc2NativeCycleFamily::Pump){
        Fixture fixture(*row.config);CHECK(tube.MatchesSelectedConfig(fixture.config));
        if(RegisteredConfiguration(*row.config,WeaponFamilyAmmoSource::Tube))return 1;++counts[1];}
    for(const auto& row:RegisteredBc2FeedProfiles())if(FindBc2FeedRegistration(row.key)==&row){
        if(RegisteredConfiguration(row.config,WeaponFamilyAmmoSource::Feed))return 1;++counts[2];}
    for(const auto* row:FixedContainerConfigs){
        if(RegisteredConfiguration(*row,WeaponFamilyAmmoSource::Fixed))return 1;++counts[3];}
    CHECK(counts[0]>=2&&counts[1]>1&&counts[2]>=1&&counts[3]==FixedContainerConfigs.size());
    std::printf("Family ammo dispatch/publication: magazine=%u tube=%u feed=%u fixed=%u; exact identity/timing, retained operation, three-copy counts and expiry gates passed.\n",
        counts[0],counts[1],counts[2],counts[3]);
}
