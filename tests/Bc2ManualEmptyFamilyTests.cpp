#include "Bc2ManualEmptyFamily.h"
#include "Bc2MagazineEmptyControl.h"
#include "Bc2ReloadNativePolicy.h"
#include "Test.h"
#include <bit>
#include <cstring>
#include <limits>
using namespace fvr::bc2;
namespace {
constexpr auto T=1000000000ll;
ReloadObservedConfig Config(const ReloadConfigDescriptor& d){
    ReloadObservedConfig c;std::memcpy(c.assetName.data(),d.assetName.data(),d.assetName.size());
    std::memcpy(c.assetPath.data(),d.assetPath.data(),d.assetPath.size());
    c.weaponData=0xb0000;c.firingData=0xc0000;c.primaryFire=0xd0000;c.ammoAddress=c.primaryFire+0x170;
    const auto& v=d.values;c.fireLogicType=v.fireLogicType;c.reloadType=v.reloadType;
    c.fireInputAction=v.fireInputAction;c.reloadInputAction=v.reloadInputAction;
    c.baseCapacity=v.baseCapacity;c.numberOfMagazines=v.numberOfMagazines;
    c.reloadDelay=v.reloadDelay;c.reloadTime=v.reloadTime;c.reloadThreshold=v.reloadThreshold;
    c.postReloadTime=v.postReloadTime;c.boltDelay=v.boltDelay;c.boltTime=v.boltTime;
    c.holdBoltUntilFireRelease=v.holdBoltUntilFireRelease;c.holdBoltUntilZoomRelease=v.holdBoltUntilZoomRelease;return c;
}
MagazineEmptyStep Step(unsigned branch=0){MagazineEmptyStep s;s.identity.owner={0x10000,0x20000,0x30000,0x40000,1,2,3};
    s.identity.firing={0x50000,0x60000,0x70000};s.identity.serverPlayer=0x80000;s.identity.serverSoldier=0x90000;s.identity.serverItem=0xa0000;
    s.sourceSequence=77;s.ownerRevision=8;s.observedNs=T;s.deadlineNs=T+100000000;s.branch=branch;
    s.before.address=s.identity.firing[branch];s.before.wrapperOffset=branch==0?0x3c:branch==1?0x40:0x10;
    s.before.currentState=s.before.nextState=2;s.before.loaded=0;s.before.reserve=16;return s;}
ReloadUpdateContext Context(){ReloadUpdateContext c;c.deltaSeconds=.005f;c.reloadTimeMultiplier=1;c.flags24Through28[0]=true;return c;}
struct Memory {unsigned base=0xd0000,calls=0,mutate=0;std::array<unsigned,128> words{};
    void Set(const ReloadConfigDescriptor& d){for(const auto& word:d.timing)words[word.offset/4]=word.expected;}
    ReloadStateMemory Access(){return {this,[](void* v,unsigned at,void* dst,std::size_t bytes)noexcept {
        auto& m=*static_cast<Memory*>(v);if(bytes!=4||at<m.base||at-m.base>=sizeof(m.words)||(at-m.base)%4)return false;
        ++m.calls;auto value=m.words[(at-m.base)/4];if(m.mutate&&m.calls==m.mutate)value^=1;
        std::memcpy(dst,&value,4);return true;},nullptr};}
};
int ExactSupportedFamilies(){
    const auto tube=Config(SpasReloadDescriptor),xm8=Config(Xm8ReloadDescriptor),aek=Config(AekMagazineNativeProfile.configuration);
    CHECK(ManualEmptyFamilyConfig(ReloadNativeFamily::SpasTube,NativeMagazineProfileId::ScopedXm8,tube));
    CHECK(ManualEmptyFamilyConfig(ReloadNativeFamily::Xm8Magazine,NativeMagazineProfileId::ScopedXm8,xm8));
    CHECK(ManualEmptyFamilyConfig(ReloadNativeFamily::Xm8Magazine,NativeMagazineProfileId::AuthoredAek,aek));
    CHECK(!ManualEmptyFamilyConfig(ReloadNativeFamily::SpasTube,NativeMagazineProfileId::ScopedXm8,xm8));
    CHECK(!ManualEmptyFamilyConfig(ReloadNativeFamily::Xm8Magazine,NativeMagazineProfileId::ScopedXm8,tube));
    CHECK(!ManualEmptyFamilyConfig(ReloadNativeFamily::Xm8Magazine,NativeMagazineProfileId::ScopedXm8,aek));
    CHECK(!ManualEmptyFamilyConfig(ReloadNativeFamily(255),NativeMagazineProfileId::ScopedXm8,tube));
    CHECK(!ManualEmptyFamilyConfig(ReloadNativeFamily::Xm8Magazine,NativeMagazineProfileId(99),xm8));return 0;
}
int ShellDescriptorRejectsEveryChangedField(){
    for(unsigned bad=0;bad<21;++bad){auto c=Config(SpasReloadDescriptor);
        switch(bad){case 0:c.assetName[0]='X';break;case 1:c.assetPath[0]='X';break;case 2:c.weaponData=0;break;
        case 3:c.firingData=0;break;case 4:c.primaryFire=0;break;case 5:++c.ammoAddress;break;
        case 6:++c.fireLogicType;break;case 7:++c.reloadType;break;case 8:++c.fireInputAction;break;case 9:++c.reloadInputAction;break;
        case 10:++c.baseCapacity;break;case 11:++c.numberOfMagazines;break;case 12:c.reloadDelay+=.01f;break;
        case 13:c.reloadTime+=.01f;break;case 14:c.reloadThreshold+=.01f;break;case 15:c.postReloadTime+=.01f;break;
        case 16:c.boltDelay+=.01f;break;case 17:c.boltTime+=.01f;break;case 18:c.holdBoltUntilFireRelease=true;break;
        case 19:c.holdBoltUntilZoomRelease=true;break;case 20:c.reloadTime=std::numeric_limits<float>::quiet_NaN();break;}
        CHECK(!ManualEmptyFamilyConfig(ReloadNativeFamily::SpasTube,NativeMagazineProfileId::ScopedXm8,c));
    }return 0;
}
int DoubleReadNativeTiming(){
    for(unsigned family=0;family<3;++family){const auto& d=family==0?SpasReloadDescriptor:family==1?Xm8ReloadDescriptor:AekMagazineNativeProfile.configuration;
        const auto f=family==0?ReloadNativeFamily::SpasTube:ReloadNativeFamily::Xm8Magazine;
        const auto id=family==2?NativeMagazineProfileId::AuthoredAek:NativeMagazineProfileId::ScopedXm8;
        for(unsigned bad=0;bad<=d.timing.size()*2;++bad){Memory m;m.Set(d);m.mutate=bad;
            CHECK(ReadManualEmptyFamilyTiming(m.Access(),f,id,Config(d))==(bad==0));
            if(!bad)CHECK(m.calls==d.timing.size()*2);
        }
    }return 0;
}
int ShellCommonBoundaryAndExplicitManualCycle(){
    const auto config=Config(SpasReloadDescriptor);auto c=Context();
    for(unsigned branch=0;branch<3;++branch){auto s=Step(branch);
        for(auto p:{ReloadRequestCyclePhase::Idle,ReloadRequestCyclePhase::Finished,ReloadRequestCyclePhase::Cancelled})
            CHECK(ManualEmptyFamilyStepEligible(s,ReloadNativeFamily::SpasTube,config,p,c,T+1));
        for(auto p:{ReloadRequestCyclePhase::Disabled,ReloadRequestCyclePhase::Holding,ReloadRequestCyclePhase::Advancing})
            CHECK(!ManualEmptyFamilyStepEligible(s,ReloadNativeFamily::SpasTube,config,p,c,T+1));
        c.inputFlags=1;c.fireRequested=true;CHECK(ManualEmptyFamilyStepEligible(s,ReloadNativeFamily::SpasTube,config,ReloadRequestCyclePhase::Idle,c,T+1));
        c.inputFlags=4;c.fireRequested=false;c.reloadRequested=true;CHECK(ManualEmptyFamilyStepEligible(s,ReloadNativeFamily::SpasTube,config,ReloadRequestCyclePhase::Idle,c,T+1));
        c=Context();s.deadlineNs=T+1;CHECK(!ManualEmptyFamilyStepEligible(s,ReloadNativeFamily::SpasTube,config,ReloadRequestCyclePhase::Idle,c,T+1));
    }
    auto s=Step();Bc2ReloadNativePolicy policy;CHECK(policy.Family()==ReloadNativeFamily::SpasTube&&policy.MatchesSelectedConfig(config));
    CHECK(ManualEmptyFamilyStepEligible(s,policy.Family(),config,policy.Phase(),c,T+1));
    ReloadCycleControl control{s.identity,1,s.sourceSequence,s.observedNs,s.deadlineNs,true};CHECK(policy.Start(control,T+1));
    CHECK(policy.Phase()==ReloadRequestCyclePhase::Arming);
    CHECK(ManualEmptyFamilyStepEligible(s,policy.Family(),config,policy.Phase(),c,T+1));
    c.inputFlags=4;c.reloadRequested=true;CHECK(!ManualEmptyFamilyStepEligible(s,policy.Family(),config,policy.Phase(),c,T+1));return 0;
}
int ArmingIntentUsesRealScopedOverride(){
    for(unsigned family=0;family<3;++family)for(unsigned branch=0;branch<3;++branch)for(unsigned flags:{0u,1u,4u,5u}){
        const auto& d=family==0?SpasReloadDescriptor:family==1?Xm8ReloadDescriptor:AekMagazineNativeProfile.configuration;
        const auto f=family==0?ReloadNativeFamily::SpasTube:ReloadNativeFamily::Xm8Magazine;
        auto s=Step(branch);s.profile=family==2?NativeMagazineProfileId::AuthoredAek:NativeMagazineProfileId::ScopedXm8;
        auto c=Context();c.inputFlags=flags;c.fireRequested=bool(flags&1);c.reloadRequested=bool(flags&4);
        const bool controlled=ManualEmptyFamilyStepEligible(s,f,Config(d),ReloadRequestCyclePhase::Arming,c,T+1);
        CHECK(controlled==!(flags&4));
        struct Call {unsigned byte=0,next=2,calls=0,flags=0;};Call call;call.flags=flags;
        const MagazineEmptyByteAccess access{&call,[](void* v,std::uint8_t expected,std::uint8_t replacement,std::uint8_t& observed)noexcept{
            auto& x=*static_cast<Call*>(v);observed=std::uint8_t(x.byte);if(x.byte==expected)x.byte=replacement;return true;}};
        MagazineEmptyByteOverride patch;
        RunMagazineEmptyByteOverride(access,0,controlled,[](void* v){auto& x=*static_cast<Call*>(v);++x.calls;
            // Exact state2 decision, positive reserve+loaded0, genuine reload4
            // versus unsolicited empty trigger. Original context is restored.
            if(!x.byte)x.next=10;},&call,patch);
        CHECK(call.calls==1&&call.byte==0&&call.next==((flags&4)?10u:2u));
        CHECK(patch.applied==controlled&&patch.restored==controlled);
        auto stale=s;stale.deadlineNs=T+1;CHECK(!ManualEmptyFamilyStepEligible(stale,f,Config(d),ReloadRequestCyclePhase::Arming,c,T+1));
        s.before.nextState=10;CHECK(!ManualEmptyFamilyStepEligible(s,f,Config(d),ReloadRequestCyclePhase::Arming,c,T+1));
    }return 0;
}
int UnsupportedUnderbarrelsRemainAutomatic(){
    for(const char* asset:{"40mmgl","40mmsmoke","40mmshotgun"}){auto c=Config(SpasReloadDescriptor);c.assetName={};std::memcpy(c.assetName.data(),asset,std::strlen(asset));
        for(auto f:{ReloadNativeFamily::SpasTube,ReloadNativeFamily::Xm8Magazine}){
            CHECK(!ManualEmptyFamilyConfig(f,NativeMagazineProfileId::ScopedXm8,c));
            CHECK(!ManualEmptyFamilyStepEligible(Step(),f,c,ReloadRequestCyclePhase::Idle,Context(),T+1));
        }
    }return 0;
}
}
int main(){if(ArmingIntentUsesRealScopedOverride()||ExactSupportedFamilies()||ShellDescriptorRejectsEveryChangedField()||DoubleReadNativeTiming()||ShellCommonBoundaryAndExplicitManualCycle()||UnsupportedUnderbarrelsRemainAutomatic())return 1;
    std::puts("6 typed manual-empty family groups passed; exact SPAS and magazine descriptors, double-read timing, explicit shell Start pulse preservation, unsupported attachment pass-through");return 0;}
