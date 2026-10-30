#include "Bc2PistolSlideProfile.h"
// Only this translation unit includes generated rows. Consumers share one
// immutable registry even when compiled with different private build flags.
#ifdef FVR_BC2_PISTOL_SLIDE_REGISTRY_HEADER
#include FVR_BC2_PISTOL_SLIDE_REGISTRY_HEADER
#endif

namespace fvr::bc2 {
std::span<const Bc2PistolSlideRegistration> RegisteredBc2PistolSlideProfiles()noexcept {
    struct Table {std::array<Bc2PistolSlideRegistration,MaxGeneratedBc2PistolSlideProfiles+1> rows{};std::size_t count=1;};
    static const Table table=[] {
        Table out;out.rows[0]=bc2_pistol_slide_profile_detail::Mp443Registration;
#ifdef FVR_BC2_PISTOL_SLIDE_REGISTRY_HEADER
        // An oversized input invalidates the complete registry; it is never
        // silently truncated and cannot fall back to the builtin row.
        if(generated::GeneratedPistolSlideProfiles.size()>MaxGeneratedBc2PistolSlideProfiles){out.count=0;return out;}
        for(const auto& row:generated::GeneratedPistolSlideProfiles)out.rows[out.count++]=row;
#endif
        return out;
    }();
    return {table.rows.data(),table.count};
}
}
