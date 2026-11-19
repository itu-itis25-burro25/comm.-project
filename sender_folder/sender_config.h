#ifndef _SENDER_CONFIG_H
#define _SENDER_CONFIG_H

#include <string.h>

enum ARQ_type{
    STOP_AND_WAIT,
    GO_BACK_N,
    SELECTIVE
};

enum extra_options{
    EMULATOR_PORT = 341,
    EMULATOR_IP,
    MIN_RTO,
    MAX_RTO,
    LOG_FILE_NAME
};

struct configuration {
    int k;
    int max_payload;
    int max_retries;
    enum ARQ_type arq_type;
    int window_size;

    char file_name[100];
    char log_file_name[100];

    int emulator_port;
    char emulator_IP[50];

    double min_rto;
    double max_rto;
};

void defaults_configurations(struct configuration* config);
void parse_configurations(struct configuration* config , int argc , char* argv[]);

#endif