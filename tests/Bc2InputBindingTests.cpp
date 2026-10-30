#include "Test.h"
#include "Bc2InputBinding.h"
#include "fvr/interaction/ControllerInput.h"
#include <bit>
#include <cstring>
#include <limits>
using namespace fvr;
int main(){
 { // Exact selected-item metadata; the two KNV-1 definitions are distinct.
    bc2::ReloadStateSnapshot s;s.owner={1,2,3,4,5,6,7};s.sequence=8;s.observedNs=1000000000;
    s.inventory=9;s.selectedSlot=0;s.config.weaponData=10;s.config.firingData=11;s.config.primaryFire=12;
    s.config.fireInputAction=37;s.config.fireLogicType=0;s.config.baseCapacity=s.config.numberOfMagazines=-1;
    std::strcpy(s.config.assetName.data(),"KNV-1");
    std::strcpy(s.config.assetPath.data(),"Objects/Weapons/Handheld/Knife_Equipable/Knife_Equipable");
    const auto valid=s;const auto owner=s.owner;
    CHECK(bc2::VerifiedSelectedFireAction(s,owner,8,1000000000)==bc2::EntryAction::MeleeAttack);
    CHECK(!bc2::VerifiedSelectedFireAction(s,owner,9,1000000000));
    CHECK(!bc2::VerifiedSelectedFireAction(s,owner,8,999999999));
    CHECK(!bc2::VerifiedSelectedFireAction(s,owner,8,1100000001));
    for(unsigned field=0;field<7;++field){auto changed=owner;
        switch(field){case 0:++changed.player;break;case 1:++changed.soldier;break;case 2:++changed.weak;break;
            case 3:++changed.weapon;break;case 4:++changed.actorGeneration;break;case 5:++changed.equipGeneration;break;default:++changed.space;}
        CHECK(!bc2::VerifiedSelectedFireAction(s,changed,8,1000000000));}
    s.config.assetPath[0]='X';CHECK(!bc2::VerifiedSelectedFireAction(s,owner,8,1000000000));s=valid;
    s.config.assetPath.fill('A');CHECK(!bc2::VerifiedSelectedFireAction(s,owner,8,1000000000));s=valid;
    s.config.fireInputAction=38;CHECK(!bc2::VerifiedSelectedFireAction(s,owner,8,1000000000));s=valid;
    s.selectedSlot=7;CHECK(!bc2::VerifiedSelectedFireAction(s,owner,8,1000000000));s=valid;
    s.config.baseCapacity=30;CHECK(!bc2::VerifiedSelectedFireAction(s,owner,8,1000000000));s=valid;
    s.config.fireInputAction=8;s.selectedSlot=7;std::strcpy(s.config.assetPath.data(),"Objects/Weapons/Handheld/Knife/Knife");
    CHECK(bc2::VerifiedSelectedFireAction(s,owner,8,1000000000)==bc2::EntryAction::Fire);
    std::strcpy(s.config.assetName.data(),"XM8_sp_s");s.config.baseCapacity=30;
    CHECK(bc2::VerifiedSelectedFireAction(s,owner,8,1000000000)==bc2::EntryAction::Fire);
 }
 { // Expack's authored reload route differs from its working Fire8 plunger.
    bc2::ReloadStateSnapshot s;s.owner={1,2,3,4,5,6,7};s.sequence=8;s.observedNs=1000000000;
    s.inventory=9;s.config.weaponData=10;s.config.firingData=11;s.config.primaryFire=12;
    s.config.fireInputAction=8;s.config.reloadInputAction=14;s.config.baseCapacity=1;s.config.numberOfMagazines=-1;
    std::strcpy(s.config.assetName.data(),"Expack_US");
    std::strcpy(s.config.assetPath.data(),"Objects/Weapons/Handheld/Beach/Expack/Expack");
    const auto valid=s;const auto owner=s.owner;
    CHECK(bc2::VerifiedSelectedReloadAction(s,owner,8,1000000000)==bc2::EntryAction::Zoom);
    CHECK(bc2::VerifiedSelectedFireAction(s,owner,8,1000000000)==bc2::EntryAction::Fire);
    CHECK(!bc2::VerifiedSelectedReloadAction(s,owner,9,1000000000));
    CHECK(!bc2::VerifiedSelectedReloadAction(s,owner,8,999999999));
    CHECK(!bc2::VerifiedSelectedReloadAction(s,owner,8,1100000001));
    for(unsigned field=0;field<7;++field){auto changed=owner;
        switch(field){case 0:++changed.player;break;case 1:++changed.soldier;break;case 2:++changed.weak;break;
            case 3:++changed.weapon;break;case 4:++changed.actorGeneration;break;case 5:++changed.equipGeneration;break;default:++changed.space;}
        CHECK(!bc2::VerifiedSelectedReloadAction(s,changed,8,1000000000));}
    for(unsigned field=0;field<12;++field){s=valid;
        switch(field){case 0:s.config.assetName[0]='X';break;case 1:s.config.assetPath[0]='X';break;
            case 2:s.config.assetPath.fill('A');break;case 3:s.config.fireInputAction=37;break;
            case 4:s.config.reloadInputAction=38;break;case 5:s.config.baseCapacity=30;break;
            case 6:s.config.numberOfMagazines=4;break;case 7:s.config.weaponData=0;break;
            case 8:s.config.firingData=0;break;case 9:s.config.primaryFire=0;break;
            case 10:s.inventory=0;break;default:s.observedNs=0;}
        CHECK(!bc2::VerifiedSelectedReloadAction(s,owner,8,1000000000));}
    s=valid;s.config.reloadInputAction=29;std::strcpy(s.config.assetName.data(),"XM8_sp_s");
    CHECK(bc2::VerifiedSelectedReloadAction(s,owner,8,1000000000)==bc2::EntryAction::Reload);
 }
 { // The route owns Zoom14/Reload29, preserves Fire8 and rolls back exactly.
    std::array<std::byte,256> bytes{};
    const auto get=[&](unsigned at){unsigned value=0;std::memcpy(&value,bytes.data()+at,4);return value;};
    const auto put=[&](unsigned at,unsigned value){std::memcpy(bytes.data()+at,&value,4);};
    put(0x98,(1u<<28)|(1u<<29));put(0x9c,0x80000000u);const auto before=bytes;
    interaction::ActionOutput a;a.active=true;a.owner=2;a.held=interaction::Fire|interaction::Reload;
    bc2::ApplySelectedReloadRoute(a,bc2::EntryAction::Zoom);
    {bc2::InputOverride p;CHECK(p.Apply(bytes,a));
     CHECK(get(40)==std::bit_cast<unsigned>(1.f));CHECK(get(0x98)==((1u<<28)|(1u<<14)));CHECK(get(0x9c)==0x80000000u);
     CHECK(p.Restore());CHECK(bytes==before);}
    {bc2::InputOverride p;CHECK(p.Apply(bytes,a));
     put(0x98,1u<<28);CHECK(!p.Restore());CHECK(get(0x98)==((1u<<28)|(1u<<29)));}
    bytes=before;a.held=interaction::Fire;
    {bc2::InputOverride p;CHECK(p.Apply(bytes,a));
     CHECK(get(0x98)==(1u<<28)&&get(40)==std::bit_cast<unsigned>(1.f));}
    CHECK(bytes==before);a.held=interaction::Reload;
    bc2::ApplySelectedReloadRoute(a,bc2::EntryAction::Reload);
    {bc2::InputOverride p;CHECK(p.Apply(bytes,a));CHECK(get(0x98)==((1u<<28)|(1u<<29)));}
    CHECK(bytes==before);
    for(const auto route:{std::optional<bc2::EntryAction>{},std::optional{bc2::EntryAction::ThrowGrenade}}){
     a.held=a.pressed=interaction::Reload;a.released=interaction::Fire;
     bc2::ApplySelectedReloadRoute(a,route);CHECK(!a.held&&!a.pressed&&a.released==interaction::Fire);
     bc2::InputOverride p;CHECK(p.Apply(bytes,a));CHECK(get(0x98)==(1u<<28)&&get(40)==0);}
    CHECK(bytes==before);
 }
 { // Existing right secondary -> Reload -> Expack Zoom; no left-trigger ADS.
    interaction::ControllerActions actions;interaction::InputOwner owner{123,1,true,true};
    interaction::InputFrame f;f.generation=1;f.spaceGeneration=1;f.predictedNs=1000000000;f.focused=f.headValid=true;
    for(auto& h:f.hands){h.gripTracked=h.aimTracked=true;h.active=interaction::Components;}
    const auto step=[&](){++f.generation;f.predictedNs+=11000000;return actions.Update(f,owner,f.predictedNs);};
    const auto dispatch=[&](const interaction::ActionOutput& a,bool reload,bool fire)->bool {
        std::array<std::byte,256> cache{};bc2::InputOverride p;
        auto mapped=a;bc2::ApplySelectedReloadRoute(mapped,bc2::EntryAction::Zoom);
        const bool applied=p.Apply(cache,mapped);
        if(applied!=a.active)return false;if(applied)p.Commit();
        unsigned bits=0,trigger=0;std::memcpy(&bits,cache.data()+0x98,4);std::memcpy(&trigger,cache.data()+40,4);
        return bits==(reload?(1u<<14):0u)&&trigger==std::bit_cast<unsigned>(fire?1.f:0.f);
    };
    for(unsigned n=0;n<3;++n)CHECK(dispatch(step(),false,false));
    f.hands[0].trigger=1;CHECK(dispatch(step(),false,false));
    f.hands[1].held=interaction::Secondary;auto a=step();CHECK(a.pressed==interaction::Reload&&dispatch(a,true,false));
    a=actions.Update(f,owner,f.predictedNs);CHECK(!a.pressed&&dispatch(a,true,false));
    f.hands[1].trigger=1;CHECK(dispatch(step(),true,true));
    f.focused=false;CHECK(dispatch(step(),false,false));f.focused=true;
    for(unsigned n=0;n<3;++n)CHECK(dispatch(step(),false,false));
    f.hands[1].held=0;f.hands[1].trigger=0;CHECK(dispatch(step(),false,false));
    f.hands[1].held=interaction::Secondary;CHECK(dispatch(step(),true,false));
    ++owner.generation;CHECK(dispatch(step(),false,false));
    for(unsigned n=0;n<2;++n)CHECK(dispatch(step(),false,false));
    f.hands[1].held=0;CHECK(dispatch(step(),false,false));
    f.hands[1].held=interaction::Secondary;CHECK(dispatch(step(),true,false));
    a.held=a.pressed=0;CHECK(dispatch(a,false,false)); // A prior action gate cannot be bypassed.
 }
 { // Selected melee owns its boolean only; no Fire8 or acquisition attack.
    std::array<std::byte,256> bytes{};
    const auto get=[&](unsigned at){unsigned value=0;std::memcpy(&value,bytes.data()+at,4);return value;};
    const auto put=[&](unsigned at,unsigned value){std::memcpy(bytes.data()+at,&value,4);};
    put(0x9c,0x80000000u);const auto neutral=bytes;
    interaction::ActionOutput a;a.active=true;a.owner=2;a.held=a.pressed=interaction::Fire;
    {bc2::InputOverride p;CHECK(p.Apply(bytes,a,{},{},{},false,bc2::EntryAction::MeleeAttack));
     CHECK(get(40)==0&&get(0x9c)==(0x80000000u|(1u<<5)));CHECK(p.Restore());CHECK(bytes==neutral);}
    {bc2::InputOverride p;CHECK(p.Apply(bytes,a,{},{},{},false,bc2::EntryAction::MeleeAttack));
     put(0x9c,0x80000004u);CHECK(!p.Restore());CHECK(get(0x9c)==0x80000004u);}
    bytes=neutral;a.held=a.pressed=0;put(0x9c,0x80000000u|(1u<<5));
    {bc2::InputOverride p;CHECK(p.Apply(bytes,a,{},{},{},false,bc2::EntryAction::MeleeAttack));
     CHECK(get(40)==0&&get(0x9c)==0x80000000u);p.Commit();}
    CHECK(bytes==neutral);
    {bc2::InputOverride p;CHECK(!p.Apply(bytes,a,{},{},{},false,bc2::EntryAction::ThrowGrenade));CHECK(bytes==neutral);}
    a.active=false;a.held=interaction::Fire;
    {bc2::InputOverride p;CHECK(!p.Apply(bytes,a,{},{},{},false,bc2::EntryAction::MeleeAttack));CHECK(bytes==neutral);}
 }

 { // Real action policy -> selected-knife native cache, including rearm.
    interaction::ControllerActions actions;interaction::InputOwner owner{123,1,true,true};
    interaction::InputFrame f;f.generation=1;f.spaceGeneration=1;f.predictedNs=1000000000;
    f.focused=f.headValid=true;
    for(auto& h:f.hands){h.gripTracked=h.aimTracked=true;h.active=interaction::Components;}
    std::array<std::byte,256> cache{};
    const auto get=[&](unsigned at){unsigned value=0;std::memcpy(&value,cache.data()+at,4);return value;};
    const auto dispatch=[&](const interaction::ActionOutput& a,bool attack)->bool {
        // Original native Gather replaces the previous tick, including when
        // the semantic policy is inactive and the adapter makes no write.
        cache={};const unsigned unrelated=0x80000000u;std::memcpy(cache.data()+0x9c,&unrelated,4);
        bc2::InputOverride patch;
        const bool applied=patch.Apply(cache,a,{},{},{},false,bc2::EntryAction::MeleeAttack);
        if(applied!=a.active)return false;if(applied)patch.Commit();
        return get(40)==0&&get(0x9c)==(unrelated|(attack?(1u<<5):0u));
    };
    const auto step=[&](){++f.generation;f.predictedNs+=11000000;return actions.Update(f,owner,f.predictedNs);};
    for(unsigned n=0;n<3;++n){const auto a=step();CHECK(!a.held&&dispatch(a,false));}
    f.hands[1].squeeze=1;f.hands[1].grip.position.x=.2f;
    auto a=step();CHECK(!a.held&&!a.pressed&&dispatch(a,false)); // Grasp/motion is not attack.
    f.hands[1].trigger=1;a=step();CHECK(a.pressed==interaction::Fire&&dispatch(a,true));
    a=actions.Update(f,owner,f.predictedNs);CHECK(!a.pressed&&a.held==interaction::Fire&&dispatch(a,true));
    f.hands[1].trigger=0;a=step();CHECK(a.released==interaction::Fire&&dispatch(a,false));
    f.hands[1].trigger=1;a=step();CHECK(a.pressed==interaction::Fire&&dispatch(a,true));
    f.focused=false;f.hands={};a=step();CHECK(a.released==interaction::Fire&&dispatch(a,false));
    f.focused=true;for(auto& h:f.hands){h.gripTracked=h.aimTracked=true;h.active=interaction::Components;}
    f.hands[1].trigger=1;
    for(unsigned n=0;n<3;++n){a=step();CHECK(!(a.held|a.pressed)&&dispatch(a,false));}
    f.hands[1].trigger=0;a=step();CHECK(dispatch(a,false));
    f.hands[1].trigger=1;a=step();CHECK(a.pressed==interaction::Fire&&dispatch(a,true));
    ++owner.generation;a=step();CHECK(a.released==interaction::Fire&&dispatch(a,false));
    for(unsigned n=0;n<2;++n){a=step();CHECK(!(a.held|a.pressed)&&dispatch(a,false));}
    f.hands[1].trigger=0;a=step();CHECK(dispatch(a,false));
    f.hands[1].trigger=1;a=step();CHECK(a.pressed==interaction::Fire&&dispatch(a,true));
    f.hands[1].trigger=0;a=step();CHECK(a.released==interaction::Fire&&dispatch(a,false));
 }

 CHECK(fvr::bc2::RoomscaleNativeAxis(0)==0);
 CHECK(fvr::bc2::RoomscaleNativeAxis(.002f)==0);
 CHECK(std::abs(fvr::bc2::RoomscaleNativeAxis(.06f)-.105f)<1e-5f);
 CHECK(std::abs(fvr::bc2::RoomscaleNativeAxis(-.06f)+.105f)<1e-5f);
 CHECK(std::abs(fvr::bc2::RoomscaleNativeAxis(6)-.8f/6.f)<1e-5f);

    std::array<std::byte,256> bytes{};auto put=[&](std::size_t at,unsigned x){std::memcpy(bytes.data()+at,&x,4);};
    auto get=[&](std::size_t at){unsigned x;std::memcpy(&x,bytes.data()+at,4);return x;};
    put(0,0x12345678);put(0x98,1u<<28);put(0x9c,0x80000000);const auto original=bytes;
    interaction::ActionOutput a{};a.owner=1;a.active=true;a.forward=.5f;a.strafe=-.7f;
    a.held=interaction::Fire|interaction::Sprint|interaction::AlternateFire|interaction::Jump;
    {bc2::InputOverride scope;CHECK(scope.Apply(bytes,a));CHECK(!scope.Apply(bytes,a));
     CHECK(get(8)==std::bit_cast<unsigned>(.5f)&&get(12)==std::bit_cast<unsigned>(-.7f));CHECK(get(40)==std::bit_cast<unsigned>(1.f));
     CHECK(get(0x98)==((1u<<28)|(1u<<31)|(1u<<14)|(1u<<15)));CHECK(get(0x9c)==0x80000000);CHECK(scope.Restore());}
    CHECK(bytes==original);
    {bc2::InputOverride scope;CHECK(scope.Apply(bytes,a));put(0x98,get(0x98)&~(1u<<31));put(0x9c,0x12345678);put(40,0);
     CHECK(!scope.Restore());CHECK(!(get(0x98)&(1u<<31)));CHECK(get(0x9c)==0x12345678);CHECK(get(40)==0);}
    bytes=original;a.active=false;{bc2::InputOverride s;CHECK(!s.Apply(bytes,a));}CHECK(bytes==original);
    a.active=true;a.forward=std::numeric_limits<float>::quiet_NaN();{bc2::InputOverride s;CHECK(!s.Apply(bytes,a));}CHECK(bytes==original);
    a.forward=0;{bc2::InputOverride s;CHECK(!s.Apply(std::span(bytes).first(100),a));}CHECK(bytes==original);
    a.held=interaction::Crouch;a.pressed=interaction::Crouch;
    {bc2::InputOverride s;CHECK(s.Apply(bytes,a));CHECK(get(0x98)&(1u<<26));}a.pressed=0;
    {bc2::InputOverride s;CHECK(s.Apply(bytes,a));CHECK(get(0x98)&(1u<<26));}
    CHECK(bytes==original);
    // Bounded grenade probe uses native bit 38, leaving other high-word buttons
    // intact; false releases it, and exact rollback keeps subsequent game edits.
    {bc2::InputOverride s;CHECK(s.Apply(bytes,a,true));CHECK(get(0x9c)==(0x80000000u|(1u<<6)));CHECK(s.Restore());}CHECK(bytes==original);
    put(0x9c,0x80000000u|(1u<<6));
    {bc2::InputOverride s;CHECK(s.Apply(bytes,a,false));CHECK(get(0x9c)==0x80000000u);CHECK(s.Restore());}CHECK(get(0x9c)&(1u<<6));
    bytes=original;
    // Weapon switching is a float action (7), not a button bit. Diagnostics
    // own its pulse/neutral value; rollback and commit preserve the native ABI.
    {bc2::InputOverride s;CHECK(s.Apply(bytes,a,{},true));CHECK(get(0x24)==std::bit_cast<unsigned>(1.f));CHECK(s.Restore());}CHECK(bytes==original);
    put(0x24,std::bit_cast<unsigned>(.5f));
    {bc2::InputOverride s;CHECK(s.Apply(bytes,a));CHECK(get(0x24)==0);CHECK(s.Restore());}
    {bc2::InputOverride s;CHECK(s.Apply(bytes,a,{},false));CHECK(get(0x24)==0);CHECK(s.Restore());}CHECK(get(0x24)==std::bit_cast<unsigned>(.5f));
    bytes=original;
    // Selection edges are signed one-tick pulses; holds and opposing requests
    // cannot cycle repeatedly. Exact rollback preserves later native changes.
    a.pressed=interaction::NextWeapon;
    {bc2::InputOverride s;CHECK(s.Apply(bytes,a));CHECK(get(0x24)==std::bit_cast<unsigned>(1.f));CHECK(s.Restore());}CHECK(bytes==original);
    a.pressed=interaction::PreviousWeapon;
    {bc2::InputOverride s;CHECK(s.Apply(bytes,a));CHECK(get(0x24)==std::bit_cast<unsigned>(-1.f));CHECK(s.Restore());}
    // Merely enabling the diagnostic must not erase normal stick selection.
    {bc2::InputOverride s;CHECK(s.Apply(bytes,a,{},false));CHECK(get(0x24)==std::bit_cast<unsigned>(-1.f));CHECK(s.Restore());}
    a.pressed=interaction::NextWeapon|interaction::PreviousWeapon;
    {bc2::InputOverride s;CHECK(s.Apply(bytes,a));CHECK(get(0x24)==0);CHECK(s.Restore());}
    a.pressed=0;a.held=interaction::NextWeapon;
    {bc2::InputOverride s;CHECK(s.Apply(bytes,a));CHECK(get(0x24)==0);CHECK(s.Restore());}
    // Dedicated mode commands touch only their verified selection bit. Other
    // high-word commands and subsequent native changes survive exact rollback.
    a.held=a.pressed=0;
    for(auto mode:{bc2::EntryAction::GrenadeLauncher,bc2::EntryAction::DynamicGadget2}){
        bc2::InputOverride s;CHECK(s.Apply(bytes,a,{},{},mode));
        CHECK(get(0x9c)==(0x80000000u|(1u<<(unsigned(mode)-32))));
        CHECK(s.Restore());CHECK(bytes==original);
    }
    {bc2::InputOverride s;CHECK(!s.Apply(bytes,a,{},{},bc2::EntryAction::ThrowGrenade));CHECK(bytes==original);}
    {bc2::InputOverride s;CHECK(s.Apply(bytes,a,{},{},bc2::EntryAction::GrenadeLauncher));
     put(0x9c,0x80000004u);CHECK(!s.Restore());CHECK(get(0x9c)==0x80000004u);}
    bytes=original;
    // Only the verified native context alias extends the ordinary Use mask.
    a.held=interaction::Use;a.pressed=interaction::Use;
    {bc2::InputOverride s;CHECK(s.Apply(bytes,a,{},{},{},true));CHECK((get(0x98)&((1u<<16)|(1u<<27)))==((1u<<16)|(1u<<27)));CHECK(s.Restore());}CHECK(bytes==original);
    {bc2::InputOverride s;CHECK(s.Apply(bytes,a));CHECK(!(get(0x98)&(1u<<16))&&(get(0x98)&(1u<<27)));CHECK(s.Restore());}
    {bc2::InputOverride s;CHECK(s.Apply(bytes,a,{},{},{},true));put(0x98,get(0x98)&~(1u<<16));CHECK(!s.Restore());CHECK(!(get(0x98)&((1u<<16)|(1u<<27))));}
    bytes=original;put(0x98,get(0x98)|(1u<<16));const auto nativeHeld=bytes;a.held=a.pressed=0;
    {bc2::InputOverride s;CHECK(s.Apply(bytes,a,{},{},{},true));CHECK(!(get(0x98)&((1u<<16)|(1u<<27))));CHECK(s.Restore());}CHECK(bytes==nativeHeld);
    bytes=original;
    // A vehicle exit command preserves every native driving/fire/aim byte.
    for(unsigned n=8;n<0x98;++n)bytes[n]=std::byte((n*19)%256);
    const auto vehicleBefore=bytes;a.held=interaction::Use|interaction::Fire;a.forward=1;
    {bc2::InputOverride s;CHECK(s.ApplyVehicleExit(bytes,a));CHECK(get(0x98)==((1u<<28)|(1u<<16)));
     auto expected=vehicleBefore;unsigned flags=(1u<<28)|(1u<<16);std::memcpy(expected.data()+0x98,&flags,4);CHECK(bytes==expected);CHECK(s.Restore());}
    CHECK(bytes==vehicleBefore);bytes=original;
    a.held=interaction::Fire;a.forward=.3f;
    {bc2::InputOverride s;CHECK(s.Apply(bytes,a));s.Commit();}
    CHECK(get(8)==std::bit_cast<unsigned>(.3f)&&get(40)==std::bit_cast<unsigned>(1.f));
    // Native gather owns the next tick. A committed transaction stores no
    // pointer and cannot later restore over the game's fresh keyboard input.
    bytes=original;CHECK(get(8)==0&&get(40)==0);return 0;
}
