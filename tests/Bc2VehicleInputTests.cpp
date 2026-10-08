#include "Test.h"
#include "../src/games/bc2/Bc2VehicleInput.h"
#include <bit>
#include <cstring>
#include <limits>
using namespace fvr::bc2;
namespace {
using Cache=std::array<std::byte,InputBytes>;
void Put(Cache& c,unsigned at,std::uint32_t value){std::memcpy(c.data()+at,&value,4);}
std::uint32_t Get(const Cache& c,unsigned at){std::uint32_t v=0;std::memcpy(&v,c.data()+at,4);return v;}
struct Fixture {
    VehicleSeatSnapshot seat{{1,2,3,0x1000,0x2000,0x3000,0x4000},123,true,true};
    VehicleSeatProfile profile{VehicleSeatRole::DriverGunner,123,
        {EntryAction::Throttle,EntryAction::Strafe,EntryAction::Yaw,EntryAction::Pitch,EntryAction::Fire},true};
    VehicleInputCommand command{seat.identity,true,{.5f,-.6f,.4f,-.3f,.75f},false};
    Cache cache{};
    Fixture(){
        for(unsigned n=0;n<11;++n)Put(cache,8+4*n,std::bit_cast<std::uint32_t>(.125f));
        Put(cache,0x98,0x812307ff);Put(cache,0x9c,0xa55aa55a);
    }
    VehicleInputPlan Plan(){return BuildVehicleInputPlan(profile,seat,command,cache);}
};
int NativeFieldsAndExactPlan(){
    Fixture f;const auto source=f.cache;const auto plan=f.Plan();
    CHECK(plan.status==VehicleInputPlanStatus::Ready&&plan.count==6&&plan.deniedAxes==0);
    auto output=f.cache;std::array<bool,InputBytes> touched{};
    for(unsigned n=0;n<plan.count;++n){const auto& edit=plan.edits[n];CHECK(Get(output,unsigned(edit.offset))==edit.before);
        Put(output,unsigned(edit.offset),edit.after);for(unsigned byte=0;byte<4;++byte)touched[edit.offset+byte]=true;}
    for(unsigned n=0;n<InputBytes;++n)if(!touched[n])CHECK(output[n]==source[n]);
    for(unsigned n=0;n<VehicleAxisCount;++n)CHECK(Near(std::bit_cast<float>(Get(output,8+4*unsigned(*f.profile.axes[n]))),f.command.axes[n]));
    CHECK((Get(output,0x98)&0x7ff)==(Get(source,0x98)&0x7ff)); // Never changes native float permission.
    CHECK(Get(output,0x9c)==Get(source,0x9c));CHECK(f.cache==source);return 0;
}
int EveryNativeRestrictionSurvives(){
    for(unsigned n=0;n<VehicleAxisCount;++n){Fixture f;
        const auto action=unsigned(*f.profile.axes[n]);Put(f.cache,0x98,Get(f.cache,0x98)&~(1u<<action));
        const auto before=f.cache;const auto plan=f.Plan();CHECK(plan.status==VehicleInputPlanStatus::Ready);
        CHECK(plan.deniedAxes==(1u<<n)&&plan.count==5);
        for(unsigned j=0;j<plan.count;++j)CHECK(plan.edits[j].offset!=8+4*action);
        CHECK(f.cache==before);
    }
    Fixture f;Put(f.cache,0x98,0);f.profile.exitVerified=false;
    const auto plan=f.Plan();CHECK(plan.status==VehicleInputPlanStatus::Ready&&plan.count==0&&plan.deniedAxes==31);
    return 0;
}
int FixedGunnerDoesNotRequireThrottle(){
    Fixture f;f.profile.role=VehicleSeatRole::Gunner;f.profile.axes[0].reset();f.profile.axes[1].reset();
    // Synthetic role fixture, not an asserted BC2 vehicle preset.
    f.profile.axes[2]=static_cast<EntryAction>(10);f.profile.axes[3]=static_cast<EntryAction>(9);
    Put(f.cache,0x98,(1u<<8)|(1u<<9)|(1u<<10));
    const auto plan=f.Plan();CHECK(plan.status==VehicleInputPlanStatus::Ready&&plan.count==4&&!plan.deniedAxes);
    CHECK(plan.edits[0].offset==48&&plan.edits[1].offset==44&&plan.edits[2].offset==40);return 0;
}
int DriverAndExit(){
    Fixture f;f.profile.role=VehicleSeatRole::Driver;f.profile.axes[4].reset();
    f.profile.axes[1]=EntryAction::Yaw;f.profile.axes[2]=static_cast<EntryAction>(10);f.profile.axes[3]=static_cast<EntryAction>(9);
    f.command.exitHeld=true;const auto plan=f.Plan();CHECK(plan.status==VehicleInputPlanStatus::Ready&&plan.count==5);
    CHECK(plan.edits[4].after==(Get(f.cache,0x98)|(1u<<16)));
    f.profile.exitVerified=false;const auto noExit=f.Plan();CHECK(noExit.status==VehicleInputPlanStatus::Ready&&noExit.count==4);
    for(unsigned n=0;n<noExit.count;++n)CHECK(noExit.edits[n].offset!=0x98);
    return 0;
}
int RejectIdentityAndState(){
    for(unsigned variant=0;variant<11;++variant){Fixture f;
        if(variant==0)++f.command.identity.actor;
        if(variant==1)++f.command.identity.actorGeneration;
        if(variant==2)++f.command.identity.seatGeneration;
        if(variant==3)++f.command.identity.controlled;
        if(variant==4)++f.command.identity.entry;
        if(variant==5)++f.command.identity.router;
        if(variant==6)++f.command.identity.cache;
        if(variant==7)f.seat.identity.entry=f.command.identity.entry=0;
        if(variant==8)f.command.active=false;
        if(variant==9)f.seat.playing=false;
        if(variant==10)f.seat.bindingVerified=false;
        const auto plan=f.Plan();CHECK(plan.status!=VehicleInputPlanStatus::Ready&&plan.count==0);
    }
    Fixture f;++f.seat.routeFingerprint;CHECK(f.Plan().status==VehicleInputPlanStatus::UnverifiedBinding);return 0;
}
int RejectProfiles(){
    for(unsigned variant=0;variant<10;++variant){Fixture f;
        if(variant==0)f.profile.role=VehicleSeatRole::Unclassified;
        if(variant==1)f.profile.role=static_cast<VehicleSeatRole>(99);
        if(variant==2)f.profile.routeFingerprint=0;
        if(variant==3)f.profile.axes[0].reset();
        if(variant==4)f.profile.axes[4].reset();
        if(variant==5)f.profile.axes[1]=f.profile.axes[0];
        if(variant==6)f.profile.axes[1]=EntryAction::SwitchPrimaryWeapon;
        if(variant==7)f.profile.axes[4]=EntryAction::Throttle;
        if(variant==8)f.profile.axes[2]=static_cast<EntryAction>(99);
        if(variant==9)f.profile.axes[2]=static_cast<EntryAction>(2); // Unsigned brake is not a signed look axis.
        const auto plan=f.Plan();CHECK(plan.status!=VehicleInputPlanStatus::Ready&&plan.count==0);
    }
    return 0;
}
int MalformedValuesRejectWholePlan(){
    for(unsigned n=0;n<VehicleAxisCount;++n)for(float value:{std::numeric_limits<float>::quiet_NaN(),std::numeric_limits<float>::infinity(),1.01f,-1.01f}){
        Fixture f;f.command.axes[n]=value;const auto plan=f.Plan();CHECK(plan.status==VehicleInputPlanStatus::InvalidInput&&plan.count==0);
    }
    Fixture f;f.command.axes[4]=-.01f;CHECK(f.Plan().status==VehicleInputPlanStatus::InvalidInput);
    f=Fixture{};Put(f.cache,8+4*5,std::bit_cast<std::uint32_t>(std::numeric_limits<float>::quiet_NaN()));
    const auto bad=f.Plan();CHECK(bad.status==VehicleInputPlanStatus::InvalidCache&&bad.count==0);
    CHECK(BuildVehicleInputPlan(f.profile,f.seat,f.command,std::span(f.cache).first(InputBytes-1)).status==VehicleInputPlanStatus::InvalidCache);
    return 0;
}
}
int main(){return NativeFieldsAndExactPlan()||EveryNativeRestrictionSurvives()||FixedGunnerDoesNotRequireThrottle()||DriverAndExit()||
    RejectIdentityAndState()||RejectProfiles()||MalformedValuesRejectWholePlan();}
