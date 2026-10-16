#include "Bc2StereoColorScale.h"
#include <iostream>
#include <stdexcept>
#include <limits>
#include <vector>
#ifdef _WIN32
#include <Windows.h>
#endif
using namespace fvr::bc2::stereoColorScale;
void Check(bool value,const char* label){if(!value)throw std::runtime_error(label);}
struct Fixture {
    static constexpr unsigned Base=0x10000,World=0x10100,Request=0x10300,Main=0x12000,Eye=0x14000;
    std::vector<unsigned char> bytes=std::vector<unsigned char>(0x10000);
    std::uint32_t failRead=0,nativeThreshold=0;bool failWrite=false,failAfterWrite=false;unsigned writes=0;
    Types types{0x19100,0x19200,0x19300};Key key{World,Request,Main,Eye,17,42,42,77,80,3};
    Pair pair;
    template<class T>void Put(unsigned p,const T& v){std::memcpy(bytes.data()+p-Base,&v,sizeof(v));}
    template<class T>T Get(unsigned p){T v{};std::memcpy(&v,bytes.data()+p-Base,sizeof(v));return v;}
    static bool ReadMemory(void* user,unsigned p,void* out,std::size_t n)noexcept {
        auto& f=*static_cast<Fixture*>(user);
        if(p==0x14d9410&&n==4){std::memcpy(out,&f.nativeThreshold,4);return true;}
        if(p<Base||p>Base+f.bytes.size()||n>Base+f.bytes.size()-p||p==f.failRead)return false;
        std::memcpy(out,f.bytes.data()+p-Base,n);return true;
    }
    static bool Replace(void* user,unsigned p,const Rgb& expected,const Rgb& replacement)noexcept {
        auto& f=*static_cast<Fixture*>(user);
        if(f.failWrite||p!=Eye+0x1530||!Same(f.Get<Rgb>(p),expected))return false;
        f.Put(p,replacement);++f.writes;if(f.failAfterWrite)f.failRead=p;return true;
    }
    static bool ReplaceEffect(void* user,unsigned p,const Effect& expected,const Effect& replacement)noexcept {
        auto& f=*static_cast<Fixture*>(user);
        if(f.failWrite||p!=Eye+0x1650||!Same(f.Get<Effect>(p),expected))return false;
        f.Put(p,replacement);++f.writes;if(f.failAfterWrite)f.failRead=p;return true;
    }
    Memory Mem(){return {this,ReadMemory,Replace};}
    EffectMemory EffectMem(){return {this,ReadMemory,ReplaceEffect};}
    Fixture(){
        Put(World,types.world);Put(World+0x88,key.frame);Put(Request,types.request);Put(Request+8,World);Put(Request+0xc4,1u);
        for(auto v:{Main,Eye}){Put(v,types.view);Put(v+0x70,Request);Put(v+0x1530,Rgb{1,1,1});}
        pair.Configure(Mem(),types);
    }
    void Left(){Pair::Scope scope(pair);Check(scope.Begin(key,0),"left begin");Check(scope.DrawReturned(key),"left return");Check(scope.Complete(key,true),"left capture");}
};
Rgb ConsumeColor(const Rgb& color,const Rgb& scale){return {color[0]*scale[0],color[1]*scale[1],color[2]*scale[2]};}
#ifdef _WIN32
void AbnormalOriginal(Pair::Scope* scope){
    __try {RaiseException(0xe1234567,0,0,nullptr);}
    __finally {if(AbnormalTermination())scope->Abort();}
}
bool CatchAbnormalOriginal(Pair::Scope* scope){
    __try {AbnormalOriginal(scope);return false;}
    __except(GetExceptionCode()==0xe1234567?EXCEPTION_EXECUTE_HANDLER:EXCEPTION_CONTINUE_SEARCH){return true;}
}
#endif
Effect EffectFromBits(std::array<unsigned,4> bits){Effect out{};std::memcpy(out.data(),bits.data(),sizeof(out));return out;}
bool EffectSubmitted(const Effect& effect){for(float v:effect)if(v>0.f)return true;return false;}
void EffectLeft(EffectPair& pair,const Key& key){EffectPair::Scope left(pair);Check(left.Begin(key,0),"effect left begin");Check(left.DrawReturned(key)&&left.Complete(key,true),"effect left draw/capture");}
#ifdef _WIN32
void AbnormalBothScopes(Pair::Scope* color,EffectPair::Scope* effect){
    __try {RaiseException(0xe1234567,0,0,nullptr);}
    __finally {if(AbnormalTermination()){effect->Abort();color->Abort();}}
}
bool CatchAbnormalBothScopes(Pair::Scope* color,EffectPair::Scope* effect){
    __try {AbnormalBothScopes(color,effect);return false;}
    __except(GetExceptionCode()==0xe1234567?EXCEPTION_EXECUTE_HANDLER:EXCEPTION_CONTINUE_SEARCH){return true;}
}
#endif
void RunEffectScopeTests(){
    Fixture code;code.Put(0x10100+0xd8,0x12000u);code.Put(0x10100+0xd4,0x12100u);
    constexpr unsigned char getter[]={0x8b,0x44,0x24,0x04,0xd9,0x81,0x50,0x16,0x00,0x00,0xd9,0x18,0xd9,0x81,0x54,0x16,0x00,0x00,0xd9,0x58,0x04,0xd9,0x81,0x58,0x16,0x00,0x00,0xd9,0x58,0x08,0xd9,0x81,0x5c,0x16,0x00,0x00,0xd9,0x58,0x0c,0xc2,0x04,0x00};
    constexpr unsigned char setter[]={0x55,0x8b,0xec,0x83,0xe4,0xf0,0x8b,0x45,0x08,0xd9,0x00,0xd9,0x99,0x50,0x16,0x00,0x00,0xd9,0x40,0x04,0xd9,0x99,0x54,0x16,0x00,0x00,0xd9,0x40,0x08,0xd9,0x99,0x58,0x16,0x00,0x00,0xd9,0x40,0x0c,0xd9,0x99,0x5c,0x16,0x00,0x00,0x8b,0xe5,0x5d,0xc2,0x04,0x00};
    constexpr unsigned char consumer[]={0x8b,0x17,0x8b,0x92,0xd8,0x00,0x00,0x00,0x8d,0x44,0x24,0x40,0x50,0x8b,0xcf,0xff,0xd2,0xf3,0x0f,0x10,0x00,0x0f,0x2f,0x05,0x10,0x94,0x4d,0x01,0x77,0x5d,0x8b,0x07,0x8b,0x90,0xd8,0x00,0x00,0x00,0x8d,0x4c,0x24,0x40,0x51,0x8b,0xcf,0xff,0xd2,0xf3,0x0f,0x10,0x40,0x04,0x0f,0x2f,0x05,0x10,0x94,0x4d,0x01,0x77,0x3e,0x8b,0x07,0x8b,0x90,0xd8,0x00,0x00,0x00,0x8d,0x4c,0x24,0x40,0x51,0x8b,0xcf,0xff,0xd2,0xf3,0x0f,0x10,0x40,0x08,0x0f,0x2f,0x05,0x10,0x94,0x4d,0x01,0x77,0x1f,0x8b,0x07,0x8b,0x90,0xd8,0x00,0x00,0x00,0x8d,0x4c,0x24,0x40,0x51,0x8b,0xcf,0xff,0xd2,0xf3,0x0f,0x10,0x40,0x0c,0x0f,0x2f,0x05,0x10,0x94,0x4d,0x01,0x76,0x26,0x8b,0x46,0x70,0x8b,0x18,0x89,0x44,0x24,0x18,0x8b,0x07,0x8b,0x90,0xd8,0x00,0x00,0x00,0x8d,0x4c,0x24,0x40,0x51,0x8b,0xcf,0x83,0xc3,0x20,0xff,0xd2,0x8b,0x4c,0x24,0x18,0x50,0x8b,0x03,0xff,0xd0};
    code.Put(0x12000,getter);code.Put(0x12100,setter);code.Put(0x14000+0x1b3f,consumer);
    code.Put(0x14000+0x44,std::array<unsigned char,3>{0x8b,0x7d,0x0c});
    code.Put(0x14000+0x1e2f,std::array<unsigned char,3>{0xc2,0x0c,0});
    Check(VerifyEffectCode(code.EffectMem(),0x10100,0x14000),"effect getter/setter/full consumer accepted");
    for(unsigned address:{0x12000u+6,0x12100u+13,0x14000u+0x1b3f+100}){
        code.bytes[address-Fixture::Base]^=1;Check(!VerifyEffectCode(code.EffectMem(),0x10100,0x14000),"changed effect binding rejected");code.bytes[address-Fixture::Base]^=1;
    }
    code.nativeThreshold=1;Check(!VerifyEffectCode(code.EffectMem(),0x10100,0x14000),"changed native effect threshold rejected");

    // Actual mission06 tuples: first clean pair, single-channel hit, four-channel
    // transition, peak, and late decay. Extra eye remained zero in all83frames.
    const std::array<std::array<unsigned,4>,5> observed{{{0,0,0,0},
        {1064403306u,0,0,0},{1065353216u,1034804304u,1034804304u,1034804304u},
        {1065353216u,1065353216u,1065353216u,1065353216u},{1047569180u,0,0,0}}};
    unsigned oldMissing=0;
    for(const auto bits:observed){
        Fixture x;const auto source=EffectFromBits(bits);x.Put(Fixture::Main+0x1650,source);x.Put(Fixture::Eye+0x1650,Effect{});
        x.Put(Fixture::Eye+0x164c,0xaabbccddu);x.Put(Fixture::Eye+0x1660,0x11223344u);x.Put(Fixture::Eye+0x15d0,0xdeadbeefu);
        x.Put(Fixture::Main+0x90,11u);x.Put(Fixture::Eye+0x90,22u);
        // The old RGB-only scope cannot fix the native effect-submit predicate.
        x.Left();{Pair::Scope oldRight(x.pair);Check(oldRight.Begin(x.key,1),"old RGB path begin");
            if(EffectSubmitted(source)!=EffectSubmitted(x.Get<Effect>(Fixture::Eye+0x1650)))++oldMissing;
            Check(oldRight.DrawReturned(x.key)&&oldRight.Complete(x.key,true),"old RGB path completes despite effect divergence");}
        EffectPair effect;effect.Configure(x.EffectMem(),x.types);EffectLeft(effect,x.key);
        EffectPair::Scope right(effect);Check(right.Begin(x.key,1),"effect right begin");
        Check(Same(source,x.Get<Effect>(Fixture::Eye+0x1650))&&EffectSubmitted(source)==EffectSubmitted(x.Get<Effect>(Fixture::Eye+0x1650)),"both native effect inputs and submit gates agree");
        Check(right.DrawReturned(x.key),"effect native draw returned");
        Check(Same(source,x.Get<Effect>(Fixture::Eye+0x1650)),"effect remains applied through capture boundary");
        Check(right.Complete(x.key,true)&&effect.Ready(x.key),"effect publishes only after exact restoration");
        Check(Same(x.Get<Effect>(Fixture::Eye+0x1650),Effect{})&&Same(x.Get<Effect>(Fixture::Main+0x1650),source),"extra restored and original source unchanged");
        Check(x.Get<unsigned>(Fixture::Eye+0x164c)==0xaabbccddu&&x.Get<unsigned>(Fixture::Eye+0x1660)==0x11223344u&&x.Get<unsigned>(Fixture::Eye+0x15d0)==0xdeadbeefu,"exact16-byte extent; opaque damage block untouched");
        Check(x.Get<unsigned>(Fixture::Main+0x90)==11&&x.Get<unsigned>(Fixture::Eye+0x90)==22&&Same(x.Get<Rgb>(Fixture::Eye+0x1530),Rgb{1,1,1}),"effect scope preserves distinct cameras and RGB");
    }
    Check(oldMissing==4,"measured old RGB-only path misses four nonzero effect tuples");
    for(unsigned phase:{0u,2u,3u}){Fixture x;x.Put(Fixture::Request+0xc4,phase);EffectPair effect;effect.Configure(x.EffectMem(),x.types);EffectPair::Scope left(effect);
        Check(!left.Begin(x.key,0)&&x.writes==0,"effect scope rejects nonrender request phases");}
    for(unsigned phase:{0u,2u,3u}){Fixture x;x.Put(Fixture::Main+0x1650,Effect{1,.5f,.5f,.5f});EffectPair effect;effect.Configure(x.EffectMem(),x.types);EffectLeft(effect,x.key);
        EffectPair::Scope right(effect);Check(right.Begin(x.key,1),"effect phase change begins from render state");x.Put(Fixture::Request+0xc4,phase);
        Check(!right.DrawReturned(x.key)&&!right.Complete(x.key,true)&&!effect.Ready(x.key),"phase change rejects publication");
        Check(Same(x.Get<Effect>(Fixture::Eye+0x1650),Effect{})&&effect.counts.restores==1&&effect.counts.restoreFailures==0,"phase change still restores exact bytes under unchanged identity");}
    {Fixture x;x.Put(Fixture::Main+0x1650,Effect{1,0,0,0});EffectPair effect;effect.Configure(x.EffectMem(),x.types);EffectLeft(effect,x.key);auto stale=x.key;stale.lease++;
        EffectPair::Scope right(effect);Check(!right.Begin(stale,1)&&x.writes==0,"effect stale lease rejected");}
    {Fixture x;auto foreign=x.key;foreign.currentThread++;EffectPair effect;effect.Configure(x.EffectMem(),x.types);EffectPair::Scope left(effect);
        Check(!left.Begin(foreign,0)&&x.writes==0,"effect foreign graphics thread rejected");}
    {Fixture x;EffectPair effect;effect.Configure(x.EffectMem(),x.types);EffectPair::Scope right(effect);Check(!right.Begin(x.key,1),"effect right before left rejected");}
    for(float bad:{std::numeric_limits<float>::quiet_NaN(),std::numeric_limits<float>::infinity()}){
        Fixture x;x.Put(Fixture::Main+0x1650,Effect{0,0,0,bad});EffectPair effect;effect.Configure(x.EffectMem(),x.types);EffectPair::Scope left(effect);
        Check(!left.Begin(x.key,0),"effect fourth component nonfinite rejected");}
    {Fixture x;const Effect source{-0.f,.25f,.5f,1.f};x.Put(Fixture::Main+0x1650,source);EffectPair effect;effect.Configure(x.EffectMem(),x.types);EffectLeft(effect,x.key);
        EffectPair::Scope right(effect);Check(right.Begin(x.key,1)&&Same(x.Get<Effect>(Fixture::Eye+0x1650),source),"effect signedzero/nonuniform exact bytes");
        Check(right.DrawReturned(x.key)&&right.Complete(x.key,true),"effect signedzero restore");}
    {Fixture x;x.Put(Fixture::Main+0x1650,Effect{1,0,0,0});EffectPair effect;effect.Configure(x.EffectMem(),x.types);EffectLeft(effect,x.key);x.Put(Fixture::Main+0x1650,Effect{.5f,0,0,0});
        EffectPair::Scope right(effect);Check(!right.Begin(x.key,1)&&x.writes==0,"effect source mutation between eyes rejected");}
    {Fixture x;x.Put(Fixture::Main+0x1650,Effect{1,0,0,0});EffectPair effect;effect.Configure(x.EffectMem(),x.types);EffectLeft(effect,x.key);EffectPair::Scope right(effect);Check(right.Begin(x.key,1),"effect conflicting write begin");
        const Effect newer{.25f,.5f,.75f,1};x.Put(Fixture::Eye+0x1650,newer);Check(!right.DrawReturned(x.key)&&!right.Complete(x.key,true)&&!effect.Ready(x.key),"effect conflicting destination cancels publication");
        Check(Same(x.Get<Effect>(Fixture::Eye+0x1650),newer)&&effect.counts.restoreFailures==1,"effect never restores over newer bytes");}
    {Fixture x;x.Put(Fixture::Main+0x1650,Effect{1,0,0,0});EffectPair effect;effect.Configure(x.EffectMem(),x.types);EffectLeft(effect,x.key);EffectPair::Scope right(effect);Check(right.Begin(x.key,1),"effect owner turnover begin");
        x.Put(Fixture::Eye+0x70,Fixture::Request+4);Check(!right.Complete(x.key,true)&&x.writes==1&&effect.counts.restoreFailures==1,"effect never restores into a replacement owner");}
    {Fixture x;x.Put(Fixture::Main+0x1650,Effect{1,0,0,0});EffectPair effect;effect.Configure(x.EffectMem(),x.types);EffectLeft(effect,x.key);EffectPair::Scope right(effect);Check(right.Begin(x.key,1)&&right.DrawReturned(x.key),"effect capture failure begin");
        Check(!right.Complete(x.key,false)&&Same(x.Get<Effect>(Fixture::Eye+0x1650),Effect{})&&!effect.Ready(x.key),"effect capture failure restores and cancels");}
    {Fixture x;x.Put(Fixture::Main+0x1650,Effect{1,0,0,0});EffectPair effect;effect.Configure(x.EffectMem(),x.types);EffectLeft(effect,x.key);x.failWrite=true;EffectPair::Scope right(effect);
        Check(!right.Begin(x.key,1)&&Same(x.Get<Effect>(Fixture::Eye+0x1650),Effect{})&&!effect.Ready(x.key),"rejected effect write leaves original destination");}
    {Fixture x;x.Put(Fixture::Main+0x1530,Rgb{.5f,.5f,.5f});x.Put(Fixture::Main+0x1650,Effect{1,.5f,.5f,.5f});x.Left();EffectPair effect;effect.Configure(x.EffectMem(),x.types);EffectLeft(effect,x.key);
        try{Pair::Scope color(x.pair);EffectPair::Scope scalar(effect);Check(color.Begin(x.key,1)&&scalar.Begin(x.key,1),"both scalar scopes begin");throw 7;}catch(int){}
        Check(Same(x.Get<Rgb>(Fixture::Eye+0x1530),Rgb{1,1,1})&&Same(x.Get<Effect>(Fixture::Eye+0x1650),Effect{})&&!x.pair.Ready(x.key)&&!effect.Ready(x.key),"C++ unwinding restores both independent scalar spans");}
#ifdef _WIN32
    {Fixture x;x.Put(Fixture::Main+0x1530,Rgb{.5f,.5f,.5f});x.Put(Fixture::Main+0x1650,Effect{1,.5f,.5f,.5f});x.Left();EffectPair effect;effect.Configure(x.EffectMem(),x.types);EffectLeft(effect,x.key);
        Pair::Scope color(x.pair);EffectPair::Scope scalar(effect);Check(color.Begin(x.key,1)&&scalar.Begin(x.key,1),"both scopes SEH begin");x.Put(Fixture::Request+0xc4,3u);Check(CatchAbnormalBothScopes(&color,&scalar),"native exception traverses both-scope finally after render phase changes");
        scalar.Abort();color.Abort();Check(Same(x.Get<Rgb>(Fixture::Eye+0x1530),Rgb{1,1,1})&&Same(x.Get<Effect>(Fixture::Eye+0x1650),Effect{})&&effect.counts.restores==1&&x.pair.counts.restores==1,"actual native SEH restores both exactly once");}
#endif
}

