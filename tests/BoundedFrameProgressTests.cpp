#include "Test.h"
#include "fvr/graphics/BoundedFrameProgress.h"
#include <array>
using namespace fvr::graphics;
struct Fixture {
    bool allowed=true;
    unsigned lease=42,pumps=0,sleeps=0,completeAt=0,changeAt=0,loseAt=0;
    std::uint64_t tick=100,sleepTicks=1;
    bool backwards=false;
    FrameProgressResult Run(std::uint64_t budget=8)noexcept {
        const auto originalLease=lease;
        return ServiceBoundedFrameProgress(budget,[&]()noexcept{return allowed;},
            [&]()noexcept{return lease==originalLease&&lease!=0;},
            [&]()noexcept{++pumps;if(pumps==completeAt)lease=0;if(pumps==changeAt)++lease;if(pumps==loseAt)allowed=false;},
            [&]()noexcept{return tick;},[&]()noexcept{++sleeps;if(backwards)--tick;else tick+=sleepTicks;});
    }
};
int main(){
    CHECK(ParseFrameProgressBudget(L"8")==8&&ParseFrameProgressBudget(L"16")==16);
    for(const auto text:{L"",L"0",L"17",L"999",L"-1",L" 8",L"8ms",L"1x",L"+8"})CHECK(!ParseFrameProgressBudget(text));
    {Fixture f;f.allowed=false;const auto r=f.Run();CHECK(r.stop==FrameProgressStop::AdmissionLost&&!f.pumps&&!f.sleeps);}
    {Fixture f;const auto r=f.Run(0);CHECK(r.stop==FrameProgressStop::AdmissionLost&&!f.pumps&&!f.sleeps);}
    {Fixture f;f.lease=0;const auto r=f.Run();CHECK(r.stop==FrameProgressStop::Completed&&!f.pumps&&!f.sleeps);}
    {Fixture f;f.completeAt=1;const auto r=f.Run();CHECK(r.stop==FrameProgressStop::Completed&&f.pumps==1&&!f.sleeps);}
    {Fixture f;f.completeAt=4;const auto r=f.Run();CHECK(r.stop==FrameProgressStop::Completed&&f.pumps==4&&f.sleeps==3);}
    // A successor that needs native Update must never be followed inside Present.
    {Fixture f;f.changeAt=2;const auto r=f.Run();CHECK(r.stop==FrameProgressStop::Completed&&f.lease==43&&f.pumps==2&&f.sleeps==1);}
    {Fixture f;f.loseAt=2;const auto r=f.Run();CHECK(r.stop==FrameProgressStop::AdmissionLost&&f.pumps==2&&f.sleeps==1);}
    {Fixture f;const auto r=f.Run();CHECK(r.stop==FrameProgressStop::BudgetExpired&&f.pumps==8&&f.sleeps==8&&r.ticks==8);}
    // An overscheduled sleep must not cause another query past the deadline.
    {Fixture f;f.sleepTicks=100;const auto r=f.Run();CHECK(r.stop==FrameProgressStop::BudgetExpired&&f.pumps==1&&f.sleeps==1&&r.ticks==100);}
    {Fixture f;f.tick=0;const auto r=f.Run();CHECK(r.stop==FrameProgressStop::ClockFailed&&!f.pumps&&!f.sleeps);}
    {Fixture f;f.backwards=true;const auto r=f.Run();CHECK(r.stop==FrameProgressStop::ClockFailed&&f.pumps==1&&f.sleeps==1);}
    {Fixture f;f.sleepTicks=0;const auto r=f.Run();CHECK(r.stop==FrameProgressStop::PollLimit&&f.pumps==17&&f.sleeps==16);}
    // Cancellation/close while asleep ends before any further GPU operation.
    for(unsigned cause=0;cause<2;++cause){
        unsigned pumps=0,sleeps=0;bool live=true,pending=true;std::uint64_t tick=10;
        const auto r=ServiceBoundedFrameProgress(8,[&]()noexcept{return live;},[&]()noexcept{return pending;},
            [&]()noexcept{++pumps;},[&]()noexcept{return tick;},[&]()noexcept{++sleeps;++tick;if(cause)live=false;else pending=false;});
        CHECK(pumps==1&&sleeps==1);CHECK(r.stop==(cause?FrameProgressStop::AdmissionLost:FrameProgressStop::Completed));
    }
    // A slow driver query is measured, and cannot be followed by more waiting.
    for(unsigned cause=0;cause<3;++cause){
        unsigned pumps=0,sleeps=0;bool live=true,pending=true;std::uint64_t tick=10;
        const auto r=ServiceBoundedFrameProgress(8,[&]()noexcept{return live;},[&]()noexcept{return pending;},
            [&]()noexcept{++pumps;tick+=20;if(cause==1)pending=false;if(cause==2)live=false;},
            [&]()noexcept{return tick;},[&]()noexcept{++sleeps;});
        CHECK(pumps==1&&sleeps==0&&r.ticks==20);
        CHECK(r.stop==(cause==0?FrameProgressStop::BudgetExpired:cause==1?FrameProgressStop::Completed:FrameProgressStop::AdmissionLost));
    }
    return 0;
}
