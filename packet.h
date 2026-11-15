#ifndef _PACKETS_H
#define _PACKETS_H

#include <stdint.h>

#define MAX_PAYLOAD_SIZE 1400


struct packet_header{
    uint8_t type; // 0-data , 1-acknoledgment
    uint32_t seq_num;
    uint16_t payload_length;
    uint32_t checksum;
};

struct data_packet{
    struct packet_header header;
    char payload[MAX_PAYLOAD_SIZE];
};


#endif