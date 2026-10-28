#pragma once
#include <cstdint>

namespace fvr::graphics {
// Diagnostic identity, not a rendering or resource-reuse authority.
struct SceneWitnessKey {
    std::uint64_t world=0,request=0,nativeFrame=0,deviceEpoch=0,channel=0;
    std::uint64_t tracking=0,space=0;
    std::int64_t predictedNs=0;
    std::uint64_t mainView=0,rightView=0;
    std::uint32_t sourceSlot=0,width=0,height=0;
    std::uint64_t nativeOwner=0,nativeRequest=0;
    bool operator==(const SceneWitnessKey&)const=default;
};
inline constexpr unsigned SceneWitnessPairs=3,SceneWitnessDrawsPerEye=256;
inline constexpr std::uint64_t SceneWitnessMaxPixels=8418816;
inline constexpr std::uint64_t SceneWitnessBufferBudget=8ull*1024*1024;
inline constexpr std::uint64_t SceneWitnessMetadataBudget=8ull*1024*1024;
inline constexpr std::uint64_t SceneWitnessEyeBufferBudget=SceneWitnessBufferBudget/(SceneWitnessPairs*2);
inline constexpr unsigned SceneWitnessEyeBufferCopies=128,SceneWitnessMaxFirstOrdinal=100000;
inline constexpr bool SceneWitnessDimensions(unsigned width,unsigned height)noexcept {
    return width&&height&&width<=16384&&height<=16384&&
        std::uint64_t(width)*height<=SceneWitnessMaxPixels;
}
inline constexpr bool SceneWitnessValid(const SceneWitnessKey& k)noexcept {
    return k.world&&k.request&&k.nativeFrame&&k.deviceEpoch&&k.channel&&k.tracking&&k.space&&
        k.mainView&&k.rightView&&k.mainView!=k.rightView&&k.sourceSlot<3&&SceneWitnessDimensions(k.width,k.height);
}
enum class SceneWitnessDrawKind : unsigned {Indexed,Vertices,IndexedInstanced,Instanced,Auto,IndexedIndirect,Indirect};
struct SceneWitnessDrawArguments {
    SceneWitnessDrawKind kind=SceneWitnessDrawKind::Indexed;
    std::uint32_t count=0,instances=0,start=0,firstInstance=0;
    std::int32_t base=0;
    std::uint64_t argumentBuffer=0;
    std::uint32_t argumentOffset=0;
};
// Strictly sequential requested pairs. A mismatched second eye consumes the
// request as failed; it cannot be silently combined with a later frame.
class SceneWitnessPairGate {
public:
    bool Begin(unsigned sequence,const SceneWitnessKey& key,unsigned eye,std::uint64_t view)noexcept {
        if(active_){failed_=true;active_=false;return false;}
        if(eye>1||!SceneWitnessValid(key)||view!=(eye?key.rightView:key.mainView)){
            if(sequence==sequence_&&mask_==1)failed_=true;
            return false;
        }
        if(eye==0){
            if(sequence!=sequence_+1||sequence>SceneWitnessPairs||(mask_==1&&!failed_))return false;
            sequence_=sequence;key_=key;mask_=0;failed_=false;
        }else if(sequence!=sequence_||mask_!=1||failed_||key!=key_){
            if(sequence==sequence_&&mask_==1)failed_=true;
            return false;
        }
        eye_=eye;active_=true;return true;
    }
    bool End(const SceneWitnessKey& key,unsigned eye)noexcept {
        if(!active_||eye!=eye_||key!=key_){failed_=true;active_=false;return false;}
        mask_|=1u<<eye;active_=false;return true;
    }
    void Abandon()noexcept {failed_=true;active_=false;}
    bool Active()const noexcept{return active_;}
    bool Complete()const noexcept{return mask_==3&&!failed_&&!active_;}
    bool Failed()const noexcept{return failed_;}
    unsigned Sequence()const noexcept{return sequence_;}
    unsigned Eye()const noexcept{return eye_;}
    const SceneWitnessKey& Key()const noexcept{return key_;}
private:
    SceneWitnessKey key_{};unsigned sequence_=0,mask_=0,eye_=0;bool active_=false,failed_=false;
};
}
