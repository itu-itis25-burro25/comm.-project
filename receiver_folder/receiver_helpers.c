#include <sys/socket.h>
#include <stdlib.h>
#include <stdio.h>

#include "receiver_helpers.h"
#include "crc32.h"
#include "utilities.h"


ssize_t receive_packet(int sockfd , struct data_packet* packet , struct sockaddr_in* source_addr){

    socklen_t source_addr_length = sizeof(*source_addr);

    ssize_t recv_bytes = recvfrom(sockfd , packet , sizeof(*packet) , 0 , 
                                (struct sockaddr*)source_addr , &source_addr_length);

    if(recv_bytes < 0){
        perror("couldn't receive 'receive_packet' function\n");
        exit(EXIT_FAILURE);
    }
    if(recv_bytes < sizeof(struct packet_header)){
        perror("couldn't receive the full packet\n");
        exit(EXIT_FAILURE);
    }

    return recv_bytes;
}

bool is_data_packet_valid(struct data_packet* packet , size_t packet_size){

    if (packet_size < sizeof(struct packet_header))
        return false;

    if (packet->header.type != DATA_PACKET)
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
    build_data_packet(&ack , ack_num , 0 , ACK_PACKET);

    ssize_t send_bytes = sendto(sockfd , &ack , sizeof(ack) , 0 , (struct sockaddr*)destination_addr , sizeof(*destination_addr));

    if(send_bytes < 0){
        perror("couldn't send ACK\n");
        return false;
    }
    if(send_bytes > sizeof(struct packet_header)){
        fprintf(stderr , "incomplete ACK transmission");
        return false;
    }

    return true;
}

bool write_payload(FILE* file , const struct data_packet* packet){
    size_t written = fwrite(packet->payload , 1 , packet->header.payload_length , file);
    return written == packet->header.payload_length;
}
