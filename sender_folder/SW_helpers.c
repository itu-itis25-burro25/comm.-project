#include <errno.h>
#include <string.h>
#include <stdint.h>
#include <stdlib.h>

#include "SW_helpers.h"
#include "packet.h"
#include "sender_config.h"
#include "crc32.h"
#include "utilities.h"
#include "sender_helpers.h"
#include "logger.h"

static bool transmit_until_acked(int sockfd , struct sockaddr_in* emulator_addr , struct data_packet* packet , 
                                 size_t packet_size , uint32_t seq_num , const struct configuration* config , double* srtt ,
                                 double* rtt_variation , double* rto){

    bool has_been_transmitted = false;
    int retries = 0;

    while(1){
        struct timeval send_time , recv_time;
        gettimeofday(&send_time , NULL);

        if(!send_packet(sockfd , emulator_addr , packet , packet_size)){
            return false;
        }
        log_normal_event(EV_SEND_DATA , seq_num, packet_size , has_been_transmitted);

        double deadline = tv_to_sec(&send_time) + *rto;
        enum ack_result result = wait_for_ack(sockfd , seq_num , deadline , &recv_time);

        if(result == ACK_ERROR) return false;
        if(result == ACK_OK){
            if(!has_been_transmitted){
                update_RTT_estimator(send_time , recv_time , srtt , rtt_variation , rto , config->max_rto , config->min_rto);

                long rtt_us = (recv_time.tv_sec - send_time.tv_sec) * 1000000L + (recv_time.tv_usec - send_time.tv_usec);
                log_rtt_event(EV_RTT_SAMPLE , seq_num , rtt_us , (long)(*srtt * 1e6) , (long)(*rto *1e6));
            }

            return true;
        }

        printf("timeout occured , retries left: %d\n" , config->max_retries - retries);
        log_rtt_event(EV_TIMEOUT , seq_num , );

        *rto *= 2;
        has_been_transmitted = true;
        retries++;

        if(retries > config->max_retries){
            fprintf(stderr , "Max retries reached , no ACK for seq %u\n" , seq_num);
            return false;
        }

        printf("packet retransmitted\n");

    }
}

bool send_file_SW(int sockfd , struct sockaddr_in emulator_addr , FILE* file , const struct configuration* config){
    struct data_packet packet;

    uint32_t sequence_number = 0;
    size_t bytes_read;

    // RFC 6298 RTO ESTIMATOR
    double srtt = -1.0; // smoothed rtt(round time trip)   
    double rtt_variation = 0.0;    
    double rto = 1.0;  // retransmission timeout
    
    printf("starting file transfer with 'STOP AND WAIT' protocol...\n\n");
    while((bytes_read = fread(packet.payload , 1 , config->max_payload , file))){

        size_t packet_size = build_data_packet(&packet , sequence_number , bytes_read , DATA_PACKET);

        if(!transmit_until_acked(sockfd , &emulator_addr , &packet , packet_size ,
                                 sequence_number , config , &srtt , &rtt_variation , &rto)){
            return false;
        }

        sequence_number = (sequence_number + 1) % (1 << config->k);
    }

    if(ferror(file)){
        fprintf(stderr , "error reading the input file\n");
        return false;
    }

    size_t fin_size = build_data_packet(&packet , sequence_number , 0 , FIN_PACKET);

    if(!transmit_until_acked(sockfd , &emulator_addr , &packet , fin_size ,
                                 sequence_number , config , &srtt , &rtt_variation , &rto)){
        fprintf(stderr , "WARNING: FIN was never acknowledged. All data was acknowledged, "
                         "so the receiver most likely finished.\n");
    }

    printf("file sended successfully\n");
    return true;

}

