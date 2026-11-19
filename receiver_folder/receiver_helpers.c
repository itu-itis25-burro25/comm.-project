#include <sys/socket.h>
#include <stdlib.h>
#include <stdio.h>
#include <errno.h>
#include <sys/time.h>

#include "receiver_helpers.h"
#include "crc32.h"
#include "utilities.h"

bool set_receive_timeout(int sockfd , double seconds){
    struct timeval tv;

    tv.tv_sec = (time_t)seconds;
    tv.tv_usec = (suseconds_t)((seconds - tv.tv_sec) * 1000000.0);
    if(tv.tv_sec == 0 && tv.tv_usec == 0) tv.tv_usec = 1;

    if(setsockopt(sockfd , SOL_SOCKET , SO_RCVTIMEO , &tv , sizeof(tv)) < 0){
        perror("set_receive_timeout function - setsockopt\n");
        return false;
    }

    return true;
}

enum recv_result receive_packet(int sockfd , struct data_packet* packet , struct sockaddr_in* source_addr , ssize_t* recv_bytes){

    while(1){

        socklen_t source_addr_length = sizeof(*source_addr);
        ssize_t size = recvfrom(sockfd , packet , sizeof(*packet) , 0 , 
                                (struct sockaddr*)source_addr , &source_addr_length);
    
        if(size >= 0){
            *recv_bytes = size;
            return RECV_OK;
        }

        if(errno == EAGAIN || errno == EWOULDBLOCK) return RECV_TIMEOUT;
        if(errno == EINTR) continue;
        
        perror("recvfrom error");
        return RECV_ERROR;
    }

}

bool is_senderPacket_valid(struct data_packet* packet , size_t packet_size){

    if (packet_size < sizeof(struct packet_header))
        return false;

    if (packet->header.type != DATA_PACKET && packet->header.type != FIN_PACKET)
        return false;

    if (packet->header.payload_length > MAX_PAYLOAD_SIZE)
        return false;

    if (packet_size != sizeof(struct packet_header)
                     + packet->header.payload_length)
        return false;

    if (!isPacket_CRC32_valid(packet, packet_size))
        return false;

    return true;
}

bool send_ack(int sockfd , struct sockaddr_in* destination_addr , uint32_t ack_num){
    struct data_packet ack;
    size_t ack_size = build_data_packet(&ack , ack_num , 0 , ACK_PACKET);

    ssize_t send_bytes = sendto(sockfd , &ack , ack_size , 0 , (struct sockaddr*)destination_addr , sizeof(*destination_addr));

    if(send_bytes < 0){
        perror("couldn't send ACK\n");
        return false;
    }
    if(send_bytes != ack_size){
        fprintf(stderr , "incomplete ACK transmission");
        return false;
    }

    return true;
}

bool write_payload(FILE* file , const struct data_packet* packet){
    size_t written = fwrite(packet->payload , 1 , packet->header.payload_length , file);
    return written == packet->header.payload_length;
}

void linger_and_reack__fin(int sockfd , uint32_t fin_seq , double linger_seconds){
    struct data_packet packet;
    struct sockaddr_in source_addr;
    
    if(!set_receive_timeout(sockfd , linger_seconds)) return;

    ssize_t recv_bytes;
    while(receive_packet(sockfd , &packet , &source_addr , &recv_bytes) == RECV_OK){
        if(!is_senderPacket_valid(&packet , recv_bytes)) continue;

        if(packet.header.type == FIN_PACKET && packet.header.seq_num == fin_seq){
            send_ack(sockfd , &source_addr , fin_seq);
        }
    }
}


