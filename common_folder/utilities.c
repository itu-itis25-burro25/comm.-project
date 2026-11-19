#include <string.h>

#include "utilities.h"
#include "crc32.h"

void init_sockaddrr_in(struct sockaddr_in* s , const char* IP_address , uint16_t port){
    memset(s , 0 , sizeof(*s));

    s->sin_addr.s_addr = inet_addr(IP_address);
    s->sin_port = htons(port);
    s->sin_family = AF_INET;
    
    return;
}

size_t build_data_packet(struct data_packet *packet , uint32_t seq , size_t bytes_read , enum packet_type type){
    packet->header.type = type;
    packet->header.seq_num = seq;
    packet->header.payload_length = bytes_read;
    
    size_t packet_size = sizeof(struct packet_header) + bytes_read;
    setCRC32_to_packet(packet , packet_size);

    return packet_size;
}
void update_RTT_estimator(struct timeval send_time , 
                          struct timeval recv_time,
                          double* srtt,
                          double* rtt_variation,
                          double* rto,
                          double max_rto,
                          double min_rto){

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

    if (*rto < min_rto) {
        *rto = min_rto;
    }

    if(*rto > max_rto){
        *rto = max_rto;
    }


    return;
}
double tv_to_sec(struct timeval* t){
    return t->tv_sec  + t->tv_usec / 1000000.0;
}

