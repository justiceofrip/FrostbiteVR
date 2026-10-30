#include "Bc2FeedEquipmentProfile.h"
#ifdef FVR_BC2_FEED_REGISTRY_HEADER
#include FVR_BC2_FEED_REGISTRY_HEADER
#endif

namespace fvr::bc2 {
namespace {
struct FeedRegistryTable {
    std::array<Bc2FeedRegistration,MaxGeneratedBc2FeedProfiles+1> rows{};
    std::array<bool,MaxGeneratedBc2FeedProfiles+1> valid{};
    std::size_t count=1;
    FeedRegistryTable()noexcept {
        rows[0]=bc2_feed_profile_detail::M60Registration;
#ifdef FVR_BC2_FEED_REGISTRY_HEADER
        if(generated::GeneratedFeedProfiles.size()>MaxGeneratedBc2FeedProfiles){count=0;return;}
        for(const auto& row:generated::GeneratedFeedProfiles)rows[count++]=row;
#endif
        const std::span<const Bc2FeedRegistration> entries{rows.data(),count};
        for(std::size_t i=0;i<count;++i)valid[i]=SelectBc2FeedRegistration(entries,rows[i].key)==&rows[i];
    }
};
const FeedRegistryTable& Registry()noexcept {
    static const FeedRegistryTable table;
    return table;
}
template<class Predicate> const Bc2FeedRegistration* Find(Predicate matches)noexcept {
    const auto& table=Registry();const Bc2FeedRegistration* found=nullptr;
    for(std::size_t i=0;i<table.count;++i)if(matches(table.rows[i])){
        // Invalid or unreviewed competing rows cannot be ignored to make an
        // otherwise ambiguous configuration resolve by array order.
        if(found||!table.valid[i])return nullptr;found=&table.rows[i];
    }
    return found;
}
}
std::span<const Bc2FeedRegistration> RegisteredBc2FeedProfiles()noexcept {
    const auto& table=Registry();return {table.rows.data(),table.count};
}
const Bc2FeedRegistration* FindBc2FeedRegistration(std::uint64_t key)noexcept {
    return key?Find([&](const auto& row){return row.key==key;}):nullptr;
}
const Bc2FeedRegistration* FindBc2FeedRegistration(const ReloadObservedConfig& config)noexcept {
    return Find([&](const auto& row){return MatchesReloadDescriptor(config,row.config);});
}
const Bc2FeedRegistration* FindBc2FeedRegistration(std::string_view asset,std::string_view configuration,
    std::string_view mesh,std::uint64_t rig)noexcept {
    return Find([&](const auto& row){return bc2_feed_profile_detail::ExactGeometry(row,asset,configuration,mesh,rig);});
}
}
