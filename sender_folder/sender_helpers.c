#include <stdio.h>
#include <unistd.h>
#include <stdint.h>
#include <stdbool.h>
#include <errno.h>
#include <sys/time.h>

#include "sender_helpers.h"
#include "crc32.h"
#include "utilities.h"


bool is_ack_valid(struct data_packet* ack , size_t packet_size){
    if(packet_size != sizeof(struct packet_header)) return false;
    if(!isPacket_CRC32_valid(ack , packet_size)) return false;
    if(ack->header.type != ACK_PACKET) return false;

    return true;
}
enum ack_result wait_for_ack(int sockfd , uint32_t exp_seq , double deadline , struct timeval* recv_time){
    struct data_packet ack;

    while(1){
        struct timeval now;
        gettimeofday(&now , NULL);

        double remaining = deadline - tv_to_sec(&now);

        if(remaining <= 0) return ACK_TIMEOUT;

        struct timeval tv;
        tv.tv_sec = (time_t)remaining;
        tv.tv_usec = (suseconds_t)((remaining - tv.tv_sec) * 1000000.0);
        if(tv.tv_sec == 0 && tv.tv_usec == 0) tv.tv_usec = 1;

        if(setsockopt(sockfd , SOL_SOCKET , SO_RCVTIMEO , (const char*)&tv , sizeof(tv)) < 0){
            perror("setscokopt failed!!\n");
            return ACK_ERROR;
        }
        
        struct sockaddr_in temp_addr;
        socklen_t temp_addr_len = sizeof(temp_addr);

        ssize_t recv_bytes = recvfrom(sockfd , &ack , sizeof(ack) , 0 , 
                                    (struct sockaddr*)&temp_addr , &temp_addr_len);

        if(recv_bytes < 0){
            if(errno == EAGAIN || errno == EWOULDBLOCK) continue;
            perror("ack receive failed\n");
            return ACK_ERROR;
        }
        
        gettimeofday(recv_time , NULL);

        if(!is_ack_valid(&ack , recv_bytes)){
            printf("corrupt/invalid ACK(ignored)\n");
            continue;
        }

        if(ack.header.seq_num == exp_seq){
            printf("ACK received successfully (seq: %u)\n" , ack.header.seq_num);
            return ACK_OK;
        }

        printf("old/wrong ACK ignored (seq: %u)\n" , ack.header.seq_num);
    }
}
bool send_packet(int sockfd , struct sockaddr_in* emulator_address , struct data_packet* packet , size_t packet_size){

    ssize_t send_bytes = sendto(sockfd , packet , packet_size , 0 , 
        (struct sockaddr*)emulator_address , sizeof(*emulator_address));

    
    if(send_bytes < 0){
        perror("failed to send packet 'sendto' functcion\n");
        return false;
    }
    else if(send_bytes < packet_size){
        perror("couldn't send all the packet!!\n");
        return false;
    }

    return true;
}