template<std::size_t N> std::array<float,N> PostFloats(std::array<unsigned,N> bits){
    std::array<float,N> result{};std::memcpy(result.data(),bits.data(),sizeof(result));return result;
}
struct PostFixture:Fixture {
    using Three=std::array<float,3>;using Five=std::array<float,5>;using Matrix=std::array<float,16>;
    Three sourceThree=PostFloats<3>({0x400ccccdu,0x3fe66666u,0x40866666u});
    Five sourceFive=PostFloats<5>({0x3ec49ba6u,0x00000000u,0x00000000u,0x3e89ba5eu,0x00000000u});
    Matrix sourceMatrix=PostFloats<16>({0x3f5bd822u,0x3d503087u,0x3d891cc1u,0x00000000u,0x3e00e32eu,0x3f6aed55u,0x3de0c1e0u,0x00000000u,0x3c7bc4d7u,0x3d00fa30u,0x3f52c42du,0x00000000u,0x00000000u,0x00000000u,0x00000000u,0x3f800000u});
    const Three extraThree{2,2,2};const Five extraFive{};
    const Matrix extraMatrix{1,0,0,0,0,1,0,0,0,0,1,0,0,0,0,1};
    unsigned failGroup=0,failRestore=0;std::array<unsigned,3> groupWrites{};
    PostProcessPair post;
    template<std::size_t N,unsigned Offset> static bool ReplaceGroup(void* user,unsigned p,const std::array<float,N>& expected,const std::array<float,N>& replacement)noexcept {
        auto& x=*static_cast<PostFixture*>(static_cast<Fixture*>(user));
        constexpr unsigned group=Offset==0x1560?0:Offset==0x1570?1:2;
        if(p!=Eye+Offset||x.failGroup==Offset||(x.failRestore==Offset&&x.groupWrites[group])||!Same(x.Get<std::array<float,N>>(p),expected))return false;
        x.Put(p,replacement);++x.groupWrites[group];++x.writes;return true;
    }
    PostFixture(){
        // A raw whole-block copy would corrupt these deliberately different holes.
        for(unsigned offset:{0x156cu,0x1584u,0x1588u,0x158cu,0x15d0u,0x15d4u,0x15f4u,0x155cu}){
            Put(Main+offset,0x12340000u+offset);Put(Eye+offset,0xfedc0000u+offset);
        }
        Put(Main+0x90,0x11111111u);Put(Eye+0x90,0x22222222u);
        SetSource();Put(Eye+0x1560,extraThree);Put(Eye+0x1570,extraFive);Put(Eye+0x1590,extraMatrix);
        post.Configure({static_cast<Fixture*>(this),ReadMemory,ReplaceGroup<3,0x1560>},
            {static_cast<Fixture*>(this),ReadMemory,ReplaceGroup<5,0x1570>},
            {static_cast<Fixture*>(this),ReadMemory,ReplaceGroup<16,0x1590>},types);
    }
    void SetSource(){Put(Main+0x1560,sourceThree);Put(Main+0x1570,sourceFive);Put(Main+0x1590,sourceMatrix);}
    void LeftPost(){PostProcessPair::Scope left(post);Check(left.Begin(key,0)&&left.DrawReturned(key)&&left.Complete(key,true),"postprocess main draw/capture");}
    bool ExtraMatchesSource(){return Same(Get<Three>(Eye+0x1560),sourceThree)&&Same(Get<Five>(Eye+0x1570),sourceFive)&&Same(Get<Matrix>(Eye+0x1590),sourceMatrix);}
    bool ExtraRestored(){return Same(Get<Three>(Eye+0x1560),extraThree)&&Same(Get<Five>(Eye+0x1570),extraFive)&&Same(Get<Matrix>(Eye+0x1590),extraMatrix);}
};
struct PostCodeFixture {
    struct Segment {unsigned address;std::vector<unsigned char> bytes;};std::vector<Segment> segments;
    void Add(unsigned address,std::initializer_list<unsigned char> bytes){segments.push_back({address,bytes});}
    void Word(unsigned address,unsigned value){std::array<unsigned char,4> bytes{};std::memcpy(bytes.data(),&value,4);segments.push_back({address,std::vector<unsigned char>(bytes.begin(),bytes.end())});}
    static bool ReadCode(void* user,unsigned address,void* out,std::size_t n)noexcept {
        auto& self=*static_cast<PostCodeFixture*>(user);
        for(const auto& span:self.segments)if(address>=span.address&&address-span.address<=span.bytes.size()&&n<=span.bytes.size()-(address-span.address)){
            std::memcpy(out,span.bytes.data()+address-span.address,n);return true;
        }return false;
    }
    Memory Mem(){return {this,ReadCode,nullptr};}
};
#ifdef _WIN32
void AbnormalAllPostScopes(Pair::Scope* color,EffectPair::Scope* effect,PostProcessPair::Scope* post){
    __try {RaiseException(0xe1234567,0,0,nullptr);}
    __finally {if(AbnormalTermination()){post->Abort();effect->Abort();color->Abort();}}
}
bool CatchAbnormalAllPostScopes(Pair::Scope* color,EffectPair::Scope* effect,PostProcessPair::Scope* post){
    __try {AbnormalAllPostScopes(color,effect,post);return false;}
    __except(GetExceptionCode()==0xe1234567?EXCEPTION_EXECUTE_HANDLER:EXCEPTION_CONTINUE_SEARCH){return true;}
}
#endif
void RunPostProcessScopeTests(){
    PostCodeFixture code;code.Word(0x14776f8+0x98,0xb86380);code.Word(0x14776f8+0x94,0xb86370);
    code.Add(0xb86380, {0x8d,0x81,0x60,0x15,0x00,0x00,0xc3});
    code.Add(0xb86370, {0x81,0xc1,0x60,0x15,0x00,0x00,0xe9,0x85,0x86,0xa3,0xff});
    code.Add(0x5bea00, {0x55,0x8b,0xec,0x83,0xe4,0xf0,0x8b,0x45,0x08,0x83,0xec,0x0c,0x56,0x8b,0xf1,0x8b,0x08,0x89,0x0e,0x8b,0x50,0x04,0x89,0x56,0x04,0xd9,0x40,0x08,0xd9,0x5e,0x08,0x83,0xc0,0x30,0xd9,0x40,0xe0,0x50,0xd9,0x5e,0x10,0x8d,0x4e,0x30,0xd9,0x40,0xe4,0xd9,0x5e,0x14,0xd9,0x40,0xe8,0xd9,0x5e,0x18,0xd9,0x40,0xec,0xd9,0x5e,0x1c,0xd9,0x40,0xf0,0xd9,0x5e,0x20,0xe8,0xa7,0x53,0xe4,0xff,0x8b,0xc6,0x5e,0x8b,0xe5,0x5d,0xc2,0x04,0x00});
    code.Add(0x403df0, {0x55,0x8b,0xec,0x83,0xe4,0xf0,0x8b,0xc1,0x8b,0x4d,0x08,0xd9,0x01,0xd9,0x18,0xd9,0x41,0x04,0xd9,0x58,0x04,0xd9,0x41,0x08,0xd9,0x58,0x08,0xd9,0x41,0x0c,0xd9,0x58,0x0c,0xd9,0x41,0x10,0xd9,0x58,0x10,0xd9,0x41,0x14,0xd9,0x58,0x14,0xd9,0x41,0x18,0xd9,0x58,0x18,0xd9,0x41,0x1c,0xd9,0x58,0x1c,0xd9,0x41,0x20,0xd9,0x58,0x20,0xd9,0x41,0x24,0xd9,0x58,0x24,0xd9,0x41,0x28,0xd9,0x58,0x28,0xd9,0x41,0x2c,0xd9,0x58,0x2c,0xd9,0x41,0x30,0xd9,0x58,0x30,0xd9,0x41,0x34,0xd9,0x58,0x34,0xd9,0x41,0x38,0xd9,0x58,0x38,0xd9,0x41,0x3c,0xd9,0x58,0x3c,0x8b,0xe5,0x5d,0xc2,0x04,0x00});
    code.Add(0xbb305c, {0x8b,0x17,0x8b,0x82,0x98,0x00,0x00,0x00,0x8b,0xcf,0xf3,0x0f,0x11,0x84,0x24,0xf0,0x01,0x00,0x00,0xff,0xd0,0x8b,0x08,0x89,0x8c,0x24,0x00,0x02,0x00,0x00,0x8b,0x50,0x04,0x89,0x94,0x24,0x04,0x02,0x00,0x00,0xf3,0x0f,0x10,0x40,0x08,0xf3,0x0f,0x11,0x84,0x24,0x08,0x02,0x00,0x00,0xf3,0x0f,0x10,0x40,0x10,0x8d,0x9c,0x24,0x00,0x02,0x00,0x00,0x2b,0xd8,0xf3,0x0f,0x11,0x84,0x24,0x10,0x02,0x00,0x00,0xf3,0x0f,0x10,0x40,0x14,0x89,0x5c,0x24,0x20,0xf3,0x0f,0x11,0x84,0x24,0x14,0x02,0x00,0x00,0xf3,0x0f,0x10,0x40,0x18,0x8d,0x9c,0x24,0x04,0x02,0x00,0x00,0x2b,0xd8,0xf3,0x0f,0x11,0x84,0x24,0x18,0x02,0x00,0x00,0xf3,0x0f,0x10,0x40,0x1c,0x89,0x5c,0x24,0x18,0x8d,0x94,0x24,0xfc,0x01,0x00,0x00,0xf3,0x0f,0x11,0x84,0x24,0x1c,0x02,0x00,0x00,0xf3,0x0f,0x10,0x40,0x20,0x8d,0x9c,0x24,0xf8,0x01,0x00,0x00,0x2b,0xd0,0xf3,0x0f,0x11,0x84,0x24,0x20,0x02,0x00,0x00,0x8d,0x48,0x38,0x2b,0xd8,0xc7,0x44,0x24,0x10,0x04,0x00,0x00,0x00,0xd9,0x41,0xf8,0x8b,0x44,0x24,0x20,0xd9,0x1c,0x0b,0x83,0xc1,0x10,0x83,0x6c,0x24,0x10,0x01,0xd9,0x41,0xec,0xd9,0x5c,0x0a,0xf0,0xd9,0x41,0xf0,0xd9,0x5c,0x08,0xf0,0x8b,0x44,0x24,0x18,0xd9,0x41,0xf4,0xd9,0x5c,0x08,0xf0,0x75,0xd3});
    code.Add(0xbb3844, {0x8b,0x4e,0x70,0x8b,0x01,0x8b,0x40,0x18,0x8d,0x94,0x24,0xc0,0x01,0x00,0x00,0x52,0xff,0xd0});
    code.Add(0x009b5023, {0x8d,0x43,0x40,0x50,0x8d,0x8c,0x24,0x74,0x02,0x00,0x00,0xe8,0xcd,0x99,0xc0,0xff});
    code.Add(0x009b50e2, {0xf3,0x0f,0x10,0x41,0x20,0xf3,0x0f,0x10,0x0d,0x20,0x94,0x4d,0x01,0x0f,0x2e,0xc1,0x9f,0xf6,0xc4,0x44,0x7b,0x27,0xf3,0x0f,0x11,0x44,0x24,0x54,0xf3,0x0f,0x10,0x41,0x24,0x8b,0x44,0x24,0x54,0xf3,0x0f,0x11,0x44,0x24,0x58,0x8b,0x54,0x24,0x58,0x89,0x84,0x24,0x70,0x02,0x00,0x00,0x89,0x94,0x24,0x74,0x02,0x00,0x00});
    code.Add(0x009b5545, {0x8b,0x4e,0x2c,0x8b,0x11,0x8b,0x92,0x90,0x00,0x00,0x00,0x8d,0x84,0x24,0xd0,0x01,0x00,0x00,0x50,0xff,0xd2});
    code.Add(0x009a941a, {0xd9,0x87,0xa8,0x00,0x00,0x00,0xf3,0x0f,0x10,0x87,0xa0,0x00,0x00,0x00,0xdc,0x0d,0x88,0x94,0x4d,0x01,0xf3,0x0f,0x11,0x44,0x24,0x50,0xf3,0x0f,0x10,0x87,0xa4,0x00,0x00,0x00,0x8d,0x87,0xd0,0x00,0x00,0x00,0xd9,0x5c,0x24,0x58,0x50,0x8d,0x8c,0x24,0xb4,0x00,0x00,0x00,0xf3,0x0f,0x11,0x44,0x24,0x58,0x0f,0x57,0xc0,0x51,0xf3,0x0f,0x11,0x44,0x24,0x64,0xe8,0x9d,0x48,0xfc,0xff,0x8b,0x8e,0x30,0xa0,0x00,0x00,0x8b,0x11,0x8b,0x52,0x48,0x83,0xc4,0x08,0x8d,0x84,0x24,0xb0,0x00,0x00,0x00,0x50,0x8b,0x83,0x10,0x01,0x00,0x00,0x50,0xff,0xd2,0x8b,0x8e,0x30,0xa0,0x00,0x00,0x8b,0x01,0x8b,0x40,0x48,0x8d,0x94,0x24,0xc0,0x00,0x00,0x00,0x52,0x8b,0x93,0x14,0x01,0x00,0x00,0x52,0xff,0xd0,0x8b,0x8e,0x30,0xa0,0x00,0x00,0x8b,0x11,0x8b,0x52,0x48,0x8d,0x84,0x24,0xd0,0x00,0x00,0x00,0x50,0x8b,0x83,0x18,0x01,0x00,0x00,0x50,0xff,0xd2,0x8b,0x8e,0x30,0xa0,0x00,0x00,0x8b,0x01,0x8d,0x54,0x24,0x50,0x52,0x8b,0x93,0xc0,0x01,0x00,0x00,0x52,0x8b,0x40,0x48,0xff,0xd0});
    code.Add(0x00ba8110, {0x8b,0x56,0x68,0x8b,0x8e,0xbc,0x00,0x00,0x00,0x55,0x52,0x8b,0x96,0xc0,0x00,0x00,0x00,0x52,0x51,0x8b,0xc8,0xe8,0x46,0x50,0xe2,0xff,0x8b,0xf8,0xeb,0x02,0x33,0xff,0x8b,0x4e,0x70,0x3b,0xf9,0x74,0x1d,0x3b,0xcb,0x74,0x0a,0x8b,0x11,0x8b,0x42,0x04,0xff,0xd0,0x89,0x5e,0x70,0x3b,0xfb,0x89,0x7e,0x70});
    code.Add(0x009cd170, {0x83,0xec,0x30,0x53,0x56,0x8b,0xf1,0x8b,0x4c,0x24,0x48,0xc7,0x06,0x50,0x63,0x44,0x01,0x33,0xdb,0x3b,0xcb,0x89,0x5e,0x04,0xc7,0x06,0xf8,0x7f,0x44,0x01,0x57,0x89,0x4e,0x08,0x74,0x06,0x8b,0x01,0x8b,0x10,0xff,0xd2,0x8b,0x4c,0x24,0x44,0x8b,0x44,0x24,0x40,0x89,0x5e,0x0c,0x89,0x5e,0x10,0x89,0x4e,0x18,0x8b,0x4c,0x24,0x48,0x3b,0xcb,0x89,0x46,0x14,0x89,0x5e,0x1c,0x89,0x5e,0x20,0x89,0x5e,0x24,0x89,0x4e,0x2c});
    code.Add(0x01448010, {0xf0,0x37,0x9b,0x00});
    code.Add(0x014469e0, {0xd0,0x91,0x9a,0x00});
    code.Add(0x014468e0, {0xd0,0x91,0x9a,0x00});
    code.Add(0x01446ae0, {0xd0,0x91,0x9a,0x00});
    Check(VerifyPostProcessCode(code.Mem(),0x14776f8,0xbb1db0,0x400000),"postprocess exact native setter/matrix/Draw/downstream float contract");
    for(auto& segment:code.segments){auto& byte=segment.bytes[segment.bytes.size()/2];byte^=1;
        Check(!VerifyPostProcessCode(code.Mem(),0x14776f8,0xbb1db0,0x400000),"changed native postprocess span rejected");byte^=1;}
    Check(!VerifyPostProcessCode(code.Mem(),0x14776f8,0xbb1db0,UINT32_MAX-16),"overflowing downstream image span rejected");
    for(unsigned sample=0;sample<2;++sample){
        PostFixture x;
        if(sample){x.sourceThree=PostFloats<3>({0x4019999au,0x4019999au,0x3fcccccdu});x.sourceFive=PostFloats<5>({0x00000000u,0x00000000u,0x00000000u,0x3ee66666u,0x3f800000u});x.sourceMatrix=PostFloats<16>({0x3f1af6c7u,0x3e398204u,0x3e422ccbu,0x00000000u,0x3ec01bf7u,0x3f4a798fu,0x3ebbc693u,0x00000000u,0x3d8e404fu,0x3d9f95deu,0x3efcbca1u,0x00000000u,0xbcccccc0u,0xbcccccc0u,0xbcccccc0u,0x3f800000u});x.SetSource();}
        const auto exactBefore=x.bytes;
        // Actual mission07 values: old scopes leave all three postprocess groups
        // different even though RGB and the blood-vignette inputs already agree.
        x.Left();EffectPair effect;effect.Configure(x.EffectMem(),x.types);EffectLeft(effect,x.key);
        {Pair::Scope color(x.pair);EffectPair::Scope blood(effect);Check(color.Begin(x.key,1)&&blood.Begin(x.key,1),"old scopes begin");
            Check(!x.ExtraMatchesSource(),"old RGB/effect path retains measured postprocess divergence");
            Check(color.DrawReturned(x.key)&&blood.DrawReturned(x.key),"old scopes native return");
            const bool a=color.Complete(x.key,true),b=blood.Complete(x.key,true);Check(a&&b,"old scopes complete despite divergence");}
        x.LeftPost();PostProcessPair::Scope right(x.post);Check(right.Begin(x.key,1)&&x.ExtraMatchesSource(),"all24 native postprocess inputs now match");
        Check(right.DrawReturned(x.key)&&x.ExtraMatchesSource(),"postprocess remains applied through capture");
        Check(right.Complete(x.key,true)&&x.post.Ready(x.key)&&x.ExtraRestored(),"all groups restored before publication");
        Check(x.bytes==exactBefore,"exact full fixture unchanged: padding, opaque fields, cameras and source preserved");
        Check(x.post.triplet.counts.writes==1&&x.post.five.counts.writes==1&&x.post.matrix.counts.writes==1&&
            x.post.triplet.counts.restores==1&&x.post.five.counts.restores==1&&x.post.matrix.counts.restores==1,"per-group diagnostics count independent restoration");
    }
    {PostFixture x;x.sourceThree=x.extraThree;x.sourceFive=x.extraFive;x.sourceMatrix=x.extraMatrix;x.SetSource();const auto exactBefore=x.bytes;
        x.LeftPost();PostProcessPair::Scope right(x.post);
        Check(right.Begin(x.key,1)&&right.DrawReturned(x.key)&&right.Complete(x.key,true)&&x.post.Ready(x.key),"equal native postprocess groups publish without modification");
        Check(x.writes==0&&x.bytes==exactBefore,"equal groups require zero writes or restores");}
    for(unsigned offset:{0x1560u,0x1570u,0x1590u}){
        PostFixture x;const auto exactBefore=x.bytes;x.LeftPost();x.failGroup=offset;
        PostProcessPair::Scope right(x.post);Check(!right.Begin(x.key,1),"one postprocess group write rejected");
        Check(x.ExtraRestored()&&x.bytes==exactBefore&&!x.post.Ready(x.key)&&!x.post.RestoreFailed(),"partial Begin restores every other applied group immediately");
        right.Abort();Check(x.bytes==exactBefore,"partial Begin abort idempotent");
    }
    for(unsigned offset:{0x1560u,0x1570u,0x1590u}){
        PostFixture x;x.LeftPost();PostProcessPair::Scope right(x.post);Check(right.Begin(x.key,1)&&right.DrawReturned(x.key),"group restoration failure fixture begins");x.failRestore=offset;
        Check(!right.Complete(x.key,true)&&x.post.RestoreFailed()&&!x.post.Ready(x.key),"any restore failure blocks publication");
        Check((offset==0x1560||Same(x.Get<PostFixture::Three>(Fixture::Eye+0x1560),x.extraThree))&&
            (offset==0x1570||Same(x.Get<PostFixture::Five>(Fixture::Eye+0x1570),x.extraFive))&&
            (offset==0x1590||Same(x.Get<PostFixture::Matrix>(Fixture::Eye+0x1590),x.extraMatrix)),"a failed group never skips restoration of other groups");
    }
    for(unsigned offset:{0x1560u,0x1570u,0x1590u})for(float bad:{std::numeric_limits<float>::quiet_NaN(),std::numeric_limits<float>::infinity()}){
        PostFixture x;x.Put(Fixture::Main+offset,bad);PostProcessPair::Scope left(x.post);
        Check(!left.Begin(x.key,0)&&x.writes==0&&!x.post.Ready(x.key),"nonfinite value in any group rejected without writes");
    }
    {PostFixture x;x.sourceMatrix[15]=std::numeric_limits<float>::quiet_NaN();x.SetSource();PostProcessPair::Scope left(x.post);
        Check(!left.Begin(x.key,0)&&x.writes==0,"last matrix component must also be finite");}
    {PostFixture x;x.sourceFive[4]=-0.f;x.SetSource();x.LeftPost();PostProcessPair::Scope right(x.post);
        Check(right.Begin(x.key,1)&&x.ExtraMatchesSource()&&right.DrawReturned(x.key)&&right.Complete(x.key,true)&&x.ExtraRestored(),"signedzero and negative matrix values retain exact bytes");}
    {PostFixture x;x.LeftPost();x.Put(Fixture::Main+0x1590,.25f);PostProcessPair::Scope right(x.post);
        Check(!right.Begin(x.key,1)&&x.ExtraRestored()&&!x.post.Ready(x.key),"late matrix source change rolls back earlier groups");}
    {PostFixture x;x.LeftPost();PostProcessPair::Scope right(x.post);Check(right.Begin(x.key,1),"destination conflict begins");x.Put(Fixture::Eye+0x1570,.75f);
        Check(!right.DrawReturned(x.key)&&!right.Complete(x.key,true)&&x.Get<float>(Fixture::Eye+0x1570)==.75f,"newer destination bytes never overwritten");
        Check(Same(x.Get<PostFixture::Three>(Fixture::Eye+0x1560),x.extraThree)&&Same(x.Get<PostFixture::Matrix>(Fixture::Eye+0x1590),x.extraMatrix),"destination conflict does not prevent unrelated group restoration");}
    {PostFixture x;x.LeftPost();PostProcessPair::Scope right(x.post);Check(right.Begin(x.key,1)&&right.DrawReturned(x.key),"capture failure fixture begins");
        Check(!right.Complete(x.key,false)&&x.ExtraRestored()&&!x.post.Ready(x.key),"failed capture restores all groups and cancels");}
    for(unsigned state:{0u,2u,3u}){
        PostFixture x;x.LeftPost();PostProcessPair::Scope right(x.post);Check(right.Begin(x.key,1),"phase transition fixture begins");x.Put(Fixture::Request+0xc4,state);
        Check(!right.DrawReturned(x.key)&&!right.Complete(x.key,true)&&x.ExtraRestored()&&!x.post.RestoreFailed(),"phase change cancels publication but restores same-owner bytes");
    }
    {PostFixture x;x.LeftPost();PostProcessPair::Scope right(x.post);Check(right.Begin(x.key,1),"owner change fixture begins");x.Put(Fixture::Eye+0x70,Fixture::Request+4);
        const auto writes=x.writes;Check(!right.Complete(x.key,true)&&x.post.RestoreFailed()&&x.writes==writes,"replacement owner never receives restoration writes");}
    {PostFixture x;x.LeftPost();try{PostProcessPair::Scope right(x.post);Check(right.Begin(x.key,1),"postprocess C++ unwind begins");throw 7;}catch(int){}
        Check(x.ExtraRestored()&&!x.post.Ready(x.key),"C++ unwind restores all postprocess groups");}
#ifdef _WIN32
    {PostFixture x;x.Put(Fixture::Main+0x1530,Rgb{.5f,.5f,.5f});x.Put(Fixture::Main+0x1650,Effect{1,.5f,.5f,.5f});
        x.Left();EffectPair effect;effect.Configure(x.EffectMem(),x.types);EffectLeft(effect,x.key);x.LeftPost();
        Pair::Scope color(x.pair);EffectPair::Scope blood(effect);PostProcessPair::Scope post(x.post);
        Check(color.Begin(x.key,1)&&blood.Begin(x.key,1)&&post.Begin(x.key,1),"all five scalar spans active before SEH");x.Put(Fixture::Request+0xc4,3u);
        Check(CatchAbnormalAllPostScopes(&color,&blood,&post),"actual native SEH traverses combined cleanup");post.Abort();blood.Abort();color.Abort();
        Check(x.ExtraRestored()&&Same(x.Get<Rgb>(Fixture::Eye+0x1530),Rgb{1,1,1})&&Same(x.Get<Effect>(Fixture::Eye+0x1650),Effect{}),"SEH restores all five spans exactly despite phase change");
        Check(x.post.triplet.counts.restores==1&&x.post.five.counts.restores==1&&x.post.matrix.counts.restores==1&&effect.counts.restores==1&&x.pair.counts.restores==1,"SEH repeated abort never restores twice");}
#endif
}

