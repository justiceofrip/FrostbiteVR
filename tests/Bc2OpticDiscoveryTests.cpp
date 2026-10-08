#include "Test.h"
#include "Bc2OpticDiscovery.h"
#include <cstring>
#include <fstream>
#include <iterator>
#include <vector>
using namespace fvr;
int main(int argc,char** argv){
    CHECK(!bc2::DiscoverOpticObservation({},{}));
    std::vector<std::byte> tiny(64);engine::PeImage bogus;bogus.machine=0x14c;
    std::uint32_t nt=0xfffffff0;std::memcpy(tiny.data()+0x3c,&nt,4);CHECK(!bc2::DiscoverOpticObservation(tiny,bogus));
    if(argc<2)return 0;
    std::ifstream file(argv[1],std::ios::binary);CHECK(file);const std::vector<char> raw((std::istreambuf_iterator<char>(file)),{});
    std::vector<std::byte> bytes(raw.size());std::memcpy(bytes.data(),raw.data(),raw.size());const auto pe=engine::InspectPe(bytes);CHECK(pe.valid);
    const auto proof=bc2::DiscoverOpticObservation(bytes,pe.image);CHECK(proof);
    CHECK(proof->filterRenderer==0x78d6b0&&proof->filterCaller==0x7b2b60&&proof->filterReturn==0x7b2b65);
    CHECK(!proof->nativeAdsStateVerified&&!proof->sceneTargetVerified&&!proof->reticleVerified);
    CHECK(proof->zoomLevel.typeInfo==0x17fc7fc&&proof->aiming.typeInfo==0x17fc898&&proof->sniperFilter.typeInfo==0x17f6b48);
    const auto offset=[&](std::uint32_t rva)->std::size_t{for(const auto& s:pe.image.sections)if(rva>=s.rva&&rva-s.rva<s.rawSize)return std::size_t(s.rawOffset)+rva-s.rva;return bytes.size();};
    for(const auto rva:{proof->filterRenderer,proof->filterRenderer+0x9e,proof->filterRenderer+0x207,
        proof->filterCaller,proof->filterCaller-0x6f+0x22,proof->zoomLevel.fields+16,proof->aiming.fields+12,
        proof->sniperFilter.fields+16,proof->scopeFilter.fields+16,proof->zoomLevel.metadata+6,proof->sniperFilter.registration+11}){
        const auto at=offset(rva);CHECK(at<bytes.size());auto changed=bytes;changed[at]^=std::byte{1};CHECK(!bc2::DiscoverOpticObservation(changed,pe.image));}
    auto wrong=pe.image;wrong.machine=0x8664;CHECK(!bc2::DiscoverOpticObservation(bytes,wrong));return 0;
}
