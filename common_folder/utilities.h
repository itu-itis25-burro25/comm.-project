#ifndef _UTILITIES_H
#define _UTILITIES_H

#include <arpa/inet.h>
#include <sys/socket.h>

#include "packet.h"

void init_sockaddrr_in(struct sockaddr_in* s , const char* IP_address , uint16_t port);
size_t build_data_packet(struct data_packet *packet , uint32_t seq , size_t bytes_read , enum packet_type type);

void update_RTT_estimator(struct timeval send_time , 
                          struct timeval recv_time,
                          double* srtt,
                          double* rtt_variation,
                          double* rto,
                          double max_rto,
                          double min_rto);
double tv_to_sec(struct timeval* t);

#endif