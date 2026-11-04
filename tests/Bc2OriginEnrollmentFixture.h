// ROOT compile/run fixture; successor396, not executed by396. Requires393 header on include path
// and canonical FVR_BC2_MUZZLE_ENROLLMENT_HEADER (same definition as gameplay).
#include "CandidateOriginEnrollment392.generated.h"
#include <cassert>
#include <iostream>
int main() {
    using namespace fvr::bc2;
    const auto& table=origin390_enrollment::Enrollments;
    unsigned firearms=0,parts=0,forced=0,pellets=0;
    assert(table.size()==238);
    for(std::size_t i=0;i<table.size();++i){
        const auto& p=table[i];assert(p.admission==origin388::Admission::Candidate);
        assert(bool(p.authoredFirearm)!=bool(p.observedPart));
        assert(p.activeState==0&&!p.activeStateEvidence.empty()&&!p.coordinateBoundaryEvidence.empty());
        assert(p.maximumDisplacementMetres==2&&p.maximumAbsoluteWorldUnits==std::numeric_limits<float>::max());
        assert(p.route.cameraFlags[2]==0);
        firearms+=p.authoredFirearm!=nullptr;parts+=p.observedPart!=nullptr;
        forced+=p.route.cameraFlags[1]!=0;pellets+=p.route.bulletsPerShell>1;
        for(std::size_t j=0;j<i;++j){
            if(p.authoredFirearm)assert(p.authoredFirearm!=table[j].authoredFirearm);
            if(p.observedPart)assert(p.observedPart!=table[j].observedPart);
        }
    }
    assert(firearms==182&&parts==56&&forced>0&&pellets>0);
    assert(muzzle_part_observation::Profiles294.size()==80);
    unsigned hk416=0;
    for(const auto& e:table)if(e.observedPart&&e.observedPart->configuration.find("/UL_rgl_HK416/")!=std::string_view::npos){
        ++hk416;assert(e.observedPart->expectedIndex==34&&e.observedPart->localExitMeters.z<-.13f);
        assert(e.observedPart->configuration.find("UL_smk_")==std::string_view::npos);
    }
    assert(hk416==6);
    const auto allDisabled=[](const auto& v){for(const auto& e:v)if(e.admission!=origin388::Admission::Disabled)return false;return true;};
    auto review=origin392::SelectedReview;review.maximumAbsoluteWorldUnits=0;
    assert(allDisabled(origin392::Make(generated_muzzle::MuzzleEnrollments,muzzle_part_observation::Profiles294,review)));
    review=origin392::SelectedReview;review.coordinateBoundaryEvidence={};
    assert(allDisabled(origin392::Make(generated_muzzle::MuzzleEnrollments,muzzle_part_observation::Profiles294,review)));
    auto firearmsCopy=generated_muzzle::MuzzleEnrollments;firearmsCopy[3].bulletsPerShell+=1;
    assert(allDisabled(origin392::Make(firearmsCopy,muzzle_part_observation::Profiles294,origin392::SelectedReview)));
    auto partCopy=muzzle_part_observation::Profiles294;partCopy[0].localExitMeters.z+=.01f;
    assert(allDisabled(origin392::Make(generated_muzzle::MuzzleEnrollments,partCopy,origin392::SelectedReview)));
    partCopy=muzzle_part_observation::Profiles294;partCopy[71].localExitMeters.z+=.01f;
    assert(allDisabled(origin392::Make(generated_muzzle::MuzzleEnrollments,partCopy,origin392::SelectedReview)));
    std::cout<<"238 exact candidates; identity, routes, provenance and fail-closed factory checks passed\n";
}
