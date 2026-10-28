#include "Test.h"
#include "Bc2ResolutionBinding.h"
#include "Bc2ResolutionRequest.h"
#include <cstring>
#include <fstream>
#include <iterator>
#include <unordered_map>
using namespace fvr;
namespace {
bc2::ResolutionBinding Binding(){bc2::ResolutionBinding b{};
    b.renderer.preferredBase=0x400000;b.renderer.rendererGlobal=0x1000;b.renderer.rendererVtable=0x1010000;b.renderer.present=0x5eea30;
    b.menu.managerGlobal=0x15a4ca8;b.registryGlobal=0x115cfdc;b.setWindowSize=0x3da860;b.setFullscreen=0x159560;
    b.getField=0x0f4c40;b.applySettings=0x5e1860;b.optionsGlobal=0x1170ea0;b.optionsVtable=0x102e570;
    b.setOptionsSize=0x3bc0a0;b.setOptionsFullscreen=0x3bc0c0;b.dispatcher=0x55e680;b.managerVtable=0x1040830;return b;
}
struct Memory {
    static constexpr unsigned Manager=0x2000000,Menu=0x2100000,Renderer=0x2200000,Settings=0x2300000,Registry=0x2400000,Options=0x2600000;
    std::unordered_map<unsigned,unsigned char> bytes;unsigned maxRead=0,rendererReads=0,optionsReads=0,readCalls=0;bool changeOwner=false,changeOptions=false;
    void Put(unsigned at,unsigned value,unsigned n=4){for(unsigned i=0;i<n;++i)bytes[at+i]=static_cast<unsigned char>(value>>(i*8));}
    void Float(unsigned at,float f){unsigned bits=0;std::memcpy(&bits,&f,4);Put(at,bits);}
    Memory(){Put(0x19a4ca8,Manager);Put(Manager,0x1440830);
        Put(Manager+0x82af0,0x1440810);Put(Manager+0x82af8,0x1440804);Put(Manager+0x82afc,0x14407f0);
        Put(Manager+0x82b20,0);Put(Manager+0x82b24,Menu);
        Put(Menu,0x142b4e8);Put(Menu+0x2c,0x142b4d4);Put(Menu+0x90,0x142b4c8);
        Put(0x401000,Renderer);Put(Renderer,0x1410000);Put(0x1410000+20,0x9eea30);Put(0x1410000+40,0x9d8180);Put(0x1410000+44,0x9d8190);
        Put(0x1410000+0x90,0x9e1860);
        Put(Renderer+0x18,1920);Put(Renderer+0x1c,1080);Put(Renderer+0x40,Settings);Put(Renderer+0x88,0x2500000);Put(0x155cfdc,Registry);
        Float(Settings+0x0c,1920);Float(Settings+0x10,1080);Put(Settings+0x35,0,1);
        Put(0x1570ea0,Options);Put(Options,0x142e570);Float(Options+0xa0,1920);Float(Options+0xa4,1080);
        Put(Options+0xac,0,1);Put(Options+0xc0,0,1);
    }
    bc2::MenuMemory View(){return {this,[](void* context,unsigned at,void* dest,std::size_t n){auto& m=*static_cast<Memory*>(context);
        ++m.readCalls;
        if(n>m.maxRead)m.maxRead=unsigned(n);
        if(at==0x401000&&++m.rendererReads==2&&m.changeOwner)m.Put(at,Renderer+4);
        if(at==0x1570ea0&&++m.optionsReads==2&&m.changeOptions)m.Put(at,Options+4);
        for(unsigned i=0;i<n;++i){const auto f=m.bytes.find(at+i);if(f==m.bytes.end())return false;static_cast<unsigned char*>(dest)[i]=f->second;}return true;
    }};}
    auto Read(){return bc2::ReadResolutionSnapshot(View(),Binding(),0x400000);}
};
int Policy(){
    CHECK(bc2::ValidResolutionSize({2784,3024}));CHECK(bc2::ValidResolutionSize({16000,2000}));
    for(const auto s:{bc2::ResolutionSize{0,1080},{1920,-1},{1920.5f,1080},{16385,1},{8192,8192}})CHECK(!bc2::ValidResolutionSize(s));
    Memory valid;const auto s=valid.Read();CHECK(s&&s->width==1920&&s->height==1080&&!s->fullscreen);CHECK(valid.maxRead<=8);
    for(unsigned bad=0;bad<20;++bad){Memory m;
        switch(bad){
        case 0:m.Put(Memory::Manager+0x82b20,Memory::Menu);break;
        case 1:m.Put(Memory::Menu,0);break;
        case 2:m.Put(Memory::Renderer,0);break;
        case 3:m.Put(0x1410000+40,0);break;
        case 4:m.Put(Memory::Renderer+0x40,0);break;
        case 5:m.Put(Memory::Renderer+0x88,0);break;
        case 6:m.Float(Memory::Settings+0x0c,1920.5f);break;
        case 7:m.Put(Memory::Settings+0x35,2,1);break;
        case 8:m.Put(Memory::Renderer+0x18,0);break;
        case 9:m.changeOwner=true;break;
        case 10:m.bytes.erase(Memory::Settings+0x10);break;
        case 11:m.Put(0x155cfdc,0);break;
        case 12:m.Put(0x1410000+0x90,0x9e1861);break;
        case 13:m.Put(0x1570ea0,0);break;
        case 14:m.Put(Memory::Options,0x142e571);break;
        case 15:m.Float(Memory::Options+0xa0,1920.5f);break;
        case 16:m.Put(Memory::Options+0xac,2,1);break;
        case 17:m.Put(Memory::Options+0xc0,2,1);break;
        case 18:m.changeOptions=true;break;
        case 19:m.bytes.erase(Memory::Options+0xa4);break;
        }CHECK(!m.Read());
    }
    auto other=*s;other.settings++;CHECK(!other.SameOwner(*s));
    other=*s;other.options++;CHECK(!other.SameOwner(*s));
    CHECK(s->options==Memory::Options&&!s->optionsFullscreen&&!s->optionsDirty&&s->optionsDesired==s->desired);
    for(unsigned bad=0;bad<4;++bad){auto binding=Binding();Memory m;
        switch(bad){case 0:++binding.optionsGlobal;break;case 1:++binding.optionsVtable;break;
            case 2:++binding.setOptionsSize;break;case 3:++binding.setOptionsFullscreen;break;}
        CHECK(!bc2::ReadResolutionSnapshot(m.View(),binding,0x400000));
    }
    // The actual regression: a new renderer size with old upstream options is
    // a partial operation, not success. Native Apply would restore 1920x1080.
    const bc2::ResolutionSize target{2784,3024};
    CHECK(bc2::ResolutionSettingsMatch(*s,s->desired,false));
    other=*s;other.desired=target;
    CHECK(!bc2::ResolutionSettingsMatch(other,target,false));
    CHECK(bc2::CanRestoreResolutionSettings(other,*s,target));
    other=*s;other.optionsDesired=target;other.optionsDirty=true;
    CHECK(!bc2::ResolutionSettingsMatch(other,target,false));
    CHECK(bc2::CanRestoreResolutionSettings(other,*s,target));
    other.desired=target;CHECK(bc2::ResolutionSettingsMatch(other,target,false));
    CHECK(bc2::CanRestoreResolutionSettings(other,*s,target));
    other.optionsDesired={1280,720};CHECK(!bc2::CanRestoreResolutionSettings(other,*s,target));
    other=*s;other.desired={1280,720};CHECK(!bc2::CanRestoreResolutionSettings(other,*s,target));
    other=*s;other.options++;CHECK(!bc2::CanRestoreResolutionSettings(other,*s,target));
    other=*s;other.optionsFullscreen=true;CHECK(!bc2::CanRestoreResolutionSettings(other,*s,target));
    // Fullscreen originals may be partially changed to windowed and restored,
    // but a mismatched original source cannot authorize any restoration.
    auto original=*s;original.fullscreen=original.optionsFullscreen=true;
    other=original;other.optionsFullscreen=false;other.optionsDesired=target;
    CHECK(bc2::CanRestoreResolutionSettings(other,original,target));
    original.optionsDesired={1280,720};CHECK(!bc2::CanRestoreResolutionSettings(other,original,target));
    CHECK(!bc2::CanRestoreResolutionSettings(*s,*s,{0,3024}));
    return 0;
}
int GuardPolicy(){
    using bc2::ResolutionGuardPhase;
    Memory m;const auto expected=bc2::ReadResolutionGuardOwner(m.View(),Binding(),0x400000);
    CHECK(expected&&m.readCalls==16&&m.maxRead==4);
    // The real object has multiple interface tables. Checking its secondary
    // table as the primary passed the old synthetic fixture but rejected BC2.
    Memory secondaryAsPrimary;secondaryAsPrimary.Put(Memory::Manager,0x14407f0);
    CHECK(secondaryAsPrimary.Read());
    CHECK(!bc2::ReadResolutionGuardOwner(secondaryAsPrimary.View(),Binding(),0x400000));
    auto secondaryBinding=Binding();secondaryBinding.managerVtable=0x10407f0;Memory actualPrimary;
    CHECK(!bc2::ReadResolutionGuardOwner(actualPrimary.View(),secondaryBinding,0x400000));CHECK(!actualPrimary.readCalls);
    CHECK(bc2::BlockResolutionEvent("changeResolution",ResolutionGuardPhase::Prepared,true,*expected,*expected));
    CHECK(bc2::BlockResolutionEvent("changeResolution",ResolutionGuardPhase::Active,true,*expected,*expected));
    for(const auto phase:{ResolutionGuardPhase::Inactive,ResolutionGuardPhase::Releasing,ResolutionGuardPhase::Released})
        CHECK(!bc2::BlockResolutionEvent("changeResolution",phase,true,*expected,*expected));
    for(const auto name:{"","changeResolutionExtra","ChangeResolution","changeFullScreen","newSP","pauseGame"})
        CHECK(!bc2::BlockResolutionEvent(name,ResolutionGuardPhase::Active,true,*expected,*expected));
    CHECK(!bc2::BlockResolutionEvent(std::string_view("changeResolution\0extra",22),ResolutionGuardPhase::Active,true,*expected,*expected));
    CHECK(!bc2::BlockResolutionEvent("changeResolution",ResolutionGuardPhase::Active,false,*expected,*expected));
    for(unsigned changed=0;changed<5;++changed){auto other=*expected;
        switch(changed){case 0:++other.manager;break;case 1:++other.renderer;break;case 2:++other.options;break;
            case 3:++other.settings;break;case 4:++other.registry;break;}
        CHECK(!bc2::BlockResolutionEvent("changeResolution",ResolutionGuardPhase::Active,true,*expected,other));
    }
    CHECK(!bc2::BlockResolutionEvent("changeResolution",ResolutionGuardPhase::Active,true,{},{}));
    // Campaign/menu transitions and an in-progress native resize do not change
    // the ownership key. No width/height, menu, dirty or swapchain reads occur.
    Memory campaign;campaign.Put(Memory::Manager+0x82b24,0);campaign.Put(Memory::Manager+0x82b20,Memory::Menu+32);
    campaign.Put(Memory::Renderer+0x18,0);campaign.Put(Memory::Renderer+0x88,0);
    campaign.bytes.erase(Memory::Options+0xa0);campaign.bytes.erase(Memory::Settings+0x0c);
    const auto afterContinue=bc2::ReadResolutionGuardOwner(campaign.View(),Binding(),0x400000);
    CHECK(afterContinue&&*afterContinue==*expected&&campaign.readCalls==16);
    for(unsigned bad=0;bad<6;++bad){Memory changed;
        switch(bad){case 0:changed.changeOwner=true;break;case 1:changed.changeOptions=true;break;
            case 2:changed.Put(Memory::Manager,0x1440834);break;case 3:changed.Put(Memory::Options,0);break;
            case 4:changed.Put(Memory::Renderer+0x40,0);break;case 5:changed.bytes.erase(0x155cfdc);break;}
        CHECK(!bc2::ReadResolutionGuardOwner(changed.View(),Binding(),0x400000));
    }
    auto binding=Binding();++binding.dispatcher;Memory wrongBinding;
    CHECK(!bc2::ReadResolutionGuardOwner(wrongBinding.View(),binding,0x400000));CHECK(!wrongBinding.readCalls);
    const bc2::ResolutionLease lease{41,42,111,222,{1,2,3,4}};
    CHECK(bc2::ValidResolutionLease(lease));CHECK(lease==lease);
    for(unsigned changed=0;changed<8;++changed){auto other=lease;
        switch(changed){case 0:++other.gamePid;break;case 1:++other.ownerPid;break;case 2:++other.gameCreation;break;
            case 3:++other.ownerCreation;break;default:++other.token[changed-4];break;}
        CHECK(other!=lease);
    }
    for(unsigned bad=0;bad<6;++bad){auto invalid=lease;
        switch(bad){case 0:invalid.gamePid=0;break;case 1:invalid.ownerPid=0;break;case 2:invalid.ownerPid=invalid.gamePid;break;
            case 3:invalid.gameCreation=0;break;case 4:invalid.ownerCreation=0;break;case 5:invalid.token={};break;}
        CHECK(!bc2::ValidResolutionLease(invalid));
    }
    return 0;
}
int ActualFile(const char* path){
    std::ifstream input(path,std::ios::binary);CHECK(input);std::vector<char> raw((std::istreambuf_iterator<char>(input)),{});
    std::vector<std::byte> bytes(raw.size());std::memcpy(bytes.data(),raw.data(),raw.size());
    const auto pe=engine::InspectPe(bytes);CHECK(pe.valid);const auto binding=bc2::DiscoverResolutionBinding(bytes,pe.image);CHECK(binding);
    CHECK(binding->managerVtable==0x1040830&&binding->menu.updateSlot==binding->managerVtable+0x30);
    for(const auto va:{0x7da860u,0x7da88eu,0x7da8adu,0x7da8d7u,0x4f4c00u,0x4f4c40u,0x7de38fu,
                      0x9e1860u,0x9e18d1u,0x9eeb34u,0x559560u,0x559587u,0x7de40du,0x7de53cu,0x1449830u,0x142fde0u,
                      0x7bc0a0u,0x7bc0c0u,0x960fcfu,0x56ade9u,0x7de030u,0x5d1685u,0x7e44d9u,
                      0x95e680u,0x961008u,0x963da1u,0x955a18u,0x955a2eu,0x957f6du,0x957f83u,0x1440830u,0x1440860u}){
        std::size_t at=bytes.size();const auto rva=va-0x400000;
        for(const auto& section:pe.image.sections)if(rva>=section.rva&&rva-section.rva<section.rawSize)at=section.rawOffset+rva-section.rva;
        CHECK(at<bytes.size());bytes[at]^=std::byte{1};CHECK(!bc2::DiscoverResolutionBinding(bytes,pe.image));bytes[at]^=std::byte{1};
    }
    return 0;
}
}
int main(int argc,char** argv){const auto status=Policy()+GuardPolicy()+(argc==2?ActualFile(argv[1]):0);
    if(!status)std::puts("Resolution binding and original-owner checks passed (no native calls)");return status;}
