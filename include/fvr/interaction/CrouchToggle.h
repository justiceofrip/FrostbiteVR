#pragma once
#include <cstdint>
namespace fvr::interaction {
struct CrouchToggleSample {
    std::uint64_t owner=0,ownerGeneration=0,space=0,menuEpoch=0,sequence=0;
    std::int64_t timeNs=0,nowNs=0;
    // The adapter supplies current gameplay, tracking, button availability and
    // normal action arming. No native or menu authority is inferred here.
    bool eligible=false,leftHeld=false,rightHeld=false;
};
// A completed toggle-button click changes posture. leftHeld is the toggle
// source; rightHeld cancels a click for a menu/recenter chord. Waiting for
// release lets a staggered chord take ownership without changing posture.
class CrouchToggle {
public:
    bool Update(const CrouchToggleSample& s)noexcept {
        if(s.owner&&s.ownerGeneration&&(s.owner!=owner_||s.ownerGeneration!=ownerGeneration_)){
            owner_=s.owner;ownerGeneration_=s.ownerGeneration;latched_=false;
        }
        const bool fresh=s.nowNs>0&&s.timeNs>0&&
            (s.timeNs>=s.nowNs?s.timeNs-s.nowNs<=50000000:s.nowNs-s.timeNs<=100000000);
        const bool valid=s.eligible&&s.owner&&s.ownerGeneration&&s.space&&s.menuEpoch&&s.sequence&&fresh;
        const bool context=s.owner==previous_.owner&&s.ownerGeneration==previous_.ownerGeneration&&
            s.space==previous_.space&&s.menuEpoch==previous_.menuEpoch;
        const bool ordered=s.sequence>=previous_.sequence&&s.timeNs>=previous_.timeNs&&
            (s.sequence==previous_.sequence||s.timeNs>previous_.timeNs);
        const bool gap=previous_.timeNs&&s.timeNs>previous_.timeNs&&s.timeNs-previous_.timeNs>150000000;
        if(!valid||!context||!ordered||gap){
            previous_=s;armed_=candidate_=false;return false;
        }
        if(s.sequence==previous_.sequence){
            if(s.timeNs!=previous_.timeNs||s.leftHeld!=previous_.leftHeld||s.rightHeld!=previous_.rightHeld)
                armed_=candidate_=false;
            return latched_;
        }
        previous_=s;
        if(s.rightHeld){candidate_=armed_=false;return latched_;}
        if(!s.leftHeld){
            if(candidate_)latched_=!latched_;
            candidate_=false;armed_=true;
        }else if(armed_){candidate_=true;armed_=false;}
        return latched_;
    }
private:
    CrouchToggleSample previous_{};
    // Authority gaps suspend output and cancel clicks. The same actor keeps
    // desired posture, so recentering or closing a menu cannot stand them up.
    std::uint64_t owner_=0,ownerGeneration_=0;
    bool latched_=false,armed_=false,candidate_=false;
};
}
