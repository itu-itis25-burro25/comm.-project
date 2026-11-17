#ifndef SW_HELPERS_H
#define SW_HELPERS_H

#include <stdio.h>
#include <stdbool.h>
#include <sys/time.h>
#include <arpa/inet.h>
#include <sys/socket.h>

#include "sender_config.h"


//sending file with stop(S) and wait(W) ARQ                       
bool send_file_SW(int sockfd , struct sockaddr_in emulator_addr , FILE* file , const struct configuration* config);

#endif