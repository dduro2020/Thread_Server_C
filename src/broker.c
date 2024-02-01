#include "proxy.h"
#include <netdb.h> 
#include <stdio.h> 
#include <stdlib.h> 
#include <string.h> 
#include <sys/socket.h> 
#include <unistd.h>
#include <netinet/in.h>
#include <arpa/inet.h> 
#include <err.h>
#include <getopt.h>
#define _GNU_SOURCE

void parse_args(int argc, char **argv, b_data_t *broker_data){
    int c;
    while (1) {
        int option_index = 0;

        static struct option long_options[] = {
            {"port", required_argument, NULL,'p'},
            {"mode", required_argument, NULL,'m'},
            {0, 0, 0, 0}
        };

        c = getopt_long(argc, argv, "",
                    long_options, &option_index);
        
        if (c == -1)
            break;

        switch (c) {
        case 'm':
            if(strcmp(optarg, "secuencial") == 0){
                broker_data->mode = SEQUENTIAL;
            }else if(strcmp(optarg, "paralelo") == 0){
                broker_data->mode = PARALLEL;
            }else if(strcmp(optarg, "justo") == 0){
                broker_data->mode = JUSTO;
            }else{
                fprintf(stderr, "error: incorrect mode, try: secuencial/paralelo/justo\n");
                exit(EXIT_FAILURE);
            }
            break;

        case 'p':
            broker_data->port = strtol(optarg, NULL, 10);
            break;
        
        case '?':
            exit(EXIT_FAILURE);

        default:
            printf("?? getopt returned character code 0%o ??\n", c);
            
        }
    }
} 

int main(int argc, char *argv[]) {
    b_data_t broker_data;    
    if (argc != 5){
        fprintf(stderr, "%s --port $BROKER_PORT --mode $MODE\n", argv[0]);
        exit(EXIT_FAILURE);
    }
    parse_args(argc, argv, &broker_data);
    printf("$BROKER:\tPORT: %d\tMODE: %d\n",broker_data.port, broker_data.mode);
    broker_config(broker_data);
    exit(EXIT_SUCCESS);
}