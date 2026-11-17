#ifndef _PACKETS_H
#define _PACKETS_H

#include <stdint.h>

#define MAX_PAYLOAD_SIZE 1400 // this is unavoidable

enum packet_type{
    DATA_PACKET,
    ACK_PACKET,
    FIN_PACKET
};

struct __attribute__((packed)) packet_header{
    uint8_t type; // 0-data , 1-acknoledgment
    uint32_t seq_num;
    uint16_t payload_length;
    uint32_t checksum;
};

struct __attribute__((packed)) data_packet{
    struct packet_header header;
    char payload[MAX_PAYLOAD_SIZE];
};


#endif