#include <math.h>
#include <stdio.h>
#include <string.h>

#include "link.h"
#include "logger.h"
#include "packet.h"

const char *emu_pkt_type_name(uint8_t type) {
    switch (type) {
        case DATA_PACKET: return "DATA";
        case ACK_PACKET:  return "ACK";
        case FIN_PACKET:  return "FIN";
        default:          return "UNKNOWN";
    }
}

bool emu_peek_header(const uint8_t *buf, size_t len, uint8_t *out_type, uint32_t *out_seq) {
    if (!buf || len < sizeof(struct packet_header)) {
        return false;
    }
    struct packet_header hdr;
    memcpy(&hdr, buf, sizeof(hdr));
    if (out_type) {
        *out_type = hdr.type;
    }
    if (out_seq) {
        *out_seq = hdr.seq_num;
    }
    return true;
}

bool emu_state_init(struct emu_state *st, const struct emu_scenario *sc, uint64_t seed) {
    memset(st, 0, sizeof(*st));
    if (!scenario_clone(&st->scenario, sc)) {
        return false;
    }
    eq_init(&st->eq);
    st->next_order_id = 1;

    for (int d = 0; d < 2; d++) {
        for (int s = 0; s < PRNG_STREAM_COUNT; s++) {
            uint64_t stream_id = (uint64_t)d * (uint64_t)PRNG_STREAM_COUNT + (uint64_t)s;
            prng_init(&st->rng[d][s], seed, stream_id);
        }
    }
    return true;
}

void emu_state_free(struct emu_state *st) {
    if (!st) {
        return;
    }
    eq_free(&st->eq);
    scenario_free(&st->scenario);
}

static bool enqueue_duplicate(struct emu_state *st, const struct scheduled_pkt *orig,
                              const char *dir_str, const char *type_str, long seq_log) {
    struct scheduled_pkt dup = *orig;
    dup.is_duplicate = true;
    dup.order_id = st->next_order_id++;
    if (!eq_push(&st->eq, &dup)) {
        return false;
    }
    char detail[64];
    snprintf(detail, sizeof(detail), "deliver_us=%lld", (long long)dup.deliver_us);
    log_emu_event(EMU_EV_DUPLICATE, dir_str, dup.pkt_idx, type_str, seq_log, dup.len, detail);
    return true;
}

