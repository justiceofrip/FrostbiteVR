#include "Bc2MagazineEmptyDiagnostic.h"
#include <cstdio>
using namespace fvr::bc2;
#define CHECK(x) if(!(x)){std::fprintf(stderr,"line%d\n",__LINE__);return 1;}
int main(){MagazineEmptyDiagnosticJournal j;
 for(unsigned n=0;n<3;++n){MagazineEmptyDiagnosticRecord r;r.sample.identity.owner={1,2,3,4,5,6,7};r.sample.branch=n;r.sample.before.address=0x10000+n*4;r.sample.before.currentState=r.sample.before.nextState=2;r.sample.before.loaded=0;r.after=r.sample.before;r.familyKnown=true;r.family=0;r.phase=2;r.hasBefore=r.hasAfter=r.hasContext=r.requested=r.applied=r.restored=r.ownerRetained=true;j.Observe(r);}
 CHECK(j.ArmingSize()==3);
 for(unsigned n=0;n<10000;++n){MagazineEmptyDiagnosticRecord r;r.sample.identity.owner={1,2,3,4,5,6,7};r.sample.branch=n%3;r.sample.before.address=0x20000+n*4;r.hasBefore=r.hasAfter=true;r.phase=6;j.Observe(r);}
 CHECK(j.Overwritten()>9000&&j.Size()<=j.Capacity);
 for(unsigned n=0;n<3;++n){CHECK(j.Row(n).phase==2&&j.Row(n).sample.branch==n&&j.Row(n).restored);}
 std::puts("Run03 cadence: retained all-three Arming after cancelled flood");return 0;}
