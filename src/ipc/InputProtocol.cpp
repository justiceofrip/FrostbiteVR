#include "fvr/ipc/InputProtocol.h"
namespace fvr::ipc {
namespace {
PoseWire Pack(const math::Pose& p){return {{{p.position.x,p.position.y,p.position.z,p.orientation.x,p.orientation.y,p.orientation.z,p.orientation.w}}};}
math::Pose Unpack(const PoseWire& p){const auto& v=p.values;return {{v[0],v[1],v[2]},{v[3],v[4],v[5],v[6]}};}
}
bool DecodeInput(const InputPacket& p,interaction::InputFrame& out) noexcept {
    if(p.magic!=0x31494646||(p.version!=1&&p.version!=2)||p.bytes!=sizeof(p)||(p.flags&~7u)||p.deadlineQpc<=0||p.reserved)return false;
    interaction::InputFrame f{};f.generation=p.generation;f.spaceGeneration=p.spaceGeneration;f.predictedNs=p.predictedNs;
    f.focused=(p.flags&1)!=0;f.headValid=(p.flags&2)!=0;f.floorRelative=(p.flags&4)!=0;
    f.worldUnitsPerMeter=p.worldUnitsPerMeter;f.referenceHead=Unpack(p.referenceHead);f.head=Unpack(p.head);
    for(unsigned i=0;i<2;++i){const auto& w=p.hands[i];auto& h=f.hands[i];if((w.flags&~3u)||(p.version==1&&w.reserved)||(p.version==2&&(w.reserved&~0x303u)))return false;
        if(p.version==2){h.touchActive=w.reserved&255;h.touched=(w.reserved>>8)&255;}
        h.gripTracked=(w.flags&1)!=0;h.aimTracked=(w.flags&2)!=0;h.active=w.active;h.held=w.held;
        h.grip=Unpack(w.grip);h.aim=Unpack(w.aim);h.stickX=w.stickX;h.stickY=w.stickY;h.trigger=w.trigger;h.squeeze=w.squeeze;
    }
    if(!interaction::ValidInput(f))return false;out=f;return true;
}
bool EncodeInput(const interaction::InputFrame& f,std::int64_t deadline,InputPacket& out) noexcept {
    if(!interaction::ValidInput(f)||deadline<=0)return false;
    InputPacket p{};p.generation=f.generation;p.spaceGeneration=f.spaceGeneration;p.predictedNs=f.predictedNs;p.deadlineQpc=deadline;
    p.flags=(f.focused?1u:0u)|(f.headValid?2u:0u)|(f.floorRelative?4u:0u);p.worldUnitsPerMeter=f.worldUnitsPerMeter;
    p.referenceHead=Pack(f.headValid?f.referenceHead:math::Pose{});p.head=Pack(f.headValid?f.head:math::Pose{});
    for(unsigned i=0;i<2;++i){const auto& h=f.hands[i];auto& w=p.hands[i];w.flags=(h.gripTracked?1u:0u)|(h.aimTracked?2u:0u);
        w.active=h.active;w.held=h.held;w.reserved=h.touchActive|(h.touched<<8);w.grip=Pack(h.gripTracked?h.grip:math::Pose{});w.aim=Pack(h.aimTracked?h.aim:math::Pose{});
        w.stickX=h.stickX;w.stickY=h.stickY;w.trigger=h.trigger;w.squeeze=h.squeeze;
    }
    out=p;return true;
}
}