int main(){try{
    RunPostProcessScopeTests();
    RunEffectScopeTests();
    // Exact contract from installed image SHA3911fcc8...; bytes are data only.
    // A changed getter field or color-matrix consumer must reject the binding.
    Fixture code;code.Put(0x10100+0x58,0x12000u);code.Put(0x10100+0x54,0x12100u);
    constexpr unsigned char getter[]={
        0x8d,0x81,0x30,0x15,0x00,0x00,0xc3,
    };
    constexpr unsigned char setter[]={
        0x55,0x8b,0xec,0x83,0xe4,0xf0,0x8b,0x45,0x08,0xd9,0x00,0xd9,0x99,0x30,0x15,0x00,
        0x00,0xd9,0x40,0x04,0xd9,0x99,0x34,0x15,0x00,0x00,0xd9,0x40,0x08,0xd9,0x99,0x38,
        0x15,0x00,0x00,0x8b,0xe5,0x5d,0xc2,0x04,0x00,
    };
    constexpr unsigned char consumer[]={
        0x8b,0x17,0x8b,0x42,0x58,0x8b,0xcf,0xff,0xd0,0xf3,0x0f,0x10,0x40,0x08,0x8b,0x17,
        0x8b,0x42,0x58,0x8b,0xcf,0xf3,0x0f,0x11,0x44,0x24,0x38,0xff,0xd0,0xf3,0x0f,0x10,
        0x40,0x04,0x8b,0x17,0x8b,0x42,0x58,0x8b,0xcf,0xf3,0x0f,0x11,0x44,0x24,0x44,0xff,
        0xd0,0xf3,0x0f,0x10,0x00,0xf3,0x0f,0x10,0x4c,0x24,0x44,0xf3,0x0f,0x11,0x84,0x24,
        0x70,0x01,0x00,0x00,0x0f,0x57,0xc0,0x8d,0x8c,0x24,0x70,0x01,0x00,0x00,0x51,0x8d,
        0x94,0x24,0xa4,0x00,0x00,0x00,0xf3,0x0f,0x11,0x84,0x24,0x78,0x01,0x00,0x00,0xf3,
        0x0f,0x11,0x84,0x24,0x7c,0x01,0x00,0x00,0xf3,0x0f,0x11,0x84,0x24,0x80,0x01,0x00,
        0x00,0xf3,0x0f,0x11,0x84,0x24,0x84,0x01,0x00,0x00,0xf3,0x0f,0x11,0x8c,0x24,0x88,
        0x01,0x00,0x00,0xf3,0x0f,0x10,0x4c,0x24,0x3c,0xf3,0x0f,0x11,0x84,0x24,0x8c,0x01,
        0x00,0x00,0xf3,0x0f,0x11,0x84,0x24,0x90,0x01,0x00,0x00,0xf3,0x0f,0x11,0x84,0x24,
        0x94,0x01,0x00,0x00,0xf3,0x0f,0x11,0x84,0x24,0x98,0x01,0x00,0x00,0xf3,0x0f,0x11,
        0x84,0x24,0xa0,0x01,0x00,0x00,0xf3,0x0f,0x11,0x84,0x24,0xa4,0x01,0x00,0x00,0xf3,
        0x0f,0x11,0x84,0x24,0xa8,0x01,0x00,0x00,0xf3,0x0f,0x11,0x84,0x24,0xac,0x01,0x00,
        0x00,0xf3,0x0f,0x10,0x05,0xf0,0x94,0x4d,0x01,0x52,0x8d,0x8c,0x24,0xc8,0x02,0x00,
        0x00,0xf3,0x0f,0x11,0x8c,0x24,0xa0,0x01,0x00,0x00,0xf3,0x0f,0x11,0x84,0x24,0xb4,
        0x01,0x00,0x00,0xe8,0x98,0xa5,0xb8,0xff,0x8d,0x9c,0x24,0xc0,0x02,0x00,0x00,0x2b,
        0xd8,0x89,0x5c,0x24,0x24,0x8d,0x9c,0x24,0xc4,0x02,0x00,0x00,0x2b,0xd8,0x89,0x5c,
        0x24,0x20,0x8d,0x94,0x24,0xbc,0x02,0x00,0x00,0x8d,0x9c,0x24,0xb8,0x02,0x00,0x00,
        0x2b,0xd0,0x8d,0x48,0x08,0x2b,0xd8,0xc7,0x44,0x24,0x10,0x04,0x00,0x00,0x00,0xd9,
        0x41,0xf8,0x8b,0x44,0x24,0x24,0xd9,0x1c,0x0b,0x83,0xc1,0x10,0x83,0x6c,0x24,0x10,
        0x01,0xd9,0x41,0xec,0xd9,0x5c,0x0a,0xf0,0xd9,0x41,0xf0,0xd9,0x5c,0x08,0xf0,0x8b,
        0x44,0x24,0x20,0xd9,0x41,0xf4,0xd9,0x5c,0x08,0xf0,0x75,0xd3,0x8b,0x17,0x8b,0x42,
        0x68,0x8b,0xcf,0xff,0xd0,0x85,0xc0,0x0f,0x84,0x07,0x01,0x00,0x00,0x8b,0x17,0x8b,
        0x42,0x68,0x8b,0xcf,0xff,0xd0,0x85,0xc0,0x0f,0x84,0xf6,0x00,0x00,0x00,0x8b,0x08,
        0x85,0xc9,0x0f,0x8e,0xec,0x00,0x00,0x00,0x89,0x8c,0x24,0x00,0x03,0x00,0x00,0xf3,
        0x0f,0x10,0x40,0x04,0xf3,0x0f,0x11,0x84,0x24,0x04,0x03,0x00,0x00,0xf3,0x0f,0x10,
        0x40,0x08,0xf3,0x0f,0x11,0x84,0x24,0x08,0x03,0x00,0x00,0xc7,0x44,0x24,0x10,0x00,
        0x00,0x00,0x00,0x0f,0x86,0x91,0x00,0x00,0x00,0x8d,0x9c,0x24,0xfc,0x02,0x00,0x00,
        0x2b,0xd8,0x89,0x5c,0x24,0x18,0x8d,0x9c,0x24,0x00,0x03,0x00,0x00,0x2b,0xd8,0x89,
        0x5c,0x24,0x28,0x8d,0x8c,0x24,0x50,0x03,0x00,0x00,0x8d,0x9c,0x24,0xf8,0x02,0x00,
        0x00,0x89,0x4c,0x24,0x24,0x2b,0xd8,0x8d,0x50,0x50,0x8d,0x48,0x18,0x8b,0x44,0x24,
        0x24,0x89,0x5c,0x24,0x20,0xeb,0x0d,0xeb,0x07,0x8d,0xa4,0x24,0x00,0x00,0x00,0x00,
        0x8b,0x5c,0x24,0x20,0xd9,0x41,0xf8,0x83,0xc1,0x10,0xd9,0x5c,0x0b,0xf0,0x8b,0x5c,
        0x24,0x18,0xd9,0x41,0xec,0x83,0xc2,0x08,0xd9,0x5c,0x0b,0xf0,0x8b,0x5c,0x24,0x28,
        0xd9,0x41,0xf0,0x83,0xc0,0x08,0xd9,0x5c,0x0b,0xf0,0x8b,0x5a,0xf8,0x89,0x58,0xf8,
        0x8b,0x5a,0xfc,0x89,0x58,0xfc,0x8b,0x5c,0x24,0x10,0x83,0xc3,0x01,0x3b,0x9c,0x24,
        0x00,0x03,0x00,0x00,0x89,0x5c,0x24,0x10,0x72,0xb6,0x8b,0x55,0x10,0x83,0x7a,0x4c,
        0x00,0xc7,0x84,0x24,0x9c,0x03,0x00,0x00,0x00,0x00,0x00,0x00,0x74,0x16,0x8b,0x8e,
        0xd4,0x00,0x00,0x00,0x8b,0x01,0x8b,0x50,0x1c,0x6a,0x00,0xff,0xd2,0x89,0x84,0x24,
        0x9c,0x03,0x00,0x00,0x8b,0x4e,0x70,0x8b,0x01,0x8b,0x40,0x18,0x8d,0x94,0x24,0xc0,
        0x01,0x00,0x00,0x52,0xff,0xd0,
    };
    code.Put(0x12000,getter);code.Put(0x12100,setter);code.Put(0x14000+0x1820,consumer);
    code.Put(0x14000+0x44,std::array<unsigned char,3>{0x8b,0x7d,0x0c});
    code.Put(0x14000+0x1e2f,std::array<unsigned char,3>{0xc2,0x0c,0});
    Check(VerifyCode(code.Mem(),0x10100,0x14000),"native getter/setter/consumer/ABI accepted");
    code.bytes[0x12000-Fixture::Base+2]^=1;
    Check(!VerifyCode(code.Mem(),0x10100,0x14000),"wrong getter field rejected");code.bytes[0x12000-Fixture::Base+2]^=1;
    code.bytes[0x14000-Fixture::Base+0x1820+180]^=1;
    Check(!VerifyCode(code.Mem(),0x10100,0x14000),"changed color matrix consumer rejected");
    // Reproduce captured native checkpoint sequence: extra view stays at1 while
    // primary changes1->0->partial->1. Distinct eye geometry is deliberately
    // outside the12 bytes and must survive every temporary value application.
    Fixture f;const Rgb color{.2f,.5f,.9f};unsigned diverged=0;
    f.Put(Fixture::Main+0x90,11u);f.Put(Fixture::Eye+0x90,22u);
    f.Put(Fixture::Main+0x1650,std::array<unsigned,4>{1,2,3,4});
    f.Put(Fixture::Eye+0x1650,std::array<unsigned,4>{5,6,7,8});
    f.Put(Fixture::Eye+0x152c,0xdeadbeefu);f.Put(Fixture::Eye+0x153c,0xf00dcafeu);
    for(float fade:{1.f,0.f,0.f,.22456324f,.54956293f,.87481540f,1.f,1.5f}){
        f.pair.Reset();f.key.lease++;f.key.frame++;f.Put(Fixture::World+0x88,f.key.frame);
        const Rgb source{fade,fade,fade};f.Put(Fixture::Main+0x1530,source);
        if(!Same(ConsumeColor(color,source),ConsumeColor(color,f.Get<Rgb>(Fixture::Eye+0x1530))))++diverged;
        f.Left();Pair::Scope right(f.pair);Check(right.Begin(f.key,1),"right begin");
        Check(Same(f.Get<Rgb>(Fixture::Eye+0x1530),source),"extra eye consumes same native modulation");
        Check(Same(ConsumeColor(color,source),ConsumeColor(color,f.Get<Rgb>(Fixture::Eye+0x1530))),"old divergence corrected");
        Check(right.DrawReturned(f.key)&&right.Complete(f.key,true),"right draw/capture/restore complete");
        Check(f.pair.Ready(f.key),"publication admitted after restore");
        Check(Same(f.Get<Rgb>(Fixture::Eye+0x1530),Rgb{1,1,1}),"extra eye restored bitwise");
        Check(Same(f.Get<Rgb>(Fixture::Main+0x1530),source),"primary source bytes untouched");
        Check(f.Get<std::array<unsigned,4>>(Fixture::Main+0x1650)==std::array<unsigned,4>{1,2,3,4}&&
              f.Get<std::array<unsigned,4>>(Fixture::Eye+0x1650)==std::array<unsigned,4>{5,6,7,8},"unproven effect fields untouched");
        Check(f.Get<unsigned>(Fixture::Eye+0x152c)==0xdeadbeef&&f.Get<unsigned>(Fixture::Eye+0x153c)==0xf00dcafe,"exact12-byte write extent");
        Check(f.Get<unsigned>(Fixture::Main+0x90)==11&&f.Get<unsigned>(Fixture::Eye+0x90)==22,"distinct cameras untouched");
    }
    Check(diverged==6&&f.pair.counts.writes==6&&f.pair.counts.restores==6,"normal unity unchanged; finite above1 allowed");
    {Fixture x;Rgb source{-0.f,.5f,2.f};x.Put(Fixture::Main+0x1530,source);x.Left();
     Pair::Scope right(x.pair);Check(right.Begin(x.key,1),"nonuniform finite scale admitted");
     Check(Same(x.Get<Rgb>(Fixture::Eye+0x1530),source),"signedzero exact bytes preserved");
     Check(right.DrawReturned(x.key)&&right.Complete(x.key,true),"nonuniform scale restored");}
    {Fixture x;x.Put(Fixture::Main+0x1530,Rgb{0,0,0});x.Left();auto changed=x.key;changed.lease++;
     Pair::Scope right(x.pair);Check(!right.Begin(changed,1)&&x.writes==0,"stale lease rejected before write");}
    {Fixture x;x.Left();auto changed=x.key;changed.request+=4;
     Pair::Scope right(x.pair);Check(!right.Begin(changed,1)&&x.writes==0,"owner turnover rejects old pair");
     x.pair.Reset();x.Left();Pair::Scope fresh(x.pair);Check(fresh.Begin(x.key,1)&&fresh.DrawReturned(x.key)&&fresh.Complete(x.key,true),"new pair recovers without cached scale");}
    {Fixture x;Pair::Scope right(x.pair);Check(!right.Begin(x.key,1)&&x.writes==0,"right before left rejected");}
    {Fixture x;auto wrong=x.key;wrong.currentThread++;
     Pair::Scope left(x.pair);Check(!left.Begin(wrong,0)&&x.writes==0,"wrong graphics thread rejected");}
    {Fixture x;x.failRead=Fixture::Main+0x1530;Pair::Scope left(x.pair);Check(!left.Begin(x.key,0),"source read failure rejected");}
    for(float bad:{std::numeric_limits<float>::quiet_NaN(),std::numeric_limits<float>::infinity()}){
     Fixture x;x.Put(Fixture::Main+0x1530,Rgb{bad,1,1});Pair::Scope left(x.pair);Check(!left.Begin(x.key,0),"nonfinite source rejected");}
    {Fixture x;Pair::Scope left(x.pair);Check(left.Begin(x.key,0),"left mutation begin");x.Put(Fixture::Main+0x1530,Rgb{.5f,.5f,.5f});
     Check(!left.DrawReturned(x.key)&&!left.Complete(x.key,true)&&!x.pair.Ready(x.key),"source changed during left draw cancels pair");}
    {Fixture x;x.Left();x.Put(Fixture::Main+0x1530,Rgb{.5f,.5f,.5f});Pair::Scope right(x.pair);
     Check(!right.Begin(x.key,1)&&!x.pair.Ready(x.key)&&x.writes==0,"source changed between eyes cancels pair");}
    {Fixture x;x.Put(Fixture::Main+0x1530,Rgb{0,0,0});x.Left();x.failRead=Fixture::Eye+0x1530;Pair::Scope right(x.pair);
     Check(!right.Begin(x.key,1)&&x.writes==0,"destination read failure rejected");}
    {Fixture x;x.Put(Fixture::Main+0x1530,Rgb{0,0,0});x.Left();x.failWrite=true;Pair::Scope right(x.pair);
     Check(!right.Begin(x.key,1)&&x.writes==0&&!x.pair.Ready(x.key),"compare-write failure cancels without mutation");}
    {Fixture x;x.Put(Fixture::Main+0x1530,Rgb{0,0,0});x.Left();x.failAfterWrite=true;Pair::Scope right(x.pair);
     Check(!right.Begin(x.key,1)&&!x.pair.Ready(x.key)&&x.pair.counts.restoreFailures==1,"postwrite readback failure cannot publish");}
    {Fixture x;x.Put(Fixture::Main+0x1530,Rgb{0,0,0});x.Left();Pair::Scope right(x.pair);Check(right.Begin(x.key,1),"restore conflict begin");
     const Rgb newer{.7f,.8f,.9f};x.Put(Fixture::Eye+0x1530,newer);
     Check(!right.DrawReturned(x.key)&&!right.Complete(x.key,true),"restore conflict rejects publication");
     Check(Same(x.Get<Rgb>(Fixture::Eye+0x1530),newer)&&x.pair.counts.restoreFailures==1,"newer bytes never overwritten");}
    {Fixture x;x.Put(Fixture::Main+0x1530,Rgb{0,0,0});x.Left();Pair::Scope right(x.pair);Check(right.Begin(x.key,1),"owner loss begin");
     x.Put(Fixture::Eye+0x70,Fixture::Request+4);Check(!right.Complete(x.key,true),"owner loss cancels");
     Check(x.writes==1&&x.pair.counts.restoreFailures==1,"no restore over replacement owner");}
    {Fixture x;x.Put(Fixture::Main+0x1530,Rgb{0,0,0});x.Left();
     const auto early=[&]{Pair::Scope right(x.pair);Check(right.Begin(x.key,1),"early return begin");return;};early();
     Check(Same(x.Get<Rgb>(Fixture::Eye+0x1530),Rgb{1,1,1})&&!x.pair.Ready(x.key),"early return restores and cancels");}
    {Fixture x;x.Put(Fixture::Main+0x1530,Rgb{0,0,0});x.Left();
     try {Pair::Scope right(x.pair);Check(right.Begin(x.key,1),"exception begin");throw 7;}catch(int){}
     Check(Same(x.Get<Rgb>(Fixture::Eye+0x1530),Rgb{1,1,1})&&!x.pair.Ready(x.key),"C++ exception restores and cancels");}
    {Fixture x;x.Put(Fixture::Main+0x1530,Rgb{0,0,0});x.Left();Pair::Scope right(x.pair);Check(right.Begin(x.key,1),"abnormal exit begin");right.Abort();right.Abort();
     Check(Same(x.Get<Rgb>(Fixture::Eye+0x1530),Rgb{1,1,1})&&x.pair.counts.restores==1&&!x.pair.Ready(x.key),"SEH cleanup callback idempotently restores and cancels");}
#ifdef _WIN32
    {Fixture x;x.Put(Fixture::Main+0x1530,Rgb{0,0,0});x.Left();Pair::Scope right(x.pair);Check(right.Begin(x.key,1),"native SEH begin");
     Check(CatchAbnormalOriginal(&right),"native structured exception propagated through finally");
     Check(Same(x.Get<Rgb>(Fixture::Eye+0x1530),Rgb{1,1,1})&&x.pair.counts.restores==1&&!x.pair.Ready(x.key),"actual SEH restores before unwinding completes");}
#endif
    std::cout<<"PASS verified consumer / observed fade sequence / unity / exact restoration / ownership / readbacks / mutation / early-return and exception cancellation\n";
    return 0;
}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}}
