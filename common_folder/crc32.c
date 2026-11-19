#include "crc32.h"

uint32_t crc32_compute(const uint8_t *data , size_t len){
    return 0;
}

void setCRC32_to_packet(struct data_packet* packet , size_t packet_size){
    packet->header.checksum = 0;
    uint32_t crc = crc32_compute((const uint8_t*)packet , packet_size); // this part will need to be changed

    packet->header.checksum = crc;
}
bool isPacket_CRC32_valid(struct data_packet* packet,
                                 size_t packet_size)
{
    uint32_t received_crc = packet->header.checksum;

    packet->header.checksum = 0;

    uint32_t calculated_crc = crc32_compute(
        (const uint8_t*)packet,
        packet_size
    );

    packet->header.checksum = received_crc;

    return received_crc == calculated_crc;
}

