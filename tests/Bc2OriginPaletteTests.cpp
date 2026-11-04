#ifdef NDEBUG
#error "Origin regression checks must remain active in Release builds"
#endif
#include "Bc2OriginPalette395.h"
#include <cstdio>
#include <vector>
using namespace fvr;
using namespace fvr::bc2;
using namespace fvr::bc2::origin_palette395;
#define CHECK(x) do{if(!(x)){std::printf("line %d: %s\n",__LINE__,#x);return 1;}}while(false)
namespace {
math::Matrix4 At(float x=0,float y=0,float z=0){math::Matrix4 m{};for(unsigned i=0;i<4;++i)m.values[i][i]=1;m.values[3]={x,y,z,1};return m;}
struct Fixture {
    RigSnapshot rig{};std::vector<Bone> ordinary;math::Matrix4 weapon=At(3,4,5),exit=At(3.25f,4.125f,5.75f);
    const std::array<unsigned,2> bones{1,2};
    Fixture(){rig.identity.count=8;rig.weaponBone=1;rig.inverseBind.assign(8,At());rig.nativeEvaluated.resize(8);
        for(auto& b:rig.nativeEvaluated)b.fill(std::byte{0x5a});ordinary=rig.nativeEvaluated;Pack(1,weapon);Pack(2,exit);}
    void Pack(unsigned bone,const math::Matrix4& world){auto skin=interaction::Multiply(rig.inverseBind[bone],world);
        for(unsigned i=0;i<4;++i){skin.values[2][i]=-skin.values[2][i];skin.values[i][2]=-skin.values[i][2];}
        for(unsigned r=0;r<4;++r)std::memcpy(ordinary[bone].data()+r*16,skin.values[r].data(),12);}
};
}
int main(){
    Fixture f;
    CHECK(PackedExitMatches(f.rig,f.ordinary,1,f.weapon)&&PackedExitMatches(f.rig,f.ordinary,2,f.exit));
    // Pump foregrip, feed lid/box/belt, and idle shotgun shell each change
    // unrelated final bones. The root and exit are invariant in both choices.
    for(unsigned mechanical:std::array<unsigned,5>{0,3,4,5,7}){
        auto shown=f.ordinary;shown[mechanical].fill(std::byte{0x17});
        CHECK(SameExitBones(f.ordinary,shown,f.bones));
        CHECK(SameExitBones(shown,f.ordinary,f.bones));
    }
    // A local fallback that moves the root/exit is not an equivalent muzzle.
    for(unsigned important:f.bones){auto changed=f.ordinary;changed[important][48]^=std::byte{1};
        CHECK(!SameExitBones(f.ordinary,changed,f.bones));}
    auto invalid=f.ordinary;invalid.pop_back();CHECK(!SameExitBones(f.ordinary,invalid,f.bones));
    CHECK(!SameExitBones({},f.ordinary,f.bones));CHECK(!SameExitBones(f.ordinary,f.ordinary,{}));
    const std::array<unsigned,2> outside{1,8};CHECK(!SameExitBones(f.ordinary,f.ordinary,outside));
    auto pad=f.ordinary;pad[2][15]^=std::byte{1};CHECK(!PackedExitMatches(f.rig,pad,2,f.exit));
    f.rig.nativeHiddenLeaves.push_back(2);CHECK(!PackedExitMatches(f.rig,f.ordinary,2,f.exit));
    f.rig.nativeHiddenLeaves.clear();auto moved=f.exit;moved.values[3][2]+=.01f;
    CHECK(!PackedExitMatches(f.rig,f.ordinary,2,moved));
    auto singular=f.exit;singular.values[0]={};CHECK(!PackedExitMatches(f.rig,f.ordinary,2,singular));
    // An exit on the root (front-loaded launcher) is valid, with no fake bone.
    const std::array<unsigned,2> rootExit{1,1};CHECK(SameExitBones(f.ordinary,f.ordinary,rootExit));
    CHECK(PackedExitMatches(f.rig,f.ordinary,1,f.weapon));
    std::puts("OriginPalette395 fixtures passed");return 0;
}
