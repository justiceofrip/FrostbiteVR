// Offline metadata exporter. Link against the normal BC2Camera/FvrCore libraries;
// never substitute a separately parsed/generated registration table here.
#include "Bc2FeedEquipmentProfile.h"
#include "Bc2FeedAmmoSupply.h"
#include "fvr/interaction/BodyAnchors.h"
#include <iomanip>
#include <iostream>
#include <locale>
#include <stdexcept>

namespace {
using namespace fvr;
using namespace fvr::bc2;
using namespace fvr::interaction;
void Text(std::string_view s) {
    std::cout << '"';
    for (const unsigned char c : s) {
        if (c == '"' || c == '\\') std::cout << '\\' << char(c);
        else if (c < 32) std::cout << "\\u00" << std::hex << std::setw(2)
            << std::setfill('0') << unsigned(c) << std::dec;
        else std::cout << char(c);
    }
    std::cout << '"';
}
void Key(std::uint64_t n) {
    std::cout << "\"0x" << std::hex << n << std::dec << '"';
}
void Matrix(const math::Matrix4& m) {
    std::cout << '[';
    for (unsigned r=0;r<4;++r) {
        if(r)std::cout << ',';std::cout << '[';
        for(unsigned c=0;c<4;++c){if(c)std::cout << ',';
            if(!std::isfinite(m.values[r][c]))throw std::runtime_error("nonfinite matrix");
            std::cout << m.values[r][c];}
        std::cout << ']';
    }
    std::cout << ']';
}
void Bone(const Bc2FeedChainBone& b) {
    std::cout << "{\"bone\":";Text(b.bone);std::cout << ",\"parent\":";Text(b.parent);
    std::cout << ",\"index\":" << b.index << ",\"parentIndex\":" << b.parentIndex << '}';
}
void SupplyConfig() {
    const auto c=Bc2FeedSupplyConfig();
    const auto contact=[](const AmmoSupplyContact& p) {
        std::cout << "{\"centerMeters\":[" << p.centerMeters[0] << ',' << p.centerMeters[1]
            << ',' << p.centerMeters[2] << "],\"radiusMeters\":" << p.radiusMeters << '}';
    };
    std::cout << "{\"source\":\"Bc2FeedSupplyConfig / BodyAnchorHandPose\",\"hand\":" << unsigned(c.hand)
        << ",\"itemNamespace\":";Key(c.itemNamespace);
    std::cout << ",\"pouch\":{\"id\":";Key(c.pouch.id);
    std::cout << ",\"generation\":" << c.pouch.generation << "},\"maxSourceLifetimeNs\":" << c.maxSourceLifetimeNs;
    std::cout << ",\"chest\":";contact({c.pouchCenterMeters,c.pouchRadiusMeters});
    std::cout << ",\"alternate\":";if(c.alternateContact)contact(*c.alternateContact);else std::cout << "null";
    const auto admitted=[](const std::array<float,3>& center) {
        math::Matrix4 m{};for(unsigned i=0;i<4;++i)m.values[i][i]=1;
        for(unsigned i=0;i<3;++i)m.values[3][i]=center[i];
        return Bc2FeedSupplyContact(m);
    };
    if(!admitted(c.pouchCenterMeters)||(c.alternateContact&&!admitted(c.alternateContact->centerMeters)))
        throw std::runtime_error("production supply centers refused by entry predicate");
    std::cout << ",\"productionContactCentersVerified\":true";
    // A synthetic forward oracle from the production body-frame function.
    // This lets offline tests check the inverse against production geometry,
    // rather than only round-tripping the Python implementation against itself.
    InputFrame input;input.generation=1;input.spaceGeneration=1;input.predictedNs=1;
    input.focused=input.headValid=true;input.worldUnitsPerMeter=1;
    input.referenceHead.position={-.7f,1.4f,.2f};input.head.position={1.2f,1.6f,-.8f};
    const auto n=std::sqrt(.01f+.04f+.09f+.81f);
    input.referenceHead.orientation={.1f/n,.2f/n,.3f/n,.9f/n};
    input.hands[0].gripTracked=true;input.hands[0].grip.position={.37f,1.1f,-.42f};
    input.hands[0].grip.orientation={0,0,0,1};
    const auto sample=BodyAnchorHandPose(input,InteractionHand::Left);
    if(!sample)throw std::runtime_error("production body-frame probe refused");
    const auto pose=[](const math::Pose& p) {
        std::cout << "{\"position\":[" << p.position.x << ',' << p.position.y << ',' << p.position.z
            << "],\"orientation\":[" << p.orientation.x << ',' << p.orientation.y << ',' << p.orientation.z
            << ',' << p.orientation.w << "]}";
    };
    std::cout << ",\"bodyFrameProbe\":{\"synthetic\":true,\"referenceHead\":";pose(input.referenceHead);
    std::cout << ",\"head\":";pose(input.head);std::cout << ",\"grip\":";pose(input.hands[0].grip);
    std::cout << ",\"productionBodyFromHand\":";Matrix(*sample);std::cout << "}}";
}
template<class Policy> void Caps(const Policy& p) {
    std::cout << "{\"completeFraction\":" << p.completeFraction
        << ",\"releaseFraction\":" << p.releaseFraction
        << ",\"pathToleranceMeters\":" << p.pathToleranceMeters
        << ",\"orientationToleranceRadians\":" << p.orientationToleranceRadians
        << ",\"maxStepMeters\":" << p.maxStepMeters
        << ",\"maxStepRadians\":" << p.maxStepRadians
        << ",\"endpointDwellNs\":" << p.endpointDwellNs
        << ",\"maximumGestureNs\":" << p.maximumGestureNs
        << ",\"maxInputGapNs\":" << p.maxInputGapNs << '}';
}
void Motion(const FeedMotionProfile& m) {
    std::cout << "{\"kind\":";Text(m.kind==FeedMotionKind::Hinge?"Hinge":
        m.kind==FeedMotionKind::FreePlacement?"FreePlacement":"Linear");
    std::cout << ",\"measured\":" << m.measured << ",\"returnToStart\":" << m.returnToStart
        << ",\"travel\":" << m.travel << ",\"policy\":";Caps(m);
    std::cout << ",\"weaponFromMotion\":";Matrix(m.weaponFromMotion);
    std::cout << ",\"motionFromContact\":";Matrix(m.motionFromContact);
    std::cout << ",\"placement\":{\"weaponFromSource\":";Matrix(m.placement.weaponFromSource);
    std::cout << ",\"weaponFromDestination\":";Matrix(m.placement.weaponFromDestination);
    std::cout << ",\"sourceFromGrip\":";Matrix(m.placement.sourceFromGrip);std::cout << "}}";
}
void Geometry(const Bc2FeedGeometry& g) {
    constexpr std::string_view names[]{"Resting","PouchDetachSource","PouchWithdrawn",
        "PouchAttachSource","PouchAttachedFeedUnseated","FeedSeatSource","FeedSeated"};
    std::cout << "{\"chainCount\":" << g.chainCount << ",\"coverTravelRadians\":" << g.coverTravelRadians;
    std::cout << ",\"weaponFromHinge\":";Matrix(g.weaponFromHinge);
    std::cout << ",\"hingeFromOpeningContact\":";Matrix(g.hingeFromOpeningContact);
    std::cout << ",\"hingeFromClosingContact\":";Matrix(g.hingeFromClosingContact);
    std::cout << ",\"weaponFromCoverReference\":";Matrix(g.weaponFromCoverReference);
    std::cout << ",\"rootBone\":";Bone(g.rootBone);std::cout << ",\"pouchBone\":";Bone(g.pouchBone);
    std::cout << ",\"coverBone\":";Bone(g.coverBone);std::cout << ",\"chain\":[";
    for(unsigned i=0;i<g.chainCount;++i){if(i)std::cout << ',';Bone(g.chain[i]);}
    std::cout << "],\"coverCompanions\":[";
    for(unsigned i=0;i<g.coverCompanionCount;++i){if(i)std::cout << ',';
        std::cout << "{\"bone\":";Bone(g.coverCompanions[i].bone);
        std::cout << ",\"weaponFromReference\":";Matrix(g.coverCompanions[i].weaponFromReference);std::cout << '}';}
    std::cout << "],\"poses\":{";
    for(unsigned i=0;i<g.poses.size();++i){if(i)std::cout << ',';Text(names[i]);const auto& p=g.poses[i];
        std::cout << ":{\"authoredSeconds\":" << p.authoredSeconds << ",\"chainCount\":" << p.chainCount
            << ",\"weaponFromPouch\":";Matrix(p.weaponFromPouch);
        std::cout << ",\"weaponFromFeedRoot\":";Matrix(p.weaponFromFeedRoot);
        std::cout << ",\"sourceFromWrist\":";Matrix(p.sourceFromWrist);
        std::cout << ",\"parentFromChainChild\":[";
        for(unsigned j=0;j+1<p.chainCount;++j){if(j)std::cout << ',';Matrix(p.parentFromChainChild[j]);}
        std::cout << "]}";}
    std::cout << "}}";
}
void Profile(const Bc2FeedRegistration& r) {
    const auto& g=r.geometry;const auto& c=r.config.values;
    std::cout << "{\"key\":";Key(r.key);std::cout << ",\"profileDigest\":";Text(r.profileDigest);
    std::cout << ",\"asset\":";Text(g.asset);std::cout << ",\"configurationPath\":";Text(g.configurationPath);
    std::cout << ",\"meshPath\":";Text(g.meshPath);std::cout << ",\"rigFingerprint\":";Key(g.rigFingerprint);
    std::cout << ",\"sourceHashes\":{\"mesh\":";Text(g.meshSha256);std::cout << ",\"lod\":";Text(g.lodSha256);
    std::cout << ",\"skeleton\":";Text(g.skeletonSha256);std::cout << ",\"clip\":";Text(g.clipSha256);
    std::cout << "},\"configValues\":{\"fireLogicType\":" << c.fireLogicType << ",\"reloadType\":" << c.reloadType
        << ",\"fireInputAction\":" << c.fireInputAction << ",\"reloadInputAction\":" << c.reloadInputAction
        << ",\"baseCapacity\":" << c.baseCapacity << ",\"numberOfMagazines\":" << c.numberOfMagazines
        << ",\"reloadDelay\":" << c.reloadDelay << ",\"reloadTime\":" << c.reloadTime
        << ",\"reloadThreshold\":" << c.reloadThreshold << ",\"postReloadTime\":" << c.postReloadTime
        << ",\"boltDelay\":" << c.boltDelay << ",\"boltTime\":" << c.boltTime
        << ",\"holdBoltUntilFireRelease\":" << c.holdBoltUntilFireRelease
        << ",\"holdBoltUntilZoomRelease\":" << c.holdBoltUntilZoomRelease << "},\"policy\":";Caps(r.interactionPolicy);
    std::cout << ",\"mechanism\":{\"id\":";Key(r.mechanism.id);
    std::cout << ",\"revision\":" << r.mechanism.revision << ",\"maxAgeNs\":" << r.mechanism.maxAgeNs << ",\"contacts\":[";
    for(unsigned i=0;i<r.mechanism.contacts.size();++i){if(i)std::cout << ',';const auto& b=r.mechanism.contacts[i];
        std::cout << "{\"role\":" << b.role << ",\"radiusMeters\":" << b.radiusMeters
            << ",\"maxAngleRadians\":" << b.maxAngleRadians << ",\"orientation\":";
        Text(b.orientation==FeedContactOrientation::Acquired?"Acquired":"Authored");std::cout << '}';}
    std::cout << "]},\"geometry\":";Geometry(g);std::cout << ",\"operations\":{";
    constexpr std::string_view names[]{"None","ReleaseLatch","OpenCover","WithdrawFeed","DetachContainer",
        "AttachContainer","SeatFeed","CloseCover","EngageLatch","CycleAction"};
    for(unsigned i=1;i<std::size(names);++i){if(i>1)std::cout << ',';Text(names[i]);std::cout << ":{\"availability\":"
        << unsigned(Bc2FeedOperationAvailability(static_cast<FeedPhysicalOperation>(i))) << ",\"motion\":";
        const auto m=BuildBc2FeedMotion(g,static_cast<FeedPhysicalOperation>(i),r.interactionPolicy);
        if(m)Motion(*m);else std::cout << "null";std::cout << '}';}
    std::cout << "}}";
}
}
int main() {
    try {
        std::cout.imbue(std::locale::classic());std::cout << std::boolalpha << std::setprecision(17);
        const auto rows=RegisteredBc2FeedProfiles();
        if(rows.size()!=24)throw std::runtime_error("expected all 24 production registrations");
        for(const auto& r:rows)if(!ValidateBc2FeedRegistration(r)||FindBc2FeedRegistration(r.key)!=&r)
            throw std::runtime_error("invalid or ambiguous production registration");
        std::cout << "{\"schema\":\"bc2vr.feed-simulation-profiles.v1\",\"nativeAcceptance\":false,"
            "\"source\":\"production RegisteredBc2FeedProfiles and BuildBc2FeedMotion\",\"supplyConfig\":";
        SupplyConfig();std::cout << ",\"profiles\":[";
        for(std::size_t i=0;i<rows.size();++i){if(i)std::cout << ',';Profile(rows[i]);}
        std::cout << "]}\n";return std::cout?0:1;
    } catch(const std::exception& e) {std::cerr << e.what() << '\n';return 1;}
}
