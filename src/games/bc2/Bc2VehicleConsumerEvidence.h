#pragma once
#include "Bc2VehicleFinalInputEvidence.h"
#include <array>
#include <atomic>
#include <cstdint>
#include <type_traits>

// Passive boundary observations only. A joined invocation is not an observed
// branch, getter, physics setter or simulation result; values grant no writes.
namespace fvr::bc2::vehicleConsumerEvidence {
inline constexpr unsigned TargetRva=0x38f660,TargetSize=681,CallerRva=0x473ba7;
inline constexpr const char* TargetHash="201d4f389e3f3972bab9bcad1fad65d35863258ba6f3ae474b853cb1c24de2cc";
inline constexpr const char* ExecutableHash="3911fcc8914b0158c434cee36f19295316e715d4c9a0e70c857e8c39e3d54258";

// Exact receiver/table/setter proof from the supported executable. These are
// read-only boundary observations, never a setter invocation or physics result.
namespace destination {
inline constexpr unsigned VtableRva=0x1056550,ThrottleRva=0x6db220,BrakeRva=0x6db250;
inline constexpr std::array<unsigned char,17> ThrottleBody{
    0x8b,0x41,0x0c,0xf3,0x0f,0x10,0x44,0x24,0x04,0xf3,0x0f,0x11,0x40,0x20,0xc2,0x04,0x00};
inline constexpr std::array<unsigned char,17> BrakeBody{
    0x8b,0x41,0x0c,0xf3,0x0f,0x10,0x44,0x24,0x04,0xf3,0x0f,0x11,0x40,0x28,0xc2,0x04,0x00};
enum Reason:unsigned {None=0,NotObserved=1,ReadFailed=2,Type=4,Code=8,Identity=16,Values=32,Context=64};
enum Field:unsigned {DriverPhysics=1,PhysicsVtable=2,ThrottleTarget=4,BrakeTarget=8,
    ThrottleCode=16,BrakeCode=32,Control=64,Throttle=128,Brake=256,
    ThrottleAgain=512,BrakeAgain=1024,ControlAgain=2048,DriverPhysicsAgain=4096,
    PhysicsVtableAgain=8192,ThrottleTargetAgain=16384,BrakeTargetAgain=32768,
    ThrottleCodeAgain=65536,BrakeCodeAgain=131072};
inline constexpr unsigned AllFields=262143;
struct Snapshot {
    unsigned physics=0,vtable=0,control=0;
    std::array<unsigned,2> targets{},words{};
    unsigned physicsAgain=0,vtableAgain=0,controlAgain=0;
    std::array<unsigned,2> targetsAgain{},wordsAgain{};
    unsigned attempted=0,read=0,reasons=NotObserved;
    bool operator==(const Snapshot&)const=default;
};
template<class Reader> Snapshot Read(unsigned image,unsigned imageSize,unsigned driver,
    unsigned expectedPhysics,unsigned expectedVtable,Reader&& read)noexcept {
    Snapshot out;out.reasons=None;
    const auto inImage=[&](unsigned at,unsigned size){return at>=image&&
        std::uint64_t(at)+size<=std::uint64_t(image)+imageSize;};
    const auto word=[&](unsigned base,unsigned offset,unsigned& value,unsigned field){
        out.attempted|=field;
        if(base<0x10000||base>UINT32_MAX-offset-4||!read(base+offset,&value,sizeof(value))){out.reasons|=ReadFailed;return false;}
        out.read|=field;return true;
    };
    const auto code=[&](unsigned at,const auto& expected,unsigned field){
        out.attempted|=field;std::array<unsigned char,17> actual{};
        if(!inImage(at,unsigned(actual.size()))||!read(at,actual.data(),actual.size())){out.reasons|=ReadFailed;return false;}
        out.read|=field;if(actual!=expected){out.reasons|=Code;return false;}return true;
    };
    if(!image||std::uint64_t(image)+VtableRva+0x5c>UINT32_MAX||
       !inImage(image+VtableRva,0x5c)||!inImage(image+ThrottleRva,17)||!inImage(image+BrakeRva,17)){
        out.reasons|=Type;return out;
    }
    if(!word(driver,0xd4,out.physics,DriverPhysics)||!word(out.physics,0,out.vtable,PhysicsVtable))return out;
    if(!expectedPhysics||out.physics!=expectedPhysics||out.vtable!=expectedVtable||out.vtable!=image+VtableRva){
        out.reasons|=Type;return out;
    }
    const bool throttleTarget=word(out.vtable,0x50,out.targets[0],ThrottleTarget);
    const bool brakeTarget=word(out.vtable,0x58,out.targets[1],BrakeTarget);
    if(!throttleTarget||!brakeTarget)return out;
    if(out.targets[0]!=image+ThrottleRva||out.targets[1]!=image+BrakeRva){out.reasons|=Type;return out;}
    const bool throttleCode=code(out.targets[0],ThrottleBody,ThrottleCode);
    const bool brakeCode=code(out.targets[1],BrakeBody,BrakeCode);
    if(!throttleCode||!brakeCode)return out;
    if(!word(out.physics,0x0c,out.control,Control))return out;
    // Repeated reads bookend each bounded view. Retain both raw words even on
    // refusal; a changed pointer/value does not become a coherent snapshot.
    word(out.control,0x20,out.words[0],Throttle);word(out.control,0x28,out.words[1],Brake);
    word(out.control,0x20,out.wordsAgain[0],ThrottleAgain);word(out.control,0x28,out.wordsAgain[1],BrakeAgain);
    word(out.physics,0x0c,out.controlAgain,ControlAgain);
    word(driver,0xd4,out.physicsAgain,DriverPhysicsAgain);word(out.physics,0,out.vtableAgain,PhysicsVtableAgain);
    word(out.vtable,0x50,out.targetsAgain[0],ThrottleTargetAgain);word(out.vtable,0x58,out.targetsAgain[1],BrakeTargetAgain);
    code(out.targets[0],ThrottleBody,ThrottleCodeAgain);code(out.targets[1],BrakeBody,BrakeCodeAgain);
    if(out.physics!=out.physicsAgain||out.vtable!=out.vtableAgain||out.control!=out.controlAgain||
       out.targets!=out.targetsAgain)out.reasons|=Identity;
    if(out.words!=out.wordsAgain)out.reasons|=Values;
    return out;
}
inline bool SameObject(const Snapshot& a,const Snapshot& b)noexcept {
    return !a.reasons&&!b.reasons&&a.read==AllFields&&b.read==AllFields&&
        a.physics==b.physics&&a.vtable==b.vtable&&a.control==b.control&&a.targets==b.targets;
}
}
// Separate observation of mutable physics state and its construction provenance.
// Exact PE proof: reports/coordinator-20261117-feedback1468/evidence.json.
// The full update includes the transition call BEFORE the state gate; these
// boundary values do not establish an update invocation or a simulation result.
namespace state {
inline constexpr unsigned ConstructorRva=0x6f2e99;
inline constexpr std::array<unsigned char,38> ConstructorBody{
    0x8b,0x45,0x08,0xf3,0x0f,0x10,0x0d,0xb4,0x7a,0x41,0x01,0x83,0xec,0x34,0x53,0x33,0xdb,0x56,0x8b,0xf1,0x8b,0x4d,0x0c,0xc7,
    0x06,0x50,0x65,0x45,0x01,0x89,0x5e,0x04,0x89,0x46,0x08,0x89,0x5e,0x0c};
inline constexpr unsigned OriginRva=0x6f308f;
inline constexpr std::array<unsigned char,15> OriginBody{
    0x8b,0x46,0x08,0x8b,0x88,0x84,0x00,0x00,0x00,0x89,0x8e,0x80,0x00,0x00,0x00};
inline constexpr unsigned SetterRva=0x6db130;
inline constexpr std::array<unsigned char,13> SetterBody{
    0x8b,0x44,0x24,0x04,0x89,0x81,0x80,0x00,0x00,0x00,0xc2,0x04,0x00};
inline constexpr unsigned TransitionRva=0x6d2170;
inline constexpr std::array<unsigned char,146> TransitionBody{
    0x8b,0x91,0x80,0x00,0x00,0x00,0x83,0xfa,0x01,0x74,0x0f,0x83,0xfa,0x03,0x74,0x0a,0x83,0xfa,0x05,0x74,0x05,0x83,0xfa,0x06,
    0x75,0x75,0xd9,0x81,0x84,0x00,0x00,0x00,0xd8,0x64,0x24,0x04,0xd9,0x5c,0x24,0x04,0xd9,0x44,0x24,0x04,0xd9,0x91,0x84,0x00,
    0x00,0x00,0xd9,0xee,0xd9,0xc9,0xdf,0xf1,0xdd,0xd8,0x77,0x53,0xf3,0x0f,0x10,0x81,0x84,0x00,0x00,0x00,0x0f,0x57,0xc9,0x0f,
    0x2f,0xc1,0x77,0x03,0x0f,0x28,0xc1,0x83,0xfa,0x01,0xf3,0x0f,0x11,0x81,0x84,0x00,0x00,0x00,0x74,0x17,0x83,0xfa,0x03,0x75,
    0x0d,0xc7,0x81,0x80,0x00,0x00,0x00,0x04,0x00,0x00,0x00,0xc2,0x04,0x00,0x83,0xfa,0x05,0x75,0x0d,0xc7,0x81,0x80,0x00,0x00,
    0x00,0x02,0x00,0x00,0x00,0xc2,0x04,0x00,0x83,0xfa,0x06,0x75,0x0a,0xc7,0x81,0x80,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0xc2,
    0x04,0x00};
inline constexpr unsigned UpdateRva=0x6e9e50;
inline constexpr std::array<unsigned char,482> UpdateBody{
    0x55,0x8b,0xec,0x83,0xe4,0xf0,0x83,0xec,0x28,0x56,0x57,0x8b,0xf1,0x8d,0x44,0x24,0x10,0x50,0x8d,0x4c,0x24,0x24,0x51,0x8b,
    0xce,0xe8,0xf2,0x14,0xff,0xff,0x8b,0x8e,0xc8,0x00,0x00,0x00,0x8d,0x54,0x24,0x10,0x52,0xe8,0x92,0x9c,0xfb,0xff,0x8b,0x4e,
    0x78,0xe8,0x5a,0xd4,0xfb,0xff,0x8b,0x8e,0xc8,0x00,0x00,0x00,0x83,0xc0,0x20,0x50,0x8d,0x44,0x24,0x24,0x50,0xe8,0x26,0x9c,
    0xfb,0xff,0x8b,0xce,0xe8,0x0f,0x98,0xfe,0xff,0x80,0xbe,0xd5,0x00,0x00,0x00,0x00,0x74,0x07,0x8b,0xce,0xe8,0x0f,0x2d,0xfe,
    0xff,0xd9,0x45,0x08,0x51,0x8b,0xce,0xd9,0x1c,0x24,0xe8,0xb1,0x82,0xfe,0xff,0x8b,0x86,0x80,0x00,0x00,0x00,0x83,0xf8,0x03,
    0x0f,0x57,0xc0,0x74,0x1a,0x83,0xf8,0x04,0x74,0x15,0x83,0xf8,0x05,0x74,0x10,0x8b,0x4e,0x0c,0xf3,0x0f,0x11,0x41,0x20,0x8b,
    0x56,0x0c,0xf3,0x0f,0x11,0x42,0x28,0x8b,0x4e,0x0c,0x85,0xc9,0xf3,0x0f,0x11,0x44,0x24,0x0c,0x74,0x26,0x0f,0xb6,0x86,0xa8,
    0x00,0x00,0x00,0xd9,0x45,0x08,0xf3,0x0f,0x10,0x41,0x04,0x50,0x56,0x51,0xf3,0x0f,0x11,0x44,0x24,0x18,0xd9,0x1c,0x24,0xe8,
    0xbc,0xfc,0xfe,0xff,0xf3,0x0f,0x10,0x44,0x24,0x0c,0x8b,0x4e,0x0c,0x85,0xc9,0x74,0x1d,0x8b,0x46,0x78,0x85,0xc0,0x74,0x16,
    0x80,0xb8,0xac,0x00,0x00,0x00,0x00,0x75,0x0d,0xf3,0x0f,0x11,0x41,0x04,0x8b,0x4e,0x0c,0xf3,0x0f,0x11,0x41,0x08,0x8b,0x46,
    0x10,0x85,0xc0,0x74,0x1c,0xf6,0x40,0x2c,0x02,0x74,0x16,0x8b,0x46,0x0c,0xd9,0x40,0x28,0xf3,0x0f,0x10,0x40,0x20,0xd9,0x58,
    0x20,0x8b,0x56,0x0c,0xf3,0x0f,0x11,0x42,0x28,0x8b,0x4e,0x78,0x8b,0x01,0x8b,0x90,0x84,0x00,0x00,0x00,0xff,0xd2,0x8b,0x4e,
    0x78,0x8b,0xf8,0x8b,0x01,0x8b,0x90,0x84,0x00,0x00,0x00,0xff,0xd2,0xd9,0x40,0x04,0xd8,0x4f,0x04,0xd9,0x00,0xd8,0x0f,0xde,
    0xc1,0xd9,0x40,0x08,0xd8,0x4f,0x08,0xde,0xc1,0xd9,0x5c,0x24,0x0c,0xd9,0x44,0x24,0x0c,0xd9,0x05,0x20,0x96,0x4d,0x01,0xdf,
    0xf1,0xdd,0xd8,0x76,0x25,0x8b,0x46,0x0c,0xf3,0x0f,0x10,0x40,0x20,0x0f,0x2f,0x05,0xa0,0xa0,0x40,0x01,0x0f,0x57,0xc0,0x76,
    0x14,0xd9,0x86,0x8c,0x00,0x00,0x00,0xd8,0x45,0x08,0xd9,0x9e,0x8c,0x00,0x00,0x00,0xeb,0x0b,0x0f,0x57,0xc0,0xf3,0x0f,0x11,
    0x86,0x8c,0x00,0x00,0x00,0xb8,0x01,0x00,0x00,0x00,0x39,0x46,0x7c,0x88,0x86,0x88,0x00,0x00,0x00,0xf3,0x0f,0x11,0x86,0x98,
    0x00,0x00,0x00,0xf3,0x0f,0x11,0x86,0x9c,0x00,0x00,0x00,0x74,0x35,0x8b,0x8e,0xc4,0x00,0x00,0x00,0x85,0xc9,0x74,0x0c,0xd9,
    0x45,0x08,0x51,0xd9,0x1c,0x24,0xe8,0x15,0x0e,0xff,0xff,0x8b,0x4e,0x78,0x8b,0x11,0x8b,0x82,0x94,0x00,0x00,0x00,0xff,0xd0,
    0x84,0xc0,0x74,0x0e,0xd9,0x45,0x08,0x51,0x8b,0xce,0xd9,0x1c,0x24,0xe8,0x66,0xf6,0xff,0xff,0x5f,0x5e,0x8b,0xe5,0x5d,0xc2,
    0x04,0x00};
enum Reason:unsigned {None=0,NotObserved=1,ReadFailed=2,Type=4,Code=8,Identity=16,Values=32,Context=64,Null=128,Overflow=256};
enum Field:unsigned {DriverPhysics=1,PhysicsVtable=2,UpdateTarget=4,SetterTarget=8,
    ConstructorCode=16,OriginCode=32,SetterCode=64,TransitionCode=128,UpdateCode=256,
    Config=512,Live=1024,Initial=2048,LiveAgain=4096,InitialAgain=8192,ConfigAgain=16384,
    DriverPhysicsAgain=32768,PhysicsVtableAgain=65536,UpdateTargetAgain=131072,SetterTargetAgain=262144,
    ConstructorCodeAgain=524288,OriginCodeAgain=1048576,SetterCodeAgain=2097152,TransitionCodeAgain=4194304,UpdateCodeAgain=8388608};
inline constexpr unsigned AllFields=16777215;
struct Snapshot {
    unsigned physics=0,vtable=0,config=0,live=0,initial=0;
    std::array<unsigned,2> targets{};
    unsigned physicsAgain=0,vtableAgain=0,configAgain=0,liveAgain=0,initialAgain=0;
    std::array<unsigned,2> targetsAgain{};
    unsigned attempted=0,read=0,reasons=NotObserved;
    bool operator==(const Snapshot&)const=default;
};
template<class Reader> Snapshot Read(unsigned image,unsigned imageSize,unsigned driver,
    unsigned expectedPhysics,unsigned expectedVtable,Reader&& read)noexcept {
    Snapshot out;out.reasons=None;
    const auto inImage=[&](unsigned rva,std::size_t size){
        return rva<=imageSize&&size<=imageSize-rva;
    };
    const auto word=[&](unsigned base,unsigned offset,unsigned& value,unsigned field){
        out.attempted|=field;
        if(!base){out.reasons|=Null;return false;}
        if(base<0x10000){out.reasons|=Type;return false;}
        if(std::uint64_t(base)+offset+sizeof(value)>std::uint64_t(UINT32_MAX)+1){out.reasons|=Overflow;return false;}
        if(!read(base+offset,&value,sizeof(value))){out.reasons|=ReadFailed;return false;}
        out.read|=field;return true;
    };
    const auto code=[&](unsigned rva,const auto& expected,unsigned field){
        out.attempted|=field;std::remove_cvref_t<decltype(expected)> actual{};
        if(!inImage(rva,actual.size())||!read(image+rva,actual.data(),actual.size())){out.reasons|=ReadFailed;return false;}
        out.read|=field;if(actual!=expected){out.reasons|=Code;return false;}return true;
    };
    if(std::uint64_t(image)+imageSize>std::uint64_t(UINT32_MAX)+1){out.reasons|=Overflow;return out;}
    if(!image||!inImage(destination::VtableRva,0x38)||!inImage(ConstructorRva,ConstructorBody.size())||
       !inImage(OriginRva,OriginBody.size())||!inImage(SetterRva,SetterBody.size())||
       !inImage(TransitionRva,TransitionBody.size())||!inImage(UpdateRva,UpdateBody.size())){out.reasons|=Type;return out;}
    if(!word(driver,0xd4,out.physics,DriverPhysics)||!word(out.physics,0,out.vtable,PhysicsVtable))return out;
    if(!expectedPhysics||out.physics!=expectedPhysics||out.vtable!=expectedVtable||out.vtable!=image+destination::VtableRva){
        out.reasons|=Type;return out;
    }
    const bool update=word(out.vtable,0x1c,out.targets[0],UpdateTarget);
    const bool setter=word(out.vtable,0x34,out.targets[1],SetterTarget);
    if(!update||!setter)return out;
    if(out.targets[0]!=image+UpdateRva||out.targets[1]!=image+SetterRva){out.reasons|=Type;return out;}
    const bool constructorCode=code(ConstructorRva,ConstructorBody,ConstructorCode);
    const bool originCode=code(OriginRva,OriginBody,OriginCode);
    const bool setterCode=code(SetterRva,SetterBody,SetterCode);
    const bool transitionCode=code(TransitionRva,TransitionBody,TransitionCode);
    const bool updateCode=code(UpdateRva,UpdateBody,UpdateCode);
    if(!constructorCode||!originCode||!setterCode||!transitionCode||!updateCode)return out;
    const bool config=word(out.physics,0x08,out.config,Config);word(out.physics,0x80,out.live,Live);
    if(config)word(out.config,0x84,out.initial,Initial);
    // Re-read the same objects, then bookend their pointers, targets and code.
    // Initial and mutable live state may differ; neither grants authority.
    word(out.physics,0x80,out.liveAgain,LiveAgain);if(config)word(out.config,0x84,out.initialAgain,InitialAgain);
    word(out.physics,0x08,out.configAgain,ConfigAgain);
    word(driver,0xd4,out.physicsAgain,DriverPhysicsAgain);word(out.physics,0,out.vtableAgain,PhysicsVtableAgain);
    word(out.vtable,0x1c,out.targetsAgain[0],UpdateTargetAgain);word(out.vtable,0x34,out.targetsAgain[1],SetterTargetAgain);
    code(ConstructorRva,ConstructorBody,ConstructorCodeAgain);code(OriginRva,OriginBody,OriginCodeAgain);
    code(SetterRva,SetterBody,SetterCodeAgain);code(TransitionRva,TransitionBody,TransitionCodeAgain);code(UpdateRva,UpdateBody,UpdateCodeAgain);
    if(out.physics!=out.physicsAgain||out.vtable!=out.vtableAgain||out.config!=out.configAgain||out.targets!=out.targetsAgain)
        out.reasons|=Identity;
    if(out.live!=out.liveAgain||out.initial!=out.initialAgain)out.reasons|=Values;
    return out;
}
inline bool SameObject(const Snapshot& a,const Snapshot& b)noexcept {
    // State transitions between entry and return are legitimate observations.
    return !a.reasons&&!b.reasons&&a.read==AllFields&&b.read==AllFields&&
        a.physics==b.physics&&a.vtable==b.vtable&&a.config==b.config&&a.targets==b.targets;
}
}
enum Reason:unsigned {None=0,NoPublication=1,PublicationChanged=2,Owner=4,Pair=8,Words=16,
    Clock=32,Overlap=64,Caller=128,Receiver=256,Read=512,Unwind=1024,Nested=2048,Physics=4096,Commit=8192};
struct Identity {
    unsigned manager=0,player=0,actor=0,weak=0,vehicle=0,entry=0,router=0,driver=0;
    std::uint64_t actorGeneration=0,seatGeneration=0,inputEpoch=0,movementEpoch=0,space=0,route=0;
    bool operator==(const Identity&)const=default;
};
inline Identity Copy(const VehicleFinalInputCommit& c)noexcept {
    return {c.manager,c.player,c.actor,c.weak,c.vehicle,c.entry,c.router,c.driver,
        c.actorGeneration,c.seatGeneration,c.inputEpoch,c.movementEpoch,c.space,c.routeFingerprint};
}
inline bool Valid(const Identity& v)noexcept {
    return v.manager&&v.player&&v.actor&&v.weak&&v.vehicle&&v.entry&&v.router&&v.driver&&
        v.actorGeneration&&v.seatGeneration&&v.inputEpoch&&v.movementEpoch&&v.space&&v.route;
}
struct View {
    Identity owner{};unsigned pair=0,current=0,previous=0;
    // Current-buffer words: action0, action2, permissions98, permissions9c.
    std::array<unsigned,4> words{};unsigned physics=0,physicsVtable=0,zeroControls=0;
    destination::Snapshot destination{};
    state::Snapshot state{};
    std::uint64_t observationSerial=0;bool generationsCurrent=false;
    bool operator==(const View&)const=default;
};
inline bool Valid(const View& v)noexcept {
    return Valid(v.owner)&&v.pair&&v.current&&v.previous&&v.current!=v.previous&&v.physics&&v.physicsVtable&&v.observationSerial&&v.generationsCurrent;
}
struct Epoch {std::uint64_t revision=0;unsigned active=0;bool operator==(const Epoch&)const=default;};
template<class Revision,class Active> Epoch SampleEpoch(Revision revision,Active active)noexcept {
    const auto first=revision();const auto count=active();const auto last=revision();
    return {last,first==last?count:UINT32_MAX};
}
struct UpdateToken {std::uint64_t revision=0;bool active=false;};
// Every entry into the original Update wrapper advances the revision, including
// foreign/refused/nested Updates. A returned pointer reused later has a new
// publication identity. No native field is read or written by this clock.
class ProducerClock {
public:
    void Start()noexcept {unsigned empty=0;(void)state_.compare_exchange_strong(empty,1);}
    void Close()noexcept {state_.store(2,std::memory_order_release);}
    bool Enabled()const noexcept{return state_.load(std::memory_order_acquire)==1;}
    UpdateToken Begin()noexcept {
        if(!Enabled())return {};
        active_.fetch_add(1,std::memory_order_acq_rel);
        const auto revision=revision_.fetch_add(1,std::memory_order_acq_rel)+1;
        if(!revision)Close();return {revision,true};
    }
    void End(UpdateToken& token)noexcept {if(token.active){active_.fetch_sub(1,std::memory_order_acq_rel);token.active=false;}}
    Epoch Sample()const noexcept{return SampleEpoch([&](){return revision_.load(std::memory_order_acquire);},
        [&](){return active_.load(std::memory_order_acquire);});}
private:
    std::atomic<std::uint64_t> revision_{0};std::atomic<unsigned> active_{0},state_{0};
};
struct Publication {
    View view{};Epoch first{},last{};std::uint64_t generation=0,update=0,gather=0,input=0;
    unsigned cache=0,committedThrottle=0,committedPermissions=0,reasons=0,firstMask=0;
    std::int64_t qpc=0;bool complete=false;
};
inline Publication MakePublication(const VehicleFinalInputCommit& c,const View& view,
    Epoch first,Epoch last,std::uint64_t updateRevision,std::int64_t qpc,unsigned checks=0)noexcept {
    Publication p;p.view=view;p.first=first;p.last=last;p.update=c.update;p.gather=c.gather;p.input=c.input;
    p.cache=c.cache;p.committedThrottle=c.throttle;p.committedPermissions=c.permissions;p.qpc=qpc;p.reasons=checks;
    if(!Valid(view)||view.owner!=Copy(c))p.reasons|=Owner;
    if(!updateRevision||first.revision!=updateRevision||first!=last||first.active!=1)p.reasons|=Overlap;
    if(view.current!=c.cache)p.reasons|=Pair;
    if(view.words[0]!=c.throttle||view.words[2]!=c.permissions||c.check!=VehicleFinalInputCheck::Okay)p.reasons|=Commit;
    if(qpc<=0||qpc<c.stamp.qpc||!c.update||!c.gather||!c.input)p.reasons|=Clock;
    p.complete=!p.reasons;return p;
}
struct Record {
    Publication publication{};View before{},after{};Epoch entry{},returned{};
    std::uint64_t invocation=0,sequence=0,returnPublication=0;std::int64_t beginQpc=0,endQpc=0;
    unsigned thread=0,receiver=0,argument=0,caller=0,reasons=0,firstMask=0;
    unsigned destinationReasons=destination::NotObserved;
    unsigned stateReasons=state::NotObserved;
    bool beforeReadable=false,afterReadable=false,normal=false,joined=false,destinationComparable=false;
    bool stateComparable=false;
};
inline unsigned PublicationMatch(const Publication& p,const View& v,Epoch epoch)noexcept {
    unsigned reasons=0;
    if(!p.complete||!p.generation)reasons|=NoPublication;
    if(epoch.active||epoch.revision!=p.last.revision)reasons|=Overlap;
    if(v.owner!=p.view.owner||!v.generationsCurrent||!v.observationSerial||v.observationSerial!=p.view.observationSerial)reasons|=Owner;
    if(v.pair!=p.view.pair||v.current!=p.view.current||v.previous!=p.view.previous)reasons|=Pair;
    if(v.words!=p.view.words)reasons|=Words;
    if(v.physics!=p.view.physics||v.physicsVtable!=p.view.physicsVtable)reasons|=Physics;
    return reasons;
}
inline void Complete(Record& r)noexcept {
    if(!r.beforeReadable||!r.afterReadable)r.reasons|=Read;
    if(!r.normal)r.reasons|=Unwind;
    if(r.receiver!=r.before.owner.driver+0xc0)r.reasons|=Receiver;
    if(r.argument!=r.before.pair)r.reasons|=Pair;
    if(r.caller!=CallerRva)r.reasons|=Caller;
    if(r.beginQpc<=0||r.endQpc<r.beginQpc||r.beginQpc<r.publication.qpc)r.reasons|=Clock;
    if(r.entry!=r.returned||r.entry.active||r.returned.active)r.reasons|=Overlap;
    if(r.returnPublication!=r.publication.generation)r.reasons|=PublicationChanged;
    r.reasons|=PublicationMatch(r.publication,r.before,r.entry);
    r.reasons|=PublicationMatch(r.publication,r.after,r.returned);
    if(r.before.owner!=r.after.owner)r.reasons|=Owner;
    if(r.before.pair!=r.after.pair||r.before.current!=r.after.current||r.before.previous!=r.after.previous)r.reasons|=Pair;
    if(r.before.words!=r.after.words||r.before.zeroControls!=r.after.zeroControls)r.reasons|=Words;
    r.joined=!r.reasons;
    r.destinationReasons=r.before.destination.reasons|r.after.destination.reasons;
    if(!destination::SameObject(r.before.destination,r.after.destination)||
       r.before.destination.physics!=r.before.physics||r.after.destination.physics!=r.after.physics||
       r.before.destination.vtable!=r.before.physicsVtable||r.after.destination.vtable!=r.after.physicsVtable)
        r.destinationReasons|=destination::Identity;
    // Keep the existing strict publication/context qualification. Raw readable
    // destination words remain available when Pair/NoPublication refuses it.
    if(!r.joined)r.destinationReasons|=destination::Context;
    r.destinationComparable=!r.destinationReasons;
    r.stateReasons=r.before.state.reasons|r.after.state.reasons;
    if(!state::SameObject(r.before.state,r.after.state)||
       r.before.state.physics!=r.before.physics||r.after.state.physics!=r.after.physics||
       r.before.state.vtable!=r.before.physicsVtable||r.after.state.vtable!=r.after.physicsVtable)
        r.stateReasons|=state::Identity;
    if(!r.joined)r.stateReasons|=state::Context;
    r.stateComparable=!r.stateReasons;
}
// Caller holds its own try-lock. No allocation, I/O, native access, or waits.
// Six first-event anchors leave rolling capacity even at the minimum size.
template<std::size_t Capacity=16> class Journal {
    static_assert(Capacity>=8);
public:
    static constexpr std::size_t MaxRecords=Capacity;
    void Publish(Publication p)noexcept {
        if(publications_&&(p.last.revision<=latest_.last.revision||p.update<=latest_.update||p.qpc<latest_.qpc)){
            p.reasons|=Overlap;p.complete=false;
        }
        p.generation=++publications_;p.firstMask=0;latest_=p;
        const bool active=(p.committedThrottle&0x7fffffffu)!=0;
        if(active&&!(publicationFirst_&1))p.firstMask|=1;
        if(!active&&(publicationFirst_&1)&&!(publicationFirst_&2))p.firstMask|=2;
        if(!p.complete&&!(publicationFirst_&4))p.firstMask|=4;
        publicationFirst_|=p.firstMask;
        Store(publicationRows_,publicationCount_,p,publicationOmitted_);
        if(!p.complete)++publicationFailures_;
    }
    void Observe(Record r)noexcept {
        r.sequence=++samples_;r.firstMask=0;Complete(r);
        const bool actual=r.beforeReadable&&(r.before.words[0]&0x7fffffffu)!=0;
        const bool expected=(r.publication.committedThrottle&0x7fffffffu)!=0;
        if(actual&&!(first_&1))r.firstMask|=1;
        if(r.beforeReadable&&!actual&&(first_&1)&&!(first_&2))r.firstMask|=2;
        if(!r.joined&&!(first_&4))r.firstMask|=4;
        if(r.beforeReadable&&r.before.zeroControls&&!(first_&8))r.firstMask|=8;
        if(r.beforeReadable&&(r.before.words[1]&0x7fffffffu)&&!(first_&16))r.firstMask|=16;
        if((actual||expected)&&!r.joined&&!(first_&32))r.firstMask|=32;
        first_|=r.firstMask;Store(rows_,count_,r,omitted_);if(!r.joined)++failures_;
    }
    const Publication& Latest()const noexcept{return latest_;}
    const Publication& PublicationAt(std::size_t i)const noexcept{return publicationRows_[i];}
    const Record& At(std::size_t i)const noexcept{return rows_[i];}
    std::size_t Size()const noexcept{return count_;}
    std::size_t PublicationSize()const noexcept{return publicationCount_;}
    std::uint64_t Samples()const noexcept{return samples_;}
    std::uint64_t Omitted()const noexcept{return omitted_;}
    std::uint64_t Failures()const noexcept{return failures_;}
    std::uint64_t Publications()const noexcept{return publications_;}
    std::uint64_t PublicationOmitted()const noexcept{return publicationOmitted_;}
    std::uint64_t PublicationFailures()const noexcept{return publicationFailures_;}
private:
    template<class T> static void Store(std::array<T,Capacity>& rows,std::size_t& count,const T& row,std::uint64_t& omitted)noexcept {
        std::size_t slot=count;
        if(count<Capacity)++count;
        else {slot=Capacity;for(std::size_t n=0;n<Capacity;++n)if(!rows[n].firstMask&&
            (slot==Capacity||Order(rows[n])<Order(rows[slot])))slot=n;++omitted;}
        rows[slot]=row;
    }
    static std::uint64_t Order(const Publication& p)noexcept{return p.generation;}
    static std::uint64_t Order(const Record& r)noexcept{return r.sequence;}
    std::array<Publication,Capacity> publicationRows_{};std::array<Record,Capacity> rows_{};Publication latest_{};
    std::size_t publicationCount_=0,count_=0;unsigned first_=0,publicationFirst_=0;
    std::uint64_t publications_=0,publicationOmitted_=0,publicationFailures_=0,samples_=0,omitted_=0,failures_=0;
};
static_assert(std::is_trivially_destructible_v<Record> && sizeof(Record)<2048);
static_assert(sizeof(Journal<>)<64*1024);
}
