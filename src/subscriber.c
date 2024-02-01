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
#include <signal.h>
#define _GNU_SOURCE

void control() {
    exit_now();
}

void parse_args(int argc, char **argv, sp_data_t *s_data_){
    int c;
    while (1) {
        int option_index = 0;

        static struct option long_options[] = {
            {"port", required_argument, NULL,'p'},
            {"ip", required_argument, NULL,'i'},
            {"topic", required_argument, NULL,'t'},
            {0, 0, 0, 0}
        };

        c = getopt_long(argc, argv, "",
                    long_options, &option_index);
        
        if (c == -1)
            break;

        switch (c) {
        case 'i':
            strncpy(s_data_->ip, optarg, 16);
            break;

        case 'p':
            s_data_->port = strtol(optarg, NULL, 10);
            break;

        case 't':
            strncpy(s_data_->topic, optarg, strlen(optarg));
            break;

        case '?':
            exit(EXIT_FAILURE);

        default:
            printf("?? getopt returned character code 0%o ??\n", c);
            
        }
    }
} 

int main(int argc, char *argv[]) {
    sp_data_t s_data_;  
    s_data_.type = SUBSCRIBER;  
    if (argc != 7){
        fprintf(stderr, "%s --ip $BROKER_IP --port $BROKER_PORT --topic $TOPIC\n", argv[0]);
        exit(EXIT_FAILURE);
    }
    parse_args(argc, argv, &s_data_);
    signal(SIGINT, control);
    if (sp_config(s_data_) < 0) {
        exit(EXIT_FAILURE);
    }
    
    while(1){
        recv_message_data();
    }
}