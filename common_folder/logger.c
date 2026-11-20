#include <stdio.h>
#include <time.h>

#include "logger.h"

static FILE *log_file = NULL;
static FILE *emu_log_file = NULL;

int64_t now_us(void) {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);

    return (int64_t)ts.tv_sec * 1000000LL + ts.tv_nsec / 1000LL;
}

bool open_logFile(char const *const file_name) {
    if (file_name == NULL) {
        return false;
    }
    if (log_file != NULL) {
        fclose(log_file);
        log_file = NULL;
    }
    log_file = fopen(file_name, "w");

    if (!log_file) {
        perror("couldn't open log file");
        return false;
    }

    setvbuf(log_file, NULL, _IOLBF, 0);
    fprintf(log_file, "time_us,event,seq,bytes,is_retransmission,rtt_us,srtt_us,rto_us\n");

    return true;
}

void close_logFile(void) {
    if (log_file != NULL) {
        fclose(log_file);
        log_file = NULL;
    }
}

static const char *event_to_string(enum log_event_type event) {
    switch (event) {
        case EV_SEND_DATA:    return "SEND_DATA";
        case EV_SEND_FIN:     return "SEND_FIN";
        case EV_ACK_RECV:     return "ACK_RECV";
        case EV_AKC_STALE:    return "ACK_STALE";
        case EV_ACK_CORRUPT:  return "ACK_CORRUPT";
        case EV_TIMEOUT:      return "TIMEOUT";
        case EV_RTT_SAMPLE:   return "RTT_SAMPLE";

        case EV_RECV_DATA:    return "RECV_DATA";
        case EV_REVC_DUP:     return "RECV_DUP";
        case EV_RECV_CORRUPT: return "RECV_CORRUPT";
        case EV_ACK_SEND:     return "ACK_SEND";
        case EV_FIN_RECV:     return "FIN_RECV";
        case EV_FIN_REACK:    return "FIN_REACK";

        default:              return "UNKNOWN";
    }
}

static void write_row(enum log_event_type event, long seq_num, long bytes,
                      int is_retransmission, long rtt_us, long srtt_us, long rto_us) {
    if (!log_file) {
        return;
    }

    fprintf(log_file, "%lld,%s,%ld,%ld,%d,%ld,%ld,%ld\n",
            (long long)now_us(), event_to_string(event), seq_num, bytes,
            is_retransmission, rtt_us, srtt_us, rto_us);
}

void log_normal_event(enum log_event_type event, long seq_num, long bytes, int is_retransmission) {
    write_row(event, seq_num, bytes, is_retransmission, -1, -1, -1);
}

void log_rtt_event(enum log_event_type event, long seq_num, long rtt_us, long srtt_us, long rto_us) {
    write_row(event, seq_num, -1, -1, rtt_us, srtt_us, rto_us);
}

bool open_emu_logFile(const char *file_name) {
    if (file_name == NULL) {
        return false;
    }
    if (emu_log_file != NULL) {
        fclose(emu_log_file);
        emu_log_file = NULL;
    }
    emu_log_file = fopen(file_name, "w");
    if (!emu_log_file) {
        perror("couldn't open emulator log file");
        return false;
    }

    setvbuf(emu_log_file, NULL, _IOLBF, 0);
    fprintf(emu_log_file, "time_us,event,dir,pkt_idx,pkt_type,seq,bytes,detail\n");
    return true;
}

void close_emu_logFile(void) {
    if (emu_log_file != NULL) {
        fclose(emu_log_file);
        emu_log_file = NULL;
    }
}

static const char *emu_event_to_string(enum emu_log_event event) {
    switch (event) {
        case EMU_EV_ARRIVE:           return "ARRIVE";
        case EMU_EV_ENQUEUE:          return "ENQUEUE";
        case EMU_EV_QUEUE_DROP:       return "QUEUE_DROP";
        case EMU_EV_DROP_LOSS:        return "DROP_LOSS";
        case EMU_EV_CORRUPT:          return "CORRUPT";
        case EMU_EV_DUPLICATE:        return "DUPLICATE";
        case EMU_EV_REORDER_HOLD:     return "REORDER_HOLD";
        case EMU_EV_REORDER_RELEASE:  return "REORDER_RELEASE";
        case EMU_EV_REORDER_TIMEOUT:  return "REORDER_TIMEOUT";
        case EMU_EV_ACTION_DROP:      return "ACTION_DROP";
        case EMU_EV_ACTION_DELAY:     return "ACTION_DELAY";
        case EMU_EV_ACTION_CORRUPT:   return "ACTION_CORRUPT";
        case EMU_EV_ACTION_DUPLICATE: return "ACTION_DUPLICATE";
        case EMU_EV_DELIVER:          return "DELIVER";
        default:                      return "UNKNOWN";
    }
}

void log_emu_event(enum emu_log_event event, const char *dir,
                   unsigned long pkt_idx, const char *pkt_type,
                   long seq_num, size_t bytes, const char *detail) {
    if (!emu_log_file) {
        return;
    }

    fprintf(emu_log_file, "%lld,%s,%s,%lu,%s,%ld,%zu,%s\n",
            (long long)now_us(),
            emu_event_to_string(event),
            dir ? dir : "",
            pkt_idx,
            pkt_type ? pkt_type : "",
            seq_num,
            bytes,
            detail ? detail : "");
}
