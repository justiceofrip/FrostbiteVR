#include "Bc2ChestKnifeInspection.h"
#include <future>
#include <thread>

namespace knife_inspection_cases {
using namespace fvr::bc2::chestKnifeNative;
struct Fixture {
    struct Capture {InspectionStamp stamp;unsigned owner=0,lifetime=0;};
    struct View {unsigned owner=0,lifetime=0,sequence=0;bool complete=false,healthy=false;};
    std::mutex gate;
    InspectionStamp stamp{1,2,3,4,5,true,false};
    unsigned owner=7,lifetime=8,reads=0,joins=0,observed=0,originalCalls=0;
    bool gap=false,bad=false;
    template<class Read> std::optional<View> Inspect(Read read){
        return InspectOutsideGate<Capture,View>(gate,[&]()->std::optional<Capture>{
            return Capture{stamp,owner,lifetime};
        },[&](const Capture& c)->std::optional<View>{
            ++reads;if(!read())return {};
            return View{c.owner,c.lifetime,19,false,false};
        },[&](const Capture& c,View& v){
            ++joins;
            if(!InspectionStillCurrent(c.stamp,stamp)||c.owner!=owner||c.lifetime!=lifetime)return false;
            v.complete=!gap;v.healthy=!bad;return true;
        });
    }
    // The observer's try-lock and original invocation must both finish while
    // the inspection is still inside its native reader.
    void Callback(bool transition=false){
        std::thread worker([&]{
            {
                std::unique_lock lock(gate,std::try_to_lock);
                if(lock.owns_lock()){
                    ++observed;if(transition)AdvanceInspectionOperation(stamp.operation);
                }else gap=true;
            }
            ++originalCalls;
        });
        worker.join();
    }
};
int Cases(){
    {
        Fixture f;const auto view=f.Inspect([&]{f.Callback();return true;});
        CHECK(view&&view->complete&&view->healthy&&view->sequence==19);
        CHECK(f.observed==1&&f.originalCalls==1&&!f.gap&&f.reads==1&&f.joins==1);
    }
    {
        Fixture f;const auto view=f.Inspect([&]{f.Callback(true);return true;});
        CHECK(!view&&f.observed==1&&f.originalCalls==1&&!f.gap&&f.reads==1&&f.joins==1);
    }
    // Same-owner operation ABA is still a different operation. No reread or
    // timestamp replacement can convert it into the captured observation.
    {
        Fixture f;CHECK(!f.Inspect([&]{
            std::lock_guard lock(f.gate);++f.owner;AdvanceInspectionOperation(f.stamp.operation);
            --f.owner;AdvanceInspectionOperation(f.stamp.operation);return true;
        }));CHECK(f.owner==7&&f.reads==1&&f.joins==1);
    }
    for(unsigned field=0;field<7;++field){
        Fixture f;CHECK(!f.Inspect([&]{
            std::lock_guard lock(f.gate);
            switch(field){
            case 0:++f.stamp.source;break;case 1:++f.stamp.pose;break;
            case 2:++f.stamp.trigger;break;case 3:++f.stamp.motion;break;
            case 4:f.stamp.enabled=false;break;case 5:f.stamp.closing=true;break;
            case 6:++f.lifetime;break;
            }return true;
        }));CHECK(f.reads==1&&f.joins==1);
    }
    // A gap already present, or arriving during the native read, remains a
    // gap even though the current snapshot and later callback are successful.
    for(bool existing:{false,true}){
        Fixture f;f.gap=existing;
        const auto view=f.Inspect([&]{f.Callback();f.gap=true;f.bad=true;return true;});
        CHECK(view&&!view->complete&&!view->healthy&&f.gap&&f.originalCalls==1);
        const auto later=f.Inspect([]{return true;});CHECK(later&&!later->complete&&!later->healthy);
    }
    {
        Fixture f;CHECK(!f.Inspect([]{return false;}));CHECK(f.reads==1&&f.joins==0);
    }
    // Force contention at each short bookend, on a different thread. Neither
    // path waits for the owner, retries the native read, nor publishes a view.
    for(bool first:{false,true}){
        Fixture f;std::promise<void> ready,release;auto released=release.get_future();
        std::thread holder;
        const auto hold=[&]{holder=std::thread([&]{std::lock_guard lock(f.gate);ready.set_value();released.wait();});ready.get_future().wait();};
        if(first)hold();
        const auto view=f.Inspect([&]{hold();return true;});
        release.set_value();holder.join();
        CHECK(!view&&f.reads==(first?0u:1u)&&f.joins==0);
    }
    {
        Fixture f;f.stamp.operation=(std::numeric_limits<std::uint64_t>::max)();
        AdvanceInspectionOperation(f.stamp.operation);
        CHECK(f.stamp.operation==(std::numeric_limits<std::uint64_t>::max)());
        CHECK(!f.Inspect([]{return true;}));
    }
    return 0;
}
}
