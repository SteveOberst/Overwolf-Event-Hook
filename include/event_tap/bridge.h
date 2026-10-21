#ifndef EVENT_TAP_BRIDGE_H
#define EVENT_TAP_BRIDGE_H

#include <stdint.h>

#if defined(OET_BUILD_oet_capture)
#define OET_CAPTURE __declspec(dllexport)
#else
#define OET_CAPTURE __declspec(dllimport)
#endif
#if defined(OET_BUILD_oet_client)
#define OET_CLIENT __declspec(dllexport)
#else
#define OET_CLIENT __declspec(dllimport)
#endif

#ifdef __cplusplus
extern "C" {
#endif

#define OET_ABI_VERSION 1u
typedef struct oet_writer oet_writer;
typedef struct oet_reader oet_reader;

typedef int32_t oet_status;
enum {
    OET_OK = 0,
    OET_NO_DATA = 1,
    OET_CLOSED = 2,
    OET_NOT_FOUND = 3,
    OET_BUSY = 4,
    OET_BUFFER_TOO_SMALL = 5,
    OET_INVALID_ARGUMENT = 6,
    OET_ALREADY_EXISTS = 7,
    OET_INCOMPATIBLE = 8,
    OET_CORRUPT = 9,
    OET_SYSTEM_ERROR = 10,
    OET_RESOURCE_EXHAUSTED = 11
};

/** Status and original Win32 error when applicable; no thread-local error state. */
typedef struct oet_result {
    oet_status status;
    uint32_t native_error;
} oet_result;

typedef int32_t oet_start;
enum { OET_RETAINED = 0, OET_NEW_ONLY = 1 };

typedef struct oet_record {
    uint64_t sequence;
    uint64_t lost_before;
    uint32_t size;
    uint32_t reserved;
} oet_record;

/** Creates one bounded channel in the current Windows session. Channel names are
 *  1..100 ASCII letters/digits/'-'/'.'/'_'. No hidden process discovery or hooks.
 *  Limits: 1..4096 slots, 1..65536 bytes/record, <=16 MiB total mapping.
 *  Objects use the caller's default Windows security descriptor.
 */
OET_CAPTURE oet_result __cdecl oet_writer_create(uint32_t abi_version,
    const char* channel, uint32_t slots, uint32_t record_bytes, oet_writer** output);

/** Copies opaque bytes. Never waits for readers; reports BUSY on mutex contention.
 *  Oversized records are rejected rather than truncated. No heap allocation on
 *  the successful publication path; not a hard-real-time contract.
 *  Calls must not race close on the same handle.
 */
OET_CAPTURE oet_result __cdecl oet_writer_publish(oet_writer* writer,
    const void* bytes, uint32_t size);

/** Marks the channel closed, then releases this writer. On BUSY it remains owned
 *  by the caller and close must be retried after stopping callbacks.
 */
OET_CAPTURE oet_result __cdecl oet_writer_close(oet_writer* writer);

/** Every reader owns its cursor; opening never removes other readers' data.
 *  RETAINED starts at the oldest retained record; NEW_ONLY establishes a boundary
 *  at open. Opening unavailable sources returns NOT_FOUND, not an empty stream.
 */
OET_CLIENT oet_result __cdecl oet_reader_open(uint32_t abi_version,
    const char* channel, oet_start start, oet_reader** output);

/** Copies one complete record. Lost records are counted from sequence gaps.
 *  BUFFER_TOO_SMALL reports required size without advancing this reader.
 *  NO_DATA means an open writer has no new record; CLOSED means drained and the
 *  writer closed or its process terminated. Native bytes have no implicit UTF-8
 *  interpretation. This is a polling API, not a shared notification wait.
 *  Serialize calls for a given reader and do not race close.
 */
OET_CLIENT oet_result __cdecl oet_reader_next(oet_reader* reader, void* bytes,
    uint32_t capacity, oet_record* record);
OET_CLIENT void __cdecl oet_reader_close(oet_reader* reader);

#ifdef __cplusplus
}
#endif
#endif
