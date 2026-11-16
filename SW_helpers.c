#include <errno.h>
#include <string.h>
#include <stdint.h>
#include <stdlib.h>

#include "SW_helpers.h"
#include <packet.h>

void update_RTT_estimator(struct timeval send_time , 
                          struct timeval recv_time,
                          double* srtt,
                          double* rtt_variation,
                          double* rto){

    double m = (double)(recv_time.tv_sec - send_time.tv_sec) + 
                               (double)(recv_time.tv_usec - send_time.tv_usec) / 1000000.0;

    if (*srtt < 0) {

        *srtt = m;
        *rtt_variation = m / 2.0;
    }else{

        double diff = *srtt - m;
        if (diff < 0) diff = -diff;
        
        *rtt_variation = (3.0 / 4.0) * (*rtt_variation) + (1.0 / 4.0) * diff;
        *srtt = (7.0 / 8.0) * (*srtt)   + (1.0 / 8.0) * m;
    }

    *rto = *srtt + 4.0 * (*rtt_variation);

    if (*rto < 0.1) {
        *rto = 0.1;
    }


    return;
}
static double tv_to_sec(struct timeval* t){
    return t->tv_sec  + t->tv_usec / 1000000.0;
}

//friend implement
uint32_t crc32_compute(const uint8_t *data, size_t len);
//assumed

static void setCRC32_to_packet(struct data_packet* packet , size_t packet_size){
    packet->header.checksum = 0;
    uint32_t crc = crc32_compute((const uint8_t)&packet , sizeof(packet)); // this part will need to be changed

    packet->header.checksum = crc;
}
static bool isPacket_valid_CRC32(struct data_packet* packet , size_t packet_size){
    return (packet->header.checksum == crc32_compute((uint8_t*)packet , sizeof(packet))); // this part must be changed too
}

static size_t build_data_packet(struct data_packet *packet , uint32_t seq , size_t bytes_read){
    packet->header.type = DATA_PACKET;
    packet->header.seq_num = seq;
    packet->header.payload_length = bytes_read;
    
    size_t packet_size = sizeof(struct packet_header) + bytes_read;
    setCRC32_to_packet(packet , packet_size);

    return packet_size;
}

static bool send_packet(int sockfd , struct sockaddr_in* emulator_address , struct data_packet* packet , size_t packet_size){

    ssize_t send_bytes = sendto(sockfd , packet , packet_size , 0 , 
        (struct sockaddr*)&emulator_address , sizeof(emulator_address));

    
    if(send_bytes < 0){
        perror("failed to send packet 'sendto' functcion\n");
        return false;
    }
    else if(send_bytes < packet_size){
        perror("couldn't send all the packet!!\n");
        return false;
    }

    return true;
}
static enum ack_result wait_for_ack(int sockfd , uint32_t exp_seq , double deadline , struct timeval* recv_time){
    struct data_packet ack;

    while(1){
        struct timeval now;
        gettimeofday(&now , NULL);

        double remaining = deadline - tv_to_sec(&now);

        if(remaining <= 0) return ACK_TIMEOUT;

        struct timeval tv;
        tv.tv_sec = (time_t)remaining;
        tv.tv_usec = (suseconds_t)((remaining - tv.tv_sec) / 1000000.0);
        if(tv.tv_sec == 0 && tv.tv_usec == 0) tv.tv_usec = 1;

        if(setsockopt(sockfd , SOL_SOCKET , SO_RCVTIMEO , (const char*)&tv , sizeof(tv) < 0)){
            perror("setscokopt failed!!\n");
            return ACK_ERROR;
        }
        
        struct sockaddr_in temp_addr;
        socklen_t temp_addr_len = sizeof(temp_addr);

        ssize_t recv_bytes = recvfrom(sockfd , &ack , sizeof(ack) , 0 , 
                                    (struct sockaddr*)&temp_addr , &temp_addr_len);

        if(recv_bytes < 0){
            if(errno == EAGAIN || errno == EWOULDBLOCK) continue;
            perror("ack receive failed\n");
            return ACK_ERROR;
        }
        
        gettimeofday(recv_time , NULL);

        if(ack.header.seq_num = exp_seq){
            printf("ACK received successfully (seq: %u)\n" , ack.header.seq_num);
            return ACK_OK;
        }

        printf("old/wrong ACK ignored (seq: %u)\n" , ack.header.seq_num);
    }
}


bool send_file_SW(int sockfd , struct sockaddr_in emulator_addr , FILE* file){
    struct data_packet packet , ack;

    uint32_t sequence_number = 0;
    size_t bytes_read;

    // RFC 6298 RTO ESTIMATOR
    double srtt = -1.0; // smoothed rtt(round time trip)   
    double rtt_variation = 0.0;    
    double rto = 1.0;  // retransmission timeout
    
    printf("starting file transfer with 'STOP AND WAIT' protocol...\n\n");
    while((bytes_read = fread(packet.payload , 1 , MAX_PAYLOAD_SIZE , file))){

        size_t packet_size = build_data_packet(&packet , sequence_number , bytes_read);

        bool acknoledgment = false , has_been_retransmitted = false;
        int retries = 0;

        do{
            struct timeval send_time;
            gettimeofday(&send_time , NULL);

            if(!send_packet(sockfd , &emulator_addr , &packet , packet_size)){
                return false;
            }

            double deadline = tv_to_sec(&send_time) + rto;
            struct timeval recv_time;
        
            enum ack_result result = wait_for_ack(sockfd , sequence_number , deadline , &recv_time);

            switch(result){
                case ACK_ERROR:
                    return false;
                case ACK_TIMEOUT:
                    printf("timeout occured, retries left: %u\n" , MAX_RETRIES - retries);

                    rto *= 2;
                    has_been_retransmitted = true;
                    retries++;

                    if(retries >= MAX_RETRIES){
                        fprintf(stderr , "Max retries reached connection lost!!\n");
                        return false;
                    }

                    printf("packet retransmitted\n");
                    continue;

                case ACK_OK:
                    if(!has_been_retransmitted){
                        uupdate_RTT_estimator(send_time , recv_time , &srtt , &rtt_variation , &rto);
                    }

                    acknoledgment = true;
            }

        }while(!acknoledgment);

        sequence_number = (sequence_number + 1) % (1 << modulo_k);
    }

    printf("file sended successfully\n");
    return true;

}

