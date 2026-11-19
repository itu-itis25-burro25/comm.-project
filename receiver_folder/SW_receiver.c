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

    uint32_t exp_seq = 0;

    while(1){
        
        set_receive_timeout(sockfd , config->timeout_duration);

        ssize_t recv_bytes;
        enum recv_result result = receive_packet(sockfd , &packet , &source_addr , &recv_bytes);

        if(result == RECV_TIMEOUT){
            fprintf(stderr , "nothing received for %.0f s , giving up\n" , config->timeout_duration);
            return false;
        }
        if(result == RECV_ERROR) return false;

        if(!is_senderPacket_valid(&packet , recv_bytes)) continue;

        uint32_t seq_num = packet.header.seq_num;

        if(packet.header.type == DATA_PACKET){
            if(seq_num == exp_seq){
                if(!write_payload(file , &packet)) return false;
                exp_seq = (exp_seq + 1) % (1 << config->k);
                printf("DATA received succesfully (seq: %d)\n" , seq_num);
            }

            send_ack(sockfd , &source_addr , seq_num);
        }else if(seq_num == exp_seq){
            send_ack(sockfd , &source_addr , seq_num);
            printf("FIN received successfully (seq: %d)\n" , seq_num);

            linger_and_reack__fin(sockfd , seq_num , config->linger_duration);
            return true;
        }
    }

}

