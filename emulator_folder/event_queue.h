#ifndef _EMU_EVENT_QUEUE_H
#define _EMU_EVENT_QUEUE_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "scenario.h"

#define EMU_MAX_PKT_BYTES 2048

struct scheduled_pkt {
    int64_t deliver_us;
    uint64_t order_id;
    int64_t arrive_us;
    int64_t tx_end_us;
    enum emu_dir dir;
    unsigned long pkt_idx;
    uint8_t orig_type;
    uint32_t orig_seq;
    bool is_duplicate;
    size_t len;
    uint8_t data[EMU_MAX_PKT_BYTES];
};

struct event_queue {
    struct scheduled_pkt *items;
    size_t size;
    size_t capacity;
};

/** Initialize an empty event queue. */
void eq_init(struct event_queue *eq);

/** Free all memory owned by the event queue. */
void eq_free(struct event_queue *eq);

/** Push a scheduled packet into the min-heap ordered by (deliver_us, order_id). */
bool eq_push(struct event_queue *eq, const struct scheduled_pkt *pkt);

/** Return a pointer to the earliest scheduled packet, or NULL if empty. */
const struct scheduled_pkt *eq_peek(const struct event_queue *eq);

/** Pop the earliest scheduled packet into *out (if non-NULL). Returns false if empty. */
bool eq_pop(struct event_queue *eq, struct scheduled_pkt *out);

/** Count non-duplicate packets in direction dir whose serialization has not finished by now_us. */
int eq_count_in_tx_queue(const struct event_queue *eq, enum emu_dir dir, int64_t now_us);

#endif
