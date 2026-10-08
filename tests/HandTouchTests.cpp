#include "Test.h"
#include "fvr/interaction/HandTouch.h"
using namespace fvr::interaction;
int main(){
 HandPoseTargets pose;pose.curl={.6f,.2f,.6f,.6f,.6f};ControllerState hand;
 CHECK(ApplyHandTouch(pose,hand).curl==pose.curl);
 hand.touchActive=ThumbTouch|IndexTouch;hand.squeeze=.6f;
 auto out=ApplyHandTouch(pose,hand);CHECK(out.curl[0]==0&&out.curl[1]==.2f&&out.curl[2]==.6f);
 hand.touched=ThumbTouch|IndexTouch;pose.curl[1]=0;out=ApplyHandTouch(pose,hand);
 CHECK(Near(out.curl[0],.68f)&&Near(out.curl[1],.15f));
 pose.curl[1]=.9f;CHECK(ApplyHandTouch(pose,hand).curl[1]==.9f);
 pose.role=HandPoseRole::WeaponSupport;CHECK(ApplyHandTouch(pose,hand).curl==pose.curl);
 pose.role=HandPoseRole::MechanismGrip;CHECK(ApplyHandTouch(pose,hand).curl==pose.curl);
 return 0;
}
