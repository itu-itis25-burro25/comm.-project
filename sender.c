#include <stdio.h>
#include <stdlib.h>
#include <stdbool.h>
#include <stdint.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>
#include <sys/time.h>
#include <arpa/inet.h>
#include <sys/socket.h>

#include "packet.h"
#include "SW_helpers.h"

#define EMULATOR_PORT 5001
#define EMULATOR_IP "127.0.0.1"


int main(int argc, char* argv[]){

    if(argc != 3){
        fprintf(stderr , "required formate:%s <file_name> <ARQ_type>\n" , argv[0]);
        exit(EXIT_FAILURE);
    }

    FILE* file = fopen(argv[1] , "rb");
    if(!file){
        printf("failed to open file\n");
        exit(EXIT_FAILURE);
    }

    enum ARQ_type arq_type;
    if(strcmp(argv[2] , "STOP_AND_WAIT") == 0){
        arq_type = STOP_AND_WAIT;
    }else if(strcmp(argv[2] , "GO_BACK_N") == 0){
        arq_type = GO_BACK_N;
    }else if(strcmp(argv[2] , "SELECTIVE") == 0){
        arq_type = SELECTIVE;
    }
    
    int sockfd;
    if((sockfd = socket(AF_INET , SOCK_DGRAM , 0)) < 0){
        perror("socket creation failed\n");
        fclose(file);
        exit(EXIT_FAILURE);
    }

    struct sockaddr_in emulator_addr;

    //INITIALIZING "emulator_addr"
    memset(&emulator_addr , 0 , sizeof(emulator_addr));
    emulator_addr.sin_family = AF_INET;
    emulator_addr.sin_addr.s_addr = inet_addr(EMULATOR_IP);
    emulator_addr.sin_port = htons(EMULATOR_PORT);

    struct data_packet packet , ack;

    if(arq_type == STOP_AND_WAIT) send_file_SW(sockfd , emulator_addr , file);
 

    fclose(file);
    close(sockfd);

    return 0;
    

}