void emu_handle_arrival(struct emu_state *st, enum emu_dir dir,
                        const uint8_t *buf, size_t len, int64_t now_us) {
    if (!buf || len == 0 || len > EMU_MAX_PKT_BYTES) {
        return;
    }

    struct dir_runtime *rt = &st->dir_rt[dir];
    const struct dir_params *dp = &st->scenario.dir[dir];
    const char *dir_str = emu_dir_name(dir);

    rt->pkt_count++;
    unsigned long pkt_idx = rt->pkt_count;

    uint8_t orig_type = 255;
    uint32_t orig_seq = 0;
    bool has_hdr = emu_peek_header(buf, len, &orig_type, &orig_seq);
    const char *type_str = has_hdr ? emu_pkt_type_name(orig_type) : "RAW";
    long seq_log = has_hdr ? (long)orig_seq : -1L;

    log_emu_event(EMU_EV_ARRIVE, dir_str, pkt_idx, type_str, seq_log, len, "");

    /* Check FIFO queue occupancy before admitting packet. */
    int occupancy = eq_count_in_tx_queue(&st->eq, dir, now_us);
    if (rt->has_held && rt->held_pkt.tx_end_us > now_us) {
        occupancy++;
    }
    if (dp->queue_capacity > 0 && occupancy >= dp->queue_capacity) {
        char detail[64];
        snprintf(detail, sizeof(detail), "occ=%d;cap=%d", occupancy, dp->queue_capacity);
        log_emu_event(EMU_EV_QUEUE_DROP, dir_str, pkt_idx, type_str, seq_log, len, detail);
        return;
    }

    /* Evaluate matching per-packet actions. */
    bool force_drop = false;
    bool force_corrupt = false;
    int forced_bit = -1;
    bool force_dup = false;
    bool force_reorder = false;
    int64_t extra_delay_us = 0;

    for (size_t i = 0; i < st->scenario.action_count; i++) {
        struct pkt_action *act = &st->scenario.actions[i];
        if (act->dir != dir) {
            continue;
        }
        bool matched = false;
        if (act->match_type == MATCH_PKT_IDX) {
            matched = (pkt_idx == act->match_value);
        } else if (act->match_type == MATCH_SEQ_NUM && has_hdr) {
            if ((uint32_t)act->match_value == orig_seq) {
                act->seen_count++;
                matched = (act->occurrence == 0 || act->seen_count == act->occurrence);
            }
        }
        if (!matched) {
            continue;
        }

        char detail[64];
        switch (act->kind) {
            case ACT_DROP:
                force_drop = true;
                log_emu_event(EMU_EV_ACTION_DROP, dir_str, pkt_idx, type_str, seq_log, len, "matched");
                break;
            case ACT_DELAY: {
                int64_t d_us = llround(act->delay_ms * 1000.0);
                extra_delay_us += d_us;
                snprintf(detail, sizeof(detail), "extra_us=%lld", (long long)d_us);
                log_emu_event(EMU_EV_ACTION_DELAY, dir_str, pkt_idx, type_str, seq_log, len, detail);
                break;
            }
            case ACT_CORRUPT:
                force_corrupt = true;
                forced_bit = act->bit_index;
                snprintf(detail, sizeof(detail), "bit_arg=%d", forced_bit);
                log_emu_event(EMU_EV_ACTION_CORRUPT, dir_str, pkt_idx, type_str, seq_log, len, detail);
                break;
            case ACT_DUPLICATE:
                force_dup = true;
                log_emu_event(EMU_EV_ACTION_DUPLICATE, dir_str, pkt_idx, type_str, seq_log, len, "matched");
                break;
            case ACT_REORDER:
                force_reorder = true;
                break;
        }
    }

    if (force_drop) {
        return;
    }

    /* Probabilistic packet loss (separate PRNG stream per direction). */
    if (dp->loss_prob > 0.0 && prng_uniform01(&st->rng[dir][PRNG_LOSS]) < dp->loss_prob) {
        log_emu_event(EMU_EV_DROP_LOSS, dir_str, pkt_idx, type_str, seq_log, len, "");
        return;
    }

    /* Serialization L/R on FIFO link. */
    int64_t serial_us = 0;
    if (dp->link_rate_bps > 0.0) {
        serial_us = llround(((double)len * 8.0 * 1000000.0) / dp->link_rate_bps);
    }
    int64_t tx_start_us = (now_us > rt->link_free_us) ? now_us : rt->link_free_us;
    int64_t tx_end_us = tx_start_us + serial_us;
    rt->link_free_us = tx_end_us;

    /* Propagation delay + order-preserving uniform jitter [-j, +j]. */
    int64_t prop_us = llround(dp->prop_delay_ms * 1000.0);
    int64_t jitter_us = 0;
    if (dp->jitter_ms > 0.0) {
        double u = prng_uniform01(&st->rng[dir][PRNG_JITTER]);
        double j_ms = (2.0 * u - 1.0) * dp->jitter_ms;
        jitter_us = llround(j_ms * 1000.0);
    }

    int64_t wire_us = prop_us + jitter_us;
    if (wire_us < 0) {
        wire_us = 0;
    }

    int64_t deliver_us = tx_end_us + wire_us;
    if (deliver_us < rt->last_deliver_us) {
        deliver_us = rt->last_deliver_us;
    }
    rt->last_deliver_us = deliver_us;
    deliver_us += extra_delay_us;

    /* Prepare scheduled packet copy. */
    struct scheduled_pkt pkt;
    memset(&pkt, 0, sizeof(pkt));
    pkt.deliver_us = deliver_us;
    pkt.arrive_us = now_us;
    pkt.tx_end_us = tx_end_us;
    pkt.dir = dir;
    pkt.pkt_idx = pkt_idx;
    pkt.orig_type = orig_type;
    pkt.orig_seq = orig_seq;
    pkt.is_duplicate = false;
    pkt.len = len;
    memcpy(pkt.data, buf, len);

    /* Single-bit corruption. */
    if (force_corrupt || (dp->corrupt_prob > 0.0 && prng_uniform01(&st->rng[dir][PRNG_CORRUPT]) < dp->corrupt_prob)) {
        size_t total_bits = len * 8;
        size_t bit_idx;
        if (force_corrupt && forced_bit >= 0) {
            bit_idx = (size_t)forced_bit % total_bits;
        } else {
            bit_idx = (size_t)prng_uniform_u64(&st->rng[dir][PRNG_CORRUPT_BIT], (uint64_t)total_bits);
        }
        pkt.data[bit_idx / 8] ^= (uint8_t)(1u << (bit_idx % 8));
        char detail[64];
        snprintf(detail, sizeof(detail), "bit=%zu", bit_idx);
        log_emu_event(EMU_EV_CORRUPT, dir_str, pkt_idx, type_str, seq_log, len, detail);
    }

    bool do_dup = force_dup || (dp->dup_prob > 0.0 && prng_uniform01(&st->rng[dir][PRNG_DUP]) < dp->dup_prob);
    bool do_reorder = force_reorder || (dp->reorder_prob > 0.0 && prng_uniform01(&st->rng[dir][PRNG_REORDER]) < dp->reorder_prob);

    char enq_detail[128];
    snprintf(enq_detail, sizeof(enq_detail),
             "serial_us=%lld;prop_us=%lld;jitter_us=%lld;deliver_us=%lld",
             (long long)serial_us, (long long)prop_us,
             (long long)jitter_us, (long long)deliver_us);

    if (rt->has_held) {
        /* Deliver current packet first, then release held packet immediately after it. */
        pkt.order_id = st->next_order_id++;
        eq_push(&st->eq, &pkt);
        log_emu_event(EMU_EV_ENQUEUE, dir_str, pkt_idx, type_str, seq_log, len, enq_detail);

        if (do_dup) {
            enqueue_duplicate(st, &pkt, dir_str, type_str, seq_log);
        }

        struct scheduled_pkt held = rt->held_pkt;
        if (held.deliver_us < pkt.deliver_us) {
            held.deliver_us = pkt.deliver_us;
        }
        if (held.deliver_us > rt->last_deliver_us) {
            rt->last_deliver_us = held.deliver_us;
        }
        held.order_id = st->next_order_id++;
        eq_push(&st->eq, &held);

        const char *held_type = (held.orig_type != 255) ? emu_pkt_type_name(held.orig_type) : "RAW";
        long held_seq = (held.orig_type != 255) ? (long)held.orig_seq : -1L;
        char rel_detail[64];
        snprintf(rel_detail, sizeof(rel_detail), "after_pkt=%lu;deliver_us=%lld",
                 pkt_idx, (long long)held.deliver_us);
        log_emu_event(EMU_EV_REORDER_RELEASE, dir_str, held.pkt_idx, held_type, held_seq, held.len, rel_detail);

        if (rt->held_has_dup) {
            enqueue_duplicate(st, &held, dir_str, held_type, held_seq);
        }

        rt->has_held = false;
        rt->held_has_dup = false;
    } else if (do_reorder) {
        /* Hold this packet until the next packet arrives or reorder_timeout_ms expires. */
        rt->held_pkt = pkt;
        rt->has_held = true;
        rt->held_has_dup = do_dup;
        int64_t timeout_us = llround(dp->reorder_timeout_ms * 1000.0);
        rt->held_deadline_us = pkt.deliver_us + timeout_us;

        char hold_detail[96];
        snprintf(hold_detail, sizeof(hold_detail), "deadline_us=%lld;%s",
                 (long long)rt->held_deadline_us, force_reorder ? "action" : "prob");
        log_emu_event(EMU_EV_REORDER_HOLD, dir_str, pkt_idx, type_str, seq_log, len, hold_detail);
    } else {
        pkt.order_id = st->next_order_id++;
        eq_push(&st->eq, &pkt);
        log_emu_event(EMU_EV_ENQUEUE, dir_str, pkt_idx, type_str, seq_log, len, enq_detail);

        if (do_dup) {
            enqueue_duplicate(st, &pkt, dir_str, type_str, seq_log);
        }
    }
}

