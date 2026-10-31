#pragma once
#include "fvr/ipc/FrameChannel.h"

namespace fvr::bc2 {
// The native Gather reader may run faster or slower than the host. Contention
// retains the original source lease; neither a repeated read nor a render
// completion supplies a new observation time. Used at reception AND dispatch.
struct GameplayInputLease {
    interaction::InputFrame input{};
    std::int64_t deadlineQpc=0;

    void Observe(ipc::ChannelResult result,const interaction::InputFrame& received,
        std::int64_t deadline) noexcept {
        if(result==ipc::ChannelResult::Ok){input=received;deadlineQpc=deadline;}
        else if(result!=ipc::ChannelResult::Busy){input={};deadlineQpc=0;}
    }
    bool Fresh(std::int64_t nowQpc)const noexcept {
        return nowQpc>0&&nowQpc<deadlineQpc;
    }
};

// Processing can outlast a lease even when reception was current. This gate
// grants no hold/edge and cannot renew the packet; the native owner/cache must
// be revalidated by the caller immediately before applying the command.
inline bool GameplayInputDispatchCurrent(const GameplayInputLease& source,
    std::int64_t nowQpc,bool ownerCurrent,bool cacheWritable) noexcept {
    return source.Fresh(nowQpc)&&ownerCurrent&&cacheWritable;
}
}
