#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include <arpa/inet.h>
#include <sys/socket.h>

#include <packet.h>

#define EMULATOR_PORT 5001
#define EMULATOR_IP "127.0.0.1"


int main(int argc, char* argv[]){
    int sockfd;
    char* message = "hello from the server udp\n";

    sockfd = socket(AF_INET , SOCK_DGRAM , 0);

    FILE* file = fopen(argv[1] , "rb");
    if(!file){
        printf("failed to open file");
        exit(EXIT_FAILURE);
    }

    struct sockaddr_in emulator_addr;

    memset(&emulator_addr , 0 , sizeof(emulator_addr));
    emulator_addr.sin_family = AF_INET;
    emulator_addr.sin_addr.s_addr = inet_addr(EMULATOR_IP);
    emulator_addr.sin_port = htons(EMULATOR_PORT);

    struct data_packet packet;
    uint32_t sequence_number = 0;
    size_t bytes_read;

    printf("starting file transfer...\n");

    while((bytes_read = fread(packet.payload , 1 , MAX_PAYLOAD_SIZE , file)) > 0){
        packet.header.type = 0; // data
        packet.header.seq_num = sequence_number++;
        packet.header.payload_length = bytes_read;

        //TODO IMPLEMENT THE CRC FUNCTION HERE
        packet.header.checksum = 0;

        size_t packet_size = bytes_read + sizeof(struct packet_header);

        ssize_t sent_bytes = sendto(sockfd , &packet , packet_size , 0 , (struct sockaddr*)&emulator_addr , sizeof(emulator_addr));
        if(sent_bytes < 0){
            printf("error happened while sending files");
            break;
        }

        //MAJOR PART IS HERE LIKE YOU WILL CHECK IF sent_bytes MATCH AND WAIT FOR ack 
        //AND SO ON... TIMEOUT SO SENDING AGAIN AND THESE STUFFs


    }

    fclose(file);
    close(sockfd);

    return 0;
    

}