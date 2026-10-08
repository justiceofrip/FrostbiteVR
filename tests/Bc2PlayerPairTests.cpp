#include "Test.h"
#include "Bc2PlayerPair.h"
#include <array>
#include <cstring>
using namespace fvr;
struct Memory {
 std::array<std::byte,0x6000> bytes{};unsigned fail=0,change=0,reads=0;
 void Put(unsigned at,unsigned v){std::memcpy(bytes.data()+at-0x10000,&v,4);}
 static bool Read(void* p,unsigned at,void* out,std::size_t n){auto& m=*static_cast<Memory*>(p);
  if(at==m.fail||at<0x10000||std::uint64_t(at)+n>0x16000)return false;
  if(at==m.change&&++m.reads==2)m.Put(at,0);
  std::memcpy(out,m.bytes.data()+at-0x10000,n);return true;
 }
};
int main(){Memory m;m.Put(0x10008,0x11000);m.Put(0x11000,0x1405000);m.Put(0x11004,64);m.Put(0x1106c,0x12000);m.Put(0x12000,0x14000);
 // A real ID zero must pass; an unreadable zero-valued field must not.
 bc2::PlayerPairMemory mem{&m,Memory::Read};const auto good=m.bytes;
 const auto match=[&]{return bc2::MatchServerPlayer(mem,0x10000,0x1405000,0x13000,0x14000);};
 const auto find=[&]{return bc2::FindServerPlayer(mem,0x10000,0x1405000,0x13000);};
 CHECK(find()==std::optional<unsigned>(0x14000));
 CHECK(match()==std::optional<unsigned>(0));CHECK(m.bytes==good);
 for(auto e:{std::pair{0x11000u,0x1405010u},std::pair{0x11004u,0u},std::pair{0x11004u,257u},std::pair{0x1106cu,0xfffffff0u},std::pair{0x12000u,0x15000u},std::pair{0x13154u,1u},std::pair{0x14154u,1u}}){m.bytes=good;m.Put(e.first,e.second);CHECK(!match());}
 for(unsigned at:{0x10008u,0x11000u,0x11004u,0x1106cu,0x12000u,0x13154u,0x14154u}){m.bytes=good;m.fail=at;CHECK(!match());CHECK(!find());}m.fail=0;
 m.bytes=good;m.Put(0x13154,63);m.Put(0x14154,63);m.Put(0x12000+63*4,0x14000);CHECK(match()==std::optional<unsigned>(63));CHECK(find()==std::optional<unsigned>(0x14000));m.Put(0x13154,64);m.Put(0x14154,64);CHECK(!match());
 m.bytes=good;m.change=0x12000;CHECK(!match());m.bytes=good;m.reads=0;CHECK(!find());CHECK(!bc2::MatchServerPlayer(mem,0xfffffffc,0x1405000,0x13000,0x14000));return 0;
}
