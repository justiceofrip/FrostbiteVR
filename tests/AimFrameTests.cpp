#include "Test.h"
#include "fvr/interaction/AimFrame.h"
#include <limits>
using namespace fvr;
int main(){
 math::Pose reference{},hand{};auto a=interaction::ControllerAim(reference,hand);CHECK(a&&std::abs(a->yaw)<1e-5&&std::abs(a->pitch)<1e-5);
 hand.orientation={0,-std::sin(.2f),0,std::cos(.2f)};a=interaction::ControllerAim(reference,hand);CHECK(a&&std::abs(a->yaw-.4f)<1e-5);
 reference=hand;a=interaction::ControllerAim(reference,hand);CHECK(a&&std::abs(a->yaw)<1e-5);
 reference={};hand.orientation={std::sin(.15f),0,0,std::cos(.15f)};a=interaction::ControllerAim(reference,hand);CHECK(a&&std::abs(a->pitch-.3f)<1e-5);
 hand.orientation={std::sqrt(.5f),0,0,std::sqrt(.5f)};a=interaction::ControllerAim(reference,hand,.4f);CHECK(a&&std::abs(a->yaw-.4f)<1e-5&&a->pitch>1.56f);
 hand.orientation={0,0,0,0};CHECK(!interaction::ControllerAim(reference,hand));
 hand={};hand.position.x=std::numeric_limits<float>::quiet_NaN();CHECK(!interaction::ControllerAim(reference,hand));
 float f=1,s=0;interaction::RotateMovement(-1.5707963268f,f,s);CHECK(std::abs(f)<1e-5&&std::abs(s+1)<1e-5);
 interaction::RotateMovement(1.5707963268f,f,s);CHECK(std::abs(f-1)<1e-5&&std::abs(s)<1e-5);return 0;
}
