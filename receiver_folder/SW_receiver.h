#ifndef _SW_RECEIVER_H
#define _SW_RECEIVER_H

#include <stdio.h>

bool receive_file_SW(int sockfd , FILE* file , struct configuration* config);

#endif