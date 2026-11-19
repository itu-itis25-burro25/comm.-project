#include <ctype.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "scenario.h"

const char *emu_dir_name(enum emu_dir dir) {
    return (dir == DIR_S2R) ? "S2R" : "R2S";
}

void scenario_init_defaults(struct emu_scenario *sc) {
    memset(sc, 0, sizeof(*sc));
    for (int d = 0; d < 2; d++) {
        sc->dir[d].link_rate_bps = 1000000.0;
        sc->dir[d].queue_capacity = 128;
        sc->dir[d].prop_delay_ms = 20.0;
        sc->dir[d].jitter_ms = 0.0;
        sc->dir[d].loss_prob = 0.0;
        sc->dir[d].corrupt_prob = 0.0;
        sc->dir[d].dup_prob = 0.0;
        sc->dir[d].reorder_prob = 0.0;
        sc->dir[d].reorder_timeout_ms = 50.0;
    }
}

bool scenario_clone(struct emu_scenario *dst, const struct emu_scenario *src) {
    *dst = *src;
    dst->actions = NULL;
    dst->action_cap = src->action_count;
    if (src->action_count > 0) {
        dst->actions = malloc(src->action_count * sizeof(struct pkt_action));
        if (!dst->actions) {
            dst->action_count = 0;
            dst->action_cap = 0;
            return false;
        }
        memcpy(dst->actions, src->actions, src->action_count * sizeof(struct pkt_action));
    }
    return true;
}

void scenario_free(struct emu_scenario *sc) {
    if (!sc) {
        return;
    }
    free(sc->actions);
    sc->actions = NULL;
    sc->action_count = 0;
    sc->action_cap = 0;
}

static bool append_action(struct emu_scenario *sc, const struct pkt_action *act) {
    if (sc->action_count == sc->action_cap) {
        size_t new_cap = (sc->action_cap == 0) ? 16 : sc->action_cap * 2;
        struct pkt_action *resized = realloc(sc->actions, new_cap * sizeof(struct pkt_action));
        if (!resized) {
            return false;
        }
        sc->actions = resized;
        sc->action_cap = new_cap;
    }
    sc->actions[sc->action_count++] = *act;
    return true;
}

static bool parse_double_val(const char *s, double *out) {
    if (!s || *s == '\0') {
        return false;
    }
    errno = 0;
    char *end = NULL;
    double val = strtod(s, &end);
    if (errno != 0 || end == s || *end != '\0') {
        return false;
    }
    *out = val;
    return true;
}

static bool parse_ulong_val(const char *s, unsigned long *out) {
    if (!s || *s == '\0' || s[0] == '-') {
        return false;
    }
    errno = 0;
    char *end = NULL;
    unsigned long val = strtoul(s, &end, 10);
    if (errno != 0 || end == s || *end != '\0') {
        return false;
    }
    *out = val;
    return true;
}

static bool parse_int_val(const char *s, int *out) {
    if (!s || *s == '\0') {
        return false;
    }
    errno = 0;
    char *end = NULL;
    long val = strtol(s, &end, 10);
    if (errno != 0 || end == s || *end != '\0') {
        return false;
    }
    *out = (int)val;
    return true;
}

static bool valid_prob(double p) {
    return p >= 0.0 && p <= 1.0;
}

static bool parse_action_tokens(struct emu_scenario *sc, char *tokens[], int ntok, int lineno) {
    /* Syntax:
     *   action <S2R|R2S> pkt <n> <drop|delay <ms>|corrupt [bit]|duplicate|reorder>
     *   action <S2R|R2S> seq <seq> [occ <n>] <drop|delay <ms>|corrupt [bit]|duplicate|reorder>
     */
    if (ntok < 5) {
        fprintf(stderr, "scenario line %d: incomplete action directive\n", lineno);
        return false;
    }

    struct pkt_action act;
    memset(&act, 0, sizeof(act));
    act.bit_index = -1;

    if (strcmp(tokens[1], "S2R") == 0) {
        act.dir = DIR_S2R;
    } else if (strcmp(tokens[1], "R2S") == 0) {
        act.dir = DIR_R2S;
    } else {
        fprintf(stderr, "scenario line %d: invalid direction '%s'\n", lineno, tokens[1]);
        return false;
    }

    if (strcmp(tokens[2], "pkt") == 0) {
        act.match_type = MATCH_PKT_IDX;
    } else if (strcmp(tokens[2], "seq") == 0) {
        act.match_type = MATCH_SEQ_NUM;
    } else {
        fprintf(stderr, "scenario line %d: expected 'pkt' or 'seq', got '%s'\n", lineno, tokens[2]);
        return false;
    }

    if (!parse_ulong_val(tokens[3], &act.match_value)) {
        fprintf(stderr, "scenario line %d: invalid match number '%s'\n", lineno, tokens[3]);
        return false;
    }

    int idx = 4;
    if (act.match_type == MATCH_SEQ_NUM && idx < ntok && strcmp(tokens[idx], "occ") == 0) {
        if (idx + 1 >= ntok || !parse_ulong_val(tokens[idx + 1], &act.occurrence)) {
            fprintf(stderr, "scenario line %d: invalid occ count\n", lineno);
            return false;
        }
        idx += 2;
    }

    if (idx >= ntok) {
        fprintf(stderr, "scenario line %d: missing action type\n", lineno);
        return false;
    }

    const char *kind = tokens[idx++];
    if (strcmp(kind, "drop") == 0) {
        act.kind = ACT_DROP;
    } else if (strcmp(kind, "delay") == 0) {
        act.kind = ACT_DELAY;
        if (idx >= ntok || !parse_double_val(tokens[idx++], &act.delay_ms) || act.delay_ms < 0.0) {
            fprintf(stderr, "scenario line %d: invalid delay ms\n", lineno);
            return false;
        }
    } else if (strcmp(kind, "corrupt") == 0) {
        act.kind = ACT_CORRUPT;
        if (idx < ntok) {
            if (!parse_int_val(tokens[idx++], &act.bit_index) || act.bit_index < 0) {
                fprintf(stderr, "scenario line %d: invalid bit index\n", lineno);
                return false;
            }
        }
    } else if (strcmp(kind, "duplicate") == 0) {
        act.kind = ACT_DUPLICATE;
    } else if (strcmp(kind, "reorder") == 0) {
        act.kind = ACT_REORDER;
    } else {
        fprintf(stderr, "scenario line %d: unknown action '%s'\n", lineno, kind);
        return false;
    }

    if (idx != ntok) {
        fprintf(stderr, "scenario line %d: unexpected trailing token '%s'\n", lineno, tokens[idx]);
        return false;
    }

    return append_action(sc, &act);
}

