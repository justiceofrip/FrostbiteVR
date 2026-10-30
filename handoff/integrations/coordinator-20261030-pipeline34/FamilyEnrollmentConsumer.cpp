// Integration consumer for the reviewed family41 enrollment. Compile the real
// registry .cpp files with the enrollment headers; do not include generated
// arrays here, since doing so would hide missing production build definitions.
// This is deterministic data/consumer evidence, not a native or headset check.
#include "Bc2FeedEquipmentProfile.h"
#include "Bc2PistolSlideProfile.h"
#include "Bc2MagazineNativeProfile.h"
#include "Bc2MagazineGeometryProfile.h"
#include "Bc2VisibilityDescriptorData.h"
#include <algorithm>
#include <array>
#include <cstdio>
#include <set>
#include <string_view>

using namespace fvr::bc2;
namespace {
#define REQUIRE(condition) do { if (!(condition)) { \
    std::fprintf(stderr,"%s:%d: %s [%.*s]\n",__FILE__,__LINE__,#condition, \
        static_cast<int>(context.size()),context.data()); return 1; } } while (false)

// Enrollment manifest from pipeline34/family-batch/Feed.candidates.h. The
// existing M60_sp row is preserved, so these are 24 total, not 24 additions.
constexpr std::array<std::string_view,24> FeedPaths{{
    "Objects/Weapons/Handheld/BU_lmg_MG3/UPG_lmg_MG3_Kobra",
    "Objects/Weapons/Handheld/BU_lmg_MG3/BU_lmg_MG3_Scoped",
    "Objects/Weapons/Handheld/MEC_lmg_QJY88/MEC_lmg_QJY88_Kobra",
    "Objects/Weapons/Handheld/BU_lmg_M60/BU_lmg_M60",
    "Objects/Weapons/Handheld/BU_lmg_M60/BU_lmg_M60_Kobra",
    "Objects/Weapons/Handheld/BU_lmg_MG3/UPG_lmg_MG3_Scoped",
    "Objects/Weapons/Handheld/BU_lmg_MG3/BU_lmg_MG3_Kobra",
    "Objects/Weapons/Handheld/BU_lmg_MG3/BU_lmg_MG3",
    "Objects/Weapons/Handheld/MEC_lmg_QJY88/MEC_lmg_QJY88",
    "Objects/Weapons/Handheld/UL_lmg_FNMinimi/UL_lmg_FNMinimi",
    "Objects/Weapons/Handheld/MEC_lmg_QJY88/MEC_lmg_QJY88_Scoped",
    "Objects/Weapons/Handheld/UL_lmg_FNMinimi/UL_lmg_FNMinimi_Kobra",
    "Objects/Weapons/Handheld/BU_lmg_MG3/UPG_lmg_MG3",
    "Objects/Weapons/Handheld/UL_lmg_FNMinimi/UL_lmg_FNMinimi_Scoped",
    "Objects/Weapons/Handheld/BU_lmg_M60/BU_lmg_M60_Scoped",
    "Objects/Weapons/Handheld/BU_lmg_MG3/SP_lmg_MG3",
    "Objects/Weapons/Handheld/MEC_lmg_QJY88/SP_lmg_QJY88_Kobra",
    "Objects/Weapons/Handheld/BU_lmg_M60/SP_lmg_M60",
    "Objects/Weapons/Handheld/BU_lmg_M60/SP_lmg_M60_Scope",
    "Objects/Weapons/Handheld/MEC_lmg_QJY88/SP_lmg_QJY88",
    "Objects/Weapons/Handheld/BU_lmg_MG3/SP_lmg_MG3_Kobra",
    "Objects/Weapons/Handheld/RU_lmg_PKM/RU_lmg_PKM_Kobra",
    "Objects/Weapons/Handheld/RU_lmg_PKM/RU_lmg_PKM_Scoped",
    "Objects/Weapons/Handheld/RU_lmg_PKM/RU_lmg_PKM"
}};
constexpr std::array<std::string_view,3> PkmPaths{{
    "Objects/Weapons/Handheld/RU_lmg_PKM/RU_lmg_PKM",
    "Objects/Weapons/Handheld/RU_lmg_PKM/RU_lmg_PKM_Kobra",
    "Objects/Weapons/Handheld/RU_lmg_PKM/RU_lmg_PKM_Scoped"
}};
struct ExpectedSlide { std::string_view asset,path; int capacity; };
// Per-configuration authored capacities from Slide.candidates.h. M9 and M9-3
// share a mesh but must retain different capacities; MP443_sp stays builtin.
constexpr std::array<ExpectedSlide,5> Slides{{
    {"M9","Objects/Weapons/Handheld/US_hg_M9Beretta/US_hg_M9Beretta",12},
    {"M9-3","Objects/Weapons/Handheld/US_hg_M9Beretta/US_hg_M9-3",20},
    {"M1911","Objects/Weapons/Handheld/Beach/M1911A1/M1911A1",7},
    {"MP443","Objects/Weapons/Handheld/RU_hg_MP443Grach/RU_hg_MP443Grach",17},
    {"MP443_sp","Objects/Weapons/Handheld/RU_hg_MP443Grach/SP_hg_MP443Grach",17}
}};

