#include "Test.h"
#include "Bc2InputBinding.h"
#include <bit>
#include <cstring>
#include <limits>
using namespace fvr;
int main(){
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
