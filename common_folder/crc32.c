#include "crc32.h"

static void setCRC32_to_packet(struct data_packet* packet , size_t packet_size){
    packet->header.checksum = 0;
    uint32_t crc = crc32_compute((const uint8_t*)packet , sizeof(packet)); // this part will need to be changed

    packet->header.checksum = crc;
}
static bool isPacket_CRC32_valid(struct data_packet* packet , size_t packet_size){
    return (packet->header.checksum == crc32_compute((uint8_t*)packet , sizeof(packet))); // this part must be changed too
}

