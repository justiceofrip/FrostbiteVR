#pragma once
#include <cstddef>
#include <string>
#include <string_view>

namespace fvr::bc2::live_snapshot {
// Magazine and bolt recovery records coexist with the ordinary interaction
// snapshot. Measured complete records exceed 32 KiB; retain a bounded 64 KiB
// response while consumers keep their existing total retention/time budgets.
inline constexpr std::size_t MaximumBytes=64*1024;
inline constexpr std::string_view OmittedSightResolution=
    R"({"observationOnly":true,"available":false,"status":"omitted","omitted":"payload_limit"})";

// Explicit worker-request composition only. Offsets delimit the serialized
// sight.resolutionObservation value, captured directly from the same stream.
// No JSON parse or new evidence reads; every other byte keeps its source clock.
// An oversized return must take the caller's existing payload_limit refusal.
inline std::string Compose(std::string_view prefix,std::string_view feed,std::string_view compactFeed,
    std::string_view suffix,std::size_t sightBegin,std::size_t sightEnd,std::size_t limit){
    const auto compose=[&](std::string_view admission){
        std::string result(prefix);result.append(admission);result.append(suffix);return result;
    };
    auto payload=compose(feed);
    if(payload.size()<=limit)return payload;
    payload=compose(compactFeed);
    if(payload.size()<=limit)return payload;
    // Bounds also refuse failed tellp() conversions. Never expand a short
    // unavailable status, drop an action record, or truncate a JSON token.
    if(sightBegin<=sightEnd&&sightEnd<=prefix.size()&&sightEnd-sightBegin>OmittedSightResolution.size())
        payload.replace(sightBegin,sightEnd-sightBegin,OmittedSightResolution);
    return payload;
}
}
