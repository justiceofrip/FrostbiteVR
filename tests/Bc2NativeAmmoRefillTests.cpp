#include "Test.h"
#include "Bc2NativeAmmoRefill.h"
#include <climits>
using namespace fvr::bc2;
int main(){
    for(int magazines:{0,2,6,8,100})for(float multiplier:{.5f,1.f,2.f,3.f,8.f})
        CHECK(NativeAmmoRefillFiniteReserve(magazines,multiplier));
    CHECK(!NativeAmmoRefillFiniteReserve(-1,1));
    CHECK(!NativeAmmoRefillFiniteReserve(1,.5f));
    CHECK(!NativeAmmoRefillFiniteReserve(1000000,2));
    for(float multiplier:{0.f,-1.f,std::numeric_limits<float>::infinity(),std::numeric_limits<float>::quiet_NaN()})
        CHECK(!NativeAmmoRefillFiniteReserve(6,multiplier));
    for(int capacity:{1,5,8,15,30,32,50,100,200,1000000})
    for(int loaded:{0,capacity/2,capacity-1})for(int reserve:{1,7,183,1000000})for(int type:{0,1}){
        const auto p=PlanNativeAmmoRefill(loaded,reserve,capacity,type);CHECK(p);
        CHECK(p->units>0&&p->expectedLoaded<=capacity&&p->expectedReserve>=0);
        CHECK(p->expectedLoaded+p->expectedReserve==loaded+reserve);
        CHECK(type!=0||p->units==1);
        MagazineAmmoMoveBytes before{};before.fill(std::byte{0xa5});auto after=before;
        std::memcpy(before.data()+0x7c,&loaded,4);std::memcpy(before.data()+0x80,&reserve,4);after=before;
        std::memcpy(after.data()+0x7c,&p->expectedLoaded,4);std::memcpy(after.data()+0x80,&p->expectedReserve,4);
        CHECK(NativeAmmoRefillMatched(*p,before,after));
        for(unsigned n=0;n<after.size();++n){auto changed=after;changed[n]^=std::byte{1};CHECK(!NativeAmmoRefillMatched(*p,before,changed));}
        auto wrong=*p;++wrong.units;CHECK(!NativeAmmoRefillMatched(wrong,before,after));
    }
    for(auto v:std::array<std::array<int,4>,11>{{{8,10,8,0},{30,10,30,1},{0,0,30,1},{-1,10,30,1},
        {0,-1,30,1},{0,10,0,1},{0,10,-1,1},{0,10,INT_MAX,1},{0,INT_MAX,30,1},{0,10,30,2},{0,10,30,-1}}})
        CHECK(!PlanNativeAmmoRefill(v[0],v[1],v[2],v[3]));
    std::puts("NativeAmmoRefill: finite family plan, conservation and exact-byte postconditions passed");return 0;
}