ReloadObservedConfig Observed(const ReloadConfigDescriptor& d) {
    ReloadObservedConfig c{};
    std::copy(d.assetName.begin(),d.assetName.end(),c.assetName.begin());
    std::copy(d.assetPath.begin(),d.assetPath.end(),c.assetPath.begin());
    // Synthetic aligned identities satisfy the read-only matcher contract.
    c.weaponData=0x10000;c.firingData=0x20000;c.primaryFire=0x30000;
    c.ammoAddress=c.primaryFire+0x170;
    const auto& v=d.values;
    c.fireLogicType=v.fireLogicType;c.reloadType=v.reloadType;
    c.fireInputAction=v.fireInputAction;c.reloadInputAction=v.reloadInputAction;
    c.baseCapacity=v.baseCapacity;c.numberOfMagazines=v.numberOfMagazines;
    c.reloadDelay=v.reloadDelay;c.reloadTime=v.reloadTime;c.reloadThreshold=v.reloadThreshold;
    c.postReloadTime=v.postReloadTime;c.boltDelay=v.boltDelay;c.boltTime=v.boltTime;
    c.holdBoltUntilFireRelease=v.holdBoltUntilFireRelease;
    c.holdBoltUntilZoomRelease=v.holdBoltUntilZoomRelease;
    return c;
}

int FeedEnrollment(std::set<std::uint64_t>& keys,std::set<std::uint64_t>& ids) {
    std::string_view context="global feed registry";
    const auto rows=RegisteredBc2FeedProfiles();
    REQUIRE(rows.size()==FeedPaths.size());
    std::set<std::string_view> paths,pkmPaths;
    for(const auto& row:rows) {
        context=row.config.assetPath;
        REQUIRE(ValidateBc2FeedRegistration(row));
        REQUIRE(std::find(FeedPaths.begin(),FeedPaths.end(),context)!=FeedPaths.end());
        REQUIRE(paths.insert(context).second);
        REQUIRE(keys.insert(row.key).second);
        REQUIRE(ids.insert(row.mechanism.id).second);
        const auto& g=row.geometry;
        const auto observed=Observed(row.config);
        REQUIRE(FindBc2FeedRegistration(row.key)==&row);
        REQUIRE(FindBc2FeedRegistration(observed)==&row);
        REQUIRE(FindBc2FeedRegistration(g.asset,g.configurationPath,g.meshPath,g.rigFingerprint)==&row);
        REQUIRE(FindBc2FeedRegistration(row.key,observed,g.asset,g.configurationPath,g.meshPath,g.rigFingerprint)==&row);
        if(std::find(PkmPaths.begin(),PkmPaths.end(),context)!=PkmPaths.end()) {
            REQUIRE(pkmPaths.insert(context).second);
            REQUIRE(g.asset=="PKM"&&g.coverCompanionCount==1);
            REQUIRE(g.coverCompanions[0].bone.bone=="jntWpn_11");
            REQUIRE(g.coverCompanions[0].bone.parent==g.rootBone.bone);
            REQUIRE(g.coverCompanions[0].bone.parentIndex==g.rootBone.index);
        }
    }
    REQUIRE(pkmPaths.size()==PkmPaths.size());
    const auto& builtin=bc2_feed_profile_detail::M60Registration;
    const auto* preserved=FindBc2FeedRegistration(builtin.key);
    REQUIRE(preserved&&preserved->profileDigest==builtin.profileDigest);
    REQUIRE(preserved->mechanism.id==builtin.mechanism.id);
    return 0;
}

