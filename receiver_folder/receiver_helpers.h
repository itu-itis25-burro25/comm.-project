#ifndef _RECEIVER_HELPERS_H
#define _RECEIVER_HELPERS_H

#include <stdio.h>
#include <stdbool.h>
#include <stdint.h>
#include <sys/socket.h>

#include "packet.h"


ssize_t receive_packet(int sockfd , struct data_packet* packet , struct sockaddr_in* source_addr);
bool is_data_packet_valid(struct data_packet* packet , size_t packet_size);
bool send_ack(int sockfd , struct sockaddr_in* destination_addr , uint32_t ack_num);
bool write_payload(FILE* file , const struct data_packet* packet);

#endif