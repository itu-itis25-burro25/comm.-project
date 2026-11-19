#include <assert.h>
#include <stdio.h>
#include <string.h>

#include "link.h"
#include "packet.h"
#include "prng.h"
#include "scenario.h"

static size_t make_test_pkt(uint8_t *buf, uint8_t type, uint32_t seq, size_t payload_len) {
    struct packet_header hdr;
    hdr.type = type;
    hdr.seq_num = seq;
    hdr.payload_length = (uint16_t)payload_len;
    hdr.checksum = 0x12345678u;
    memcpy(buf, &hdr, sizeof(hdr));
    for (size_t i = 0; i < payload_len; i++) {
        buf[sizeof(hdr) + i] = (uint8_t)((seq + i) & 0xFFu);
    }
    return sizeof(hdr) + payload_len;
}

static int popcount_diff(const uint8_t *a, const uint8_t *b, size_t len) {
    int diff = 0;
    for (size_t i = 0; i < len; i++) {
        uint8_t x = a[i] ^ b[i];
        while (x) {
            diff += (int)(x & 1u);
            x >>= 1;
        }
    }
    return diff;
}

static void test_prng_determinism(void) {
    struct prng_state a1, a2, b1;
    prng_init(&a1, 987654321ULL, 0);
    prng_init(&a2, 987654321ULL, 0);
    prng_init(&b1, 987654321ULL, 1);

    int same_count = 0;
    for (int i = 0; i < 100; i++) {
        uint64_t v1 = prng_next_u64(&a1);
        uint64_t v2 = prng_next_u64(&a2);
        uint64_t v3 = prng_next_u64(&b1);
        assert(v1 == v2);
        if (v1 == v3) same_count++;
        double u1 = prng_uniform01(&a1);
        double u2 = prng_uniform01(&a2);
        assert(u1 == u2);
        assert(u1 >= 0.0 && u1 < 1.0);
    }
    assert(same_count == 0);
    printf("  [PASS] prng determinism & stream independence\n");
}

static void test_scenario_parser(void) {
    const char *tmp_path = "/tmp/blg337_unit_test.scn";
    FILE *fp = fopen(tmp_path, "w");
    assert(fp != NULL);
    fprintf(fp,
            "# unit test scenario\n"
            "link_rate_bps = 2000000\n"
            "queue_capacity = 64\n"
            "prop_delay_ms = 15\n"
            "jitter_ms = 2.5\n"
            "data_loss = 0.05\n"
            "ack_loss = 0.02\n"
            "corrupt = 0.01\n"
            "duplicate = 0.03\n"
            "reorder = 0.04\n"
            "action S2R pkt 5 drop\n"
            "action R2S seq 12 occ 2 delay 200\n");
    fclose(fp);

    struct emu_scenario sc;
    scenario_init_defaults(&sc);
    assert(scenario_load_file(&sc, tmp_path));
    assert(sc.dir[DIR_S2R].link_rate_bps == 2000000.0);
    assert(sc.dir[DIR_S2R].queue_capacity == 64);
    assert(sc.dir[DIR_S2R].prop_delay_ms == 15.0);
    assert(sc.dir[DIR_S2R].jitter_ms == 2.5);
    assert(sc.dir[DIR_S2R].loss_prob == 0.05);
    assert(sc.dir[DIR_R2S].loss_prob == 0.02);
    assert(sc.dir[DIR_S2R].corrupt_prob == 0.01);
    assert(sc.action_count == 2);
    assert(sc.actions[0].kind == ACT_DROP && sc.actions[0].match_value == 5);
    assert(sc.actions[1].kind == ACT_DELAY && sc.actions[1].occurrence == 2 && sc.actions[1].delay_ms == 200.0);

    scenario_free(&sc);
    remove(tmp_path);
    printf("  [PASS] scenario parser\n");
}

static void test_serialization_and_fifo(void) {
    struct emu_scenario sc;
    scenario_init_defaults(&sc);
    /* Baseline: 1 Mbit/s, 20 ms prop delay, queue capacity = 2 */
    sc.dir[DIR_S2R].queue_capacity = 2;

    struct emu_state st;
    assert(emu_state_init(&st, &sc, 42));

    uint8_t buf[2048];
    size_t len = make_test_pkt(buf, DATA_PACKET, 0, 1024); /* 1035 bytes -> 8280 us @ 1 Mbit/s */

    emu_handle_arrival(&st, DIR_S2R, buf, len, 0);
    emu_handle_arrival(&st, DIR_S2R, buf, len, 100);
    /* Queue capacity = 2: 3rd packet at t=200 must be dropped while first two are still in TX queue. */
    emu_handle_arrival(&st, DIR_S2R, buf, len, 200);

    struct scheduled_pkt p1, p2;
    assert(eq_pop(&st.eq, &p1));
    assert(eq_pop(&st.eq, &p2));
    assert(eq_peek(&st.eq) == NULL); /* 3rd packet dropped */

    assert(p1.tx_end_us == 8280);
    assert(p1.deliver_us == 28280);
    assert(p2.tx_end_us == 16560);
    assert(p2.deliver_us == 36560);

    emu_state_free(&st);
    scenario_free(&sc);
    printf("  [PASS] serialization L/R & FIFO queue drop\n");
}

