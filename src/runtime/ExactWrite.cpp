#include "fvr/runtime/ExactWrite.h"

namespace fvr::runtime {

bool ExactWriteJournal::WriteBytes(std::uint64_t address, const void* data, std::size_t size) {
    Entry e{address, std::vector<std::uint8_t>(size), std::vector<std::uint8_t>(size)};
    if (!memory_.Read(address, e.original.data(), size)) return false;
    std::memcpy(e.written.data(), data, size);
    if (!memory_.Write(address, data, size)) return false;
    entries_.push_back(std::move(e));
    return true;
}

RestoreReport ExactWriteJournal::RestoreAll() {
    RestoreReport report;
    std::vector<std::uint8_t> current;
    for (auto it = entries_.rbegin(); it != entries_.rend(); ++it) {
        current.resize(it->written.size());
        if (!memory_.Read(it->address, current.data(), current.size())) {
            ++report.failed;
            continue;
        }
        if (current != it->written) {
            ++report.leftForeign;
            continue;
        }
        if (memory_.Write(it->address, it->original.data(), it->original.size()))
            ++report.restored;
        else
            ++report.failed;
    }
    entries_.clear();
    return report;
}

} // namespace fvr::runtime
