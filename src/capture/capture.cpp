#include "transport/channel.hpp"
#include <cstring>
#include <limits>

struct oet_writer { std::unique_ptr<event_tap::Channel> channel; };

extern "C" OET_CAPTURE oet_result __cdecl oet_writer_create(uint32_t abi,
    const char* name, uint32_t slots, uint32_t bytes, oet_writer** output) {
    if (output) *output = nullptr;
    return event_tap::boundary([&] {
        if (!output || abi != OET_ABI_VERSION) event_tap::fail(OET_INVALID_ARGUMENT);
        auto writer = std::make_unique<oet_writer>();
        writer->channel = event_tap::Channel::create(name, slots, bytes);
        *output = writer.release();
        return event_tap::ok;
    });
}

extern "C" OET_CAPTURE oet_result __cdecl oet_writer_publish(oet_writer* writer,
    const void* bytes, uint32_t size) {
    return event_tap::boundary([&] {
        if (!writer || (!bytes && size)) event_tap::fail(OET_INVALID_ARGUMENT);
        auto& channel = *writer->channel;
        event_tap::Lock lock(channel.mutex.get(), 0);
        if (lock.abandoned()) event_tap::fail(OET_CORRUPT);
        channel.validate();
        auto& h = channel.header();
        if (size > h.record_bytes) event_tap::fail(OET_INVALID_ARGUMENT);
        if (h.published == std::numeric_limits<uint64_t>::max())
            event_tap::fail(OET_RESOURCE_EXHAUSTED);
        const uint64_t sequence = h.published + 1;
        auto* slot = channel.slot(sequence);
        const event_tap::Slot entry{sequence, size, 0};
        std::memcpy(slot, &entry, sizeof(entry));
        if (size) std::memcpy(slot + sizeof(entry), bytes, size);
        h.published = sequence;
        return event_tap::ok;
    });
}

extern "C" OET_CAPTURE oet_result __cdecl oet_writer_close(oet_writer* writer) {
    if (!writer) return event_tap::ok;
    const auto result = event_tap::boundary([&] {
        auto& channel = *writer->channel;
        event_tap::Lock lock(channel.mutex.get(), 100);
        channel.header().closed = 1;
        return event_tap::ok;
    });
    if (result.status == OET_OK) delete writer;
    return result;
}
