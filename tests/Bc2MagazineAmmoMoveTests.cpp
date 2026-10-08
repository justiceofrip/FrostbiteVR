#include "Test.h"
#include "Bc2MagazineAmmoMove.h"
#include <algorithm>
#include <limits>
using namespace fvr::bc2;
namespace {
void Put(MagazineAmmoMoveBytes& raw,unsigned offset,int value){std::memcpy(raw.data()+offset,&value,4);}
MagazineAmmoMoveBytes State(int loaded,int reserve){
    MagazineAmmoMoveBytes raw;raw.fill(std::byte{0xa5});Put(raw,0x7c,loaded);Put(raw,0x80,reserve);return raw;
}
int SharedCapacityAndResourceConservation(){
    for(int capacity:{1,5,6,8,15,20,30,32,50,100,200,1000000})
        for(int rounds:{1,std::max(1,capacity/2),capacity})for(int reserve:{0,1,183,1000000}){
            const auto remove=PlanMagazineAmmoMove(MagazineAmmoMoveKind::Remove,rounds,reserve,capacity,rounds);
            CHECK(remove&&remove->delta==-rounds&&remove->expectedLoaded==0);
            auto before=State(rounds,reserve),empty=before;Put(empty,0x7c,0);
            CHECK(MagazineAmmoMoveMatched(*remove,before,empty));
            const auto returned=PlanMagazineAmmoMove(MagazineAmmoMoveKind::Return,0,reserve,capacity,rounds);
            CHECK(returned&&returned->delta==rounds&&returned->expectedLoaded==rounds);
            CHECK(MagazineAmmoMoveMatched(*returned,empty,before));
            // Removed rounds belong to the retained magazine, never to reserve.
            CHECK(remove->expectedLoaded+remove->reserve+remove->rounds==rounds+reserve);
            CHECK(returned->expectedLoaded+returned->reserve==rounds+reserve);
        }
    return 0;
}
int RejectUnsafeNativeInputs(){
    using K=MagazineAmmoMoveKind;
    for(int bad:{-1,0,1000001,std::numeric_limits<int>::max(),std::numeric_limits<int>::min()}){
        CHECK(!PlanMagazineAmmoMove(K::Remove,5,10,bad,5));
        CHECK(!PlanMagazineAmmoMove(K::Remove,5,10,30,bad));
    }
    for(int bad:{-1,1000001,std::numeric_limits<int>::max(),std::numeric_limits<int>::min()}){
        CHECK(!PlanMagazineAmmoMove(K::Remove,5,bad,30,5));
        CHECK(!PlanMagazineAmmoMove(K::Remove,bad,10,30,5));
    }
    CHECK(!PlanMagazineAmmoMove(K::Remove,0,10,30,5));
    CHECK(!PlanMagazineAmmoMove(K::Remove,5,10,30,6));
    CHECK(!PlanMagazineAmmoMove(K::Remove,5,10,30,4));
    CHECK(!PlanMagazineAmmoMove(K::Return,1,10,30,30));
    CHECK(!PlanMagazineAmmoMove(K::Return,0,10,30,31));
    CHECK(!PlanMagazineAmmoMove(static_cast<K>(2),0,10,30,30));
    return 0;
}
int RejectPartialOrUnrelatedMutation(){
    auto plan=*PlanMagazineAmmoMove(MagazineAmmoMoveKind::Remove,30,183,30,30);
    auto before=State(30,183),after=before;Put(after,0x7c,0);
    for(unsigned at=0;at<before.size();++at){
        if(at>=0x7c&&at<0x80)continue;
        auto changed=after;changed[at]^=std::byte{1};
        CHECK(!MagazineAmmoMoveMatched(plan,before,changed));
    }
    auto changed=after;Put(changed,0x7c,1);CHECK(!MagazineAmmoMoveMatched(plan,before,changed));
    CHECK(!MagazineAmmoMoveMatched(plan,before,before));
    CHECK(!MagazineAmmoMoveMatched(plan,State(29,183),after));
    CHECK(!MagazineAmmoMoveMatched(plan,State(30,182),after));
    // A caller cannot forge a new delta or postcondition onto an old plan.
    auto corrupt=plan;corrupt.delta=-29;CHECK(!MagazineAmmoMoveMatched(corrupt,before,after));
    corrupt=plan;corrupt.expectedLoaded=1;CHECK(!MagazineAmmoMoveMatched(corrupt,before,changed));
    return 0;
}
}
int main(){
    if(SharedCapacityAndResourceConservation()||RejectUnsafeNativeInputs()||RejectPartialOrUnrelatedMutation())return 1;
    std::puts("MagazineAmmoMove: bounded native plans and exact local postconditions passed; no gameplay authority");
    return 0;
}
