#include "Test.h"
#include "Bc2Profile.h"
#include "fvr/engine/BindingValidation.h"
#include <vector>
#include <algorithm>
using namespace fvr;
void Put16(std::vector<std::byte>& b,std::size_t at,unsigned v){b[at]=std::byte(v&255);b[at+1]=std::byte((v>>8)&255);}
void Put32(std::vector<std::byte>& b,std::size_t at,unsigned v){Put16(b,at,v&65535);Put16(b,at+2,v>>16);}
void Pattern(std::vector<std::byte>& b,std::size_t at,const char* text){const auto pattern=engine::ParsePattern(text);for(const auto& v:*pattern)b[at++]=std::byte(v.value);}
std::vector<std::byte> Fixture(){
    std::vector<std::byte>b(0x1800);Put16(b,0,0x5a4d);Put32(b,0x3c,0x80);Put32(b,0x80,0x4550);Put16(b,0x84,0x14c);Put16(b,0x86,3);Put16(b,0x94,224);Put16(b,0x98,0x10b);Put32(b,0x98+28,0x400000);Put32(b,0x98+56,0x4000);
    auto section=[&](unsigned n,unsigned rva,unsigned raw,unsigned size,unsigned flags){const auto at=0x178+n*40;Put32(b,at+8,size);Put32(b,at+12,rva);Put32(b,at+16,size);Put32(b,at+20,raw);Put32(b,at+36,flags);};
    section(0,0x1000,0x200,0x1000,0x60000020);section(1,0x2000,0x1200,0x400,0x40000040);section(2,0x3000,0x1600,0x200,0xc0000040);
    Pattern(b,0x220,"55 8B EC 83 E4 F0 81 EC 44 01 00 00 53 56 8B 35 ?? ?? ?? ?? 8B 06 8B 50 18 57 8B F9 8B CE FF D2");Put32(b,0x230,0x403000);
    Pattern(b,0x280,"56 8B 35 ?? ?? ?? ?? 8B 8E D8 09 00 00 E8 ?? ?? ?? ?? 8B CE E8 ?? ?? ?? ?? 8B 8E D8 09 00 00 5E E9");Put32(b,0x283,0x403004);Put32(b,0x295,static_cast<unsigned>(0x1020-(0x1080+25)));
    Pattern(b,0x300,"83 EC 20 53 56 8B F1 8B 06 8B 50 18 57 FF D2 E8 ?? ?? ?? ?? 8A D8 E8 ?? ?? ?? ?? 8B 4E 40 8A 51 35 8A 8E 86 01 00 00");
    Pattern(b,0x380,"56 8B F1 8B 96 68 01 00 00 33 C9 38 8E 84 01 00 00 74 0F 38 8E 85 01 00 00 74 07 B9 01 00 00 00 33 D2 8B 86 88 00 00 00 57 8B 38 51 52 50 8B 47 20 FF D0");
    for(unsigned slot=0;slot<12;++slot)Put32(b,0x1200+4*slot,0x401400+slot*16);Put32(b,0x1214,0x401100);Put32(b,0x1228,0x401460);Put32(b,0x122c,0x401470);Put32(b,0x660,0xc318418b);Put32(b,0x670,0xc31c418b);return b;
}
std::optional<bc2::DiscoveryProfile> Discover(const std::vector<std::byte>& b){auto pe=engine::InspectPe(b);if(!pe.valid)return {};return bc2::DiscoverProfile(b,pe.image);}
std::vector<std::byte> WorldFixture(){
    auto b=Fixture();
    Pattern(b,0xe00,"55 8B EC 83 E4 F0 81 EC 88 04 00 00 56 57 8B F9 80 7F 38 00 74 22 8B 4F 2C 8B 01 8B 57 30 8B 40 14 52 FF D0");
    Pattern(b,0x800,"83 EC 54 57 8B F9 8B 87 94 00 00 00 80 B8 D2 00 00 00 00 0F 84 79 01 00 00 56 8B 74 24 60 83 BE C4 00 00 00 01 0F 85 66 01 00 00 53 8B 9F 9C 00 00 00");
    Pattern(b,0xa00,"81 EC 84 0F 00 00 53 55 56 57 89 4C 24 44 C7 44 24 14 00 00 00 00 8D B9 58 01 00 00 C7 44 24 48 09 00 00 00 BB 08 00 00 00");
    Pattern(b,0xa80,"55 8B EC 83 E4 F0 81 EC 94 03 00 00 53 56 8B F1 8B 0D 00 30 40 00 8B 01 8B 50 0C 57 FF D2 83 F8 02");
    Pattern(b,0xb00,"55 8B EC 83 E4 F0 81 EC 94 00 00 00 53 56 8B F1 57 8D 46 50 50 8D 4C 24 64 51 E8 00 00 00 00 0F 10 08 0F 57 C0");
    b[0x8a7]=std::byte{0xe8};Put32(b,0x8a8,0x1800-(0x1600+0xac));b[0x928]=std::byte{0xe8};Put32(b,0x929,0x1880-(0x1600+0x12d));
    Pattern(b,0x975,"C7 86 C4 00 00 00 03 00 00 00");Pattern(b,0x996,"C2 04 00");Pattern(b,0xcd5,"83 26 FE 5F 5E 5B 8B E5 5D C3");
    for(unsigned slot=0;slot<12;++slot)Put32(b,0x1280+slot*4,0x401400);Put32(b,0x1294,0x401600);
    for(unsigned slot=0;slot<7;++slot)Put32(b,0x12c0+slot*4,0x401400);Put32(b,0x12d4,0x401c00);return b;
}
std::optional<bc2::RenderPathCandidates> RenderPath(const std::vector<std::byte>& b){const auto pe=engine::InspectPe(b);if(!pe.valid)return {};return bc2::DiscoverRenderPath(b,pe.image);}
std::vector<std::byte> ViewFixture(){
    auto b=Fixture();
    Pattern(b,0xd00,"8B 44 24 08 56 57 33 FF 57 57 8B F1 8B 4C 24 14 50 51 8B CE E8 00 00 00 00 C7 06 80 20 40 00 89 BE 70 16 00 00 89 BE 74 16 00 00");
    Pattern(b,0xdda,"8B 4E 70 8B 41 28 83 C1 24 3B 41 08");
    Pattern(b,0x800,"81 C1 90 00 00 00 E9 00 00 00 00 CC CC CC CC CC 81 C1 F0 04 00 00 E9 00 00 00 00");
    Put32(b,0x807,0x1800-(0x1600+11));Put32(b,0x817,0x1800-(0x1600+27));
    Pattern(b,0xa00,"55 8B EC 83 E4 F0 83 EC 08 56 8B F1 57 8B 7D 08");Pattern(b,0xbc5,"C2 04 00");
    Pattern(b,0xc00,"8B 41 70 C3");Pattern(b,0xc10,"8D 81 90 00 00 00 C3");Pattern(b,0xc20,"8D 81 F0 04 00 00 C3");
    Pattern(b,0xc30,"8D 81 70 0C 00 00 C3");Pattern(b,0xc40,"8D 81 D0 10 00 00 C3");Pattern(b,0xc50,"8A 81 52 0C 00 00 C3");Pattern(b,0xc60,"8D 81 58 0C 00 00 C3");
    for(unsigned slot=0;slot<19;++slot)Put32(b,0x1280+slot*4,0x401400);
    for(auto pair:{std::pair{2u,0x1a00u},{5u,0x1600u},{6u,0x1a10u},{7u,0x1610u},{8u,0x1a20u},{9u,0x1a30u},{10u,0x1a40u},{16u,0x1a60u},{18u,0x1a50u}})Put32(b,0x1280+pair.first*4,0x400000+pair.second);
    return b;
}
std::optional<bc2::ViewLayoutCandidates> ViewLayout(const std::vector<std::byte>& b){const auto pe=engine::InspectPe(b);if(!pe.valid)return {};return bc2::DiscoverViewLayout(b,pe.image);}
std::vector<std::byte> CacheFixture(){
    auto b=Fixture();
    Pattern(b,0x700,"56 8B F1 F6 06 01 74 05 E8 00 00 00 00 8D 86 20 02 00 00 5E C3");
    Pattern(b,0x720,"56 8B F1 F6 06 02 74 05 E8 00 00 00 00 8D 86 E0 02 00 00 5E C3");
    Pattern(b,0x740,"56 8B F1 F6 06 08 74 05 E8 00 00 00 00 8D 86 90 00 00 00 5E C3");
    Put32(b,0x709,0x1600-(0x1500+13));Put32(b,0x729,0x1800-(0x1520+13));Put32(b,0x749,0x1a00-(0x1540+13));
    Pattern(b,0x800,"55 8B EC 83 E4 F0 81 EC 94 00 00 00 53 56 8B F1 57 8D 46 50 50 8D 4C 24 64 51 E8 00 00 00 00 0F 10 08 0F 57 C0");
    Pattern(b,0xa00,"55 8B EC 83 E4 F0 83 EC 44 53 56 8B F1 83 7E 04 01 57 75 3B 0F B6");
    Pattern(b,0xc00,"51 56 8B F1 83 7E 04 01 D9 46 20 75 35 0F B6 46 08 50 8D 4E");
    Pattern(b,0x9d5,"83 26 FE 5F 5E 5B 8B E5 5D C3");Pattern(b,0xbc1,"83 26 FD 5F 5E 5B 8B E5 5D C3");Pattern(b,0xc7c,"83 26 F7 5E 59 C3");return b;
}
std::optional<bc2::CameraCacheCandidates> Caches(const std::vector<std::byte>& b){const auto pe=engine::InspectPe(b);if(!pe.valid)return {};return bc2::DiscoverCameraCaches(b,pe.image);}
std::vector<std::byte> VisibilityFixture(){
    auto b=WorldFixture();b.insert(b.begin()+0x1200,0x4000,std::byte{});
    Put32(b,0x98+56,0x8000);Put32(b,0x178+8,0x5000);Put32(b,0x178+16,0x5000);
    Put32(b,0x1a0+12,0x6000);Put32(b,0x1a0+20,0x5200);Put32(b,0x1c8+12,0x7000);Put32(b,0x1c8+20,0x5600);
    Put32(b,0x230,0x407000);Put32(b,0x283,0x407004);Put32(b,0xa92,0x407000);
    Pattern(b,0x1800,"55 8B EC 83 E4 F0 81 EC F4 09 00 00 53 56 57 8B F1 E8 00 00 00 00 84 C0 74 09 8B 06 8B 50 18 8B CE FF D2");
    Pattern(b,0x2000,"55 8B EC 83 E4 F0 B8 04 68 00 00 E8 00 00 00 00 53 8B 5D 0C 89 4C 24 2C 8B 8B 84 16 00 00 2B 8B 80 16 00 00");
    Put32(b,0x528c,0x402600);b[0x1b83]=std::byte{0xe8};Put32(b,0x1b84,0x2e00-(0x2600+0x388));
    Pattern(b,0x1be8,"5F 5E 5B 8B E5 5D C2 18 00");Pattern(b,0x4e9a,"5F 5E 5B 8B E5 5D C2 18 00");return b;
}
std::optional<bc2::VisibilityPathCandidates> Visibility(const std::vector<std::byte>& b){const auto pe=engine::InspectPe(b);if(!pe.valid)return {};return bc2::DiscoverVisibilityPath(b,pe.image);}
std::vector<std::byte> LifecycleFixture(){
    auto b=ViewFixture();b.insert(b.begin()+0x1200,0x1000,std::byte{});
    Put32(b,0x98+56,0x5000);Put32(b,0x178+8,0x2000);Put32(b,0x178+16,0x2000);
    Put32(b,0x1a0+12,0x3000);Put32(b,0x1a0+20,0x2200);Put32(b,0x1c8+12,0x4000);Put32(b,0x1c8+20,0x2600);
    Put32(b,0x230,0x404000);Put32(b,0x283,0x404004);Put32(b,0xd1b,0x403080);Pattern(b,0xdfc,"C2 08 00");
    Pattern(b,0x1200,"56 68 B0 56 00 00 8B F1 E8 00 00 00 00 83 C4 04 85 C0 74 11 8B 4C 24 08 51 56 8B C8 E8 00 00 00 00 5E C2 04 00 33 C0 5E C2 04 00");Put32(b,0x121d,static_cast<unsigned>(0x1b00-(0x2000+33)));
    Pattern(b,0x1260,"83 C1 20 B8 01 00 00 00 F0 0F C1 01 40 C3");
    Pattern(b,0x1280,"56 8D 41 20 83 CE FF F0 0F C1 30 4E 75 10 85 C9 74 0C 8B 11 8B 82 EC 00 00 00 6A 01 FF D0 8B C6 5E C3");
    Pattern(b,0x12c0,"8A 44 24 04 88 81 52 0C 00 00 C2 04 00");
    Pattern(b,0x12e0,"56 8B F1 E8 00 00 00 00");Put32(b,0x12e4,0x2140-(0x20e0+8));Pattern(b,0x12fb,"C2 04 00");
    Put32(b,0x134d,0x403080);Pattern(b,0x1364,"8B 5E 70 8B 4B 28 8B 43 24 3B C1");Pattern(b,0x1391,"83 43 28 FC");
    Put32(b,0x2280,0x402060);Put32(b,0x2284,0x402080);Put32(b,0x2280+17*4,0x4020c0);Put32(b,0x2280+59*4,0x4020e0);
    for(unsigned i=0;i<5;++i)Put32(b,0x23a0+i*4,0x401400);Put32(b,0x23ac,0x402000);return b;
}
std::optional<bc2::ViewLifecycleCandidates> Lifecycle(const std::vector<std::byte>& b){const auto pe=engine::InspectPe(b);if(!pe.valid)return {};return bc2::DiscoverViewLifecycle(b,pe.image);}
std::vector<std::byte> CallbackFixture(){auto b=Fixture();
    Pattern(b,0x800,"56 8B 74 24 08 8B 06 8B 50 0C 57 8B F9 8B CE FF D2 85 C0 74 0B 8B 06 8B 50 0C 8B CE FF D2 8B F0 8B 06 8B 90 9C 00 00 00 8B CE FF D2 85 C0 75 06 89 B7 BC 00 00 00 5F 5E C2 04 00");
    Pattern(b,0x900,"56 8B F1 8B 46 24 3B 46 28 8D 4E 20 73 18 85 C0 8D 50 04 89 51 04 74 19 8B 4C 24 08 89 08 C6 46 30 01 5E C2 04 00 8D 54 24 08 52 50 E8 ?? ?? ?? ?? C6 46 30 01 5E C2 04 00");
    Pattern(b,0xa00,"56 8B F1 8B 4E 24 8B 46 20 3B C1 74 0F 8B 54 24 08 39 10 74 07 83 C0 04 3B C1 75 F5 8D 50 04 3B D1 73 0E 2B CA 51 52 50 FF 15 ?? ?? ?? ?? 83 C4 0C 83 46 24 FC C6 46 30 01 5E C2 04 00");
    return b;
}
std::optional<bc2::ViewCallbackCandidates> Callbacks(const std::vector<std::byte>& b){const auto pe=engine::InspectPe(b);if(!pe.valid)return {};return bc2::DiscoverViewCallbacks(b,pe.image);}
std::vector<std::byte> InitializationFixture(){auto b=VisibilityFixture();
    Pattern(b,0x3000,"55 8B EC 83 E4 F8 81 EC C8 00 00 00 53 55 56 8B F1 57 89 74 24 18 E8 ?? ?? ?? ?? 8B CE E8 ?? ?? ?? ?? 8B 86 AC 00 00 00");
    Pattern(b,0x37ad,"8B 8B 84 16 00 00 2B 8B 80 16 00 00 8D 83 80 16 00 00");
    Pattern(b,0x3a1e,"5F 5E 5D 5B 8B E5 5D C3");Pattern(b,0x3b00,"8B 81 70 16 00 00 C3 CC CC CC CC CC CC CC CC CC");Put32(b,0x5298,0x403e00);
    Pattern(b,0x3c00,"83 EC 18 53 55 8B D9 8B 4B 08 68 ?? ?? ?? ?? 8D 44 24 14 33 ED 50 C6 43 30 00 89 6B 0C E8");Pattern(b,0x3d5c,"5D 5B 83 C4 18 C2 04 00");return b;
}
std::optional<bc2::ViewInitializationCandidates> Initialization(const std::vector<std::byte>& b){const auto pe=engine::InspectPe(b);if(!pe.valid)return {};return bc2::DiscoverViewInitialization(b,pe.image);}
std::vector<std::byte> ProjectionFixture(){auto b=Fixture();
    Pattern(b,0x800,"55 8B EC 83 E4 F0 83 EC 18 56 57 8B 7D 08 8A 47 08 8B F1 8B 4E 0C 81 4E 08 00 10 00 00 81 4E 08 00 08 00 00 80 46 07 02");
    Pattern(b,0x857,"89 46 24");Pattern(b,0x88c,"89 46 1C");Pattern(b,0x8c6,"5F 5E 8B E5 5D C2 08 00");
    Pattern(b,0x900,"55 8B EC 83 E4 F0 83 EC 4C 56 8B F1 8B 46 0C 81 4E 08 00 80 00 00 80 46 07 01 89 46 0C");
    Pattern(b,0x963,"89 46 20 5E 8B E5 5D C2 04 00");
    Pattern(b,0xa00,"F6 84 24 90 00 00 00 02 74 0C 8D 8C 24 90 00 00 00 E8 00 00 00 00 8D 8C 24 70 03 00 00 51 8B CE E8 00 00 00 00 80 7D 34 00");
    Pattern(b,0xb00,"F6 84 24 C0 00 00 00 02 8B D8 74 0C 8D 8C 24 C0 00 00 00 E8 00 00 00 00 8D 8C 24 A0 03 00 00 51 8B CB E8 00 00 00 00 8B 54 24 20");
    for(unsigned at:{0x8a4u,0xa20u,0xb22u}){b[at]=std::byte{0xe8};Put32(b,at+1,0x900u-(at+5));}return b;
}
std::optional<bc2::ProjectionOverrideCandidates> Overrides(const std::vector<std::byte>& b){auto pe=engine::InspectPe(b);if(!pe.valid)return {};return bc2::DiscoverProjectionOverrides(b,pe.image);}
int main(){
    {auto b=ProjectionFixture();const auto p=Overrides(b);CHECK(p&&p->setter==0x1700&&p->meshCaller==0x1825&&p->terrainCaller==0x1927);
    b[0x96b]=std::byte{8};CHECK(!Overrides(b));b=ProjectionFixture();Put32(b,0xa21,0);CHECK(!Overrides(b));
    b=ProjectionFixture();Put32(b,0xb23,0);CHECK(!Overrides(b));b=ProjectionFixture();Put32(b,0x8a5,0);CHECK(!Overrides(b));
    b=ProjectionFixture();std::copy_n(b.begin()+0x900,0x6d,b.begin()+0xc00);CHECK(!Overrides(b));
    b=ProjectionFixture();std::copy_n(b.begin()+0xa00,41,b.begin()+0xc00);CHECK(!Overrides(b));}

    {auto b=Fixture();Pattern(b,0x800,"55 8B EC 83 E4 F0 83 EC 18 56 57 8B 7D 08 8A 47 08 8B F1 8B 4E 0C 81 4E 08 00 10 00 00 81 4E 08 00 08 00 00 80 46 07 02");
    Pattern(b,0x857,"89 46 24");Pattern(b,0x88c,"89 46 1C");Pattern(b,0x8c6,"5F 5E 8B E5 5D C2 08 00");
    auto pe=engine::InspectPe(b);CHECK(bc2::DiscoverContextCamera(b,pe.image)==0x1600);
    b[0x8cc]=std::byte{4};CHECK(!bc2::DiscoverContextCamera(b,pe.image));b[0x8cc]=std::byte{8};
    std::copy(b.begin()+0x800,b.begin()+0x8ce,b.begin()+0x900);CHECK(!bc2::DiscoverContextCamera(b,pe.image));}

    auto bytes=Fixture();auto profile=Discover(bytes);CHECK(profile);CHECK(profile->rendererGlobal==0x3000&&profile->gameRendererGlobal==0x3004&&profile->frame==0x1020&&profile->dispatch==0x1080&&profile->rendererVtable==0x2000);
    // Relocate preferred base AND all VA relationships: no executable hash/RVA allowlist.
    Put32(bytes,0x98+28,0x600000);Put32(bytes,0x230,0x603000);Put32(bytes,0x283,0x603004);for(unsigned slot=0;slot<12;++slot)Put32(bytes,0x1200+4*slot,0x601400+slot*16);Put32(bytes,0x1214,0x601100);Put32(bytes,0x1228,0x601460);Put32(bytes,0x122c,0x601470);CHECK(Discover(bytes));
    bytes=Fixture();Put32(bytes,0x295,0);CHECK(!Discover(bytes));bytes=Fixture();Put32(bytes,0x230,0x401000);CHECK(!Discover(bytes));
    bytes=Fixture();Put32(bytes,0x660,0xc31c418b);CHECK(!Discover(bytes));bytes=Fixture();Put32(bytes,0x1214,0x401200);CHECK(!Discover(bytes));
    bytes=Fixture();std::copy_n(bytes.begin()+0x220,32,bytes.begin()+0x500);CHECK(!Discover(bytes)); // duplicate signature fails closed
    bytes=Fixture();auto pe=engine::InspectPe(bytes);pe.image.sections[0].rawSize=0xffffffff;CHECK(!bc2::DiscoverProfile(bytes,pe.image));
    pe=engine::InspectPe(bytes);pe.image.imageSize++;CHECK(!bc2::DiscoverProfile(bytes,pe.image));
    auto world=WorldFixture();const auto chain=RenderPath(world);CHECK(chain&&chain->worldRender==0x1600&&chain->prepareView==0x1800&&chain->drawView==0x1880&&chain->updateViewCache==0x1900&&chain->subsystemVtable==0x20c0);
    Put32(world,0x929,0);CHECK(!RenderPath(world));world=WorldFixture();world[0x97b]=std::byte{1};CHECK(!RenderPath(world));
    world=WorldFixture();Put32(world,0xa92,0x403004);CHECK(!RenderPath(world));world=WorldFixture();world[0xcd7]=std::byte{0xff};CHECK(!RenderPath(world));
    world=WorldFixture();std::copy_n(world.begin()+0x1280,48,world.begin()+0x1300);CHECK(!RenderPath(world));
    auto views=ViewFixture();const auto layout=ViewLayout(views);CHECK(layout&&layout->vtable==0x2080&&layout->copyRenderView==0x1800&&layout->primaryOffset==0x90&&layout->activeOffset==0xc52);
    Put32(views,0x817,0);CHECK(!ViewLayout(views));views=ViewFixture();views[0xc12]=std::byte{0x80};CHECK(!ViewLayout(views));
    views=ViewFixture();views[0xbc6]=std::byte{8};CHECK(!ViewLayout(views));views=ViewFixture();views[0xc02]=std::byte{0x74};CHECK(!ViewLayout(views));
    views=ViewFixture();std::copy_n(views.begin()+0x1280,19*4,views.begin()+0x1300);CHECK(ViewLayout(views)); // inherited vtables do not override the concrete constructor
    views=ViewFixture();Put32(views,0xd1b,0x402084);CHECK(!ViewLayout(views));
    views=ViewFixture();std::copy_n(views.begin()+0xd00,43,views.begin()+0xf00);CHECK(!ViewLayout(views));
    auto cache=CacheFixture();const auto cacheProfile=Caches(cache);CHECK(cacheProfile&&cacheProfile->updateView==0x1600&&cacheProfile->updateProjection==0x1800&&cacheProfile->updateFrustum==0x1a00);
    Put32(cache,0x729,0);CHECK(!Caches(cache));cache=CacheFixture();cache[0xc7e]=std::byte{0xfe};CHECK(!Caches(cache));
    cache=CacheFixture();std::copy_n(cache.begin()+0xa00,22,cache.begin()+0xd00);CHECK(!Caches(cache));
    auto visibility=VisibilityFixture();const auto vis=Visibility(visibility);CHECK(vis&&vis->worldUpdate==0x2600&&vis->prepareVisibility==0x2e00);
    Put32(visibility,0x1b84,0);CHECK(!Visibility(visibility));visibility=VisibilityFixture();visibility[0x4ea1]=std::byte{0x10};CHECK(!Visibility(visibility));
    visibility=VisibilityFixture();Put32(visibility,0x528c,0x402610);CHECK(!Visibility(visibility));
    auto life=LifecycleFixture();const auto ownership=Lifecycle(life);CHECK(ownership&&ownership->createView==0x2000&&ownership->requestVtable==0x31a0&&ownership->destructor==0x2140);
    Put32(life,0x121d,0);CHECK(!Lifecycle(life));life=LifecycleFixture();life[0x1394]=std::byte{0xf8};CHECK(!Lifecycle(life));
    life=LifecycleFixture();life[0x12fc]=std::byte{8};CHECK(!Lifecycle(life));life=LifecycleFixture();Put32(life,0x2284,0x402060);CHECK(!Lifecycle(life));
    life=LifecycleFixture();std::copy_n(life.begin()+0x23a0,20,life.begin()+0x2320);CHECK(!Lifecycle(life));
    auto callbacks=CallbackFixture();const auto cb=Callbacks(callbacks);CHECK(cb&&cb->rememberMain==0x1600&&cb->registerView==0x1700&&cb->unregisterView==0x1800);
    callbacks[0x831]=std::byte{0xb8};CHECK(!Callbacks(callbacks));callbacks=CallbackFixture();std::copy_n(callbacks.begin()+0x800,59,callbacks.begin()+0xb00);CHECK(!Callbacks(callbacks));
    auto init=InitializationFixture();const auto initialization=Initialization(init);CHECK(initialization&&initialization->rebuild==0x3e00&&initialization->parentGetter==0x4900&&initialization->refreshRegistered==0x4a00);
    init[0x37b5]=std::byte{0x88};CHECK(!Initialization(init));init=InitializationFixture();Put32(init,0x5298,0x403e10);CHECK(!Initialization(init));
    init=InitializationFixture();init[0x3a25]=std::byte{0xc2};CHECK(!Initialization(init));
    init=InitializationFixture();init[0x3d62]=std::byte{8};CHECK(!Initialization(init));
    return 0;
}