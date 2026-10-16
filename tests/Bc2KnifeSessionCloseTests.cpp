#include "Test.h"
#include "Bc2KnifeSessionClose.h"
#include "Bc2KnifeSessionCloseProof.h"
#include <array>
#include <cstring>
#include <limits>
#include <cstdio>
#include <initializer_list>
using namespace fvr::bc2::private_knife;

namespace {
struct CloseState {unsigned currentState=0,previousState=0,nextState=0,flagsA8=0;float phaseTimer=0;int loaded=0,reserve=0;};
struct CloseItem {struct {std::array<CloseState,2> branches;} client;CloseState server;};
struct Snapshot {struct {std::array<unsigned,3> current{},previous{};} selected;CloseItem knife,original;};
Snapshot CloseFacts(){
    Snapshot s;s.selected.current={7,7,7};s.selected.previous={1,1,1};
    for(auto* v:{&s.knife.client.branches[0],&s.knife.client.branches[1],&s.knife.server}){
        v->currentState=v->nextState=2;v->loaded=v->reserve=-1;}
    for(auto* v:{&s.original.client.branches[0],&s.original.client.branches[1],&s.original.server}){
        v->currentState=v->nextState=2;v->loaded=30;v->reserve=210;}
    return s;
}
bool OriginalConverged(const Snapshot& s){
    for(unsigned slot:s.selected.current)if(slot!=1)return false;
    for(const auto* v:{&s.original.client.branches[0],&s.original.client.branches[1],&s.original.server})
        if(v->currentState!=2||v->nextState!=2)return false;
    return true;
}
int PartialSelection(){
    constexpr std::int64_t start=518794758961900ll; // retained408 original close source clock
    // Actual native setter notify6b1770 calls6e1eb0(false) on the old item.
    // The verified idle path sets its current/next to1. No ammo/selector write
    // is performed by this policy; emulate only the observed native boundary.
    {auto s=CloseFacts();KnifeSessionClose g;g.Begin(start,true,false,start);CHECK(g.Admit(start+1,true));
        CHECK(g.Choose(1,start+2,KnifeCloseConsumerReady(s,1)));CHECK(g.Mask()==2);
        s.selected.current[1]=1;s.selected.previous[1]=7;
        s.knife.server.previousState=2;s.knife.server.currentState=s.knife.server.nextState=1;
        CHECK(KnifeCloseConsumerReady(s,0)); // old all-knife idle condition fails here
        CHECK(g.Choose(0,start+3,KnifeCloseConsumerReady(s,0)));CHECK(g.Mask()==3);
        CHECK(!g.Complete(start+4,OriginalConverged(s),true));
        s.selected.current[0]=1;s.selected.previous[0]=7;
        CHECK(!g.Complete(start+5,OriginalConverged(s),true)); // derived selection still pending
        s.selected.current[2]=1;
        CHECK(!g.Complete(start+6,OriginalConverged(s),false));
        s.original.client.branches[0].nextState=0;
        CHECK(!g.Complete(start+7,OriginalConverged(s),true));
        s.original.client.branches[0].nextState=2;
        CHECK(g.Complete(start+8,OriginalConverged(s),true));CHECK(g.DesktopHandback());
        CHECK(g.DeadlineNs()==start+KnifeSessionClose::LimitNs);
        for(const auto* v:{&s.original.client.branches[0],&s.original.client.branches[1],&s.original.server})CHECK(v->loaded==30&&v->reserve==210);
        for(const auto* v:{&s.knife.client.branches[0],&s.knife.client.branches[1],&s.knife.server})CHECK(v->loaded==-1&&v->reserve==-1);}
    // Client-first order: the client inactive copy cannot veto a settled server.
    {auto s=CloseFacts();KnifeSessionClose g;g.Begin(start,true,false,start);CHECK(g.Admit(start+1,true));
        CHECK(g.Choose(0,start+2,KnifeCloseConsumerReady(s,0)));
        s.selected.current[0]=s.selected.current[2]=1;
        for(auto& v:s.knife.client.branches)v.currentState=v.nextState=1;
        CHECK(KnifeCloseConsumerReady(s,1));CHECK(g.Choose(1,start+3,KnifeCloseConsumerReady(s,1)));
        CHECK(!g.Complete(start+4,false,true));CHECK(g.Mask()==3&&!g.DesktopHandback());}
    // +3c remains a required final observation, not an additional chooser consumer.
    {auto s=CloseFacts();s.knife.client.branches[0].currentState=s.knife.client.branches[0].nextState=1;
        CHECK(KnifeCloseConsumerReady(s,0));CHECK(!KnifeCloseConsumerReady(s,2));}
    for(unsigned branch=0;branch<2;++branch)for(unsigned fault=0;fault<6;++fault){
        auto s=CloseFacts();auto& own=branch?s.knife.server:s.knife.client.branches[1];
        if(fault==0)own.currentState=1;if(fault==1)own.nextState=5;if(fault==2)own.flagsA8|=8;
        if(fault==3)own.phaseTimer=-1;if(fault==4)own.phaseTimer=1000001;
        if(fault==5)own.phaseTimer=std::numeric_limits<float>::quiet_NaN();
        CHECK(!KnifeCloseConsumerReady(s,branch));}
    // Missing native evidence/expired original close never becomes a retry lease.
    {auto s=CloseFacts();KnifeSessionClose g;g.Begin(start,true,false,start);CHECK(g.Admit(start+1,true));
        CHECK(g.Choose(1,start+2,KnifeCloseConsumerReady(s,1)));
        CHECK(!g.Choose(0,start+3,false)); // unchanged exact identity/selection/read guards
        CHECK(!g.Choose(0,start+KnifeSessionClose::LimitNs,KnifeCloseConsumerReady(s,0)));
        g.Begin(start+KnifeSessionClose::LimitNs,true,false,start+KnifeSessionClose::LimitNs);
        CHECK(g.Phase()==KnifeClosePhase::Unresolved&&g.Mask()==2&&!g.DesktopHandback());}
    std::puts("Five partial-selection close groups PASS: native state1 transition, both orders, observation role, own-consumer refusal, immutable deadline.");
    return 0;
}
}

