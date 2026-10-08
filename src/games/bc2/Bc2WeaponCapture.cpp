#include "Bc2WeaponCapture.h"
#include <algorithm>
#include <iomanip>
#include <cmath>
#include <limits>
namespace fvr::bc2 {
namespace {
bool Same(const WeaponCaptureSample& a,const WeaponCaptureSample& b){
    return a.assetName==b.assetName&&a.weapon==b.weapon&&a.actor==b.actor&&a.ownerGeneration==b.ownerGeneration&&a.space==b.space&&a.skeleton==b.skeleton&&a.unitsPerMeter==b.unitsPerMeter&&a.provenance==b.provenance;
}
void String(std::ostream& o,const std::string& s){
    o<<'"';for(const unsigned char c:s){if(c=='"'||c=='\\')o<<'\\'<<char(c);else if(c<32){const char* hex="0123456789abcdef";o<<"\\u00"<<hex[c>>4]<<hex[c&15];}else o<<char(c);}o<<'"';
}
void Matrix(std::ostream& o,const math::Matrix4& m){o<<'[';for(unsigned r=0;r<4;++r)for(unsigned c=0;c<4;++c){if(r||c)o<<',';o<<m.values[r][c];}o<<']';}
}
bool WeaponCapture::Observe(WeaponCaptureSample sample){
    if(sample.assetName.empty()||sample.assetName.size()>63||sample.skeleton.empty()||!sample.generation||!sample.space||!sample.ownerGeneration||!sample.actor||!sample.weapon||!std::isfinite(sample.unitsPerMeter)||sample.unitsPerMeter<=0||
       !interaction::InverseAnimatedTransform(sample.nativeWeapon)||!interaction::InverseAnimatedTransform(sample.nativeLeft)||!interaction::InverseAnimatedTransform(sample.nativeRight)||
       (sample.nativeMuzzle&&!interaction::InverseAnimatedTransform(*sample.nativeMuzzle))){++rejected_;return false;}
    if(sample.provenance){const auto& p=*sample.provenance;
        if(!p.equipmentGeneration||!p.weaponData||!p.rig.weak||p.rig.soldier!=sample.actor||
           !p.rig.animation||!p.rig.skeleton||!p.rig.pose||!p.rig.count){++rejected_;return false;}}
    if(sample.nativeWeaponBones.size()>MaxWeaponBones){
        sample.weaponBonesDropped+=unsigned(sample.nativeWeaponBones.size()-MaxWeaponBones);
        sample.nativeWeaponBones.resize(MaxWeaponBones);
    }
    if(sample.weaponBonesDropped)sample.weaponBonesComplete=false;
    for(unsigned n=0;n<sample.nativeWeaponBones.size();++n){
        const auto& bone=sample.nativeWeaponBones[n];
        bool valid=!bone.name.empty()&&bone.name.size()<=63&&bone.parentName.size()<=63&&
            (!bone.inverseBind||bool(interaction::InverseAnimatedTransform(*bone.inverseBind)));
        for(const auto& row:bone.native.values)for(float v:row)valid=valid&&std::isfinite(v);
        for(unsigned prior=0;prior<n;++prior)valid=valid&&bone.name!=sample.nativeWeaponBones[prior].name;
        if(!valid){++rejected_;return false;}
    }
    if(!sample.leftHandBonesCaptured){
        if(!sample.nativeLeftHandBones.empty()||sample.leftHandBonesDropped||sample.leftHandBonesComplete){++rejected_;return false;}
    }else{
        auto& bones=sample.nativeLeftHandBones;
        if(bones.size()>MaxLeftHandBones){
            const auto dropped=bones.size()-MaxLeftHandBones;
            if(dropped>std::numeric_limits<unsigned>::max()-sample.leftHandBonesDropped){++rejected_;return false;}
            sample.leftHandBonesDropped+=unsigned(dropped);bones.resize(MaxLeftHandBones);
        }
        if(sample.leftHandBonesDropped)sample.leftHandBonesComplete=false;
        const auto find=[&](const std::string& name){return std::find_if(bones.begin(),bones.end(),[&](const auto& b){return b.name==name;});};
        const auto root=find(sample.leftName);
        if(sample.leftName.empty()||root==bones.end()||root->native.values!=sample.nativeLeft.values||find(root->parentName)!=bones.end()){
            ++rejected_;return false;
        }
        for(unsigned n=0;n<bones.size();++n){
            const auto& bone=bones[n];
            bool valid=!bone.name.empty()&&bone.name.size()<=63&&bone.parentName.size()<=63&&
                (!bone.inverseBind||bool(interaction::InverseAnimatedTransform(*bone.inverseBind)));
            for(const auto& row:bone.native.values)for(float value:row)valid=valid&&std::isfinite(value);
            // Authored hidden/collapsed leaves remain raw evidence. They cannot
            // supply a grasp transform; do not guess hidden state from scale.
            if(bone.hidden)valid=valid&&bones.begin()+n!=root&&
                std::none_of(bones.begin(),bones.end(),[&](const auto& other){return other.parentName==bone.name;});
            else if(sample.leftHandBonesComplete)valid=valid&&bool(interaction::InverseAnimatedTransform(bone.native));
            // Incomplete raw observations may include finite singular poses.
            // Their explicit completeness=false prevents use as grasp evidence.
            for(unsigned prior=0;prior<n;++prior)valid=valid&&bone.name!=bones[prior].name;
            if(!valid){++rejected_;return false;}
            // Incomplete snapshots may name an omitted ancestor, but cycles and
            // a complete subtree disconnected from its declared wrist are bad.
            auto at=bones.begin()+n;unsigned hops=0;
            while(at!=root){
                if(++hops>bones.size()){++rejected_;return false;}
                at=find(at->parentName);
                if(at==bones.end()){
                    if(sample.leftHandBonesComplete){++rejected_;return false;}
                    break;
                }
            }
        }
    }
    const bool transition=!previous_||!Same(*previous_,sample)||sample.capturedMs<previous_->capturedMs||sample.generation<previous_->generation;
    const auto attachment=interaction::Multiply(sample.nativeWeapon,*interaction::InverseAnimatedTransform(sample.nativeRight));
    if(transition){episodeSince_=stableSince_=sample.capturedMs;attachmentCandidate_=attachment;++episode_;}
    bool stable=true;
    for(unsigned r=0;r<4;++r)for(unsigned c=0;c<3;++c)
        if(std::abs(attachment.values[r][c]-attachmentCandidate_.values[r][c])>(r==3?.003f*sample.unitsPerMeter:.01f))stable=false;
    if(!stable){attachmentCandidate_=attachment;stableSince_=sample.capturedMs;}
    sample.equipAgeMs=sample.capturedMs-episodeSince_;sample.captureEpisode=episode_;
    sample.attachmentPending=sample.equipAgeMs<600||sample.capturedMs-stableSince_<150;
    previous_=sample;
    auto found=std::find_if(groups_.begin(),groups_.end(),[&](const auto& g){return Same(g.samples[(g.next+SamplesPerGroup-1)%SamplesPerGroup],sample);});
    if(found==groups_.end()){
        if(groups_.size()==MaxGroups){++capacityDropped_;return false;}
        groups_.emplace_back();found=groups_.end()-1;
    }
    if(found->count){
        const auto& last=found->samples[(found->next+SamplesPerGroup-1)%SamplesPerGroup];
        if(sample.generation==last.generation||(sample.capturedMs>=last.capturedMs&&sample.capturedMs-last.capturedMs<100))return false;
    }
    sample.captureSequence=observed_+1;
    found->samples[found->next]=std::move(sample);found->next=(found->next+1)%SamplesPerGroup;found->count=(std::min)(found->count+1,SamplesPerGroup);++observed_;return true;
}
std::vector<WeaponCaptureSample> WeaponCapture::Samples()const{
    std::vector<WeaponCaptureSample> out;
    for(const auto& group:groups_)for(unsigned n=0;n<group.count;++n)out.push_back(group.samples[(group.next+SamplesPerGroup-group.count+n)%SamplesPerGroup]);
    std::stable_sort(out.begin(),out.end(),[](const auto& a,const auto& b){return a.captureSequence<b.captureSequence;});return out;
}
void WeaponCapture::Report(std::ostream& out)const{
    out<<std::setprecision(9)<<"\"weapon_profile_capture\":{\"schema_version\":2,\"groups\":"<<groups_.size()<<",\"sample_limit_per_group\":"<<SamplesPerGroup<<",\"left_hand_bone_limit\":"<<MaxLeftHandBones<<",\"left_hand_pose_source\":\"native_evaluated_before_vr\",\"observations\":"<<observed_<<",\"invalid_samples\":"<<rejected_<<",\"capacity_dropped\":"<<capacityDropped_<<"},\"weapon_profile_samples\":[";
    const auto samples=Samples();
    for(unsigned n=0;n<samples.size();++n){const auto& s=samples[n];if(n)out<<',';
        out<<"{\"asset_name\":";String(out,s.assetName);out<<",\"profile_id\":";String(out,"bc2:"+s.assetName);
        out<<",\"skeleton\":";String(out,s.skeleton);
        // This collector observes a selected label and actor rig. It does not
        // have a submitted mesh backlink; no caller-controlled true flag exists.
        out<<",\"asset_label_source\":\"selected_soldier_weapon_data\",\"native_pose_source\":\"actor_first_person_rig_pre_vr\",\"pose_asset_binding_verified\":false,\"submitted_mesh_binding_verified\":false,\"capture_provenance\":";
        if(!s.provenance)out<<"null";
        else {const auto& p=*s.provenance;const auto& r=p.rig;
            out<<"{\"equipment_generation\":"<<p.equipmentGeneration<<",\"weapon_data\":"<<p.weaponData<<",\"persistence\":"<<p.persistence
               <<",\"rig\":{\"soldier\":"<<r.soldier<<",\"weak\":"<<r.weak<<",\"animation\":"<<r.animation<<",\"skeleton\":"<<r.skeleton
               <<",\"pose\":"<<r.pose<<",\"world_header\":"<<r.worldHeader<<",\"world_matrices\":"<<r.worldMatrices<<",\"skin_matrices\":"<<r.skinMatrices
               <<",\"evaluated_matrices\":"<<r.evaluatedMatrices<<",\"count\":"<<r.count<<",\"native_ik\":"<<(r.nativeIk?"true":"false")<<"}}";}

        out<<",\"capture_sequence\":"<<s.captureSequence<<",\"capture_episode\":"<<s.captureEpisode<<",\"units_per_meter\":"<<s.unitsPerMeter<<",\"generation\":"<<s.generation<<",\"space\":"<<s.space<<",\"owner_generation\":"<<s.ownerGeneration<<",\"actor\":"<<s.actor<<",\"weapon\":"<<s.weapon<<",\"captured_ms\":"<<s.capturedMs<<",\"predicted_ns\":"<<s.predictedNs<<",\"equip_age_ms\":"<<s.equipAgeMs<<",\"attachment_pending\":"<<(s.attachmentPending?"true":"false");
        out<<",\"bone_roles\":{\"weapon_root\":";String(out,s.rootName);out<<",\"left_wrist\":";String(out,s.leftName);out<<",\"right_wrist\":";String(out,s.rightName);out<<",\"muzzle\":";if(s.muzzleName.empty())out<<"null";else String(out,s.muzzleName);out<<'}';
        out<<",\"native\":";Matrix(out,s.nativeWeapon);out<<",\"native_left_wrist\":";Matrix(out,s.nativeLeft);out<<",\"native_right_wrist\":";Matrix(out,s.nativeRight);
        out<<",\"native_muzzle\":";if(s.nativeMuzzle)Matrix(out,*s.nativeMuzzle);else out<<"null";
        out<<",\"weapon_bones_complete\":"<<(s.weaponBonesComplete?"true":"false")<<",\"weapon_bones_dropped\":"<<s.weaponBonesDropped<<",\"native_weapon_bones\":[";
        for(unsigned b=0;b<s.nativeWeaponBones.size();++b){const auto& bone=s.nativeWeaponBones[b];if(b)out<<',';
            out<<"{\"name\":";String(out,bone.name);out<<",\"parent_name\":";if(bone.parentName.empty())out<<"null";else String(out,bone.parentName);
            out<<",\"hidden\":"<<(bone.hidden?"true":"false")<<",\"native\":";Matrix(out,bone.native);if(bone.inverseBind){out<<",\"inverse_bind\":";Matrix(out,*bone.inverseBind);}out<<'}';
        }out<<"],\"left_hand_bones_captured\":"<<(s.leftHandBonesCaptured?"true":"false")
            <<",\"left_hand_bones_complete\":"<<(s.leftHandBonesComplete?"true":"false")
            <<",\"left_hand_bones_dropped\":"<<s.leftHandBonesDropped<<",\"native_left_hand_bones\":[";
        for(unsigned b=0;b<s.nativeLeftHandBones.size();++b){const auto& bone=s.nativeLeftHandBones[b];if(b)out<<',';
            out<<"{\"name\":";String(out,bone.name);out<<",\"parent_name\":";if(bone.parentName.empty())out<<"null";else String(out,bone.parentName);
            out<<",\"hidden\":"<<(bone.hidden?"true":"false")<<",\"native\":";Matrix(out,bone.native);if(bone.inverseBind){out<<",\"inverse_bind\":";Matrix(out,*bone.inverseBind);}out<<'}';
        }out<<"]}";
    }out<<']';
}
}
