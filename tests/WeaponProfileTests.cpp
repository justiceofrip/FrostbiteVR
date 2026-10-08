#include "Test.h"
#include "Bc2WeaponProfiles.h"
#include <limits>
#include <string>
using namespace fvr;
using namespace interaction;
namespace {
math::Matrix4 Identity(){math::Matrix4 m{};for(unsigned i=0;i<4;++i)m.values[i][i]=1;return m;}
math::Vec3 Direction(math::Vec3 p,const math::Matrix4& m){
    return {p.x*m.values[0][0]+p.y*m.values[1][0]+p.z*m.values[2][0],
        p.x*m.values[0][1]+p.y*m.values[1][1]+p.z*m.values[2][1],
        p.x*m.values[0][2]+p.y*m.values[1][2]+p.z*m.values[2][2]};
}
bool Same(math::Vec3 a,math::Vec3 b){return Near(a.x,b.x)&&Near(a.y,b.y)&&Near(a.z,b.z);}
}
int main(){
    CHECK(bc2::WeaponProfiles().size()==3);
    const auto* spas=bc2::FindWeaponProfile("SPAS12_sp");
    const auto* xm8=bc2::FindWeaponProfile("XM8_sp_s");
    CHECK(spas&&xm8&&spas!=xm8);
    CHECK(spas->telemetryKind==1&&xm8->telemetryKind==2);
    CHECK(spas->core.stableId=="bc2:SPAS12_sp"&&xm8->core.stableId=="bc2:XM8_sp_s");
    CHECK(bc2::FindWeaponProfileByTelemetryKind(1)==spas);
    CHECK(bc2::FindWeaponProfileByTelemetryKind(2)==xm8);
    CHECK(!bc2::FindWeaponProfileByTelemetryKind(0)&&!bc2::FindWeaponProfileByTelemetryKind(4));
    for(const auto name:{"","SPAS12","SPAS12_sp_extra","spas12_sp","XM8_sp","XM8_sp_s_extra","XM8_GL","40mm_GL","HG"}){
        const auto* unknown=bc2::FindWeaponProfile(name);CHECK(!unknown);
        for(unsigned n=0;n<static_cast<unsigned>(WeaponFeature::Count);++n){
            const auto feature=static_cast<WeaponFeature>(n);
            CHECK(WeaponFeatureStatus(nullptr,feature)==WeaponStatus::UnknownProfile);
            CHECK(!WeaponFeatureEnabled(nullptr,feature));
        }
    }
    const auto* launcher=bc2::FindWeaponProfile("40mmgl");CHECK(launcher&&launcher->telemetryKind==3);
    CHECK(WeaponFeatureEnabled(&launcher->core,WeaponFeature::AimAlignment));
    CHECK(WeaponFeatureStatus(&launcher->core,WeaponFeature::SupportGrip)==WeaponStatus::NativeVerified);
    CHECK(!WeaponFeatureEnabled(&launcher->core,WeaponFeature::TranslatedMuzzle));
    CHECK(launcher->core.features[1].headsetEvidence.empty());
    const std::string embedded("SPAS12_sp\0extra",15);
    CHECK(!bc2::FindWeaponProfile(embedded));
    for(const auto* profile:{spas,xm8}){
        CHECK(ValidateWeaponProfile(profile->core));
        CHECK(WeaponFeatureStatus(&profile->core,WeaponFeature::AimAlignment)==WeaponStatus::NativeVerified);
        CHECK(WeaponFeatureStatus(&profile->core,WeaponFeature::SupportGrip)==WeaponStatus::HeadsetAccepted);
        CHECK(WeaponFeatureStatus(&profile->core,WeaponFeature::TranslatedMuzzle)==WeaponStatus::NativeVerified);
        for(unsigned n=0;n<3;++n)CHECK(WeaponFeatureEnabled(&profile->core,static_cast<WeaponFeature>(n)));
    }

    // Evidence applies per feature: one tested capability must not enable the
    // other two, and a capture/reference alone does not count as verification.
    auto candidate=spas->core;candidate.features={};
    for(unsigned n=0;n<3;++n){
        const auto feature=static_cast<WeaponFeature>(n);
        CHECK(WeaponFeatureStatus(&candidate,feature)==WeaponStatus::Unverified);
        CHECK(!WeaponFeatureEnabled(&candidate,feature));
    }
    candidate.features[0]={WeaponVerification::NativeVerified,"fixture/aim",{}};
    CHECK(WeaponFeatureEnabled(&candidate,WeaponFeature::AimAlignment));
    CHECK(!WeaponFeatureEnabled(&candidate,WeaponFeature::SupportGrip));
    CHECK(!WeaponFeatureEnabled(&candidate,WeaponFeature::TranslatedMuzzle));
    candidate.features[0]={};
    candidate.features[1]={WeaponVerification::NativeVerified,"fixture/grip",{}};
    CHECK(!WeaponFeatureEnabled(&candidate,WeaponFeature::AimAlignment));
    CHECK(WeaponFeatureEnabled(&candidate,WeaponFeature::SupportGrip));
    CHECK(!WeaponFeatureEnabled(&candidate,WeaponFeature::TranslatedMuzzle));
    CHECK(!WeaponAimFrame(candidate,Identity()));
    candidate.features={};
    candidate.features[2]={WeaponVerification::NativeVerified,"fixture/muzzle",{}};
    CHECK(WeaponFeatureEnabled(&candidate,WeaponFeature::TranslatedMuzzle));
    CHECK(!WeaponFeatureEnabled(&candidate,WeaponFeature::AimAlignment));
    CHECK(!WeaponFeatureEnabled(&candidate,WeaponFeature::SupportGrip));

    // Grip and muzzle evidence need no aim-axis claim. Missing axes are distinct
    // from supplied malformed axes, and never enable the aiming transform.
    auto supportOnly=candidate;supportOnly.features={};supportOnly.modelForward={};supportOnly.modelUp={};
    supportOnly.features[1]={WeaponVerification::NativeVerified,"fixture/grip",{}};
    CHECK(ValidateWeaponProfile(supportOnly));
    CHECK(WeaponFeatureEnabled(&supportOnly,WeaponFeature::SupportGrip));
    CHECK(!WeaponFeatureEnabled(&supportOnly,WeaponFeature::AimAlignment));
    CHECK(!WeaponFeatureEnabled(&supportOnly,WeaponFeature::TranslatedMuzzle));
    CHECK(!WeaponAimFrame(supportOnly,Identity()));
    auto missingAxes=supportOnly;missingAxes.features[0]={WeaponVerification::NativeVerified,"fixture/aim",{}};
    CHECK(!ValidateWeaponProfile(missingAxes));
    CHECK(!WeaponFeatureEnabled(&missingAxes,WeaponFeature::AimAlignment));
    missingAxes.features[0]={WeaponVerification::HeadsetAccepted,"fixture/aim","fixture/headset"};
    CHECK(!ValidateWeaponProfile(missingAxes));
    auto badOptionalAxes=supportOnly;badOptionalAxes.modelForward={0,0,1};
    CHECK(!ValidateWeaponProfile(badOptionalAxes)); // Partly supplied axes.
    badOptionalAxes=supportOnly;badOptionalAxes.modelUp.x=std::numeric_limits<float>::quiet_NaN();
    CHECK(!ValidateWeaponProfile(badOptionalAxes));
    badOptionalAxes=supportOnly;badOptionalAxes.modelForward={0,0,2};badOptionalAxes.modelUp={0,1,0};
    CHECK(!ValidateWeaponProfile(badOptionalAxes));
    supportOnly.features={};supportOnly.features[2]={WeaponVerification::NativeVerified,"fixture/muzzle",{}};
    CHECK(ValidateWeaponProfile(supportOnly));
    CHECK(WeaponFeatureEnabled(&supportOnly,WeaponFeature::TranslatedMuzzle));

    auto invalid=spas->core;invalid.features[0].nativeEvidence={};
    CHECK(!ValidateWeaponProfile(invalid));
    CHECK(WeaponFeatureStatus(&invalid,WeaponFeature::SupportGrip)==WeaponStatus::InvalidProfile);
    invalid=spas->core;invalid.features[1].headsetEvidence={};CHECK(!ValidateWeaponProfile(invalid));
    invalid=spas->core;invalid.features[0].verification=static_cast<WeaponVerification>(255);CHECK(!ValidateWeaponProfile(invalid));
    invalid=spas->core;invalid.stableId={};CHECK(!ValidateWeaponProfile(invalid));
    invalid=spas->core;invalid.stableId=std::string_view(embedded);CHECK(!ValidateWeaponProfile(invalid));
    invalid=spas->core;invalid.revision=0;CHECK(!ValidateWeaponProfile(invalid));
    invalid=spas->core;invalid.modelForward={};CHECK(!ValidateWeaponProfile(invalid));
    invalid=spas->core;invalid.modelForward={0,0,-2};CHECK(!ValidateWeaponProfile(invalid));
    invalid=spas->core;invalid.modelUp=invalid.modelForward;CHECK(!ValidateWeaponProfile(invalid));
    invalid=spas->core;invalid.modelUp.x=std::numeric_limits<float>::quiet_NaN();CHECK(!ValidateWeaponProfile(invalid));
    CHECK(WeaponFeatureStatus(&spas->core,WeaponFeature::Count)==WeaponStatus::InvalidProfile);
    CHECK(!WeaponFeatureEnabled(&spas->core,static_cast<WeaponFeature>(255)));

    // Both current profiles must reproduce the accepted row-0/row-2 sign
    // conversion for arbitrary aim orientation, not just an identity fixture.
    auto yaw=Identity();const float y=.63f;
    yaw.values[0]={std::cos(y),0,std::sin(y),0};yaw.values[2]={-std::sin(y),0,std::cos(y),0};
    auto roll=Identity();const float r=-.27f;
    roll.values[0]={std::cos(r),std::sin(r),0,0};roll.values[1]={-std::sin(r),std::cos(r),0,0};
    auto aim=Multiply(roll,yaw);aim.values[3]={4,5,6,1};
    for(const auto* profile:{spas,xm8}){
        auto expected=aim;expected.values[3]={0,0,0,1};
        for(unsigned column=0;column<3;++column){expected.values[0][column]*=-1;expected.values[2][column]*=-1;}
        const auto oriented=WeaponAimFrame(profile->core,aim);CHECK(oriented);
        for(unsigned row=0;row<4;++row)for(unsigned column=0;column<4;++column)
            CHECK(Near(oriented->values[row][column],expected.values[row][column]));
        CHECK(Same(Direction(profile->core.modelForward,*oriented),Direction({0,0,1},aim)));
        CHECK(Same(Direction(profile->core.modelUp,*oriented),Direction({0,1,0},aim)));
    }
    // A future verified adapter may use a different model axis convention.
    // Test the transform semantics rather than hardcoding the current signs.
    auto other=spas->core;other.modelForward={1,0,0};other.modelUp={0,0,1};
    const auto otherFrame=WeaponAimFrame(other,aim);CHECK(otherFrame);
    CHECK(Same(Direction(other.modelForward,*otherFrame),Direction({0,0,1},aim)));
    CHECK(Same(Direction(other.modelUp,*otherFrame),Direction({0,1,0},aim)));
    auto reflection=Identity();reflection.values[0][0]=-1;CHECK(!WeaponAimFrame(spas->core,reflection));
    auto malformed=Identity();malformed.values[0][0]=2;CHECK(!WeaponAimFrame(spas->core,malformed));
    malformed=Identity();malformed.values[1][2]=std::numeric_limits<float>::infinity();CHECK(!WeaponAimFrame(spas->core,malformed));
    malformed=Identity();malformed.values[0][3]=1;CHECK(!WeaponAimFrame(spas->core,malformed));
    CHECK(!WeaponAimFrame(invalid,aim));
    return 0;
}
