#include "Test.h"
#include "Bc2Rig.h"
#include <algorithm>
#include <cstring>
#include <functional>
#include <utility>

using namespace fvr;
namespace {
// The same eight role bones as the production reader regression, with distinct
// world/skin/evaluated palettes and the complete native ownership graph.
struct Memory {
    std::vector<std::byte> data=std::vector<std::byte>(0x10000);
    std::vector<std::pair<std::uint32_t,std::size_t>> reads;
    bool scalarIdentity=false;
    std::uint32_t unreadable=0;
    unsigned refusedGroups=0,successful=0;
    std::size_t successfulBytes=0;
    std::function<void(Memory&,std::uint32_t)> afterRead;
    static bool Group(std::uint32_t at,std::size_t n){
        return (n==8&&(at==0x103b4||at==0x12050||at==0x15018))||(n==12&&at==0x16000);
    }
    static bool Read(void* context,std::uint32_t at,void* out,std::size_t n){
        auto& m=*static_cast<Memory*>(context);m.reads.emplace_back(at,n);
        if(n>65536||at<0x10000||at-0x10000>m.data.size()||n>m.data.size()-(at-0x10000))return false;
        if(m.scalarIdentity&&Group(at,n)){
            // A failed bulk reader may have partially overwritten its output.
            // Every accepted field must come from a subsequent successful read.
            std::memset(out,0xa5,n);++m.refusedGroups;return false;
        }
        if(m.unreadable&&at<=m.unreadable&&std::uint64_t(at)+n>m.unreadable)return false;
        std::memcpy(out,m.data.data()+at-0x10000,n);++m.successful;m.successfulBytes+=n;
        if(m.afterRead)m.afterRead(m,at);return true;
    }
    void Put(std::uint32_t at,std::uint32_t value){std::memcpy(data.data()+at-0x10000,&value,4);}
    bc2::RigMemory Reader(){return {this,Read};}
    explicit Memory(bool nativeIk=false){
        Put(0x11000,0x10004);data[0x475]=std::byte{1};Put(0x103b4,0x12000);Put(0x103b8,0x13000);Put(0x13004,0x10000);
        Put(0x12050,0x14000);Put(0x12054,0x15000);Put(0x12938,7);Put(0x14010,8);Put(0x15028,0x14000);Put(0x1501c,8);
        Put(0x15018,0x16000);Put(0x16000,8);Put(0x16004,0x18000);Put(0x14018,0x17000);Put(0x1401c,0x17020);
        Put(0x1400c,0x17100);Put(0x15010,0x17100);Put(0x16008,0x1a000);Put(0x1231c,0x1b000);Put(0x17104,8);Put(0x17108,0x17200);
        data[0x299c]=nativeIk?std::byte{1}:std::byte{0};
        const char* names[]={"root","LeftArm","LeftForeArm","LeftHand","RightArm","RightForeArm","RightHand","jntWpn_1"};
        const int parents[]={-1,0,1,2,0,4,5,0};
        for(unsigned n=0;n<8;++n){
            Put(0x17000+n*4,0x19000+n*64);Put(0x17200+n*0x98,0x19000+n*64);Put(0x17204+n*0x98,std::uint32_t(parents[n]));
            std::memcpy(data.data()+0x9000+n*64,names[n],std::strlen(names[n])+1);
            math::Matrix4 matrix{};for(unsigned a=0;a<4;++a)matrix.values[a][a]=1;
            std::memcpy(data.data()+0x7200+n*0x98+0x4c,&matrix,64);
            matrix.values[3][0]=float(n);matrix.values[3][2]=2;matrix.values[3][3]=0;
            std::memcpy(data.data()+0x8000+n*64,&matrix,64);std::memcpy(data.data()+0xa000+n*64,&matrix,64);
            matrix.values[3][1]=.125f;std::memcpy(data.data()+0xb000+n*64,&matrix,64);
        }
    }
};
bool Same(const bc2::RigSnapshot& a,const bc2::RigSnapshot& b){
    const auto matrices=[](const auto& x,const auto& y){
        if(x.size()!=y.size())return false;
        for(unsigned n=0;n<x.size();++n)if(x[n].values!=y[n].values)return false;
        return true;
    };
    return a.identity==b.identity&&a.names==b.names&&a.parents==b.parents&&a.nativeWorld==b.nativeWorld&&
        a.nativeEvaluated==b.nativeEvaluated&&matrices(a.world,b.world)&&matrices(a.inverseBind,b.inverseBind)&&
        matrices(a.evaluatedWorld,b.evaluatedWorld)&&a.skinConsistencyError==b.skinConsistencyError&&
        a.left.shoulder==b.left.shoulder&&a.left.elbow==b.left.elbow&&a.left.wrist==b.left.wrist&&
        a.right.shoulder==b.right.shoulder&&a.right.elbow==b.right.elbow&&a.right.wrist==b.right.wrist&&
        a.weaponBone==b.weaponBone&&a.nativeHiddenLeaves==b.nativeHiddenLeaves;
}
int CallbackReductionAndScalarFallback(){
    for(bool nativeIk:{false,true}){
        Memory grouped(nativeIk),scalar(nativeIk);scalar.scalarIdentity=true;const auto original=grouped.data;
        const auto batch=bc2::ReadFirstPersonRig(grouped.Reader(),0x10000,0x11000);
        const auto fallback=bc2::ReadFirstPersonRig(scalar.Reader(),0x10000,0x11000);
        CHECK(batch&&fallback&&Same(*batch,*fallback));CHECK(grouped.data==original&&scalar.data==original);
        // Each identity bookend replaces 2+2+2+3 scalar calls with four calls.
        // No additional native bytes or allocation are needed for the grouping.
        CHECK(grouped.successful==(nativeIk?42u:40u));CHECK(scalar.successful==(nativeIk?52u:50u));
        CHECK(scalar.refusedGroups==8);CHECK(grouped.successfulBytes==scalar.successfulBytes);
        for(const auto& [at,n]:grouped.reads)CHECK(n<=65536);
        Memory identity(nativeIk),scalarIdentity(nativeIk);scalarIdentity.scalarIdentity=true;
        const auto a=bc2::ReadFirstPersonRigIdentity(identity.Reader(),0x10000,0x11000);
        const auto b=bc2::ReadFirstPersonRigIdentity(scalarIdentity.Reader(),0x10000,0x11000);
        CHECK(a&&b&&*a==*b&&*a==batch->identity);
        CHECK(identity.successful==(nativeIk?22u:20u));CHECK(scalarIdentity.successful==(nativeIk?32u:30u));
        CHECK(scalarIdentity.refusedGroups==8);CHECK(identity.successfulBytes==scalarIdentity.successfulBytes);
    }
    return 0;
}
int MissingFieldsFailClosed(){
    // Each scalar field in each new group remains individually required, even
    // if the first grouped read fails after writing partial output bytes.
    for(bool fallback:{false,true})for(auto at:{0x103b4u,0x103b8u,0x12050u,0x12054u,0x15018u,0x1501cu,0x16000u,0x16004u,0x16008u}){
        Memory memory;memory.scalarIdentity=fallback;memory.unreadable=at;
        CHECK(!bc2::ReadFirstPersonRig(memory.Reader(),0x10000,0x11000));
        CHECK(!bc2::ReadFirstPersonRigIdentity(memory.Reader(),0x10000,0x11000));
    }
    return 0;
}
int OwnershipChangesFailClosed(){
    // Mutate after the first dynamic palette read. The ending identity must
    // reject every changed relationship, including valid replacement palettes.
    const std::pair<std::uint32_t,std::uint32_t> changes[]={
        {0x11000,0x10008},{0x10475,0},{0x103b4,0x12004},{0x103b8,0x13004},{0x13004,0x10004},
        {0x12050,0x14004},{0x12054,0x15004},{0x14010,9},{0x15028,0x14004},{0x1501c,9},
        {0x15018,0x16004},{0x16000,9},{0x16004,0x18040},{0x16008,0x1a040},{0x1299c,0},{0x1231c,0x1b040}
    };
    for(bool fallback:{false,true})for(const auto& [address,value]:changes){
        Memory memory(true);memory.scalarIdentity=fallback;bool changed=false;
        memory.afterRead=[&](Memory& target,std::uint32_t at){
            if(at==0x18000&&!changed){target.Put(address,value);changed=true;}
        };
        CHECK(!bc2::ReadFirstPersonRig(memory.Reader(),0x10000,0x11000));CHECK(changed);
    }
    // A valid replacement seen by the second identity-only bookend also fails.
    Memory identity;unsigned bookends=0;
    identity.afterRead=[&](Memory& target,std::uint32_t at){
        if(at==0x1299c&&++bookends==1)target.Put(0x16004,0x18040);
    };
    CHECK(!bc2::ReadFirstPersonRigIdentity(identity.Reader(),0x10000,0x11000));CHECK(bookends==2);
    return 0;
}
int FreshEveryInvocation(){
    Memory memory(true);
    const auto before=bc2::ReadFirstPersonRig(memory.Reader(),0x10000,0x11000);CHECK(before);
    // Valid data at the same identity addresses must be reread on the next call.
    const float x=31.f;
    for(auto at:{0x18000u,0x1a000u,0x1b000u})std::memcpy(memory.data.data()+at-0x10000+3*64+48,&x,4);
    const auto after=bc2::ReadFirstPersonRig(memory.Reader(),0x10000,0x11000);CHECK(after);
    CHECK(before->identity==after->identity&&before->nativeWorld!=after->nativeWorld);
    CHECK(after->world[3].values[3][0]==x&&after->evaluatedWorld[3].values[3][0]==x);
    memory.Put(0x11000,0x10008);CHECK(!bc2::ReadFirstPersonRig(memory.Reader(),0x10000,0x11000));
    return 0;
}
}
int main(){
    CHECK(CallbackReductionAndScalarFallback()==0);CHECK(MissingFieldsFailClosed()==0);
    CHECK(OwnershipChangesFailClosed()==0);CHECK(FreshEveryInvocation()==0);
    return 0;
}
