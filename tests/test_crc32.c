#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "crc32.h"
#include "packet.h"

int main(void) {
    /* 1. Standard IEEE 802.3 CRC-32 check value for ASCII "123456789". */
    const char *vec = "123456789";
    uint32_t crc_vec = crc32_compute((const uint8_t *)vec, 9);
    printf("crc32(\"123456789\") = 0x%08X (expected 0xCBF43926)\n", crc_vec);
    assert(crc_vec == 0xCBF43926u);

    /* 2. Empty buffer check. */
    uint32_t crc_empty = crc32_compute((const uint8_t *)"", 0);
    assert(crc_empty == 0x00000000u);

    /* 3. Full packet single-bit flip check across header and payload. */
    struct data_packet pkt;
    memset(&pkt, 0, sizeof(pkt));
    pkt.header.type = DATA_PACKET;
    pkt.header.seq_num = 42;
    pkt.header.payload_length = 100;
    for (size_t i = 0; i < 100; i++) {
        pkt.payload[i] = (char)(i * 7 + 3);
    }

    size_t pkt_size = sizeof(struct packet_header) + 100;
    setCRC32_to_packet(&pkt, pkt_size);
    assert(isPacket_CRC32_valid(&pkt, pkt_size));

    uint8_t *raw = (uint8_t *)&pkt;
    for (size_t byte_i = 0; byte_i < pkt_size; byte_i++) {
        for (int bit = 0; bit < 8; bit++) {
            raw[byte_i] ^= (uint8_t)(1u << bit);
            assert(!isPacket_CRC32_valid(&pkt, pkt_size));
            raw[byte_i] ^= (uint8_t)(1u << bit);
            assert(isPacket_CRC32_valid(&pkt, pkt_size));
        }
    }

    printf("test_crc32: ALL CHECKS PASSED (%zu bits tested)\n", pkt_size * 8);
    return 0;
}
