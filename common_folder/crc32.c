#include "crc32.h"

#define CRC32_IEEE8023_POLY 0xEDB88320u

static uint32_t crc32_table[256];
static bool crc32_table_ready = false;

static void crc32_init_table(void) {
    for (uint32_t i = 0; i < 256; i++) {
        uint32_t c = i;
        for (int bit = 0; bit < 8; bit++) {
            if (c & 1u) {
                c = CRC32_IEEE8023_POLY ^ (c >> 1);
            } else {
                c >>= 1;
            }
        }
        crc32_table[i] = c;
    }
    crc32_table_ready = true;
}

uint32_t crc32_compute(const uint8_t *data, size_t len) {
    if (!crc32_table_ready) {
        crc32_init_table();
    }
    if (data == NULL || len == 0) {
        return 0u;
    }

    uint32_t crc = 0xFFFFFFFFu;
    for (size_t i = 0; i < len; i++) {
        crc = crc32_table[(crc ^ data[i]) & 0xFFu] ^ (crc >> 8);
    }
    return crc ^ 0xFFFFFFFFu;
}

void setCRC32_to_packet(struct data_packet *packet, size_t packet_size) {
    if (packet == NULL || packet_size < sizeof(struct packet_header)) {
        return;
    }
    packet->header.checksum = 0;
    uint32_t crc = crc32_compute((const uint8_t *)packet, packet_size);
    packet->header.checksum = crc;
}

bool isPacket_CRC32_valid(struct data_packet *packet, size_t packet_size) {
    if (packet == NULL || packet_size < sizeof(struct packet_header)) {
        return false;
    }
    uint32_t received_crc = packet->header.checksum;
    packet->header.checksum = 0;
    uint32_t calculated_crc = crc32_compute((const uint8_t *)packet, packet_size);
    packet->header.checksum = received_crc;
    return received_crc == calculated_crc;
}
