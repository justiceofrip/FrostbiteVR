#include "Bc2WeaponVisibilityProbe.h"
#include "Bc2VisibilityDescriptorData.h"
#include <algorithm>
namespace fvr::bc2 {
namespace {
bool PalettePreserved(const WeaponVisibilityReceipt& r)noexcept {
    if(!r.evidence)return false;const auto& p=*r.evidence;
    if(p.reason!=WeaponVisibilityReason::None||p.rig!=r.rig||p.nativeOwner!=r.nativeOwner||p.request!=r.request||
        p.inputSequence!=r.inputSequence||p.physicalEquipGeneration!=r.physicalEquipGeneration||p.hidden!=r.hidden||
        p.ordinary.empty()||p.ordinary.size()!=p.privatePalette.size()||p.originalNative.size()!=p.ordinary.size()||
        p.weightedBones.empty()||p.weightedBones.size()>=p.ordinary.size())return false;
    for(std::size_t n=0;n<p.ordinary.size();++n){
        const bool weighted=std::find(p.weightedBones.begin(),p.weightedBones.end(),n)!=p.weightedBones.end();
        if((!weighted||!p.hidden)&&p.ordinary[n]!=p.privatePalette[n])return false;
        if(weighted&&p.hidden)for(unsigned row=0;row<4;++row)for(unsigned col=0;col<16;++col){
            const auto actual=p.privatePalette[n][row*16+col];
            if(col<12?actual!=std::byte{}:actual!=p.ordinary[n][row*16+col])return false;
        }
    }return true;
}
}
void Bc2WeaponVisibilityProbe::Cancel(unsigned why,std::int64_t now)noexcept {
    if(!enabled_||phase_==WeaponVisibilityProbePhase::Done||phase_==WeaponVisibilityProbePhase::Failed)return;
    phase_=WeaponVisibilityProbePhase::Failed;failure_=why;WeaponVisibilityProbeSample s;s.phase=phase_;s.sampledNs=now;Record(s,why);
}
void Bc2WeaponVisibilityProbe::Record(const WeaponVisibilityProbeSample& s,unsigned reason)noexcept {
    if(rowCount_>=rows_.size())return;
    if(rowCount_&&!reason&&rows_[rowCount_-1].phase==unsigned(s.phase)&&s.sampledNs-lastRow_<250000000)return;
    auto& row=rows_[rowCount_++];row.phase=unsigned(s.phase);row.reason=reason;row.now=s.sampledNs;
    row.input=s.intent.input.sequence;row.request=s.intent.request;row.hidden=s.intent.hide;
    row.sourceObserved=s.intent.input.observedNs;row.sourceDeadline=s.intent.input.deadlineNs;
    if(s.receipt){const auto& r=*s.receipt;row.draw=r.drawSerial;row.receiptInput=r.inputSequence;row.observed=r.observedNs;row.deadline=r.deadlineNs;
        row.weighted=unsigned(r.evidence->weightedBones.size());row.preserved=unsigned(r.evidence->ordinary.size())-row.weighted;}
    lastRow_=s.sampledNs;
}
WeaponVisibilityProbeSample Bc2WeaponVisibilityProbe::Tick(const ReloadStateOwner& owner,const interaction::HandInteractionSample& input,
    std::shared_ptr<const SelectedMeshesSnapshot> meshes,std::string_view asset,const std::optional<WeaponVisibilityReceipt>& receipt)noexcept {
    WeaponVisibilityProbeSample s;s.phase=phase_;s.sampledNs=input.nowNs;
    if(!enabled_||phase_==WeaponVisibilityProbePhase::Done||phase_==WeaponVisibilityProbePhase::Failed)return s;
    const auto fail=[&](unsigned why){Cancel(why,input.nowNs);s.phase=phase_;return s;};
    if(input.nowNs<=0||input.nowNs<lastNow_)return fail(1);lastNow_=input.nowNs;
    if(!started_)started_=input.nowNs;
    if(input.nowNs-started_>11000000000LL)return fail(2);
    const bool warming=phase_==WeaponVisibilityProbePhase::Warmup;
    if(warming&&input.nowNs-started_>2000000000LL)return fail(10);
    const bool fresh=input.sequence&&input.observedNs>0&&input.observedNs<=input.nowNs&&input.deadlineNs>input.nowNs&&
        input.deadlineNs-input.observedNs<=150000000&&input.focused&&input.tracked[0]&&input.tracked[1]&&
        owner.player>=0x10000&&owner.soldier>=0x10000&&owner.weak>=0x10000&&owner.weapon>=0x10000&&
        owner.actorGeneration&&owner.equipGeneration&&owner.space&&
        input.owner.actor==((std::uint64_t(owner.weak)<<32)|owner.soldier)&&input.owner.actorGeneration==owner.actorGeneration&&
        input.owner.equipGeneration&&input.owner.space==owner.space;
    // There is no render intent before the first complete owner/input/mesh
    // observation. An initially unavailable IPC packet is not a tracking loss
    // of an already acquired weapon. Never extend this bounded warmup or revive
    // a failed/expired request after a real baseline has begun.
    if(!fresh){if(warming){Record(s);return s;}return fail(3);}
    if(lastSequence_&&(input.sequence<lastSequence_||(input.sequence==lastSequence_&&
        (input.observedNs!=originalObserved_||input.deadlineNs!=originalDeadline_))))return fail(4);
    lastSequence_=input.sequence;originalObserved_=input.observedNs;originalDeadline_=input.deadlineNs;
    if(asset.empty()&&warming){Record(s);return s;}
    const bool legacy=asset=="SPAS12_sp"||asset=="XM8_sp_s";
    if(!meshes||meshes->owner!=owner||meshes->observedNs>input.nowNs||meshes->deadlineNs<=input.nowNs){
        if(!warming)return fail(6);Record(s);return s;
    }
    if(!legacy&&(!ResolveVisibilityDescriptor(*meshes,owner,VisibilityDescriptors,input.nowNs,true)||
        std::string_view(meshes->weaponName.data())!=asset))return fail(5);
    if(warming&&!legacy)configuration_=meshes;
    if(configuration_&&(meshes->weaponName!=configuration_->weaponName||meshes->weaponData!=configuration_->weaponData||
        meshes->states!=configuration_->states||meshes->stateCount!=configuration_->stateCount||
        meshes->soleConfiguredArray!=configuration_->soleConfiguredArray||meshes->meshTypeInfo!=configuration_->meshTypeInfo||
        meshes->stateTypeInfo!=configuration_->stateTypeInfo))return fail(13);
    if(phase_==WeaponVisibilityProbePhase::Warmup){owner_=owner;physical_=input.owner;phase_=WeaponVisibilityProbePhase::Baseline;phaseAt_=input.nowNs;request_=1;}
    if(owner_!=owner||physical_!=input.owner)return fail(7);
    s.phase=phase_;s.intent={true,phase_==WeaponVisibilityProbePhase::Hidden,request_,owner,input,meshes};
    if(configuration_)s.intent.authorizationDeadlineNs=started_+11000000000ll;
    const auto phaseIndex=unsigned(phase_)-unsigned(WeaponVisibilityProbePhase::Baseline);
    if(receipt&&receipt->nativeOwner==owner&&receipt->request==request_&&receipt->hidden==s.intent.hide&&receipt->verifiedCopyMask==3&&
        receipt->observedNs>0&&receipt->observedNs<=input.nowNs&&receipt->deadlineNs>input.nowNs&&receipt->drawSerial&&
        receipt->evidence&&WeaponVisibilityCurrent(*receipt->evidence,s.intent,input.nowNs)&&PalettePreserved(*receipt)){
        s.receipt=receipt;if(receipt->drawSerial!=lastDraw_[phaseIndex]){++receipts_[phaseIndex];lastDraw_[phaseIndex]=receipt->drawSerial;}
    }
    Record(s);
    // Hold each accepted phase long enough for existing receiver eye samples;
    // elapsed time alone never acknowledges hide or restoration.
    if(s.receipt&&receipts_[phaseIndex]>=2&&input.nowNs-phaseAt_>=2000000000LL){
        phase_=WeaponVisibilityProbePhase(unsigned(phase_)+1);phaseAt_=input.nowNs;++request_;
        s={};s.phase=phase_;s.sampledNs=input.nowNs;
        if(phase_!=WeaponVisibilityProbePhase::Done)s.intent={true,phase_==WeaponVisibilityProbePhase::Hidden,request_,owner,input,meshes};
        if(configuration_&&s.intent.enabled)s.intent.authorizationDeadlineNs=started_+11000000000ll;
        // The next Gather supplies its own fresh configured snapshot. Do not
        // extend the previous plan/receipt across this phase boundary.
        Record(s);
    }
    return s;
}
void Bc2WeaponVisibilityProbe::Report(std::ostream& out)const {
    out<<"{\"requested\":"<<(enabled_?"true":"false")<<",\"phase\":"<<unsigned(phase_)<<",\"failure\":"<<failure_
       <<",\"player\":"<<owner_.player<<",\"soldier\":"<<owner_.soldier<<",\"weak\":"<<owner_.weak<<",\"weapon\":"<<owner_.weapon
       <<",\"actor_generation\":"<<owner_.actorGeneration<<",\"native_equip_generation\":"<<owner_.equipGeneration
       <<",\"physical_equip_generation\":"<<physical_.equipGeneration<<",\"space\":"<<owner_.space
       <<",\"paired_private_palette_sequence_verified\":"<<(phase_==WeaponVisibilityProbePhase::Done?"true":"false")
       <<",\"empty_hands_acknowledged\":false,\"gpu_visibility_verified\":false,\"headset_verified\":false,\"receipts\":["
       <<receipts_[0]<<','<<receipts_[1]<<','<<receipts_[2]<<"],\"rows\":[";
    for(unsigned n=0;n<rowCount_;++n){if(n)out<<',';const auto& r=rows_[n];out<<"{\"phase\":"<<r.phase<<",\"reason\":"<<r.reason<<",\"now_ns\":"<<r.now
        <<",\"input\":"<<r.input<<",\"request\":"<<r.request<<",\"source_observed_ns\":"<<r.sourceObserved<<",\"source_deadline_ns\":"<<r.sourceDeadline
        <<",\"hidden\":"<<(r.hidden?"true":"false")<<",\"draw_serial\":"<<r.draw
        <<",\"receipt_input\":"<<r.receiptInput<<",\"receipt_observed_ns\":"<<r.observed<<",\"receipt_deadline_ns\":"<<r.deadline
        <<",\"weighted_entries\":"<<r.weighted<<",\"ordinary_entries_preserved\":"<<r.preserved<<'}';}out<<"]}";
}
}