void emu_check_reorder_timeouts(struct emu_state *st, int64_t now_us) {
    for (int d = 0; d < 2; d++) {
        struct dir_runtime *rt = &st->dir_rt[d];
        if (!rt->has_held || now_us < rt->held_deadline_us) {
            continue;
        }
        const char *dir_str = emu_dir_name((enum emu_dir)d);
        struct scheduled_pkt held = rt->held_pkt;
        held.deliver_us = now_us;
        if (held.deliver_us > rt->last_deliver_us) {
            rt->last_deliver_us = held.deliver_us;
        }
        held.order_id = st->next_order_id++;
        eq_push(&st->eq, &held);

        const char *held_type = (held.orig_type != 255) ? emu_pkt_type_name(held.orig_type) : "RAW";
        long held_seq = (held.orig_type != 255) ? (long)held.orig_seq : -1L;
        char detail[64];
        snprintf(detail, sizeof(detail), "deliver_us=%lld", (long long)held.deliver_us);
        log_emu_event(EMU_EV_REORDER_TIMEOUT, dir_str, held.pkt_idx, held_type, held_seq, held.len, detail);

        if (rt->held_has_dup) {
            enqueue_duplicate(st, &held, dir_str, held_type, held_seq);
        }

        rt->has_held = false;
        rt->held_has_dup = false;
    }
}

int64_t emu_next_wakeup_us(const struct emu_state *st) {
    int64_t earliest = -1;
    const struct scheduled_pkt *top = eq_peek(&st->eq);
    if (top != NULL) {
        earliest = top->deliver_us;
    }
    for (int d = 0; d < 2; d++) {
        const struct dir_runtime *rt = &st->dir_rt[d];
        if (rt->has_held) {
            if (earliest < 0 || rt->held_deadline_us < earliest) {
                earliest = rt->held_deadline_us;
            }
        }
    }
    return earliest;
}
