#include "Test.h"
#include "fvr/interaction/RoomscaleFollow.h"
#include <cmath>
#include <limits>
using namespace fvr;
int main(){
 interaction::RoomscaleFollow follow;interaction::RoomscaleStep s{1,1,1000,{},{},0,false};
 auto o=follow.Update(s);CHECK(o.valid&&!o.driving);
 s.headLocalMeters.x=.4f;s.nowMs+=16;o=follow.Update(s);CHECK(o.driving&&o.strafeMetersPerSecond>.6f&&!o.forwardMetersPerSecond);
 // A wall blocks native motion: request remains, camera compensation stays zero.
 s.nowMs+=16;o=follow.Update(s);CHECK(o.driving&&!o.consumedLocalMeters.x);
 s.bodyWorldMeters.x=.2f;s.nowMs+=16;o=follow.Update(s);CHECK(std::abs(o.consumedLocalMeters.x-.2f)<1e-5);
 s.bodyWorldMeters.x=.29f;s.nowMs+=16;o=follow.Update(s);CHECK(!o.driving&&std::abs(o.consumedLocalMeters.x-.29f)<1e-5);
 // Native movement + remaining physical offset equals the physical head target.
 CHECK(std::abs(s.bodyWorldMeters.x+s.headLocalMeters.x-o.consumedLocalMeters.x-.4f)<1e-5);
 s.manualMovement=true;s.bodyWorldMeters.x=.5f;s.nowMs+=16;o=follow.Update(s);CHECK(!o.driving&&std::abs(o.consumedLocalMeters.x-.29f)<1e-5);
 s.manualMovement=false;s.space=2;s.headLocalMeters={};s.nowMs+=16;o=follow.Update(s);CHECK(!o.driving&&!o.consumedLocalMeters.x);
 // World Z motion is local right when body heading is -90 degrees.
 s.bodyYaw=-1.5707963268f;s.headLocalMeters.x=.4f;s.nowMs+=16;follow.Update(s);
 s.bodyWorldMeters.z=.2f;s.nowMs+=16;o=follow.Update(s);CHECK(std::abs(o.consumedLocalMeters.x-.2f)<1e-5);
 auto before=o.consumedLocalMeters;follow.Update(s);o=follow.Update(s);CHECK(o.consumedLocalMeters.x==before.x);
 // Multiple native updates can fall inside one coarse clock tick.
 s.bodyWorldMeters.z+=.02f;o=follow.Update(s);CHECK(std::abs(o.consumedLocalMeters.x-before.x-.02f)<1e-5);before=o.consumedLocalMeters;
 // Loss of input, a long gap, and a teleport cannot consume unrelated movement.
 follow.Suspend();s.bodyWorldMeters.x+=10;s.nowMs+=16;o=follow.Update(s);CHECK(o.consumedLocalMeters.x==before.x);
 s.bodyWorldMeters.z+=.1f;s.nowMs+=1000;o=follow.Update(s);CHECK(o.consumedLocalMeters.x==before.x);
 s.bodyWorldMeters.z+=10;s.nowMs+=16;o=follow.Update(s);CHECK(o.consumedLocalMeters.x==before.x);
 s.headLocalMeters.x=std::numeric_limits<float>::quiet_NaN();s.nowMs+=16;o=follow.Update(s);CHECK(!o.valid&&!o.driving);
 s.headLocalMeters={4,0,0};o=follow.Update(s);CHECK(!o.valid);
 s.owner=2;s.headLocalMeters={};s.nowMs+=16;o=follow.Update(s);CHECK(o.valid&&!o.consumedLocalMeters.x);return 0;
}
