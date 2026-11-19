#ifndef _LOGGER_H
#define _LOGGER_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

enum log_event_type {

    // sender logs
    EV_SEND_DATA,
    EV_SEND_FIN,
    EV_ACK_RECV,
    EV_AKC_STALE,
    EV_ACK_CORRUPT,
    EV_TIMEOUT,
    EV_RTT_SAMPLE,

    // receiver logs
    EV_RECV_DATA,
    EV_REVC_DUP,
    EV_RECV_CORRUPT,
    EV_ACK_SEND,
    EV_FIN_RECV,
    EV_FIN_REACK
};

enum emu_log_event {
    EMU_EV_ARRIVE,
    EMU_EV_ENQUEUE,
    EMU_EV_QUEUE_DROP,
    EMU_EV_DROP_LOSS,
    EMU_EV_CORRUPT,
    EMU_EV_DUPLICATE,
    EMU_EV_REORDER_HOLD,
    EMU_EV_REORDER_RELEASE,
    EMU_EV_REORDER_TIMEOUT,
    EMU_EV_ACTION_DROP,
    EMU_EV_ACTION_DELAY,
    EMU_EV_ACTION_CORRUPT,
    EMU_EV_ACTION_DUPLICATE,
    EMU_EV_DELIVER
};

int64_t now_us(void);

bool open_logFile(char const *const file_name);
void close_logFile(void);

void log_normal_event(enum log_event_type event, long seq_num, long bytes, int is_retransmission);
void log_rtt_event(enum log_event_type event, long seq_num, long rtt_us, long srtt_us, long rto_us);

bool open_emu_logFile(const char *file_name);
void close_emu_logFile(void);
void log_emu_event(enum emu_log_event event, const char *dir,
                   unsigned long pkt_idx, const char *pkt_type,
                   long seq_num, size_t bytes, const char *detail);

#endif
