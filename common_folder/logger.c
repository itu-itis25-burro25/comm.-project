#include <stdio.h>
#include <time.h>

#include "logger.h"

static FILE* log_file = NULL;

int64_t now_us(void){
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC , &ts);

    return (int64_t) ts.tv_sec * 1000000 + ts.tv_nsec / 1000;
}

bool open_logFile(char const* const file_name){
    log_file= fopen(file_name , "w");

    if(!log_file){
        perror("couldn't open file\n");
        return false;
    }

    setvbuf(log_file , NULL , _IOLBF , 0);
    fprintf(log_file , "time_us,event,seq,bytes,is_retransmission,rtt_us,srtt_us,rto_us\n");

    return true;
}
void close_logFile(void){
    fclose(log_file);
    log_file = NULL;
}


static char* event_to_string(enum log_event_type event, char* string, size_t max_len) {
    if (string == NULL || max_len == 0) return NULL;

    const char *name;
    switch (event) {
        case EV_SEND_DATA:   name = "SEND_DATA";   break;
        case EV_SEND_FIN:    name = "SEND_FIN";    break;
        case EV_ACK_RECV:    name = "ACK_RECV";    break;
        case EV_AKC_STALE:   name = "AKC_STALE";   break;
        case EV_ACK_CORRUPT: name = "ACK_CORRUPT"; break;
        case EV_TIMEOUT:     name = "TIMEOUT";     break;
        case EV_RTT_SAMPLE:  name = "RTT_SAMPLE";  break;

        case EV_RECV_DATA:    name = "RECV_DATA";    break;
        case EV_REVC_DUP:     name = "RECV_DUP";     break;
        case EV_RECV_CORRUPT: name = "RECV_CORRUPT"; break;
        case EV_ACK_SEND:     name = "ACK_SEND";     break;
        case EV_FIN_RECV:     name = "FIN_RECV";     break;
        case EV_FIN_REACK:    name = "FIN_REACK";    break;

        default:             name = "UNKNOWN";     break;
    }

    snprintf(string, max_len, "%s", name);
    return string;
}

static void write_row(enum log_event_type event , long seq_num , long bytes , int is_retransmission , long rtt_us , long srtt_us , long rto_us){
    
    char event_str[100];
    event_to_string(event , event_str , 100);

    fprintf(log_file , "%lld,%s,%ld,%ld,%d,%ld,%ld,%ld\n" ,
         (long long)now_us() , event_str ,  seq_num , bytes , is_retransmission ,
        rtt_us , srtt_us , rto_us);
}

void log_normal_event(enum log_event_type event , long seq_num , long bytes , int is_retransmission){
    write_row(event , seq_num , bytes , is_retransmission , -1 , -1 , -1);
}

void log_rtt_event(enum log_event_type event , long seq_num , long rtt_us , long srtt_us , long rto_us){
    write_row(event , seq_num , -1 , -1 , rtt_us , srtt_us , rto_us);
}

