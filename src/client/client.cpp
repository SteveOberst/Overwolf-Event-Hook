#include "transport/channel.hpp"
#include <algorithm>
#include <cstring>
#include <limits>

struct oet_reader {
    std::unique_ptr<event_tap::Channel> channel;
    uint64_t next = 1;
};

extern "C" OET_CLIENT oet_result __cdecl oet_reader_open(uint32_t abi,
    const char* name, oet_start start, oet_reader** output) {
    if (output) *output = nullptr;
    return event_tap::boundary([&] {
        if (!output || abi != OET_ABI_VERSION || (start != OET_RETAINED && start != OET_NEW_ONLY))
            event_tap::fail(OET_INVALID_ARGUMENT);
        auto reader = std::make_unique<oet_reader>();
        reader->channel = event_tap::Channel::open(name);
        event_tap::Lock lock(reader->channel->mutex.get(), 100);
        if (lock.abandoned()) event_tap::fail(OET_CORRUPT);
        const auto& h = reader->channel->header();
        if (h.published == std::numeric_limits<uint64_t>::max())
            event_tap::fail(OET_RESOURCE_EXHAUSTED);
        reader->next = start == OET_NEW_ONLY ? h.published + 1 :
            (h.published >= h.slots ? h.published - h.slots + 1 : 1);
        *output = reader.release();
        return event_tap::ok;
    });
}

extern "C" OET_CLIENT oet_result __cdecl oet_reader_next(oet_reader* reader,
    void* bytes, uint32_t capacity, oet_record* record) {
    if (record) *record = {};
    return event_tap::boundary([&] {
        if (!reader || !record || (!bytes && capacity)) event_tap::fail(OET_INVALID_ARGUMENT);
        auto& channel = *reader->channel;
        event_tap::Lock lock(channel.mutex.get(), 100);
        if (lock.abandoned()) event_tap::fail(OET_CORRUPT);
        channel.validate();
        const auto& h = channel.header();
        if (reader->next > h.published)
            return oet_result{h.closed || channel.producer_exited() ? OET_CLOSED : OET_NO_DATA, 0};
        const uint64_t oldest = h.published >= h.slots ? h.published - h.slots + 1 : 1;
        const uint64_t sequence = std::max(reader->next, oldest);
        event_tap::Slot entry{};
        const auto* slot = channel.slot(sequence);
        std::memcpy(&entry, slot, sizeof(entry));
        if (entry.sequence != sequence || entry.size > h.record_bytes || entry.reserved)
            event_tap::fail(OET_CORRUPT);
        *record = oet_record{sequence, sequence - reader->next, entry.size, 0};
        if (capacity < entry.size) return oet_result{OET_BUFFER_TOO_SMALL, 0};
        if (entry.size) std::memcpy(bytes, slot + sizeof(entry), entry.size);
        if (sequence == std::numeric_limits<uint64_t>::max())
            event_tap::fail(OET_RESOURCE_EXHAUSTED);
        reader->next = sequence + 1;
        return event_tap::ok;
    });
}

extern "C" OET_CLIENT void __cdecl oet_reader_close(oet_reader* reader) { delete reader; }
