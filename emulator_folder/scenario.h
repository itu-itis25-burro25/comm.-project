#ifndef _EMU_SCENARIO_H
#define _EMU_SCENARIO_H

#include <stdbool.h>
#include <stddef.h>

enum emu_dir {
    DIR_S2R = 0,
    DIR_R2S = 1
};

struct dir_params {
    double link_rate_bps;
    int queue_capacity;
    double prop_delay_ms;
    double jitter_ms;
    double loss_prob;
    double corrupt_prob;
    double dup_prob;
    double reorder_prob;
    double reorder_timeout_ms;
};

enum action_match_type {
    MATCH_PKT_IDX,
    MATCH_SEQ_NUM
};

enum action_kind {
    ACT_DROP,
    ACT_DELAY,
    ACT_CORRUPT,
    ACT_DUPLICATE,
    ACT_REORDER
};

struct pkt_action {
    enum emu_dir dir;
    enum action_match_type match_type;
    unsigned long match_value;
    unsigned long occurrence;
    unsigned long seen_count;
    enum action_kind kind;
    double delay_ms;
    int bit_index;
};

struct emu_scenario {
    struct dir_params dir[2];
    struct pkt_action *actions;
    size_t action_count;
    size_t action_cap;
};

/** Return string label ("S2R" or "R2S") for an emulator direction. */
const char *emu_dir_name(enum emu_dir dir);

/** Initialize scenario with baseline configuration parameters from the spec. */
void scenario_init_defaults(struct emu_scenario *sc);

/** Parse a .scn file into an already-initialized or clean scenario struct. */
bool scenario_load_file(struct emu_scenario *sc, const char *path);

/** Deep-copy a scenario struct (allocating an independent actions array). */
bool scenario_clone(struct emu_scenario *dst, const struct emu_scenario *src);

/** Free dynamically allocated scenario resources. */
void scenario_free(struct emu_scenario *sc);

#endif
