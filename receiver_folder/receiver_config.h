#ifndef _RECEIVER_CONFIG_H
#define _RECEIVER_CONFIG_H

#include <string.h>

enum ARQ_type{
    STOP_AND_WAIT,
    GO_BACK_N,
    SELECTIVE
};

enum extra_options{
    EMULATOR_PORT = 341,
    EMULATOR_IP,
    RECEIVER_PORT,
    RECEIVER_IP
};

struct configuration {
    int k;
    int max_payload;
    enum ARQ_type arq_type;
    int window_size;

    char file_name[100];

    int emulator_port;
    char emulator_IP[50];

    int receiver_port;
    char receiver_IP[50];
};

void defaults_configurations(struct configuration* config);
void parse_configurations(struct configuration* config , int argc , char* argv[]);

#endif