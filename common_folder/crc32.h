#ifndef _CRC32_H
#define _CRC32_H

#include <stdint.h>
#include <stdlib.h>

#include "packet.h"

uint32_t crc32_compute(const uint8_t *data , size_t len);

//assumed to be correct check it with you team member
static void setCRC32_to_packet(struct data_packet* packet , size_t packet_size);
static bool isPacket_CRC32_valid(struct data_packet* packet , size_t packet_size);

#endif