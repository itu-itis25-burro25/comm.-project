#ifndef _SW_RECEIVER_H
#define _SW_RECEIVER_H

#include <stdio.h>
#include <stdbool.h>

#include "receiver_config.h"

bool receive_file_SW(int sockfd , FILE* file , struct configuration* config);

#endif