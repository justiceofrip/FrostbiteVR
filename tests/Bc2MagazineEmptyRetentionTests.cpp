#include "Bc2MagazineEmptyDiagnostic.h"
#include <cassert>
#include <iostream>
using namespace fvr::bc2;
MagazineEmptyDiagnosticRecord Local(unsigned address=0x123400){
 MagazineEmptyDiagnosticRecord r;r.stage=MagazineEmptyDiagnosticStage::Eligibility;
 r.sample.branch=0;r.sample.identity.owner={0x10000,0x20000,0x30000,0x40000,1,1,1};
 r.hasBefore=r.hasAfter=true;r.sample.before.address=address;
 r.sample.before.currentState=r.sample.before.nextState=2;r.sample.before.loaded=0;
 r.after=r.sample.before;return r;
}
int main(){
 MagazineEmptyDiagnosticJournal j;auto entry=Local();entry.after.nextState=10;j.Observe(entry);
 for(unsigned n=0;n<100000;++n){MagazineEmptyDiagnosticRecord foreign;foreign.sample.before.address=0x80000000+n*4;j.Observe(foreign);}
 assert(j.Size()==1&&j.Row(0).after.nextState==10&&j.UnownedSkipped()==100000);
 for(unsigned n=0;n<10000;++n){auto local=Local(0x123400+n*4);local.sample.before.loaded=local.after.loaded=30;j.Observe(local);}
 assert(j.Size()==97&&j.Row(0).after.nextState==10&&j.Overwritten()==9904);
 for(unsigned n=1;n<40;++n){auto e=Local(0x50000+n*4);e.after.nextState=10;j.Observe(e);}
 assert(j.SalientSize()==32&&j.SalientDropped()==8&&j.Size()==128&&j.Row(0).sample.before.address==0x123400);
 assert(j.Observed()==110040);
 MagazineEmptyDiagnosticJournal late;late.Observe(entry);
 MagazineEmptyDiagnosticRecord boundary;boundary.sample.before.address=entry.sample.before.address;late.Observe(boundary);
 assert(late.Size()==2&&late.Row(1).leaseInvalidLastKnown&&!late.Row(1).hasBefore&&!late.Row(1).hasAfter&&!late.Row(1).ownerRetained);
 assert(late.Row(1).sample.identity==entry.sample.identity&&late.LastKnownAttributed()==1);
 auto sibling=Local(0x777700);sibling.sample.branch=1;late.Observe(sibling);late.Observe(boundary);assert(late.LastKnownAttributed()==2);
 auto newer=Local();newer.sample.identity.owner.equipGeneration=2;newer.sample.before.address=0x555500;late.Observe(newer);
 late.Observe(boundary);assert(late.UnownedSkipped()==1);
 MagazineEmptyDiagnosticJournal reserve;
 for(unsigned n=0;n<20;++n){auto l=Local(0x990000+n*4);reserve.Observe(l);MagazineEmptyDiagnosticRecord dead;dead.sample.before.address=l.sample.before.address;reserve.Observe(dead);}
 assert(reserve.SalientSize()==16&&reserve.BoundaryDropped()==4&&reserve.TransitionDropped()==0);
 for(unsigned n=0;n<32;++n){auto e=Local(0xaa0000+n*4);e.after.nextState=10;reserve.Observe(e);}
 assert(reserve.SalientSize()==32&&reserve.BoundaryDropped()==4&&reserve.TransitionDropped()==16&&reserve.SalientDropped()==20);
 unsigned entries=0;for(std::size_t n=0;n<reserve.Size();++n)if(reserve.Row(n).hasAfter&&reserve.Row(n).after.nextState==10)++entries;
 assert(entries==16);
 std::cout<<"journal retention regression passed: foreign flood, local flood, protected overflow\n";
}
