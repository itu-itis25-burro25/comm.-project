#include <stdio.h>
#include <stdlib.h>
#include <stdbool.h>
#include <stdint.h>
#include <string.h>
#include <unistd.h>
#include <arpa/inet.h>
#include <sys/socket.h>

#include "packet.h"
#include "receiver_config.h"
#include "utilities.h"
#include "SW_receiver.h"


int main(int argc , char* argv[]){
    struct configuration config;
    defaults_configurations(&config);
    parse_configurations(&config , argc , argv);

    FILE* file = fopen(config.file_name , "wb");
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

    struct sockaddr_in recv_addr;
    init_sockaddrr_in(&recv_addr , config.receiver_IP , config.receiver_port);

    if(bind(sockfd , (struct sockaddr*)&recv_addr , sizeof(recv_addr)) < 0){
        perror("couldn't bind recv_addr\n");
        close(sockfd);
        fclose(file);
        exit(EXIT_FAILURE);
    }

    printf("\n\nUDP RECEIVER LISTENING ON THE PORT...\n\n");

    switch(config.arq_type){
        case STOP_AND_WAIT:
            if(!receive_file_SW(sockfd , file , &config)) exit(EXIT_FAILURE);
            break;
        case SELECTIVE:
        case GO_BACK_N:
            break;
    }

    fclose(file);
    close(sockfd);
    return 0;

}
