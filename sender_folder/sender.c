#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <arpa/inet.h>
#include <sys/socket.h>

#include "packet.h"
#include "SW_helpers.h"
#include "sender_config.h"
#include "utilities.h"
#include "logger.h"

int main(int argc, char* argv[]){

    struct configuration config;
    defaults_configurations(&config);
    parse_configurations(&config , argc , argv);
    
    FILE* file = fopen(config.file_name , "rb");
    if(!file){
        printf("failed to open file\n");
        exit(EXIT_FAILURE);
    }
    
    if(!open_logFile(config.log_file_name)){
        printf("failed to open log file\n");
        exit(EXIT_FAILURE);
    }

    int sockfd;
    if((sockfd = socket(AF_INET , SOCK_DGRAM , 0)) < 0){
        perror("socket creation failed\n");
        fclose(file);
        exit(EXIT_FAILURE);
    }

    struct sockaddr_in emulator_addr;
    init_sockaddrr_in(&emulator_addr , config.emulator_IP , config.emulator_port);

    if(config.arq_type == STOP_AND_WAIT) send_file_SW(sockfd , emulator_addr , file , &config); // if false return failure remember to add


    fclose(file);
    close_logFile();
    close(sockfd);

    return 0;
    

}