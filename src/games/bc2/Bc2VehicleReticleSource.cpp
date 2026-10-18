#include "Bc2VehicleReticleSource.h"
#include <algorithm>
#include <array>
#include <cstring>
#include <limits>
namespace fvr::bc2 {namespace {
struct Code {unsigned rva,size;std::uint64_t fingerprint;const char* name;};
const Code Proofs[]{
#include "Bc2VehicleReticleSourceProof.inc"
};
struct Reader {
    const VehicleRouteMemory& memory;
    bool Read(std::uint64_t at,void* out,std::size_t n)const noexcept {
        return memory.read&&at>=0x10000&&n&&n<=512&&at+n<=UINT32_MAX&&memory.read(memory.context,unsigned(at),out,n);
    }
    std::optional<unsigned> Word(std::uint64_t at)const noexcept {unsigned x=0;if(!Read(at,&x,4))return {};return x;}
};
struct Snapshot {
    unsigned table=0,target=0,effects=0,firing=0,primary=0,cameraOwner=0;
    unsigned additionalTransform=0;std::uint8_t adjustmentFlag=0;
    std::array<std::byte,128> transforms{};
    std::array<std::byte,12> inheritedVelocity{};
    std::array<std::byte,80> shot{};
    bool operator==(const Snapshot&)const=default;
};
std::optional<Snapshot> Sample(const Reader& r,unsigned image,const BoatAimSnapshot& aim)noexcept {
    const auto mounted=aim.nativeWeapon;
    const auto table=r.Word(mounted),current=r.Word(std::uint64_t(aim.weaponComponent)+0xd0),target=r.Word(std::uint64_t(mounted)+0xb0);
    if(!table||!current||!target||*current!=mounted||r.Word(std::uint64_t(*table)+8)!=image+0x3d4c10||
       r.Word(std::uint64_t(*table)+12)!=image+0x3bf840||r.Word(std::uint64_t(*table)+16)!=image+0x3bf860||
       *target==mounted+0xf4)return {};
    const auto effects=r.Word(std::uint64_t(mounted)+0x134),firing=r.Word(std::uint64_t(mounted)+12);
    if(!effects||!firing||!r.memory.type||!r.memory.type(r.memory.context,*firing,"WeaponFiringData")||r.Word(std::uint64_t(*effects)+16)!=*firing)return {};
    const auto primary=r.Word(std::uint64_t(*firing)+64);
    if(!primary||!r.memory.type(r.memory.context,*primary,"FiringFunctionData"))return {};
    Snapshot out;out.table=*table;out.target=*target;out.effects=*effects;out.firing=*firing;out.primary=*primary;
    const auto additional=r.Word(std::uint64_t(*effects)+0x174),cameraOwner=r.Word(std::uint64_t(*effects)+0x128);
    if(!additional||!cameraOwner||!r.Read(std::uint64_t(*firing)+0x46,&out.adjustmentFlag,1))return {};
    out.additionalTransform=*additional;out.cameraOwner=*cameraOwner;
    if(!r.Read(std::uint64_t(*effects)+0x20,out.inheritedVelocity.data(),out.inheritedVelocity.size())||
       !r.Read(std::uint64_t(*effects)+0x30,out.transforms.data(),out.transforms.size())||
       !r.Read(std::uint64_t(*primary)+0xa0,out.shot.data(),out.shot.size()))return {};
    if(r.Word(std::uint64_t(aim.weaponComponent)+0xd0)!=mounted||r.Word(std::uint64_t(mounted)+0x134)!=*effects||
       r.Word(std::uint64_t(mounted)+12)!=*firing||r.Word(std::uint64_t(*effects)+16)!=*firing||
       r.Word(std::uint64_t(*firing)+64)!=*primary)return {};
    return out;
}
template<std::size_t N> float Float(const std::array<std::byte,N>& b,unsigned offset)noexcept {float v=0;std::memcpy(&v,b.data()+offset,4);return v;}
}
VehicleReticleSourceBinding VerifyVehicleReticleSourceCode(const VehicleRouteMemory& memory,unsigned image)noexcept {
    if(!memory.read||!image||image>UINT32_MAX-0x1800000)return {};
    for(const auto& p:Proofs){std::array<unsigned char,4096> data{};
        if(p.size>data.size()||!memory.read(memory.context,image+p.rva,data.data(),p.size))return {};
        std::uint64_t fingerprint=14695981039346656037ull;
        for(unsigned n=0;n<p.size;++n){fingerprint^=data[n];fingerprint*=1099511628211ull;}
        if(fingerprint!=p.fingerprint)return {};}
    return {image,true};
}
std::optional<VehicleGunRayObservation> ReadPblDriverGunRayObservation(const VehicleRouteMemory& memory,const VehicleReticleSourceBinding& binding,const BoatAimSnapshot& aim,VehicleReticleSourceDiagnostic* diagnostic)noexcept {
    VehicleReticleSourceDiagnostic fallback;auto& status=diagnostic?*diagnostic:fallback;status={};status.stage=1;
    if(!binding.verified||!binding.image||!aim.nativeWeapon||!aim.weaponComponent||!aim.owner.actor||!aim.owner.actorGeneration||!aim.owner.seatGeneration)return {};
    status.stage=2;
    const Reader r{memory};const auto a=Sample(r,binding.image,aim),b=Sample(r,binding.image,aim);
    if(!a||!b||*a!=*b)return {};
    status.stage=3;status.additionalTransform=a->additionalTransform;status.adjustment=a->adjustmentFlag;
    if(a->additionalTransform||a->adjustmentFlag)return {};
    // Native ordinary shot path at579ec3 consumes InitialDirection+10 and InitialPosition+20.
    // This observer admits the canonical authored direction basis. Camera aim
    // requires its live owner, not merely an authored flag. Never substitute HMD aim.
    unsigned count=0;std::memcpy(&count,a->shot.data()+0x34,4);
    status.stage=4;status.count=count;status.cameraOwner=a->cameraOwner;status.inheritSpeed=Float(a->shot,0x3c);
    if(!std::isfinite(status.inheritSpeed))status.inheritSpeed=0; // Keep diagnostic JSON finite; native value still rejects below.
    for(unsigned n=0;n<3;++n)if(a->shot[0x48+n]!=std::byte{})status.cameraFlags|=1u<<n;
    // +40 is MuzzleExplosion, not a custom-aim flag. It does not select the
    // projectile direction branch. Native57a11c/57a12e tests effects+128
    // before +49. With a null owner, that entire camera override is inactive;
    // the later +49 branch only retains the identical pre-override basis.
    // Active camera owners and the other camera/relative-aim branches reject.
    const float dx=Float(a->shot,0x10),dy=Float(a->shot,0x14),dz=Float(a->shot,0x18);
    const float inherit=Float(a->shot,0x3c);
    if(!count||count>128||a->shot[0x48]!=std::byte{}||a->shot[0x49]>std::byte{1}||a->shot[0x4a]!=std::byte{}||
       (a->shot[0x49]!=std::byte{}&&a->cameraOwner)||
       !std::isfinite(dx)||!std::isfinite(dy)||!std::isfinite(dz)||dx!=0||dy!=0||dz<0||
       double(dz)*dz>std::numeric_limits<float>::max()||
       (inherit!=0&&inherit!=1))return {};
    // Native4182e0 constructs a basis from InitialDirection (not Euler angles).
    // Only canonical forward or the native zero/+Z fallback is supported here.
    // Native57a73e..57a7ce adds effects+20 * InheritWeaponSpeedAmount to
    // the transformed launch velocity. Admit the observed0/1 coefficients and
    // use the same double-bracketed vector. This is an initial sight line, not
    // gravity/spread compensation or a predicted projectile impact point.
    math::Matrix4 base{},local{};std::memcpy(&base,a->transforms.data(),64);std::memcpy(&local,a->transforms.data()+64,64);
    status.stage=5;
    for(unsigned row=0;row<4;++row){base.values[row][3]=row==3?1.f:0.f;local.values[row][3]=row==3?1.f:0.f;}
    if(!interaction::InverseRigid(base)||!interaction::InverseRigid(local))return {};
    // Exact read-only equivalent of the native 6cddf0 getter: orientation is
    // base*local, while its translations are added directly (not multiplied).
    auto combined=interaction::Multiply(base,local);
    for(unsigned axis=0;axis<3;++axis)combined.values[3][axis]=base.values[3][axis]+local.values[3][axis];
    std::array<float,3> d{},p{};
    for(unsigned axis=0;axis<3;++axis){
        p[axis]=combined.values[3][axis];
        for(unsigned row=0;row<3;++row){d[axis]+=Float(a->shot,row*4)*combined.values[row][axis];p[axis]+=Float(a->shot,0x20+row*4)*combined.values[row][axis];}
        const float velocity=Float(a->inheritedVelocity,axis*4);
        if(!std::isfinite(velocity))return {};
        d[axis]+=inherit*velocity;
    }
    const double length=std::sqrt(double(d[0])*d[0]+double(d[1])*d[1]+double(d[2])*d[2]);
    status.stage=6;
    if(!std::isfinite(length)||length<=0||!std::isfinite(p[0])||!std::isfinite(p[1])||!std::isfinite(p[2]))return {};
    status.stage=7;
    return VehicleGunRayObservation{{p[0],p[1],-p[2]},{float(d[0]/length),float(d[1]/length),float(-d[2]/length)},a->effects,a->firing,a->primary};
}
}
