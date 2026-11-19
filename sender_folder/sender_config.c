#include <getopt.h>
#include <stdlib.h>
#include <stdio.h>

#include "sender_config.h"
#include "packet.h"

static struct option long_options[] = {
        {"emulatorPort" , required_argument , NULL , EMULATOR_PORT},
        {"emulatorIP" , required_argument , NULL , EMULATOR_IP},
        {"minimumRTO" , required_argument , NULL , MIN_RTO},
        {"maximumRTO" , required_argument , NULL , MAX_RTO},
        {"logFileName" , required_argument , NULL , LOG_FILE_NAME},
        {NULL, 0, NULL, 0}  
};

void defaults_configurations(struct configuration* config){

    config->emulator_port = 5000;
    config->arq_type = STOP_AND_WAIT;
    config->k = 8;
    config->max_payload = 1024;
    config->max_retries = 3;
    config->window_size = 1;
    
    strcpy(config->file_name , "default_fileInput.txt");
    strcpy(config->log_file_name , "default_senderLog.txt");

    strcpy(config->emulator_IP , "127.0.0.1");

    config->min_rto = 1.0;
    config->max_rto = 60.0;

    return;
}
void parse_configurations(struct configuration* config , int argc , char* argv[]){

    int opt = 0;
    while((opt = getopt_long(argc , argv , "f:k:p:q:w:r:" , long_options , NULL)) != -1){
        switch(opt){
            case 'f':
                strcpy(config->file_name , optarg);
                break;
            case 'k':
                config->k = atoi(optarg);
                break;
            case 'p':
                config->max_payload = atoi(optarg);
                break;
            case 'q':
                if(strcmp(optarg , "STOP_AND_WAIT") == 0){
                    config->arq_type = STOP_AND_WAIT;
                }else if(strcmp(optarg , "GO_BACK_N") == 0){
                    config->arq_type = GO_BACK_N;
                }else if(strcmp(optarg , "SELECTIVE") == 0){
                    config->arq_type = SELECTIVE;
                }else {
                    fprintf(stderr, "Invalid ARQ protocol\n");
                    exit(EXIT_FAILURE);
                }

                break;
            case 'w':
                config->window_size = atoi(optarg);
                break;
            case 'r':
                config->max_retries = atoi(optarg);
                break;
            case EMULATOR_PORT:
                config->emulator_port = atoi(optarg);
                break;
            case EMULATOR_IP:
                strcpy(config->emulator_IP , optarg);
                break;
            case MIN_RTO:
                config->min_rto = atof(optarg);
                break;
            case MAX_RTO:
                config->max_rto = atof(optarg);
                break;
            case LOG_FILE_NAME:
                strcpy(config->log_file_name , optarg);
                break;
            default:
                printf("ERROR: unknown parameter!\n");
                exit(EXIT_FAILURE);
        }
    }

    if(config->k < 3 || config->k > 8){
        fprintf(stderr , "k must be between 3 and 8\n");
        exit(EXIT_FAILURE);
    }
    if(config->max_payload < 1 || config->max_payload > MAX_PAYLOAD_SIZE){
        fprintf(stderr , "payload must be between 1 and %d\n" , MAX_PAYLOAD_SIZE);
        exit(EXIT_FAILURE);
    }
    if(config->window_size < 1){
        fprintf(stderr , "window must be at least 1\n");
        exit(EXIT_FAILURE);
    }

}

