#pragma once
// Exact-write restoration (AGENTS.md): every temporary native write goes through a
// journal that records the original bytes, and is undone on every outcome - normal end
// of frame, early return, exception, detach. Restore is "exact": if the game has since
// written its own value over ours, we leave the game's value alone instead of
// clobbering it with a stale original.
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <type_traits>
#include <vector>

namespace fvr::runtime {

class IMemory {
public:
    virtual ~IMemory() = default;
    virtual bool Read(std::uint64_t address, void* out, std::size_t size) = 0;
    virtual bool Write(std::uint64_t address, const void* in, std::size_t size) = 0;
};

struct RestoreReport {
    std::size_t restored = 0;
    std::size_t leftForeign = 0; // game overwrote our value; kept the game's
    std::size_t failed = 0;
};

class ExactWriteJournal {
public:
    explicit ExactWriteJournal(IMemory& memory) : memory_(memory) {}
    ~ExactWriteJournal() { RestoreAll(); }
    ExactWriteJournal(const ExactWriteJournal&) = delete;
    ExactWriteJournal& operator=(const ExactWriteJournal&) = delete;

    bool WriteBytes(std::uint64_t address, const void* data, std::size_t size);

    template <typename T>
    bool Write(std::uint64_t address, const T& value) {
        static_assert(std::is_trivially_copyable_v<T>);
        return WriteBytes(address, &value, sizeof(T));
    }

    // Reverse order, so overlapping writes unwind to the true original.
    RestoreReport RestoreAll();
    std::size_t Pending() const { return entries_.size(); }

private:
    struct Entry {
        std::uint64_t address;
        std::vector<std::uint8_t> original;
        std::vector<std::uint8_t> written;
    };
    IMemory& memory_;
    std::vector<Entry> entries_;
};

} // namespace fvr::runtime
