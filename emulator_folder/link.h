#ifndef _EMU_LINK_H
#define _EMU_LINK_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "event_queue.h"
#include "prng.h"
#include "scenario.h"

struct dir_runtime {
    unsigned long pkt_count;
    int64_t link_free_us;
    int64_t last_deliver_us;
    bool has_held;
    bool held_has_dup;
    struct scheduled_pkt held_pkt;
    int64_t held_deadline_us;
};

struct emu_state {
    struct emu_scenario scenario;
    struct event_queue eq;
    uint64_t next_order_id;
    struct prng_state rng[2][PRNG_STREAM_COUNT];
    struct dir_runtime dir_rt[2];
};

/** Initialize emulator runtime state with scenario settings and PRNG master seed. */
bool emu_state_init(struct emu_state *st, const struct emu_scenario *sc, uint64_t seed);

/** Release all resources held by emulator runtime state. */
void emu_state_free(struct emu_state *st);

/** Return human-readable packet type name ("DATA", "ACK", "FIN", "UNKNOWN"). */
const char *emu_pkt_type_name(uint8_t type);

/** Inspect packed header from raw datagram buffer if len >= sizeof(packet_header). */
bool emu_peek_header(const uint8_t *buf, size_t len, uint8_t *out_type, uint32_t *out_seq);

/** Process an incoming datagram in direction dir at timestamp now_us. */
void emu_handle_arrival(struct emu_state *st, enum emu_dir dir,
                        const uint8_t *buf, size_t len, int64_t now_us);

/** Release any held reordered packet whose timeout expired by now_us. */
void emu_check_reorder_timeouts(struct emu_state *st, int64_t now_us);

/** Return the earliest scheduled timestamp (us), or -1 if no event is pending. */
int64_t emu_next_wakeup_us(const struct emu_state *st);

#endif
