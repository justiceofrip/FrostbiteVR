#include "Test.h"
#include "fvr/ipc/InputProtocol.h"
#include <limits>
using namespace fvr;
int main(){
    interaction::InputFrame f{};f.generation=0x123456789abcdefULL;f.spaceGeneration=0x100000009ULL;f.predictedNs=1234567890123;
    f.focused=f.headValid=f.floorRelative=true;f.worldUnitsPerMeter=2.5f;f.head.position={.2f,1.6f,-.1f};
    auto& h=f.hands[1];h.gripTracked=h.aimTracked=true;h.grip.position={.2f,1.3f,-.5f};h.aim.position={.3f,1.4f,-.6f};h.active=interaction::Components;
    h.held=interaction::Secondary;h.trigger=.9f;h.squeeze=.7f;h.stickX=-.3f;h.stickY=.2f;
    ipc::InputPacket p{};CHECK(ipc::EncodeInput(f,9999,p));interaction::InputFrame out{};CHECK(ipc::DecodeInput(p,out));
    CHECK(out.generation==f.generation&&out.spaceGeneration==f.spaceGeneration&&out.predictedNs==f.predictedNs&&out.floorRelative);
    CHECK(out.hands[1].grip.position.z==-.5f&&out.hands[1].aim.position.z==-.6f&&out.hands[1].trigger==.9f&&out.worldUnitsPerMeter==2.5f);
    auto bad=p;bad.version=3;CHECK(!ipc::DecodeInput(bad,out));bad=p;bad.bytes--;CHECK(!ipc::DecodeInput(bad,out));
    bad=p;bad.hands[0].reserved=4;CHECK(!ipc::DecodeInput(bad,out));bad=p;bad.flags|=8;CHECK(!ipc::DecodeInput(bad,out));
    bad=p;bad.hands[1].flags|=4;CHECK(!ipc::DecodeInput(bad,out));bad=p;bad.hands[1].trigger=std::numeric_limits<float>::quiet_NaN();CHECK(!ipc::DecodeInput(bad,out));
    bad=p;bad.hands[1].aim.values[6]=0;CHECK(!ipc::DecodeInput(bad,out));bad=p;bad.worldUnitsPerMeter=0;CHECK(!ipc::DecodeInput(bad,out));
    h.touchActive=interaction::ThumbTouch|interaction::IndexTouch;h.touched=interaction::ThumbTouch;
    CHECK(ipc::EncodeInput(f,10000,p)&&p.version==2);CHECK(ipc::DecodeInput(p,out));
    CHECK(out.hands[1].touchActive==3&&out.hands[1].touched==1&&out.hands[0].touchActive==0);
    bad=p;bad.hands[1].reserved=0x100;CHECK(!ipc::DecodeInput(bad,out)); // touched without active sensor
    bad=p;bad.version=1;CHECK(!ipc::DecodeInput(bad,out)); // v1 never reinterprets its reserved word
    bad.hands[1].reserved=0;CHECK(ipc::DecodeInput(bad,out)&&out.hands[1].touchActive==0);
    h.touchActive=0;h.touched=1;CHECK(!ipc::EncodeInput(f,10000,p));h.touched=0;
    f.focused=f.headValid=false;f.hands={};CHECK(ipc::EncodeInput(f,10000,p));CHECK(ipc::DecodeInput(p,out));CHECK(!out.focused&&!out.hands[1].active);
    return 0;
}
