#include "Bc2ReserveFinalValidation.h"
#include <cstdlib>
#include <thread>
#include <atomic>
using namespace fvr::bc2;
#define CHECK(x) do {if(!(x))std::abort();}while(false)
int main(){
 CHECK(ValidateReserveFinal(true,true,1,10,10)==ReserveFinalVerdict::Available);
 std::atomic<unsigned> revision{10};std::thread callback([&]{revision.fetch_add(2);});callback.join();
 CHECK(revision.load()!=10); // Old classifier hard-rejected the callback.
 CHECK(ValidateReserveFinal(true,true,1,10,revision.load())==ReserveFinalVerdict::CohortGap);
 CHECK(ValidateReserveFinal(true,true,2,10,10)==ReserveFinalVerdict::CohortGap);
 CHECK(ValidateReserveFinal(false,true,1,10,12)==ReserveFinalVerdict::Rejected);
 CHECK(ValidateReserveFinal(true,false,1,10,12)==ReserveFinalVerdict::Rejected);
 CHECK(ValidateReserveFinal(false,false,2,10,12)==ReserveFinalVerdict::Rejected);
 CHECK(ValidateReserveFinal(true,true,1,12,12)==ReserveFinalVerdict::Available);
}
