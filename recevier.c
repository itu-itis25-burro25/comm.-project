#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include <arpa/inet.h>
#include <sys/socket.h>

#include <packet.h>

#define RECEIVER_PORT 5001
#define RECEIVER_IP "127.0.0.1"

int main(int argc , char* argv[]){
    if(argc != 2){
        exit(EXIT_FAILURE); 
    }

    FILE* file = fopen(argv[1] , "wb");
    if(!file){
        printf("couldn't open file\n");
        exit(EXIT_FAILURE);
    }

    int sockfd;
    struct sockaddr_in rec_addr , emulator_addr;

    if(sockfd = socket(AF_INET , SOCK_DGRAM , 0)){
        perror("Socket creation failed");
        fclose(file);
        exit(EXIT_FAILURE);
    }

    memset(&rec_addr , 0 , sizeof(rec_addr));
    rec_addr.sin_family = AF_INET;
    rec_addr.sin_port = htons(RECEIVER_PORT);
    rec_addr.sin_addr.s_addr = inet_addr(RECEIVER_IP);

    if(!bind(sockfd , (struct sockaddr*)&rec_addr , sizeof(rec_addr))){
        perror("couldn't bind rec_addr\n");
        close(sockfd);
        fclose(file);
        exit(EXIT_FAILURE);
    }

    printf("\n\nUDP RECEIVER LISTENING ON THE PORT...\n\n");

    struct data_packet packet;

    

}