static bool apply_kv(struct emu_scenario *sc, const char *key, const char *val, int lineno) {
    double dval = 0.0;
    int ival = 0;

    if (strcmp(key, "link_rate_bps") == 0) {
        if (!parse_double_val(val, &dval) || dval < 0.0) goto bad_val;
        sc->dir[DIR_S2R].link_rate_bps = dval;
        sc->dir[DIR_R2S].link_rate_bps = dval;
    } else if (strcmp(key, "link_rate_s2r_bps") == 0) {
        if (!parse_double_val(val, &dval) || dval < 0.0) goto bad_val;
        sc->dir[DIR_S2R].link_rate_bps = dval;
    } else if (strcmp(key, "link_rate_r2s_bps") == 0) {
        if (!parse_double_val(val, &dval) || dval < 0.0) goto bad_val;
        sc->dir[DIR_R2S].link_rate_bps = dval;
    } else if (strcmp(key, "queue_capacity") == 0) {
        if (!parse_int_val(val, &ival) || ival < 0) goto bad_val;
        sc->dir[DIR_S2R].queue_capacity = ival;
        sc->dir[DIR_R2S].queue_capacity = ival;
    } else if (strcmp(key, "queue_capacity_s2r") == 0) {
        if (!parse_int_val(val, &ival) || ival < 0) goto bad_val;
        sc->dir[DIR_S2R].queue_capacity = ival;
    } else if (strcmp(key, "queue_capacity_r2s") == 0) {
        if (!parse_int_val(val, &ival) || ival < 0) goto bad_val;
        sc->dir[DIR_R2S].queue_capacity = ival;
    } else if (strcmp(key, "prop_delay_ms") == 0) {
        if (!parse_double_val(val, &dval) || dval < 0.0) goto bad_val;
        sc->dir[DIR_S2R].prop_delay_ms = dval;
        sc->dir[DIR_R2S].prop_delay_ms = dval;
    } else if (strcmp(key, "prop_delay_s2r_ms") == 0) {
        if (!parse_double_val(val, &dval) || dval < 0.0) goto bad_val;
        sc->dir[DIR_S2R].prop_delay_ms = dval;
    } else if (strcmp(key, "prop_delay_r2s_ms") == 0) {
        if (!parse_double_val(val, &dval) || dval < 0.0) goto bad_val;
        sc->dir[DIR_R2S].prop_delay_ms = dval;
    } else if (strcmp(key, "jitter_ms") == 0) {
        if (!parse_double_val(val, &dval) || dval < 0.0) goto bad_val;
        sc->dir[DIR_S2R].jitter_ms = dval;
        sc->dir[DIR_R2S].jitter_ms = dval;
    } else if (strcmp(key, "jitter_s2r_ms") == 0) {
        if (!parse_double_val(val, &dval) || dval < 0.0) goto bad_val;
        sc->dir[DIR_S2R].jitter_ms = dval;
    } else if (strcmp(key, "jitter_r2s_ms") == 0) {
        if (!parse_double_val(val, &dval) || dval < 0.0) goto bad_val;
        sc->dir[DIR_R2S].jitter_ms = dval;
    } else if (strcmp(key, "data_loss") == 0) {
        if (!parse_double_val(val, &dval) || !valid_prob(dval)) goto bad_val;
        sc->dir[DIR_S2R].loss_prob = dval;
    } else if (strcmp(key, "ack_loss") == 0) {
        if (!parse_double_val(val, &dval) || !valid_prob(dval)) goto bad_val;
        sc->dir[DIR_R2S].loss_prob = dval;
    } else if (strcmp(key, "corrupt") == 0) {
        if (!parse_double_val(val, &dval) || !valid_prob(dval)) goto bad_val;
        sc->dir[DIR_S2R].corrupt_prob = dval;
        sc->dir[DIR_R2S].corrupt_prob = dval;
    } else if (strcmp(key, "corrupt_s2r") == 0) {
        if (!parse_double_val(val, &dval) || !valid_prob(dval)) goto bad_val;
        sc->dir[DIR_S2R].corrupt_prob = dval;
    } else if (strcmp(key, "corrupt_r2s") == 0) {
        if (!parse_double_val(val, &dval) || !valid_prob(dval)) goto bad_val;
        sc->dir[DIR_R2S].corrupt_prob = dval;
    } else if (strcmp(key, "duplicate") == 0) {
        if (!parse_double_val(val, &dval) || !valid_prob(dval)) goto bad_val;
        sc->dir[DIR_S2R].dup_prob = dval;
        sc->dir[DIR_R2S].dup_prob = dval;
    } else if (strcmp(key, "duplicate_s2r") == 0) {
        if (!parse_double_val(val, &dval) || !valid_prob(dval)) goto bad_val;
        sc->dir[DIR_S2R].dup_prob = dval;
    } else if (strcmp(key, "duplicate_r2s") == 0) {
        if (!parse_double_val(val, &dval) || !valid_prob(dval)) goto bad_val;
        sc->dir[DIR_R2S].dup_prob = dval;
    } else if (strcmp(key, "reorder") == 0) {
        if (!parse_double_val(val, &dval) || !valid_prob(dval)) goto bad_val;
        sc->dir[DIR_S2R].reorder_prob = dval;
        sc->dir[DIR_R2S].reorder_prob = dval;
    } else if (strcmp(key, "reorder_s2r") == 0) {
        if (!parse_double_val(val, &dval) || !valid_prob(dval)) goto bad_val;
        sc->dir[DIR_S2R].reorder_prob = dval;
    } else if (strcmp(key, "reorder_r2s") == 0) {
        if (!parse_double_val(val, &dval) || !valid_prob(dval)) goto bad_val;
        sc->dir[DIR_R2S].reorder_prob = dval;
    } else if (strcmp(key, "reorder_timeout_ms") == 0) {
        if (!parse_double_val(val, &dval) || dval < 0.0) goto bad_val;
        sc->dir[DIR_S2R].reorder_timeout_ms = dval;
        sc->dir[DIR_R2S].reorder_timeout_ms = dval;
    } else if (strcmp(key, "reorder_timeout_s2r_ms") == 0) {
        if (!parse_double_val(val, &dval) || dval < 0.0) goto bad_val;
        sc->dir[DIR_S2R].reorder_timeout_ms = dval;
    } else if (strcmp(key, "reorder_timeout_r2s_ms") == 0) {
        if (!parse_double_val(val, &dval) || dval < 0.0) goto bad_val;
        sc->dir[DIR_R2S].reorder_timeout_ms = dval;
    } else {
        fprintf(stderr, "scenario line %d: unknown key '%s'\n", lineno, key);
        return false;
    }

    return true;

bad_val:
    fprintf(stderr, "scenario line %d: invalid value '%s' for key '%s'\n", lineno, val, key);
    return false;
}

