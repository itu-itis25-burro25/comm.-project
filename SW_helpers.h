#ifndef SW_HELPERS_H
#define SW_HELPERS_H

#include <stdio.h>
#include <stdbool.h>
#include <sys/time.h>
#include <arpa/inet.h>
#include <sys/socket.h>

#define MAX_RETRIES 5

enum ack_result{
    ACK_OK,
    ACK_ERROR,
    ACK_TIMEOUT
};

void update_RTT_estimator(struct timeval send_time , 
                          struct timeval recv_time,
                          double* srtt,
                          double* rtt_variation,
                          double* rto);

//sending file with stop(S) and wait(W) ARQ                       
bool send_file_SW(int sockfd , struct sockaddr_in emulator_addr , FILE* file);

#endif