int VisibilityJoin(const Bc2PistolSlideRegistration& slide,const MagazineGeometryProfile& magazine) {
    const auto& selected=slide.geometry->selection;
    const std::string_view context=selected.configurationPath;
    const VisibilityDescriptor* descriptor=nullptr;
    for(const auto& row:VisibilityDescriptors) {
        if(row.asset!=selected.asset||row.configurationPath!=context)continue;
        REQUIRE(!descriptor);
        descriptor=&row;
    }
    REQUIRE(descriptor);
    REQUIRE(descriptor->rigFingerprint==selected.rigFingerprint);
    REQUIRE(descriptor->weightedSectionDataVerified);
    REQUIRE(std::find(descriptor->meshes.begin(),descriptor->meshes.end(),selected.meshPath)!=descriptor->meshes.end());
    REQUIRE(std::find(descriptor->weightedNames.begin(),descriptor->weightedNames.end(),slide.geometry->slide)!=descriptor->weightedNames.end());
    REQUIRE(std::find(descriptor->weightedNames.begin(),descriptor->weightedNames.end(),magazine.bones.magazine)!=descriptor->weightedNames.end());

    // Exercise the shared visibility identity matcher without granting native
    // hide/show or input suppression to this synthetic snapshot.
    constexpr std::int64_t now=1000000000;
    SelectedMeshesSnapshot snapshot{};
    snapshot.owner={0x10000,0x20000,0x30000,0x40000,1,2,3};
    snapshot.sequence=1;snapshot.observedNs=now-1000000;snapshot.deadlineNs=now+100000000;
    snapshot.weaponData=0x50000;snapshot.meshTypeInfo=0x60000;snapshot.stateTypeInfo=0x70000;
    snapshot.stateCount=1;snapshot.soleConfiguredArray=snapshot.states[0].array=0x80000;
    REQUIRE(!descriptor->meshes.empty()&&descriptor->meshes.size()<=snapshot.states[0].meshes.size());
    REQUIRE(selected.asset.size()<snapshot.weaponName.size());
    REQUIRE(context.size()<snapshot.configurationPath.size());
    snapshot.states[0].count=static_cast<std::uint8_t>(descriptor->meshes.size());
    snapshot.configurationPathVerified=true;
    std::copy(selected.asset.begin(),selected.asset.end(),snapshot.weaponName.begin());
    std::copy(context.begin(),context.end(),snapshot.configurationPath.begin());
    for(std::size_t i=0;i<descriptor->meshes.size();++i) {
        auto& mesh=snapshot.states[0].meshes[i];
        REQUIRE(descriptor->meshes[i].size()<mesh.assetPath.size());
        mesh.address=0x90000+static_cast<std::uint32_t>(i)*0x100;
        mesh.typeInfo=snapshot.meshTypeInfo;mesh.namePointer=0xa0000+static_cast<std::uint32_t>(i)*0x100;
        std::copy(descriptor->meshes[i].begin(),descriptor->meshes[i].end(),mesh.assetPath.begin());
    }
    REQUIRE(VisibilityConfigurationMatches(snapshot,snapshot.owner,*descriptor,now));
    return 0;
}

int SlideEnrollment(std::set<std::uint64_t>& keys,std::set<std::uint64_t>& ids) {
    std::string_view context="global slide registry";
    const auto rows=RegisteredBc2PistolSlideProfiles();
    REQUIRE(rows.size()==Slides.size());
    std::set<std::string_view> paths;
    std::set<NativeMagazineProfileId> magazineIds;
    for(const auto& row:rows) {
        context=row.configuration.assetPath;
        REQUIRE(Bc2PistolSlideRegistrationValid(row));
        const auto expected=std::find_if(Slides.begin(),Slides.end(),[&](const auto& value){return value.path==context;});
        REQUIRE(expected!=Slides.end());
        REQUIRE(row.configuration.assetName==expected->asset);
        REQUIRE(row.configuration.values.baseCapacity==expected->capacity);
        REQUIRE(paths.insert(context).second);
        REQUIRE(keys.insert(row.key).second);
        REQUIRE(ids.insert(row.interaction->id).second);
        const auto& selected=row.geometry->selection;
        const auto observed=Observed(row.configuration);
        REQUIRE(ResolveBc2PistolSlideRegistration(row.key)==&row);
        REQUIRE(FindBc2PistolSlideRegistration(observed)==&row);
        REQUIRE(FindBc2PistolSlideRegistration(row.configuration)==&row);
        REQUIRE(FindBc2PistolSlideRegistration(selected)==&row);
        REQUIRE(FindBc2PistolSlideGeometry(selected)==row.geometry);
        REQUIRE(BuildBc2PistolSlideProfile(selected));
        const auto* magazine=FindMagazineNativeProfile(observed);
        REQUIRE(magazine&&magazine->enabled&&magazine->profile);
        REQUIRE(magazineIds.insert(magazine->id).second);
        REQUIRE(ResolveMagazineNativeProfile(magazine->id)==magazine->profile);
        REQUIRE(magazine->profile->Reviewed()&&magazine->profile->Matches(observed));
        REQUIRE(magazine->profile->configuration.values.baseCapacity==expected->capacity);
        const auto* geometry=FindMagazineGeometry(*magazine->profile);
        REQUIRE(geometry&&MagazineGeometryMatchesConfiguration(*geometry,*magazine->profile));
        REQUIRE(geometry->asset==selected.asset&&geometry->configurationPath==context);
        REQUIRE(geometry->mesh==selected.meshPath&&geometry->rigFingerprint==selected.rigFingerprint);
        if(VisibilityJoin(row,*geometry))return 1;
    }
    const auto& builtin=bc2_pistol_slide_profile_detail::Mp443Registration;
    const auto* preserved=ResolveBc2PistolSlideRegistration(builtin.key);
    REQUIRE(preserved&&preserved->profileDigest==builtin.profileDigest);
    REQUIRE(preserved->interaction->id==builtin.interaction->id);
    return 0;
}
}

int main() {
    std::set<std::uint64_t> keys,ids;
    if(FeedEnrollment(keys,ids)||SlideEnrollment(keys,ids))return 1;
    std::puts("Family enrollment consumer passed: 24 feed rows, three PKM companions, five slide/magazine/visibility joins. Deterministic evidence only.");
    return 0;
}
