#ifndef _RECEIVER_HELPERS_H
#define _RECEIVER_HELPERS_H

#include <stdio.h>
#include <stdbool.h>
#include <stdint.h>
#include <sys/socket.h>
#include <netinet/in.h>

#include "packet.h"

enum recv_result { RECV_OK , RECV_TIMEOUT , RECV_ERROR };

bool set_receive_timeout(int sockfd , double seconds);
enum recv_result receive_packet(int sockfd , struct data_packet* packet , struct sockaddr_in* source_addr , ssize_t* recv_bytes);
bool is_senderPacket_valid(struct data_packet* packet , size_t packet_size);
bool send_ack(int sockfd , struct sockaddr_in* destination_addr , uint32_t ack_num);
bool write_payload(FILE* file , const struct data_packet* packet);
void linger_and_reack__fin(int sockfd , uint32_t fin_seq , double linger_seconds);

#endif