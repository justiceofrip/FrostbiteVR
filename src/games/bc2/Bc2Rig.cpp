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
    bool Blocks(std::uint32_t at,void* out,std::size_t n)const {
        if(at<0x10000||!n||n>1024*1024||std::uint64_t(at)+n>UINT32_MAX)return false;
        // The native RigMemory callback accepts at most 64 KiB per request.
        // Read fresh bytes every invocation; this is not a topology cache.
        for(std::size_t done=0;done<n;){const auto count=std::min<std::size_t>(n-done,65536);
            if(!Read(at+std::uint32_t(done),static_cast<std::byte*>(out)+done,count))return false;
            done+=count;
        }return true;
    }
    std::uint32_t U32(std::uint32_t at)const {std::uint32_t value=0;Read(at,&value,4);return value;}
    template<std::size_t N> bool Words(std::uint32_t at,std::array<std::uint32_t,N>& values)const {
        // Only adjacent used fields are grouped. Readers that expose individual
        // fields still work, and a partial failed read contributes no bytes.
        if(Read(at,values.data(),N*sizeof(std::uint32_t)))return true;
        for(std::size_t n=0;n<N;++n)if(!Read(at+std::uint32_t(n*4),&values[n],4))return false;
        return true;
    }
    bool Identity(std::uint32_t soldier,std::uint32_t weak,RigIdentity& i)const {
        if(soldier<0x10000||soldier>UINT32_MAX-0x500||U32(weak)!=soldier+4)return false;
        unsigned char flags=0;if(!Read(soldier+0x475,&flags,1)||!(flags&1))return false;
        std::array<std::uint32_t,2> owner{},rig{},pose{};std::array<std::uint32_t,3> header{};
        if(!Words(soldier+0x3b4,owner))return false;
        i.soldier=soldier;i.weak=weak;i.animation=owner[0];
        const auto root=owner[1];if(!root||U32(root+4)!=soldier||i.animation<0x10000||i.animation>UINT32_MAX-0xa00)return false;
        if(!Words(i.animation+0x50,rig))return false;i.skeleton=rig[0];i.pose=rig[1];
        if(i.skeleton<0x10000||i.skeleton>UINT32_MAX-0x40||i.pose<0x10000||i.pose>UINT32_MAX-0x30)return false;
        i.count=U32(i.skeleton+0x10);if(i.count<6||i.count>1024||U32(i.pose+0x28)!=i.skeleton)return false;
        if(!Words(i.pose+0x18,pose)||pose[1]!=i.count)return false;
        i.worldHeader=pose[0];if(i.worldHeader<0x10000||i.worldHeader>UINT32_MAX-12||!Words(i.worldHeader,header)||header[0]!=i.count)return false;
        i.worldMatrices=header[1];i.skinMatrices=header[2];
        unsigned char custom=0;if(!Read(i.animation+0x99c,&custom,1)||custom>1)return false;i.nativeIk=custom!=0;
        i.evaluatedMatrices=i.nativeIk?U32(i.animation+0x31c):i.skinMatrices;
        for(auto at:{i.worldMatrices,i.skinMatrices,i.evaluatedMatrices})if(at<0x10000||std::uint64_t(at)+i.count*64ull>UINT32_MAX)return false;return true;
    }
    std::optional<std::string> Name(std::uint32_t at)const {
        std::string out;std::array<unsigned char,64> bytes{};
        for(unsigned n=0;n<bytes.size();){
            if(at>UINT32_MAX-n)return {};const auto address=at+n;
            // BC2's x86 pages are 4 KiB. Never speculate into the next page
            // just to find a terminator that may already be in this one.
            const auto count=std::min({unsigned(bytes.size())-n,4096u-(address&4095u),UINT32_MAX-address});
            if(!count)return {};
            const bool block=Read(address,bytes.data(),count);
            for(unsigned i=0;i<count;++i){
                unsigned char c=bytes[i];
                // Some readers expose only the string's actual bytes. A failed
                // wider read must not reject a valid short, terminated name.
                if(!block&&!Read(address+i,&c,1))return {};
                if(!c)return out.empty()?std::nullopt:std::optional(out);
                if(c<32||c>126)return {};out.push_back(char(c));
            }n+=count;
        }return {};
    }
};
}
std::optional<RigIdentity> ReadFirstPersonRigIdentity(const RigMemory& memory,std::uint32_t soldier,std::uint32_t weak)noexcept {
    Reader read(memory);RigIdentity before{},after{};
    if(!read.Identity(soldier,weak,before)||!read.Identity(soldier,weak,after)||before!=after)return {};
    return before;
}
std::optional<RigSnapshot> ReadFirstPersonRig(const RigMemory& memory,std::uint32_t soldier,std::uint32_t weak){
    Reader read(memory);RigSnapshot result{};auto& id=result.identity;if(!read.Identity(soldier,weak,id))return {};
    const auto names=read.U32(id.skeleton+0x18),namesEnd=read.U32(id.skeleton+0x1c),definition=read.U32(id.skeleton+0xc);
    if(names<0x10000||std::uint64_t(names)+id.count*4ull!=namesEnd||definition<0x10000||definition>UINT32_MAX-12||read.U32(definition+4)!=id.count||read.U32(id.pose+0x10)!=definition)return {};
    const auto bones=read.U32(definition+8);constexpr unsigned boneStride=0x98;
    if(bones<0x10000||std::uint64_t(bones)+id.count*std::uint64_t(boneStride)>UINT32_MAX)return {};
    std::vector<std::uint32_t> nameAddresses(id.count);
    std::vector<std::array<std::byte,boneStride>> records(id.count);
    if(!read.Blocks(names,nameAddresses.data(),id.count*4))return {};
    if(!read.Blocks(bones,records.data(),id.count*boneStride)){
        // Preserve the former reader's acceptance when unused record padding
        // is inaccessible. Discard partial blocks and reread every used field.
        for(unsigned n=0;n<id.count;++n){const auto record=bones+n*boneStride;
            if(!read.Read(record,records[n].data(),4)||!read.Read(record+4,records[n].data()+4,4)||
               !read.Read(record+0x4c,records[n].data()+0x4c,64))return {};
        }
    }
    result.names.reserve(id.count);result.parents.reserve(id.count);
    for(unsigned n=0;n<id.count;++n){
        std::uint32_t name=0;std::int32_t parent=0;
        std::memcpy(&name,records[n].data(),4);std::memcpy(&parent,records[n].data()+4,4);
        if(!name||name!=nameAddresses[n])return {};
        const auto text=read.Name(name);if(!text||parent< -1||parent>=std::int32_t(id.count))return {};
        if(std::find(result.names.begin(),result.names.end(),*text)!=result.names.end())return {};
        result.names.push_back(*text);result.parents.push_back(parent);
    }
    const auto bone=[&](const char* name)->std::optional<unsigned>{const auto i=std::find(result.names.begin(),result.names.end(),name);if(i==result.names.end())return {};return unsigned(i-result.names.begin());};
    const auto ls=bone("LeftArm"),le=bone("LeftForeArm"),lw=bone("LeftHand"),rs=bone("RightArm"),re=bone("RightForeArm"),rw=bone("RightHand");
    if(!ls||!le||!lw||!rs||!re||!rw)return {};result.left={*ls,*le,*lw};result.right={*rs,*re,*rw};
    if(!interaction::ValidateArms(result.parents,result.left,result.right))return {};
    result.weaponBone=read.U32(id.animation+0x938);if(result.weaponBone>=id.count||result.names[result.weaponBone].rfind("jntWpn_",0)!=0)return {};
    const auto armBranch=[&](unsigned n){
        // ValidateArms already checked the complete parent graph. Even a
        // weapon-named descendant must not relax an arm/hand pose requirement.
        for(auto at=std::int32_t(n);at!=-1;at=result.parents[at])
            if(at==std::int32_t(*ls)||at==std::int32_t(*le)||at==std::int32_t(*lw)||
               at==std::int32_t(*rs)||at==std::int32_t(*re)||at==std::int32_t(*rw))return true;
        return false;
    };
    result.nativeWorld.resize(id.count);result.world.reserve(id.count);
    result.nativeEvaluated.resize(id.count);result.inverseBind.reserve(id.count);result.evaluatedWorld.reserve(id.count);
    if(!read.Read(id.worldMatrices,result.nativeWorld.data(),id.count*64))return {};
    if(!read.Read(id.evaluatedMatrices,result.nativeEvaluated.data(),id.count*64))return {};
    const auto canonical=[](const std::array<std::byte,64>& bytes){math::Matrix4 m{};std::memcpy(&m,bytes.data(),64);
        for(unsigned row=0;row<4;++row)m.values[row][3]=row==3?1.f:0.f;
        for(unsigned i=0;i<4;++i){m.values[2][i]=-m.values[2][i];m.values[i][2]=-m.values[i][2];}return m;
    };
    std::vector<std::array<std::byte,64>> baseSkin(id.count);if(!read.Read(id.skinMatrices,baseSkin.data(),id.count*64))return {};
    const auto collapsed=[](const math::Matrix4& m){
        for(const auto& row:m.values)for(float x:row)if(!std::isfinite(x))return false;
        for(unsigned r=0;r<3;++r)for(unsigned c=0;c<3;++c)
            if(std::abs(m.values[r][c]-(r==c?.0001f:0.f))>1e-7f)return false;
        return true;
    };
    std::vector<bool> hidden(id.count,false);
    for(unsigned n=0;n<id.count;++n){
        bool weaponMember=false;
        for(auto at=result.parents[n];at!=-1;at=result.parents[at])weaponMember|=at==std::int32_t(result.weaponBone);
        hidden[n]=weaponMember&&n!=result.weaponBone&&!armBranch(n)&&result.names[n].rfind("jntWpn",0)==0&&
            collapsed(canonical(result.nativeWorld[n]))&&collapsed(canonical(baseSkin[n]))&&collapsed(canonical(result.nativeEvaluated[n]));
    }
    for(unsigned n=0;n<id.count;++n)if(hidden[n]){
        for(unsigned child=0;child<id.count;++child)if(result.parents[child]==std::int32_t(n)&&!hidden[child])return {};
    }
    for(unsigned n=0;n<id.count;++n){
        const auto world=canonical(result.nativeWorld[n]);result.world.push_back(world);
        std::array<std::byte,64> bytes{};std::memcpy(bytes.data(),records[n].data()+0x4c,64);
        const auto inverseBind=canonical(bytes);const auto bind=interaction::InverseAnimatedTransform(inverseBind);if(!bind)return {};result.inverseBind.push_back(inverseBind);
        const auto skin=canonical(baseSkin[n]),expected=interaction::Multiply(inverseBind,world);
        // Native weapon animation hides weapon geometry with a 1e-4 diagonal in
        // all three palettes (observed on SPAS jntWpn_7 and Garand jntWpn_6).
        // Their translations no longer obey bind*world. This is structural
        // preservation, not an attachment binding or permission to unhide them.
        if(hidden[n]){
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
