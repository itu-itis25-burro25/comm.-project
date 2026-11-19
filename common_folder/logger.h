#ifndef _LOGGER_H
#define _LOGGER_H

#include <stdbool.h>
#include <stdint.h>

enum log_event_type{

    //sender logs
    EV_SEND_DATA,
    EV_SEND_FIN,
    EV_ACK_RECV,
    EV_AKC_STALE,
    EV_ACK_CORRUPT,
    EV_TIMEOUT,
    EV_RTT_SAMPLE,

    //receiver logs
    EV_RECV_DATA,
    EV_REVC_DUP,
    EV_RECV_CORRUPT,
    EV_ACK_SEND,
    EV_FIN_RECV,
    EV_FIN_REACK
};

int64_t now_us(void);

bool open_logFile(char const* const file_name);
void close_logFile(void);

void log_normal_event(enum log_event_type event , long seq_num , long bytes , int is_retransmission);
void log_rtt_event(enum log_event_type event , long seq_num , long rtt_us , long srtt_us , long rto_us);

#endif