#include "Test.h"
#include "../src/games/bc2/NativeWorkPool.h"
using namespace fvr::bc2;
int main(){
    WorkPoolHeader header{0x1000,0x1000,0x1500};const auto original=header;WorkPoolLease lease;
    header.end+=40;CHECK(!lease.Expand(header,0x1000,1280,0x2000,5120));CHECK(header.end==0x1028);header=original;
    CHECK(!lease.Expand(header,0x1000,1280,0xfffffff0,5120));CHECK(header==original);
    CHECK(lease.Expand(header,0x1000,1280,0x2000,5120));CHECK(header.capacity==0x3400);
    header.end+=40;const auto busy=header;CHECK(!lease.RestoreEmpty(header));CHECK(header==busy);
    header.end=header.begin;header.capacity+=40;const auto changed=header;CHECK(!lease.RestoreEmpty(header));CHECK(header==changed);
    header.capacity-=40;CHECK(lease.RestoreEmpty(header));CHECK(header==original&&!lease.Active());CHECK(!lease.RestoreEmpty(header));
    return 0;
}