bool scenario_load_file(struct emu_scenario *sc, const char *path) {
    FILE *fp = fopen(path, "r");
    if (!fp) {
        perror("failed to open scenario file");
        return false;
    }

    char line[512];
    int lineno = 0;
    bool ok = true;

    while (fgets(line, sizeof(line), fp) != NULL) {
        lineno++;

        char *comment = strchr(line, '#');
        if (comment) {
            *comment = '\0';
        }

        char *p = line;
        while (*p && isspace((unsigned char)*p)) {
            p++;
        }
        if (*p == '\0') {
            continue;
        }

        if (strncmp(p, "action", 6) == 0 && isspace((unsigned char)p[6])) {
            char *tokens[16];
            int ntok = 0;
            char *saveptr = NULL;
            char *tok = strtok_r(p, " \t\r\n", &saveptr);
            while (tok != NULL && ntok < 16) {
                tokens[ntok++] = tok;
                tok = strtok_r(NULL, " \t\r\n", &saveptr);
            }
            if (!parse_action_tokens(sc, tokens, ntok, lineno)) {
                ok = false;
                break;
            }
            continue;
        }

        char *eq = strchr(p, '=');
        if (eq) {
            *eq = ' ';
        }

        char *saveptr = NULL;
        char *key = strtok_r(p, " \t\r\n", &saveptr);
        char *val = strtok_r(NULL, " \t\r\n", &saveptr);
        char *extra = strtok_r(NULL, " \t\r\n", &saveptr);

        if (!key || !val || extra != NULL) {
            fprintf(stderr, "scenario line %d: expected 'key = value'\n", lineno);
            ok = false;
            break;
        }

        if (!apply_kv(sc, key, val, lineno)) {
            ok = false;
            break;
        }
    }

    fclose(fp);
    return ok;
}
