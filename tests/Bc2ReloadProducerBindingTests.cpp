#include "Bc2ReloadProducerBinding.h"
#include "Test.h"
#include <vector>
#include <cstring>
#include <thread>
#include <iostream>
#include <sstream>
using namespace fvr::bc2;
namespace {
constexpr std::int64_t Now=1000000000ll;
struct Fixture {
    Bc2ReloadProducerBinding binding;ReloadProducerView key{1,2,3,4,0};
    ReloadPackedSource source{{10,11,12,13,14},20,0xa7f219a1426216abull,21,0,Now,Now+100000000,6,2,4,true,true,false,false};
    std::vector<std::byte> input,output;
    Fixture(){binding.Enable(true);input.resize(6*64);output.resize(6*48);
        for(unsigned b=0;b<6;++b)for(unsigned row=0;row<4;++row)for(unsigned col=0;col<4;++col){
            const float value=col==3?-9999.f:float(b*20+row*4+col+.125);
            std::memcpy(input.data()+b*64+row*16+col*4,&value,4);
            if(col<3)std::memcpy(output.data()+b*48+col*16+row*4,&value,4);
        }}
    void Pair(std::uint64_t serial=100){binding.ObservePacked(serial,true,source,0x10000,Now+1,input,output);binding.ObservePacked(serial,false,source,0x20000,Now+2,input,output);}
    void Publish(){Bc2ReloadProducerBinding::Scope scope(binding,key,true,Now);Pair();scope.Complete(true,Now+3);}
};
int ImmutablePackingAndEye(){
    Fixture f;f.Publish();auto result=f.binding.Read(f.key,f.source.owner,Now+4);CHECK(result);
    CHECK(result->evidence.exactRequestAssociation&&!result->evidence.selectedMeshIdentityVerified);
    CHECK(result->evidence.packedShellValid&&result->evidence.packedOpticValid);
    CHECK(!std::memcmp(result->evidence.packedShell.data(),f.output.data()+2*48,48));
    CHECK(!std::memcmp(result->evidence.packedOptic.data(),f.output.data()+4*48,48));
    std::fill(f.input.begin(),f.input.end(),std::byte{0});std::fill(f.output.begin(),f.output.end(),std::byte{0});
    CHECK(f.binding.Read(f.key,f.source.owner,Now+5)->evidence.packedShell==result->evidence.packedShell);
    auto wrong=f.key;wrong.eye=1;CHECK(!f.binding.Read(wrong,f.source.owner,Now+5));
    wrong=f.key;++wrong.view;CHECK(!f.binding.Read(wrong,f.source.owner,Now+5));
    wrong=f.key;++wrong.nativeFrame;CHECK(!f.binding.Read(wrong,f.source.owner,Now+5));
    wrong=f.key;++wrong.request;CHECK(!f.binding.Read(wrong,f.source.owner,Now+5));return 0;
}
int PairAndSourceGuards(){
    for(unsigned n=0;n<9;++n){Fixture f;{
        Bc2ReloadProducerBinding::Scope scope(f.binding,f.key,true,Now);
        if(n==0){f.Pair();} // Aborted native scope.
        if(n==1){f.binding.ObservePacked(1,false,f.source,1,Now+1,f.input,f.output);scope.Complete(true,Now+2);}
        if(n==2){f.binding.ObservePacked(1,true,f.source,1,Now+1,f.input,f.output);scope.Complete(true,Now+2);}
        if(n==3){f.Pair();f.Pair(200);scope.Complete(true,Now+3);}
        if(n==4){f.output[20]=std::byte{0xff};f.Pair();scope.Complete(true,Now+3);}
        if(n==5){f.source.rigFingerprint=3;f.Pair();scope.Complete(true,Now+3);}
        if(n==6){f.source.selectedMeshIdentityVerified=true;f.Pair();scope.Complete(true,Now+3);}
        if(n==7){f.Pair();scope.Complete(false,Now+3);}
        if(n==8){f.binding.ObservePacked(1,true,f.source,1,Now+1,f.input,f.output);++f.source.inputGeneration;f.binding.ObservePacked(1,false,f.source,2,Now+2,f.input,f.output);scope.Complete(true,Now+3);}
    }CHECK(!f.binding.Read(f.key,f.source.owner,Now+4));}return 0;
}
int NoAsyncOrNestedGuess(){
    Fixture f;f.Pair();CHECK(f.binding.Statistics().outsideScope==2);
    {Bc2ReloadProducerBinding::Scope parent(f.binding,f.key,true,Now);
        std::thread worker([&]{f.Pair();});worker.join();
        {Bc2ReloadProducerBinding::Scope rejected(f.binding,{1,2,9,4,1},false,Now);f.Pair();rejected.Complete(true,Now+3);}
        f.Pair();parent.Complete(true,Now+3);}
    CHECK(f.binding.Read(f.key,f.source.owner,Now+4));CHECK(f.binding.Statistics().outsideScope==6);return 0;
}
int AmbiguityFreshnessAndOwner(){
    Fixture f;f.source.selectedMeshes1p=88;f.source.selectedMeshIdentityVerified=true;f.Publish();
    CHECK(f.binding.Read(f.key,f.source.owner,Now+4)->evidence.selectedMeshIdentityVerified);
    auto owner=f.source.owner;++owner.weapon;CHECK(!f.binding.Read(f.key,owner,Now+4));
    CHECK(!f.binding.Read(f.key,f.source.owner,f.source.deadlineNs));
    f.Publish();CHECK(!f.binding.Read(f.key,f.source.owner,Now+5));CHECK(f.binding.Statistics().ambiguous==1);return 0;
}
int DefaultOffAndReport(){
    Fixture f;f.binding.Enable(false);f.Publish();CHECK(!f.binding.Read(f.key,f.source.owner,Now+4));
    f.binding.Enable(true);f.Publish();CHECK(f.binding.Read(f.key,f.source.owner,Now+4));
    std::ostringstream report;f.binding.Report(report);CHECK(report.str().find("\"pack_destinations\":[65536,131072]")!=std::string::npos);
    CHECK(report.str().find("\"selected_mesh_verified\":false")!=std::string::npos);
    CHECK(report.str().find("\"snapshot_complete\":true")!=std::string::npos);
    return 0;
}
int ScopeEndAndPairTimes(){
    Fixture f;{Bc2ReloadProducerBinding::Scope scope(f.binding,f.key,true,Now);
        f.Pair();scope.Complete(true,Now+1);}CHECK(!f.binding.Read(f.key,f.source.owner,Now+4));
    Fixture late;late.source.observedNs=Now+1;{Bc2ReloadProducerBinding::Scope scope(late.binding,late.key,true,Now);
        late.Pair();scope.Complete(true,Now+3);}CHECK(late.binding.Read(late.key,late.source.owner,Now+4));return 0;
}
}
int main(){if(ImmutablePackingAndEye()||PairAndSourceGuards()||NoAsyncOrNestedGuess()||AmbiguityFreshnessAndOwner()||ScopeEndAndPairTimes()||DefaultOffAndReport())return 1;std::cout<<"producer binding:6 cases passed\n";}
