#include <stdlib.h>

#include "event_queue.h"

static bool pkt_earlier(const struct scheduled_pkt *a, const struct scheduled_pkt *b) {
    if (a->deliver_us != b->deliver_us) {
        return a->deliver_us < b->deliver_us;
    }
    return a->order_id < b->order_id;
}

void eq_init(struct event_queue *eq) {
    eq->items = NULL;
    eq->size = 0;
    eq->capacity = 0;
}

void eq_free(struct event_queue *eq) {
    if (!eq) {
        return;
    }
    free(eq->items);
    eq->items = NULL;
    eq->size = 0;
    eq->capacity = 0;
}

bool eq_push(struct event_queue *eq, const struct scheduled_pkt *pkt) {
    if (eq->size == eq->capacity) {
        size_t new_cap = (eq->capacity == 0) ? 64 : eq->capacity * 2;
        struct scheduled_pkt *resized = realloc(eq->items, new_cap * sizeof(struct scheduled_pkt));
        if (!resized) {
            return false;
        }
        eq->items = resized;
        eq->capacity = new_cap;
    }

    size_t idx = eq->size++;
    while (idx > 0) {
        size_t parent = (idx - 1) / 2;
        if (!pkt_earlier(pkt, &eq->items[parent])) {
            break;
        }
        eq->items[idx] = eq->items[parent];
        idx = parent;
    }
    eq->items[idx] = *pkt;
    return true;
}

const struct scheduled_pkt *eq_peek(const struct event_queue *eq) {
    if (eq->size == 0) {
        return NULL;
    }
    return &eq->items[0];
}

bool eq_pop(struct event_queue *eq, struct scheduled_pkt *out) {
    if (eq->size == 0) {
        return false;
    }
    if (out) {
        *out = eq->items[0];
    }

    eq->size--;
    if (eq->size > 0) {
        struct scheduled_pkt last = eq->items[eq->size];
        size_t idx = 0;
        while (1) {
            size_t left = 2 * idx + 1;
            size_t right = left + 1;
            if (left >= eq->size) {
                break;
            }
            size_t smallest = left;
            if (right < eq->size && pkt_earlier(&eq->items[right], &eq->items[left])) {
                smallest = right;
            }
            if (!pkt_earlier(&eq->items[smallest], &last)) {
                break;
            }
            eq->items[idx] = eq->items[smallest];
            idx = smallest;
        }
        eq->items[idx] = last;
    }
    return true;
}

int eq_count_in_tx_queue(const struct event_queue *eq, enum emu_dir dir, int64_t now_us) {
    int count = 0;
    for (size_t i = 0; i < eq->size; i++) {
        const struct scheduled_pkt *p = &eq->items[i];
        if (p->dir == dir && !p->is_duplicate && p->tx_end_us > now_us) {
            count++;
        }
    }
    return count;
}