int main(int argc,char** argv){
    if(argc==2&&std::strcmp(argv[1],"--partial-selection")==0)return PartialSelection();
    CHECK(argc==1);CHECK(PartialSelection()==0);
    constexpr std::int64_t start=1000000000;
    {KnifeSessionClose g;g.Begin(start,false,false,start);CHECK(g.Phase()==KnifeClosePhase::Complete);
        CHECK(!g.DesktopHandback());CHECK(!g.Choose(0,start,true));CHECK(g.Mask()==0);}
    {KnifeSessionClose g;g.Begin(start,true,false,start);CHECK(!g.Admit(start,false));CHECK(!g.Choose(0,start,true));
        CHECK(g.Admit(start+1,true));CHECK(!g.Choose(0,start+2,false));CHECK(!g.Choose(2,start+2,true));
        CHECK(g.Choose(0,start+2,true));CHECK(!g.Choose(0,start+3,true));CHECK(!g.Complete(start+3,true,true));
        CHECK(g.Choose(1,start+4,true));CHECK(!g.Complete(start+5,false,true));CHECK(!g.Complete(start+5,true,false));
        CHECK(g.Complete(start+6,true,true));CHECK(g.DesktopHandback());CHECK(g.CompletedNs()==start+6);}
    {for(bool owned:{false,true}){KnifeSessionClose g;g.Begin(start,owned,true,start);
        CHECK(g.Phase()==KnifeClosePhase::Unresolved);CHECK(!g.Admit(start+1,true));CHECK(!g.Choose(0,start+1,true));
        g.Begin(start+2,false,false,start+2);CHECK(g.Phase()==KnifeClosePhase::Unresolved);}}
    {KnifeSessionClose g;g.Begin(start,true,false,start);CHECK(g.Admit(start+1,true));CHECK(g.Choose(0,start+2,true));
        CHECK(!g.Choose(1,start+KnifeSessionClose::LimitNs,true));CHECK(g.Mask()==1);
        CHECK(!g.Complete(start+KnifeSessionClose::LimitNs+1,true,true));CHECK(!g.DesktopHandback());}
    {KnifeSessionClose g;g.Begin(start,true,false,start);g.Begin(start+1000000000,true,false,start+1000000000);
        CHECK(g.DeadlineNs()==start+KnifeSessionClose::LimitNs);g.CheckDeadline(g.DeadlineNs());
        CHECK(g.Phase()==KnifeClosePhase::Unresolved);CHECK(!g.Admit(start+10,true));}
    {KnifeSessionClose g;g.Begin(start,true,false,start);CHECK(g.Admit(start,true));g.Reject();
        CHECK(!g.Choose(0,start+1,true));CHECK(!g.Complete(start+2,true,true));
        KnifeSessionClose invalid;invalid.Begin(0,false,false,0);CHECK(invalid.Phase()==KnifeClosePhase::Unresolved);}
    std::puts("Six session-close groups PASS; no-owner cancellation, native proof, once per inventory, partial/unknown retention, absolute bound, no revival.");
}
