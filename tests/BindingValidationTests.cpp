#include "Test.h"
#include "fvr/engine/BindingValidation.h"
#include "fvr/engine/ModuleApi.h"
#include <array>
using namespace fvr::engine;
int main(){
    CHECK(!ParsePattern(""));CHECK(!ParsePattern("?? ?"));CHECK(!ParsePattern("5Z"));CHECK(!ParsePattern("1"));CHECK(!ParsePattern("55 8b ?? 0"));
    const auto pattern=ParsePattern("55 8b ??");CHECK(pattern);std::array<std::byte,8> bytes{std::byte{0x55},std::byte{0x8b},std::byte{0xff}};
    auto match=UniqueMatch(bytes,*pattern);CHECK(match&&*match==0);bytes[4]=std::byte{0x55};bytes[5]=std::byte{0x8b};CHECK(!UniqueMatch(bytes,*pattern));
    CHECK(!UniqueMatch(std::span(bytes).first(2),*pattern));FrostbiteEvidence evidence;CHECK(!evidence.Capabilities());
    BindingEvidence verified{true,true,true,true,true};evidence.camera=evidence.renderOnly=evidence.stateRestore=evidence.eyeTargets=verified;
    CHECK(!evidence.Capabilities());evidence.coordinates=evidence.simulationOnce=evidence.gpuOwnership=true;CHECK(evidence.Capabilities()==FVR_CAP_STEREO);
    evidence.weapon=evidence.skeleton=evidence.input=verified;CHECK(evidence.Capabilities()==FVR_CAP_STEREO);evidence.localOwner=verified;CHECK(evidence.Capabilities()&FVR_CAP_HANDS);
    evidence.renderOnly.relationships=false;CHECK(!evidence.Capabilities());return 0;
}