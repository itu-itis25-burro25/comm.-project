#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include <unistd.h>
#include <arpa/inet.h>
#include <sys/socket.h>

#include "packet.h"

#define RECEIVER_PORT 5001
#define RECEIVER_IP "127.0.0.1"

int main(int argc , char* argv[]){
    if(argc != 2){
        fprintf(stderr , "required format:%s <file_name>\n" , argv[0]);
        exit(EXIT_FAILURE); 
    }

    FILE* file = fopen(argv[1] , "wb");
    if(!file){
        printf("couldn't open file\n");
        exit(EXIT_FAILURE);
    }

    int sockfd;
    if((sockfd = socket(AF_INET , SOCK_DGRAM , 0)) < 0){
        perror("Socket creation failed");
        fclose(file);
        exit(EXIT_FAILURE);
    }

    struct sockaddr_in recv_addr , emulator_addr;

    memset(&recv_addr , 0 , sizeof(recv_addr));
    recv_addr.sin_family = AF_INET;
    recv_addr.sin_port = htons(RECEIVER_PORT);
    recv_addr.sin_addr.s_addr = inet_addr(RECEIVER_IP);

    if(bind(sockfd , (struct sockaddr*)&recv_addr , sizeof(recv_addr)) < 0){
        perror("couldn't bind recv_addr\n");
        close(sockfd);
        fclose(file);
        exit(EXIT_FAILURE);
    }

    printf("\n\nUDP RECEIVER LISTENING ON THE PORT...\n\n");

    struct data_packet packet;

    while(1){

        socklen_t emulator_addr_length = sizeof(emulator_addr_length);
        ssize_t recv_bytes = recvfrom(sockfd , &packet , sizeof(packet), 0 , (struct sockaddr*)&emulator_addr , &emulator_addr_length);

        if(recv_bytes < 0){
            perror("receive failed\n");
            break;
        }

        if(recv_bytes < sizeof(struct packet_header)){
            fprintf(stderr , "recv data packet to small\n");
            continue;
        }

        //check if crc is correct and validate ack

        //check final packert terminating one

        printf("received packet sequence number: %u\npayload size: %u\n" , packet.header.seq_num , packet.header.payload_length);
        fwrite(packet.payload , 1 , packet.header.payload_length , file);

        struct data_packet ack;
        ack.header.type = ACK_PACKET;
        ack.header.checksum = 0; // ADD THE CRC32 FUNCTION
        ack.header.payload_length = 0;
        ack.header.seq_num = packet.header.seq_num;

        printf("sending acknoldegment...\n");
        ssize_t sent_bytes = sendto(sockfd , &ack , sizeof(struct packet_header) , 0 , (struct sockaddr*)&emulator_addr , sizeof(emulator_addr));

        if(sent_bytes < 0){
            perror("couldn't send the ack!!\n");

        }

        break;

    }

    fclose(file);
    close(sockfd);
    return 0;

}
