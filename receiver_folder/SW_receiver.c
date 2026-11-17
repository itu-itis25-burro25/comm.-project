#include <unistd.h>
#include <sys/socket.h>

#include "SW_receiver.h"
#include "packet.h"
#include "receiver_helpers.h"
#include "receiver_config.h"
#include "utilities.h"

bool receive_file_SW(int sockfd , FILE* file , struct configuration* config){
    
    struct data_packet packet;

    struct sockaddr_in source_addr;
    init_sockaddrr_in(&source_addr , config->emulator_IP , config->emulator_port);

    ssize_t recv_bytes = receive_packet(sockfd , &packet , &source_addr);

    if(!is_data_packet_valid(&packet , recv_bytes)) return false;

    if(!send_ack(sockfd , &source_addr , 0)) return false;

}