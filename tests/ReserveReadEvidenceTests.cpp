#include "Bc2ReserveReadEvidence.h"
#include "Bc2ReserveFinalValidation.h"
#include <sstream>
#include <cstdlib>
using namespace fvr::bc2;
#define CHECK(x) do{if(!(x))std::abort();}while(false)
int main(){ReserveReadEvidenceJournal j;ReserveReadEvidence e;e.stage=1;for(unsigned n=0;n<40;++n)j.Observe(e);e.stage=7;e.targetKnown=true;e.targetMatched=true;e.check=1;e.result=3;e.sampledRevision=10;e.currentRevision=12;e.validationNs=101;e.deadlineNs=200;j.Observe(e);
 std::ostringstream out;j.Report(out,true);CHECK(out.str().find("\"totals\":[40,1]")!=std::string::npos);CHECK(out.str().find("\"dropped\":[8,0]")!=std::string::npos);CHECK(out.str().find("\"current_revision\":12")!=std::string::npos);
 std::ostringstream pending;j.Report(pending,false);CHECK(pending.str().find("\"rows\":[]")!=std::string::npos);
 // Structural proof can itself complete a callback and consume the deadline.
 unsigned revision=10;long long now=100;auto structural=[&]{revision=12;now=201;return true;};
 const bool valid=structural();const auto at=now;const auto after=revision;
 CHECK(ValidateReserveFinal(valid,at<200,1,10,after)==ReserveFinalVerdict::Rejected);
 now=101;CHECK(ValidateReserveFinal(valid,now<200,1,10,after)==ReserveFinalVerdict::CohortGap);
}
