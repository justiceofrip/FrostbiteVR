#include "Bc2PlayerPair.h"
namespace fvr::bc2 {
std::optional<std::uint32_t> MatchServerPlayer(const PlayerPairMemory& m,
 std::uint32_t context,std::uint32_t expectedVtable,std::uint32_t client,std::uint32_t server) noexcept {
 const auto read=[&](std::uint32_t base,std::uint32_t offset)->std::optional<std::uint32_t>{
  const auto at=std::uint64_t(base)+offset;
  if(!m.read||base<0x10000||at+4>UINT32_MAX)return {};
  std::uint32_t out=0;if(!m.read(m.context,std::uint32_t(at),&out,4))return {};return out;
 };
 const auto manager=read(context,8),id=read(client,0x154),serverId=read(server,0x154);
 if(!manager||!id||!serverId||*id!=*serverId)return {};
 const auto vt=read(*manager,0),capacity=read(*manager,4),array=read(*manager,0x6c);
 if(!vt||*vt!=expectedVtable||!capacity||!*capacity||*capacity>256||*id>=*capacity||!array)return {};
 if(read(*array,*id*4)!=std::optional(server))return {};
 // Recheck the complete relationship; zero is a valid player ID, not a failed read.
 if(read(context,8)!=manager||read(*manager,0x6c)!=array||read(client,0x154)!=id||
    read(server,0x154)!=id||read(*array,*id*4)!=std::optional(server))return {};
 return id;
}
std::optional<std::uint32_t> FindServerPlayer(const PlayerPairMemory& m,
 std::uint32_t context,std::uint32_t expectedVtable,std::uint32_t client) noexcept {
 const auto read=[&](std::uint32_t base,std::uint32_t offset)->std::optional<std::uint32_t>{
  const auto at=std::uint64_t(base)+offset;std::uint32_t out=0;
  if(!m.read||base<0x10000||at+4>UINT32_MAX||!m.read(m.context,std::uint32_t(at),&out,4))return {};
  return out;
 };
 const auto manager=read(context,8),id=read(client,0x154);
 if(!manager||!id||*id>=256)return {};
 const auto array=read(*manager,0x6c);if(!array)return {};
 const auto server=read(*array,*id*4);if(!server)return {};
 if(!MatchServerPlayer(m,context,expectedVtable,client,*server))return {};
 return server;
}

}
