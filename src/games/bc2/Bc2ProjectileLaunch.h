#pragma once
#include "Bc2MuzzleAdmission.h"
#include <bit>

namespace fvr::bc2 {
struct ProjectileLaunchBinding {
    unsigned helper=0,helperReturn=0,virtualCall=0,virtualReturn=0;
    bool verified=false;
};
// Numeric fingerprints of the reviewed complete server function and helper.
// Source: overnight426/crash/projectile-endpoint-proof.json. No executable data
// or guessed function signature is embedded. Live bytes must match before hooks.
inline ProjectileLaunchBinding VerifyProjectileLaunchCode(const MuzzleReadMemory& memory,
    unsigned base,unsigned serverRva)noexcept {
    if(!memory.read||serverRva!=0x179dc0||base<0x10000||base>UINT32_MAX-0x17aa50)return {};
    const auto matches=[&](unsigned rva,unsigned size,std::uint64_t expected){
        std::array<std::byte,0xc90> bytes{};
        if(size>bytes.size()||!memory.read(memory.context,base+rva,bytes.data(),size))return false;
        std::uint64_t value=14695981039346656037ULL;
        for(unsigned n=0;n<size;++n){value^=std::to_integer<unsigned char>(bytes[n]);value*=1099511628211ULL;}
        return value==expected;
    };
    if(!matches(serverRva,0xc90,0x885fa17a32cef9b0ULL)||!matches(0x156e90,0x12a,0xe56f87d5e320df48ULL))return {};
    return {base+0x156e90,base+0x17aa06,base+0x17aa0b,base+0x17aa0d,true};
}
struct ProjectileLaunchArguments {
    unsigned contextOutput=0,composedAddress=0,secondaryAddress=0,velocityAddress=0;
    std::array<float,16> composed{},secondary{};
    std::array<float,3> velocity{};
    unsigned token=0,primary=0,projectilesPerShell=0,context=0,derivedContext=0;
    float effectsScalar=0,multiplier=0;
};
inline std::optional<ProjectileLaunchArguments> ReadProjectileLaunchArguments(const MuzzleReadMemory& memory,
    const ProjectileLaunchBinding& binding,unsigned returnSlot,unsigned stackLow,unsigned stackHigh)noexcept {
    if(!memory.read||!binding.verified||stackLow>=stackHigh||returnSlot<stackLow||returnSlot>UINT32_MAX-0x1b4||
       returnSlot+0x1b4>stackHigh)return {};
    std::array<unsigned,12> args{};
    if(!memory.read(memory.context,returnSlot,args.data(),sizeof(args))||args[0]!=binding.helperReturn||
       args[1]!=returnSlot+0x1b0||args[2]!=returnSlot+0x90||args[3]!=returnSlot+0x130||args[4]!=returnSlot+0x60)return {};
    ProjectileLaunchArguments out;out.contextOutput=args[1];out.composedAddress=args[2];out.secondaryAddress=args[3];out.velocityAddress=args[4];
    out.effectsScalar=std::bit_cast<float>(args[5]);out.token=args[6];out.primary=args[7];out.projectilesPerShell=args[8];
    out.context=args[9];out.derivedContext=args[10];out.multiplier=std::bit_cast<float>(args[11]);
    if(!memory.read(memory.context,out.composedAddress,out.composed.data(),64)||
       !memory.read(memory.context,out.secondaryAddress,out.secondary.data(),64)||
       !memory.read(memory.context,out.velocityAddress,out.velocity.data(),12))return {};
    for(unsigned row=0;row<4;++row)for(unsigned col=0;col<3;++col)
        if(!std::isfinite(out.composed[row*4+col])||!std::isfinite(out.secondary[row*4+col]))return {};
    for(float v:out.velocity)if(!std::isfinite(v))return {};
    if(!std::isfinite(out.effectsScalar)||!std::isfinite(out.multiplier)||!out.projectilesPerShell)return {};
    return out;
}
// Shared control order for the native hook and ABI fixture. Observation failure
// cannot suppress/repeat the original or substitute its result. Observers are
// nonthrowing bounded copies; native exceptions propagate without an after claim.
template<class Original,class Before,class After>
inline std::uintptr_t ObserveProjectileContextHelper(Original original,Before before,After after){
    before();const auto result=original();after(result);return result;
}
} // namespace fvr::bc2
