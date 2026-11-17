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
    EMULATOR_IP
};

struct configuration {
    int k;
    int max_payload;
    int max_retries;
    enum ARQ_type arq_type;
    int window_size;

    char file_name[100];

    int emulator_port;
    char emulator_IP[50];
};

void defaults_configurations(struct configuration* config);
void parse_configurations(struct configuration* config , int argc , char* argv[]);

#endif