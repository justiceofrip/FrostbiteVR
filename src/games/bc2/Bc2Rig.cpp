#include "Bc2Rig.h"
#include <algorithm>
#include <cstring>
#include <cmath>
namespace fvr::bc2 {
namespace {
class Reader {
    const RigMemory& memory;
public:
    explicit Reader(const RigMemory& m):memory(m){}
    bool Read(std::uint32_t at,void* out,std::size_t n)const {return memory.read&&at>=0x10000&&n&&n<=1024*1024&&std::uint64_t(at)+n<=UINT32_MAX&&memory.read(memory.context,at,out,n);}
    std::uint32_t U32(std::uint32_t at)const {std::uint32_t value=0;Read(at,&value,4);return value;}
    bool Identity(std::uint32_t soldier,std::uint32_t weak,RigIdentity& i)const {
        if(soldier<0x10000||soldier>UINT32_MAX-0x500||U32(weak)!=soldier+4)return false;
        unsigned char flags=0;if(!Read(soldier+0x475,&flags,1)||!(flags&1))return false;
        i.soldier=soldier;i.weak=weak;i.animation=U32(soldier+0x3b4);
        const auto root=U32(soldier+0x3b8);if(!root||U32(root+4)!=soldier||i.animation<0x10000||i.animation>UINT32_MAX-0xa00)return false;
        i.skeleton=U32(i.animation+0x50);i.pose=U32(i.animation+0x54);
        if(i.skeleton<0x10000||i.skeleton>UINT32_MAX-0x40||i.pose<0x10000||i.pose>UINT32_MAX-0x30)return false;
        i.count=U32(i.skeleton+0x10);if(i.count<6||i.count>1024||U32(i.pose+0x28)!=i.skeleton||U32(i.pose+0x1c)!=i.count)return false;
        i.worldHeader=U32(i.pose+0x18);if(i.worldHeader<0x10000||i.worldHeader>UINT32_MAX-12||U32(i.worldHeader)!=i.count)return false;
        i.worldMatrices=U32(i.worldHeader+4);i.skinMatrices=U32(i.worldHeader+8);
        unsigned char custom=0;if(!Read(i.animation+0x99c,&custom,1)||custom>1)return false;i.nativeIk=custom!=0;
        i.evaluatedMatrices=i.nativeIk?U32(i.animation+0x31c):i.skinMatrices;
        for(auto at:{i.worldMatrices,i.skinMatrices,i.evaluatedMatrices})if(at<0x10000||std::uint64_t(at)+i.count*64ull>UINT32_MAX)return false;return true;
    }
    std::optional<std::string> Name(std::uint32_t at)const {
        std::string out;for(unsigned n=0;n<64;++n){unsigned char c=0;if(at>UINT32_MAX-n||!Read(at+n,&c,1))return {};if(!c)return out.empty()?std::nullopt:std::optional(out);if(c<32||c>126)return {};out.push_back(char(c));}return {};
    }
};
}
std::optional<RigSnapshot> ReadFirstPersonRig(const RigMemory& memory,std::uint32_t soldier,std::uint32_t weak){
    Reader read(memory);RigSnapshot result{};auto& id=result.identity;if(!read.Identity(soldier,weak,id))return {};
    const auto names=read.U32(id.skeleton+0x18),namesEnd=read.U32(id.skeleton+0x1c),definition=read.U32(id.skeleton+0xc);
    if(names<0x10000||std::uint64_t(names)+id.count*4ull!=namesEnd||definition<0x10000||definition>UINT32_MAX-12||read.U32(definition+4)!=id.count||read.U32(id.pose+0x10)!=definition)return {};
    const auto bones=read.U32(definition+8);constexpr unsigned boneStride=0x98;
    if(bones<0x10000||std::uint64_t(bones)+id.count*std::uint64_t(boneStride)>UINT32_MAX)return {};
    result.names.reserve(id.count);result.parents.reserve(id.count);
    for(unsigned n=0;n<id.count;++n){
        const auto record=bones+n*boneStride,name=read.U32(record);if(!name||name!=read.U32(names+n*4))return {};
        const auto text=read.Name(name);std::int32_t parent=0;if(!text||!read.Read(record+4,&parent,4)||parent< -1||parent>=std::int32_t(id.count))return {};
        if(std::find(result.names.begin(),result.names.end(),*text)!=result.names.end())return {};
        result.names.push_back(*text);result.parents.push_back(parent);
    }
    const auto bone=[&](const char* name)->std::optional<unsigned>{const auto i=std::find(result.names.begin(),result.names.end(),name);if(i==result.names.end())return {};return unsigned(i-result.names.begin());};
    const auto ls=bone("LeftArm"),le=bone("LeftForeArm"),lw=bone("LeftHand"),rs=bone("RightArm"),re=bone("RightForeArm"),rw=bone("RightHand");
    if(!ls||!le||!lw||!rs||!re||!rw)return {};result.left={*ls,*le,*lw};result.right={*rs,*re,*rw};
    if(!interaction::ValidateArms(result.parents,result.left,result.right))return {};
    result.weaponBone=read.U32(id.animation+0x938);if(result.weaponBone>=id.count||result.names[result.weaponBone].rfind("jntWpn_",0)!=0)return {};
    result.nativeWorld.resize(id.count);result.world.reserve(id.count);
    result.nativeEvaluated.resize(id.count);result.inverseBind.reserve(id.count);result.evaluatedWorld.reserve(id.count);
    if(!read.Read(id.worldMatrices,result.nativeWorld.data(),id.count*64))return {};
    if(!read.Read(id.evaluatedMatrices,result.nativeEvaluated.data(),id.count*64))return {};
    const auto canonical=[](const std::array<std::byte,64>& bytes){math::Matrix4 m{};std::memcpy(&m,bytes.data(),64);
        for(unsigned row=0;row<4;++row)m.values[row][3]=row==3?1.f:0.f;
        for(unsigned i=0;i<4;++i){m.values[2][i]=-m.values[2][i];m.values[i][2]=-m.values[i][2];}return m;
    };
    std::vector<std::array<std::byte,64>> baseSkin(id.count);if(!read.Read(id.skinMatrices,baseSkin.data(),id.count*64))return {};
    for(unsigned n=0;n<id.count;++n){
        const auto world=canonical(result.nativeWorld[n]);result.world.push_back(world);
        std::array<std::byte,64> bytes{};if(!read.Read(bones+n*boneStride+0x4c,bytes.data(),64))return {};
        const auto inverseBind=canonical(bytes);const auto bind=interaction::InverseAnimatedTransform(inverseBind);if(!bind)return {};result.inverseBind.push_back(inverseBind);
        const auto skin=canonical(baseSkin[n]),expected=interaction::Multiply(inverseBind,world);
        // Observed SPAS reload hides this leaf with a 1e-4 diagonal basis in
        // all three native palettes. Its translation no longer obeys bind*world.
        // Preserve its exact native bytes; it is never an IK/attachment frame.
        const auto collapsed=[](const math::Matrix4& m){
            for(const auto& row:m.values)for(float x:row)if(!std::isfinite(x))return false;
            for(unsigned r=0;r<3;++r)for(unsigned c=0;c<3;++c)
                if(std::abs(m.values[r][c]-(r==c?.0001f:0.f))>1e-7f)return false;
            return true;
        };
        if(result.names[n]=="jntWpn_7"&&n!=result.weaponBone&&result.parents[n]==std::int32_t(result.weaponBone)&&
           std::find(result.parents.begin(),result.parents.end(),std::int32_t(n))==result.parents.end()&&
           collapsed(world)&&collapsed(skin)&&collapsed(canonical(result.nativeEvaluated[n]))){
            result.nativeHiddenLeaves.push_back(n);
            result.evaluatedWorld.push_back(interaction::Multiply(*bind,canonical(result.nativeEvaluated[n])));
            continue;
        }
        if(!interaction::InverseAnimatedTransform(world)||!interaction::InverseAnimatedTransform(skin))return {};
        for(unsigned r=0;r<4;++r)for(unsigned c=0;c<3;++c)result.skinConsistencyError=std::max(result.skinConsistencyError,std::abs(expected.values[r][c]-skin.values[r][c]));
        // The final native IK palette can differ from the ordinary bone pose.
        // Recover its posed world joints via bind * skin, keeping authored IK.
        const auto evaluated=interaction::Multiply(*bind,canonical(result.nativeEvaluated[n]));
        if(!interaction::InverseAnimatedTransform(evaluated))return {};result.evaluatedWorld.push_back(evaluated);
    }
    // A mismatched snapshot or unverified palette convention is not a binding.
    if(result.skinConsistencyError>.002f)return {};
    RigIdentity after{};if(!read.Identity(soldier,weak,after)||after!=id)return {};return result;
}
std::optional<RigPosePlan> BuildRigPosePlan(const RigSnapshot& rig,std::span<const interaction::BoneWrite> writes){
    const auto& id=rig.identity;
    if(!id.count||id.count>1024||rig.nativeEvaluated.size()!=id.count||rig.inverseBind.size()!=id.count||rig.evaluatedWorld.size()!=id.count||writes.empty()||writes.size()>id.count||id.evaluatedMatrices<0x10000||std::uint64_t(id.evaluatedMatrices)+id.count*64ull>UINT32_MAX)return {};
    RigPosePlan plan;plan.identity=id;auto& out=plan.edits;out.reserve(writes.size());
    for(const auto& w:writes){
        if(std::find(rig.nativeHiddenLeaves.begin(),rig.nativeHiddenLeaves.end(),w.index)!=rig.nativeHiddenLeaves.end())return {};
        if(w.index>=id.count||!interaction::InverseAnimatedTransform(w.transform)||!interaction::InverseAnimatedTransform(rig.inverseBind[w.index]))return {};
        for(const auto& prior:out)if(prior.index==w.index)return {};
        auto skin=interaction::Multiply(rig.inverseBind[w.index],w.transform);
        // Reverse the canonical basis conversion and replace xyz only. Native
        // SIMD padding must survive even when it contains non-float metadata.
        for(unsigned i=0;i<4;++i){skin.values[2][i]=-skin.values[2][i];skin.values[i][2]=-skin.values[i][2];}
        RigBoneEdit edit{w.index,id.evaluatedMatrices+w.index*64,rig.nativeEvaluated[w.index],rig.nativeEvaluated[w.index]};
        for(unsigned row=0;row<4;++row)std::memcpy(edit.after.data()+row*16,skin.values[row].data(),12);
        out.push_back(edit);
    }
    return plan;
}
}
