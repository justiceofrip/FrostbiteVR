#include "Test.h"
#include "../src/platform/windows/NativeResolutionControllerPolicy.h"
#include <cstring>
using namespace fvr::bc2;
namespace policy=fvr::bc2::resolutionController;
namespace {
void Put(std::vector<std::byte>& b,std::size_t at,std::uint32_t value,unsigned size=4){for(unsigned i=0;i<size;++i)b.at(at+i)=std::byte(value>>(8*i));}
std::uint32_t Get(const std::vector<std::byte>& b,std::size_t at){std::uint32_t value=0;std::memcpy(&value,b.data()+at,4);return value;}
std::vector<std::byte> Image(){
    // Two tiny PE32 sections, one relocated executable pointer. The helper's
    // mutable state is not part of executable identity, its loaded code is.
    std::vector<std::byte> b(0x600);Put(b,0,0x5a4d,2);Put(b,0x3c,0x80);Put(b,0x80,0x4550);
    Put(b,0x84,0x14c,2);Put(b,0x86,2,2);Put(b,0x88,1234);Put(b,0x94,224,2);
    Put(b,0x98,0x10b,2);Put(b,0x98+28,0x10000000);Put(b,0x98+56,0x3000);Put(b,0x98+60,0x200);Put(b,0x98+92,16);
    Put(b,0x98+96+5*8,0x2000);Put(b,0x98+100+5*8,12);
    Put(b,0x178+8,0x200);Put(b,0x178+12,0x1000);Put(b,0x178+16,0x200);Put(b,0x178+20,0x200);Put(b,0x178+36,0x60000020);
    Put(b,0x1a0+8,0x200);Put(b,0x1a0+12,0x2000);Put(b,0x1a0+16,0x200);Put(b,0x1a0+20,0x400);Put(b,0x1a0+36,0x42000040);
    std::fill(b.begin()+0x200,b.begin()+0x400,std::byte{0x90});Put(b,0x210,0x10001234);
    Put(b,0x400,0x1000);Put(b,0x404,12);Put(b,0x408,0x3010,2);return b;
}
int Identity(){
    std::uint64_t value=99;CHECK(policy::Unsigned(L"18446744073709551615",value)&&value==UINT64_MAX);
    for(const auto bad:{L"",L"-1",L"+1",L"1 ",L"12tail",L"18446744073709551616",L"000000000000000000001"})CHECK(!policy::Unsigned(bad,value));
    std::array<std::uint32_t,4> token{};CHECK(policy::Token(L"0123456789abcdef00112233fedcba98",token));
    CHECK(token[0]==0x01234567&&token[1]==0x89abcdef&&token[2]==0x00112233&&token[3]==0xfedcba98);
    for(const auto bad:{L"00000000000000000000000000000000",L"0123456789ABCDEF00112233fedcba98",L"1234",L"z123456789abcdef00112233fedcba98"})CHECK(!policy::Token(bad,token));
    ResolutionRequest input{};input.width=2784;input.height=3024;input.lease={123,456,789,1011,token};CHECK(ValidResolutionLease(input.lease));
    auto output=input;CHECK(policy::MatchingReply(input,output));
    for(unsigned n=0;n<9;++n){output=input;switch(n){case 0:++output.magic;break;case 1:++output.version;break;case 2:++output.width;break;case 3:++output.height;break;
        case 4:++output.lease.gamePid;break;case 5:++output.lease.gameCreation;break;case 6:++output.lease.ownerPid;break;case 7:++output.lease.ownerCreation;break;case 8:++output.lease.token[3];break;}CHECK(!policy::MatchingReply(input,output));}
    // Release uses the saved lease even after the supervisor has exited; its
    // dimensions are optional and the native receipt echoes the original size.
    output=input;input.width=0;input.height=0;CHECK(policy::MatchingReply(input,output,false));
    ++output.lease.ownerCreation;CHECK(!policy::MatchingReply(input,output,false));return 0;
}
int Receipts(){
    ResolutionRequest configured{};configured.passed=1;configured.applied=1;configured.hookRestored=1;configured.callbacksDrained=1;configured.guardInstalled=1;configured.guardActive=1;
    CHECK(policy::SetupComplete(configured));CHECK(!policy::ReleaseComplete(configured));
    for(unsigned n=0;n<12;++n){auto r=configured;switch(n){case 0:r.passed=0;break;case 1:r.error=12;break;case 2:r.applied=0;break;case 3:r.hookRestored=0;break;case 4:r.callbacksDrained=0;break;
        case 5:r.guardActive=0;break;case 6:r.guardInstalled=0;break;case 7:r.guardDisabled=1;break;case 8:r.guardDrained=1;break;case 9:r.guardOwnerLost=1;break;case 10:r.guardNeverInstalled=1;break;case 11:r.guardCleanupPending=1;break;}CHECK(!policy::SetupComplete(r));}
    auto released=configured;released.guardActive=0;released.guardDisabled=1;released.guardDrained=1;released.guardReleased=1;CHECK(policy::ReleaseComplete(released));
    released.guardOwnerLost=1;released.guardReleaseReason=2;CHECK(policy::ReleaseComplete(released));CHECK(!policy::SetupComplete(released));
    // Only explicit absence can report never-installed; an installed release
    // and an absent helper are mutually exclusive evidence states.
    ResolutionRequest absent{};absent.passed=1;absent.guardNeverInstalled=1;absent.guardDisabled=1;absent.guardDrained=1;CHECK(policy::ReleaseComplete(absent));
    for(unsigned n=0;n<10;++n){auto r=released;switch(n){case 0:r.error=14;break;case 1:r.guardActive=1;break;case 2:r.guardCleanupPending=1;break;case 3:r.guardDisabled=0;break;
        case 4:r.guardDrained=0;break;case 5:r.guardReleased=0;break;case 6:r.guardNeverInstalled=1;break;case 7:r.guardInstalled=0;break;case 8:r.hookRestored=0;break;case 9:r.callbacksDrained=0;break;}CHECK(!policy::ReleaseComplete(r));}
    return 0;
}
int Images(){
    const auto file=Image();const auto normal=policy::Plan(file,0x10000000),relocated=policy::Plan(file,0x11000000);
    CHECK(normal&&relocated);CHECK(normal->imageSize==0x3000&&normal->timestamp==1234&&normal->executable.size()==1);
    CHECK(normal->headers.size()==0x200&&normal->ContainsCode(0x1000)&&normal->ContainsCode(0x11ff));CHECK(!normal->ContainsCode(0x1200)&&!normal->ContainsCode(0x2000));
    CHECK(Get(normal->headers,0x98+28)==0x10000000);CHECK(Get(relocated->headers,0x98+28)==0x11000000);
    // Windows rewrites exactly this PE32 header word at relocation. The
    // verifier compares the normalized full header, never skips that field.
    std::vector<std::byte> mappedHeader(file.begin(),file.begin()+0x200);Put(mappedHeader,0x98+28,0x11000000);
    CHECK(mappedHeader==relocated->headers);CHECK(mappedHeader!=normal->headers);
    Put(mappedHeader,0x98+28,0x12000000);CHECK(mappedHeader!=relocated->headers);
    Put(mappedHeader,0x98+28,0x11000000);Put(mappedHeader,0x88,4321);CHECK(mappedHeader!=relocated->headers);
    CHECK(Get(normal->executable[0].bytes,0x10)==0x10001234);CHECK(Get(relocated->executable[0].bytes,0x10)==0x11001234);
    auto changed=file;changed[0x230]=std::byte{0xcc};auto other=policy::Plan(changed,0x11000000);CHECK(other&&other->executable[0].bytes!=relocated->executable[0].bytes);
    // Non-code data is mutable, but timestamp/header changes still mismatch.
    changed=file;changed[0x450]=std::byte{0xab};other=policy::Plan(changed,0x11000000);CHECK(other&&other->executable[0].bytes==relocated->executable[0].bytes);
    Put(changed,0x88,4321);other=policy::Plan(changed,0x11000000);CHECK(other&&other->headers!=relocated->headers);
    for(unsigned n=0;n<12;++n){auto b=file;switch(n){case 0:Put(b,0,0,2);break;case 1:Put(b,0x84,0x8664,2);break;case 2:Put(b,0x94,1,2);break;case 3:Put(b,0x98+56,0x1000);break;
        case 4:Put(b,0x178+36,0xe0000020);break;case 5:Put(b,0x1a0+12,0x1000);break;case 6:Put(b,0x178+20,0x600);break;case 7:Put(b,0x404,7);break;
        case 8:Put(b,0x408,0xa010,2);break;case 9:Put(b,0x408,0x31ff,2);break;case 10:Put(b,0x98+96+5*8,0);break;case 11:b.resize(100);break;}CHECK(!policy::Plan(b,0x11000000));}
    changed=file;Put(changed,0x98+96+5*8,0);Put(changed,0x98+100+5*8,0);CHECK(policy::Plan(changed,0x10000000));CHECK(!policy::Plan(changed,0x11000000));
    CHECK(!policy::Plan(file,0));CHECK(!policy::Plan(file,0xfffff000));return 0;
}
}
int main(){if(const auto n=Identity())return n;if(const auto n=Receipts())return n;return Images();}