static void test_jitter_order_preservation(void) {
    struct emu_scenario sc;
    scenario_init_defaults(&sc);
    sc.dir[DIR_S2R].link_rate_bps = 0;
    sc.dir[DIR_S2R].prop_delay_ms = 20.0;
    sc.dir[DIR_S2R].jitter_ms = 10.0;
    sc.dir[DIR_S2R].queue_capacity = 256;

    struct emu_state st;
    assert(emu_state_init(&st, &sc, 777));

    uint8_t buf[2048];
    size_t len = make_test_pkt(buf, DATA_PACKET, 0, 100);
    for (int i = 0; i < 100; i++) {
        emu_handle_arrival(&st, DIR_S2R, buf, len, (int64_t)i * 1000LL);
    }

    int64_t prev_deliver = -1;
    uint32_t expected_idx = 1;
    struct scheduled_pkt pkt;
    while (eq_pop(&st.eq, &pkt)) {
        assert(pkt.deliver_us >= prev_deliver);
        assert(pkt.pkt_idx == expected_idx++);
        prev_deliver = pkt.deliver_us;
    }
    assert(expected_idx == 101);

    emu_state_free(&st);
    scenario_free(&sc);
    printf("  [PASS] jitter order preservation across 100 packets\n");
}

static void test_impairments_and_actions(void) {
    struct emu_scenario sc;
    scenario_init_defaults(&sc);
    sc.dir[DIR_S2R].link_rate_bps = 0;
    sc.dir[DIR_S2R].prop_delay_ms = 10.0;
    sc.dir[DIR_S2R].reorder_timeout_ms = 50.0;

    struct emu_state st;
    /* Set up deterministic actions via temp file */
    const char *tmp_path = "/tmp/blg337_unit_imp.scn";
    FILE *fp = fopen(tmp_path, "w");
    assert(fp != NULL);
    fprintf(fp,
            "link_rate_bps = 0\n"
            "prop_delay_ms = 10\n"
            "reorder_timeout_ms = 50\n"
            "action S2R pkt 1 corrupt\n"
            "action S2R pkt 2 duplicate\n"
            "action S2R pkt 3 reorder\n"
            "action S2R pkt 5 drop\n"
            "action S2R pkt 6 delay 100\n"
            "action S2R pkt 7 reorder\n");
    fclose(fp);

    assert(scenario_load_file(&sc, tmp_path));
    assert(emu_state_init(&st, &sc, 123));

    uint8_t orig[7][64];
    size_t len = sizeof(struct packet_header) + 20;
    for (int i = 0; i < 7; i++) {
        make_test_pkt(orig[i], DATA_PACKET, (uint32_t)i, 20);
        emu_handle_arrival(&st, DIR_S2R, orig[i], len, (int64_t)i * 1000LL);
    }

    /* Trigger lone held packet 7 timeout (deliver_us=16ms + timeout=50ms = 66ms). */
    emu_check_reorder_timeouts(&st, 66000);

    /* Expected delivery order:
     * pkt 1 (1-bit corrupted, deliver 10ms)
     * pkt 2 (deliver 11ms), pkt 2 dup (deliver 11ms)
     * pkt 4 (deliver 13ms), pkt 3 (reordered after pkt 4, deliver 13ms)
     * pkt 5 dropped
     * pkt 7 (lone held timeout at 66ms)
     * pkt 6 (delayed +100ms -> deliver 115ms)
     */
    struct scheduled_pkt out;

    assert(eq_pop(&st.eq, &out));
    assert(out.pkt_idx == 1 && popcount_diff(out.data, orig[0], len) == 1);

    assert(eq_pop(&st.eq, &out));
    assert(out.pkt_idx == 2 && !out.is_duplicate);
    assert(eq_pop(&st.eq, &out));
    assert(out.pkt_idx == 2 && out.is_duplicate);

    assert(eq_pop(&st.eq, &out));
    assert(out.pkt_idx == 4);
    assert(eq_pop(&st.eq, &out));
    assert(out.pkt_idx == 3);

    assert(eq_pop(&st.eq, &out));
    assert(out.pkt_idx == 7 && out.deliver_us == 66000);

    assert(eq_pop(&st.eq, &out));
    assert(out.pkt_idx == 6 && out.deliver_us == 115000);

    assert(eq_peek(&st.eq) == NULL);

    emu_state_free(&st);
    scenario_free(&sc);
    remove(tmp_path);
    printf("  [PASS] corruption (1-bit), duplicate, reorder, lone-packet timeout, drop, delay\n");
}

int main(void) {
    printf("Running emulator unit tests...\n");
    test_prng_determinism();
    test_scenario_parser();
    test_serialization_and_fifo();
    test_jitter_order_preservation();
    test_impairments_and_actions();
    printf("test_emulator_unit: ALL TESTS PASSED\n");
    return 0;
}
