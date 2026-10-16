#include "Test.h"
#include "Bc2PumpReceiverGapProbe.h"
#include <iostream>
using namespace fvr::bc2;
int main(){
    const PumpReceiverGapWitness w{4,9,7,31,1000000000,1100000000,7,24,8,.04f};
    PumpReceiverGapProbe p;CHECK(p.Begin(w,1020000000));CHECK(!p.Begin(w,1020000001));
    CHECK(p.Withhold(1110000000));CHECK(p.Withhold(4020000000));CHECK(p.Withhold(6119999999));
    CHECK(p.Space(4)==4&&!p.Completed());CHECK(!p.Withhold(6120000000));
    CHECK(p.Space(4)==5&&p.ReturnedNs()-w.deadlineNs>=5000000000ll);
    CHECK(p.Published(1000,5,6120000001,true)&&p.Completed());
    for(unsigned n=0;n<7;++n){auto bad=w;if(n==0)bad.deadlineNs=1020000000;if(n==1)bad.observedNs=1030000000;
        if(n==2)bad.deadlineNs+=1;if(n==3)bad.travel=.029f;if(n==4)bad.space=UINT64_MAX;if(n==5)bad.loaded=8;if(n==6)bad.cycle=0;
        PumpReceiverGapProbe denied;CHECK(!denied.Begin(bad,1020000000)&&!denied.Began());}
    for(unsigned n=0;n<5;++n){PumpReceiverGapProbe denied;CHECK(denied.Begin(w,1020000000));
        if(n==0){CHECK(!denied.Published(32,4,1040000000,true));}
        else {CHECK(!denied.Withhold(6120000000));CHECK(!denied.Published(n==1?31:32,n==2?4:5,n==3?6119999999:6120000001,n!=4));}
        CHECK(denied.Failed()&&!denied.Completed());}
    PumpReceiverGapProbe reversed;CHECK(reversed.Begin(w,1020000000));CHECK(reversed.Withhold(1020000001));CHECK(reversed.Withhold(1020000000)&&reversed.Failed());
    std::cout<<"Receiver gap timing, original expiry, new space, neutral and fail-closed cases PASS\n";
}
