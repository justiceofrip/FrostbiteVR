#pragma once
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstring>

namespace fvr::bc2::stereoColorScale {
using Rgb=std::array<float,3>;
using Effect=std::array<float,4>;
template<std::size_t N> struct ScalarMemory {
    using Value=std::array<float,N>;
    void* user=nullptr;
    bool(*read)(void*,std::uint32_t,void*,std::size_t) noexcept=nullptr;
    // Called only under the existing native render thread's view lifetime.
    // Compare all expected bytes before writing; no allocation or native calls.
    bool(*replace)(void*,std::uint32_t,const Value&,const Value&) noexcept=nullptr;
};
using Memory=ScalarMemory<3>;
using EffectMemory=ScalarMemory<4>;
template<std::size_t N> inline bool Read(const ScalarMemory<N>& m,std::uint32_t base,unsigned offset,void* out,std::size_t n) noexcept {
    return m.read&&base>=0x10000&&offset<=UINT32_MAX-base&&n&&n-1<=UINT32_MAX-(base+offset)&&
        m.read(m.user,base+offset,out,n);
}
template<std::size_t N> inline bool Same(const std::array<float,N>& a,const std::array<float,N>& b) noexcept {return std::memcmp(a.data(),b.data(),sizeof(a))==0;}
template<std::size_t N> inline bool Finite(const std::array<float,N>& value) noexcept {
    for(float x:value)if(!std::isfinite(x))return false;return true;
}
struct Types {std::uint32_t view=0,request=0,world=0;};
struct Key {
    std::uint32_t world=0,request=0,main=0,eye=0,frame=0,thread=0,currentThread=0;
    std::uint64_t lease=0,tracking=0,space=0;
    bool operator==(const Key&)const=default;
};
struct Counters {std::uint64_t main=0,right=0,writes=0,restores=0,rejected=0,restoreFailures=0;};

// Verified installed build3911fcc8...: view virtual58 returns+1530; virtual54
// writes only those three floats. Draw consumes them as diagonal(R,G,B,1).
// Consumer hash covers all646 bytes of BB35D0..BB3855, not just its first call.
inline bool VerifyCode(const Memory& m,std::uint32_t vtable,std::uint32_t draw) noexcept {
    std::uint32_t getter=0,setter=0;
    if(!Read(m,vtable,0x58,&getter,4)||!Read(m,vtable,0x54,&setter,4))return false;
    constexpr unsigned char get[]={0x8d,0x81,0x30,0x15,0,0,0xc3};
    constexpr unsigned char set[]={0x55,0x8b,0xec,0x83,0xe4,0xf0,0x8b,0x45,8,0xd9,0,
        0xd9,0x99,0x30,0x15,0,0,0xd9,0x40,4,0xd9,0x99,0x34,0x15,0,0,
        0xd9,0x40,8,0xd9,0x99,0x38,0x15,0,0,0x8b,0xe5,0x5d,0xc2,4,0};
    std::array<unsigned char,sizeof(get)> a{};std::array<unsigned char,sizeof(set)> b{};
    std::array<unsigned char,3> argument{},epilogue{};std::array<unsigned char,646> consumer{};
    if(!Read(m,getter,0,a.data(),a.size())||!Read(m,setter,0,b.data(),b.size())||
       !Read(m,draw,0x44,argument.data(),argument.size())||!Read(m,draw,0x1e2f,epilogue.data(),epilogue.size())||
       !Read(m,draw,0x1820,consumer.data(),consumer.size()))return false;
    std::uint64_t hash=14695981039346656037ull;for(auto c:consumer){hash^=c;hash*=1099511628211ull;}
    return !std::memcmp(a.data(),get,sizeof(get))&&!std::memcmp(b.data(),set,sizeof(set))&&
        argument==std::array<unsigned char,3>{0x8b,0x7d,0x0c}&&
        epilogue==std::array<unsigned char,3>{0xc2,0x0c,0}&&hash==0x707f2cc483e767bdull;
}

// Verified independent scalar block: getter/setter touch exactly four floats.
// Native Draw copies the view values to its own stack before synchronous submit.
// No texture/resource descriptor or opaque damage block is copied here.
inline bool VerifyEffectCode(const EffectMemory& m,std::uint32_t vtable,std::uint32_t draw) noexcept {
    std::uint32_t getter=0,setter=0;
    if(!Read(m,vtable,0xd8,&getter,4)||!Read(m,vtable,0xd4,&setter,4))return false;
    constexpr unsigned char get[]={0x8b,0x44,0x24,0x04,0xd9,0x81,0x50,0x16,0x00,0x00,0xd9,0x18,0xd9,0x81,0x54,0x16,0x00,0x00,0xd9,0x58,0x04,0xd9,0x81,0x58,0x16,0x00,0x00,0xd9,0x58,0x08,0xd9,0x81,0x5c,0x16,0x00,0x00,0xd9,0x58,0x0c,0xc2,0x04,0x00};
    constexpr unsigned char set[]={0x55,0x8b,0xec,0x83,0xe4,0xf0,0x8b,0x45,0x08,0xd9,0x00,0xd9,0x99,0x50,0x16,0x00,0x00,0xd9,0x40,0x04,0xd9,0x99,0x54,0x16,0x00,0x00,0xd9,0x40,0x08,0xd9,0x99,0x58,0x16,0x00,0x00,0xd9,0x40,0x0c,0xd9,0x99,0x5c,0x16,0x00,0x00,0x8b,0xe5,0x5d,0xc2,0x04,0x00};
    std::array<unsigned char,sizeof(get)> a{};std::array<unsigned char,sizeof(set)> b{};
    std::array<unsigned char,161> consumer{};std::array<unsigned char,3> argument{},epilogue{};
    if(!Read(m,getter,0,a.data(),a.size())||!Read(m,setter,0,b.data(),b.size())||
       !Read(m,draw,0x1b3f,consumer.data(),consumer.size())||
       !Read(m,draw,0x44,argument.data(),argument.size())||!Read(m,draw,0x1e2f,epilogue.data(),epilogue.size()))return false;
    std::uint64_t hash=14695981039346656037ull;for(auto c:consumer){hash^=c;hash*=1099511628211ull;}
    std::uint32_t thresholdAddress=0,threshold=~0u;
    std::memcpy(&thresholdAddress,consumer.data()+0x18,4);
    return !std::memcmp(a.data(),get,sizeof(get))&&!std::memcmp(b.data(),set,sizeof(set))&&
        argument==std::array<unsigned char,3>{0x8b,0x7d,0x0c}&&epilogue==std::array<unsigned char,3>{0xc2,0x0c,0}&&
        hash==0x7c9e697f43dfecd6ull&&Read(m,thresholdAddress,0,&threshold,4)&&threshold==0;
}

// Pin the exact numeric setters, matrix copy and native Draw consumer.
// The first two words use MOV in the copy helper, but their downstream
// consumer MOVSS-loads descriptor+A0/A4 into a submitted float4. Pin that
// arithmetic path and its constructor/vtable attachments as well.
// This verifier never invokes an engine function or follows a resource pointer.
template<std::size_t Bytes> inline bool PostProcessCodeSpan(const Memory& memory,std::uint32_t address,unsigned offset,std::uint64_t expected)noexcept {
    std::array<unsigned char,Bytes> bytes{};if(!Read(memory,address,offset,bytes.data(),bytes.size()))return false;
    std::uint64_t hash=14695981039346656037ull;for(auto c:bytes){hash^=c;hash*=1099511628211ull;}return hash==expected;
}
inline bool VerifyPostProcessCode(const Memory& memory,std::uint32_t vtable,std::uint32_t draw,std::uint32_t imageBase)noexcept {
    std::uint32_t getter=0,setter=0;std::int32_t displacement=0;
    if(!Read(memory,vtable,0x98,&getter,4)||!Read(memory,vtable,0x94,&setter,4)||
       !PostProcessCodeSpan<7>(memory,getter,0,0x39b90713e086892dull)||
       !PostProcessCodeSpan<11>(memory,setter,0,0x92ee30baec6cf0ceull)||!Read(memory,setter,7,&displacement,4))return false;
    const std::int64_t copy=std::int64_t(setter)+11+displacement;
    if(copy<0x10000||copy>UINT32_MAX||!PostProcessCodeSpan<82>(memory,std::uint32_t(copy),0,0x0277838d3736fd80ull)||
       !Read(memory,std::uint32_t(copy),0x45,&displacement,4))return false;
    const std::int64_t matrix=copy+0x49+displacement;
    return matrix>=0x10000&&matrix<=UINT32_MAX&&PostProcessCodeSpan<111>(memory,std::uint32_t(matrix),0,0xd23d94e7bb770901ull)&&
        PostProcessCodeSpan<224>(memory,draw,0x12ac,0x0e0c42aa38c0b5c2ull)&&
        PostProcessCodeSpan<18>(memory,draw,0x1a94,0xdb288f402ce1a5fcull)&&
        PostProcessCodeSpan<16>(memory,imageBase,0x5b5023,0x3f494f36b09f8501ull)&&
        PostProcessCodeSpan<61>(memory,imageBase,0x5b50e2,0xb57d5b5da102198dull)&&
        PostProcessCodeSpan<21>(memory,imageBase,0x5b5545,0x92f2fbd4aa2aba5cull)&&
        PostProcessCodeSpan<185>(memory,imageBase,0x5a941a,0x3c59a24ee35a18a1ull)&&
        PostProcessCodeSpan<58>(memory,imageBase,0x7a8110,0x4efbe51b95e22b3eull)&&
        PostProcessCodeSpan<80>(memory,imageBase,0x5cd170,0xb375b6a238935ea2ull)&&
        PostProcessCodeSpan<4>(memory,imageBase,0x1048010,0x841a113a73792d8bull)&&
        PostProcessCodeSpan<4>(memory,imageBase,0x10469e0,0x20d7964868350da0ull)&&
        PostProcessCodeSpan<4>(memory,imageBase,0x10468e0,0x20d7964868350da0ull)&&
        PostProcessCodeSpan<4>(memory,imageBase,0x1046ae0,0x20d7964868350da0ull);
}

template<std::size_t N,unsigned Offset,unsigned RequestState=UINT32_MAX> class ScalarPair {
    using Value=std::array<float,N>;
    ScalarMemory<N> memory_{};Types types_{};Key key_{};Value source_{};
    enum class Phase {Empty,Main,LeftComplete,Right,Complete,Rejected};Phase phase_=Phase::Empty;
    bool Word(std::uint32_t base,unsigned offset,std::uint32_t expected)const noexcept {
        std::uint32_t found=0;return Read(memory_,base,offset,&found,4)&&found==expected;
    }
    bool Owns(const Key& k,bool requireRequestState=true)const noexcept {
        return types_.view&&types_.request&&types_.world&&k.lease&&k.tracking&&k.space&&k.frame&&
            k.thread&&k.currentThread==k.thread&&k.main!=k.eye&&
            Word(k.world,0,types_.world)&&Word(k.world,0x88,k.frame)&&
            Word(k.request,0,types_.request)&&Word(k.request,8,k.world)&&
            (!requireRequestState||RequestState==UINT32_MAX||Word(k.request,0xc4,RequestState))&&
            Word(k.main,0,types_.view)&&Word(k.main,0x70,k.request)&&
            Word(k.eye,0,types_.view)&&Word(k.eye,0x70,k.request);
    }
    bool ReadValue(std::uint32_t view,Value& value)const noexcept {
        return Read(memory_,view,Offset,value.data(),sizeof(value))&&Finite(value);
    }
    bool SourceUnchanged(const Key& k)const noexcept {
        Value current{};return k==key_&&Owns(k)&&ReadValue(k.main,current)&&Same(current,source_)&&Owns(k);
    }
    void Reject()noexcept {if(phase_!=Phase::Rejected){++counts.rejected;phase_=Phase::Rejected;}}
public:
    Counters counts{};
    ScalarPair()=default;
    void Configure(ScalarMemory<N> memory,Types types)noexcept {memory_=memory;types_=types;Reset();}
    void Reset()noexcept {key_={};source_={};phase_=Phase::Empty;}
    bool Ready(const Key& current)const noexcept {return phase_==Phase::Complete&&SourceUnchanged(current);}
    class Scope {
        ScalarPair& pair_;Key key_{};Value before_{},written_{};unsigned eye_=0;
        bool engaged_=false,active_=false,writeSucceeded_=false,drawReturned_=false,finished_=false;
        bool DestinationUnchanged()const noexcept {
            Value current{};return pair_.Owns(key_)&&pair_.ReadValue(key_.eye,current)&&Same(current,written_);
        }
        bool Restore()noexcept {
            if(!active_)return true;active_=false;
            Value current{};
            // Restore only our exact bytes to the same owner, even after render phase changes.
            bool okay=pair_.Owns(key_,false)&&pair_.ReadValue(key_.eye,current);
            // A rejected compare-before-write may have made no change at all.
            if(okay&&!writeSucceeded_&&Same(current,before_))return true;
            okay=okay&&Same(current,written_)&&pair_.memory_.replace&&
                pair_.memory_.replace(pair_.memory_.user,key_.eye+Offset,written_,before_);
            Value restored{};okay=okay&&pair_.Owns(key_,false)&&pair_.ReadValue(key_.eye,restored)&&Same(restored,before_);
            if(okay)++pair_.counts.restores;else {++pair_.counts.restoreFailures;pair_.Reject();}
            return okay;
        }
    public:
        explicit Scope(ScalarPair& pair)noexcept:pair_(pair){}
        Scope(const Scope&)=delete;Scope& operator=(const Scope&)=delete;
        ~Scope(){Abort();}
        bool Begin(const Key& k,unsigned eye)noexcept {
            if(engaged_||finished_||eye>1)return false;
            engaged_=true;key_=k;eye_=eye;
            if(eye==0){
                if(pair_.phase_!=Phase::Empty||!pair_.Owns(k)||!pair_.ReadValue(k.main,pair_.source_)||!pair_.Owns(k)){
                    pair_.Reject();return false;
                }
                pair_.key_=k;pair_.phase_=Phase::Main;++pair_.counts.main;return true;
            }
            if(pair_.phase_!=Phase::LeftComplete||!pair_.SourceUnchanged(k)||!pair_.ReadValue(k.eye,before_)||!pair_.Owns(k)){
                pair_.Reject();return false;
            }
            pair_.phase_=Phase::Right;++pair_.counts.right;
            written_=pair_.source_;
            // Unity/equal native values need no write, but retain exact checks.
            if(Same(before_,pair_.source_))return true;
            active_=true;
            writeSucceeded_=pair_.memory_.replace&&pair_.memory_.replace(pair_.memory_.user,k.eye+Offset,before_,pair_.source_);
            if(writeSucceeded_)++pair_.counts.writes;
            if(!writeSucceeded_||!pair_.SourceUnchanged(k)||!DestinationUnchanged()){
                pair_.Reject();Restore();return false;
            }
            return true;
        }
        bool DrawReturned(const Key& now)noexcept {
            const bool okay=engaged_&&!finished_&&pair_.phase_==(eye_?Phase::Right:Phase::Main)&&
                now==key_&&pair_.SourceUnchanged(now)&&(!eye_||DestinationUnchanged());
            if(!okay)pair_.Reject();return drawReturned_=okay;
        }
        bool Complete(const Key& now,bool captured)noexcept {
            if(!engaged_||finished_)return false;
            bool okay=drawReturned_&&captured&&now==key_&&pair_.SourceUnchanged(now)&&
                pair_.phase_==(eye_?Phase::Right:Phase::Main)&&(!eye_||DestinationUnchanged());
            const bool restored=Restore();finished_=true;
            okay=okay&&restored;
            if(okay)pair_.phase_=eye_?Phase::Complete:Phase::LeftComplete;else pair_.Reject();
            return okay;
        }
        void Abort()noexcept {
            if(!engaged_||finished_)return;
            pair_.Reject();Restore();finished_=true;
        }
    };
};
using Pair=ScalarPair<3,0x1530>;
// Native world rendering consumes request state1; completion writes state3.
using EffectPair=ScalarPair<4,0x1650,1>;
// Native setters and Draw consume three disjoint numeric groups. Padding and
// the neighboring opaque damage block are deliberately outside these spans.
using PostProcessTripletPair=ScalarPair<3,0x1560,1>;
using PostProcessFivePair=ScalarPair<5,0x1570,1>;
using PostProcessMatrixPair=ScalarPair<16,0x1590,1>;
struct PostProcessPair {
    PostProcessTripletPair triplet;
    PostProcessFivePair five;
    PostProcessMatrixPair matrix;
    void Configure(ScalarMemory<3> a,ScalarMemory<5> b,ScalarMemory<16> c,Types types)noexcept {
        triplet.Configure(a,types);five.Configure(b,types);matrix.Configure(c,types);
    }
    void Reset()noexcept {triplet.Reset();five.Reset();matrix.Reset();}
    bool RestoreFailed()const noexcept {
        return triplet.counts.restoreFailures||five.counts.restoreFailures||matrix.counts.restoreFailures;
    }
    bool Ready(const Key& key)const noexcept {
        const bool a=triplet.Ready(key),b=five.Ready(key),c=matrix.Ready(key);
        return a&&b&&c;
    }
    class Scope {
        PostProcessTripletPair::Scope triplet_;
        PostProcessFivePair::Scope five_;
        PostProcessMatrixPair::Scope matrix_;
    public:
        explicit Scope(PostProcessPair& pair)noexcept:triplet_(pair.triplet),five_(pair.five),matrix_(pair.matrix){}
        Scope(const Scope&)=delete;Scope& operator=(const Scope&)=delete;
        ~Scope(){Abort();}
        bool Begin(const Key& key,unsigned eye)noexcept {
            const bool a=triplet_.Begin(key,eye),b=five_.Begin(key,eye),c=matrix_.Begin(key,eye);
            // A later group may reject its write. Roll back every earlier group
            // before returning to native Draw, without publishing a partial set.
            if(!a||!b||!c){Abort();return false;}return true;
        }
        bool DrawReturned(const Key& key)noexcept {
            const bool a=triplet_.DrawReturned(key),b=five_.DrawReturned(key),c=matrix_.DrawReturned(key);
            return a&&b&&c;
        }
        bool Complete(const Key& key,bool captured)noexcept {
            // Never short-circuit cleanup when another group's check fails.
            const bool c=matrix_.Complete(key,captured),b=five_.Complete(key,captured),a=triplet_.Complete(key,captured);
            return a&&b&&c;
        }
        void Abort()noexcept {matrix_.Abort();five_.Abort();triplet_.Abort();}
    };
};

}
