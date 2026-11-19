#ifndef _SENDER_HELPERS_H
#define _SENDER_HELPERS_H

#include <unistd.h>
#include <stddef.h>
#include <stdint.h>
#include <sys/time.h>
#include <sys/socket.h>
#include <stdbool.h>
#include <netinet/in.h>

#include "packet.h"
#

enum ack_result{
    ACK_OK,
    ACK_ERROR,
    ACK_TIMEOUT
};


bool is_ack_valid(struct data_packet* ack , size_t packet_size);
enum ack_result wait_for_ack(int sockfd , uint32_t exp_seq , double deadline , struct timeval* recv_time);
bool send_packet(int sockfd , struct sockaddr_in* emulator_address , struct data_packet* packet , size_t packet_size);

#endif