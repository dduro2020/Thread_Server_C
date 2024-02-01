#include <stdio.h> 
#include <stdlib.h> 
#include <string.h> 
#include <sys/socket.h> 
#include <unistd.h>
#include <netinet/in.h>
#include <arpa/inet.h> 
#include <err.h>
#include <pthread.h>
#include <sys/select.h>
#include <netdb.h> 
#include "proxy.h"
#include <time.h>
#include <semaphore.h>
#include <fcntl.h>

#define MAX_DATA_LEN 100
#define BACKLOG 1024
#define MAX_PUBS 100
#define MAX_SUBS 900
#define MAX_TOPICS 10
#define SEQUENTIAL 0
#define PARALLEL 1
#define JUSTO 2
#define MILLION 1000000000

int n_topics = 0; 
int sockfd_ps = 0;
int priv_id_;
char topic_[MAX_DATA_LEN];
client_type_t type_;
ids_t ids_;
broker_t broker;
topics_t topics;
pthread_barrier_t barrier;

void ids_init(){
    ids_.p_ids = -1;
    ids_.s_ids = 0;
}

void initialize_topics() {
    topics.head = NULL;
    topics.topics_count = 0;
}

void print_topic_summary() {
    printf("Resumen:\n");

    topic_node* current = topics.head;

    while (current != NULL) {
        printf("\t%s: %d Suscriptores - %d Publicadores\n", current->topic, current->subscribers, current->publishers);
        current = current->next;
    }
}

int is_empty(topic_node* head) {
    return head == NULL;
}

int add_topic(char* topic) {
    if (topics.topics_count >= MAX_TOPICS) {
        return -1;
    }

    topic_node* new_node = (topic_node*)malloc(sizeof(topic_node));
    strcpy(new_node->topic, topic);
    new_node->connections = 0;
    new_node->next = NULL;

    if (is_empty(topics.head)) {
        topics.head = new_node;
    } else {
        topic_node* current = topics.head;
        while (current->next != NULL) {
            current = current->next;
        }
        current->next = new_node;
    }

    topics.topics_count++;
    return 0;
}

void remove_topic(char* topic) {
    topic_node* current = topics.head;
    topic_node* previous = NULL;

    while (current != NULL) {
        if (strcmp(current->topic, topic) == 0) {
            if (previous == NULL) {
                topics.head = current->next;
            } else {
                previous->next = current->next;
            }
            free(current);
            topics.topics_count--;
            return;
        }
        previous = current;
        current = current->next;
    }
}

topic_node* find_topic(char* topic) {
    topic_node* current = topics.head;

    while (current != NULL) {
        if (strcmp(current->topic, topic) == 0) {
            return current;
        }
        current = current->next;
    }

    return NULL;
}

int handle_topics(operation_t action, char* topic) {
    topic_node* node = find_topic(topic);

    if (action == REGISTER_PUBLISHER || action == REGISTER_SUBSCRIBER) {
        if (node == NULL) {
            if (add_topic(topic) < 0) {
                return -1;
            }
            node = find_topic(topic);
        }
        node->connections++;
        if (action == REGISTER_PUBLISHER) {
            if (broker.n_pubs >= MAX_PUBS) {
                return -1;
            }
            node->publishers++;
        } else {
            if (broker.n_subs >= MAX_SUBS) {
                return -1;
            }
            node->subscribers++;
        }
    } else if ((action == UNREGISTER_PUBLISHER || action == UNREGISTER_SUBSCRIBER)) {
        if (node == NULL) {
            return -1;
        }
        node->connections--;
        if (action == UNREGISTER_PUBLISHER) {
            node->publishers--;
        } else {
            node->subscribers--;
        }
        if (node->connections == 0) {
            remove_topic(node->topic);
        }
    }

    return 0;
}

void broker_init(broker_mode_t mode_) {
    broker.mode = mode_;
    pthread_mutex_init(&broker.publishers_mutex, NULL);
    pthread_mutex_init(&broker.subscribers_mutex, NULL);
    broker.publishers = NULL;
    broker.subscribers = NULL;
    sem_init(&broker.semaphore, 0, 1);
}

response_t register_client(client_type_t type, int fd, char *topic) {
    client_t **list;
    pthread_mutex_t *mutex;
    response_t response;
    struct timespec ttime;
    int aux_id;
    response.response_status = OK;

    if (type == PUBLISHER) {
        pthread_mutex_lock(&broker.publishers_mutex);
        if (handle_topics(REGISTER_PUBLISHER, topic) < 0) {
            response.response_status = LIMIT;
            response.id = -1;
            pthread_mutex_unlock(&broker.publishers_mutex);
            return response;
        }
        if (broker.publishers == NULL) {
            broker.publishers = (client_t*) malloc(sizeof(client_t));
            broker.n_pubs = 0;
        }
        if (broker.n_pubs > MAX_PUBS) {
            response.response_status = LIMIT;
            response.id = -1;
            pthread_mutex_unlock(&broker.publishers_mutex);
            return response;
        } else {
            mutex = &broker.publishers_mutex;
            list = &broker.publishers;
            ids_.p_ids = ids_.p_ids + 1;
            aux_id = ids_.p_ids;
            broker.n_pubs = broker.n_pubs + 1;
        }
        
        pthread_mutex_unlock(&broker.publishers_mutex);
    } else if (type == SUBSCRIBER) {
        pthread_mutex_lock(&broker.subscribers_mutex);
        if (handle_topics(REGISTER_SUBSCRIBER, topic) < 0) {
            response.response_status = LIMIT;
            response.id = -1;
            pthread_mutex_unlock(&broker.subscribers_mutex);
            return response;
        }
        
        if (broker.subscribers == NULL) {
            broker.subscribers = (client_t*) malloc(sizeof(client_t));
            broker.n_subs = 0;
        }
        if (broker.n_subs > MAX_SUBS) {
            response.response_status = LIMIT;
            response.id = -1;
            pthread_mutex_unlock(&broker.subscribers_mutex);
            return response;
        } else {
            mutex = &broker.subscribers_mutex;
            list = &broker.subscribers;
            ids_.s_ids = ids_.s_ids + 1;
            aux_id = ids_.s_ids;
            broker.n_subs = broker.n_subs + 1;
        }
        
        pthread_mutex_unlock(&broker.subscribers_mutex);
    }

    if (response.response_status == OK) {
        pthread_mutex_lock(mutex);
        client_t *client = *list;

        while (client != NULL) {
            client = client->next;
        }

        client = (client_t *)malloc(sizeof(client_t));
        client->id = aux_id;
        client->fd = fd;
        response.id = aux_id;
        strncpy(client->topic, topic, sizeof(client->topic));
        client->next = NULL;
        client->prev = NULL;

        if (*list != NULL) {
            (*list)->prev = client;
            client->next = *list;
        }

        *list = client;

        clock_gettime(CLOCK_REALTIME, &ttime);
        printf("[%ld.%ld] Nuevo cliente (%d) Publicador/Suscriptor conectado: %s\n", ttime.tv_sec, ttime.tv_nsec, aux_id, topic);
        print_topic_summary();
        pthread_mutex_unlock(mutex);
    }

    return response;
}

response_t unregister_client(client_type_t type, int id) {
    pthread_mutex_t *mutex;
    client_t **list;
    response_t response;
    struct timespec ttime;

    if (type == PUBLISHER) {
        pthread_mutex_lock(&broker.publishers_mutex);
        mutex = &broker.publishers_mutex;
        list = &broker.publishers;
        broker.n_pubs = broker.n_pubs - 1;
        
        pthread_mutex_unlock(&broker.publishers_mutex);
    } else if (type == SUBSCRIBER) {
        pthread_mutex_lock(&broker.subscribers_mutex);
        mutex = &broker.subscribers_mutex;
        list = &broker.subscribers;
        broker.n_subs = broker.n_subs - 1;
       
        pthread_mutex_unlock(&broker.subscribers_mutex);
    }

    pthread_mutex_lock(mutex);
    client_t *client = *list;

    while (client != NULL && client->id != id) {
        client = client->next;
    }
    if (type == PUBLISHER) {
        handle_topics(UNREGISTER_PUBLISHER, client->topic);
    } else {
        handle_topics(UNREGISTER_SUBSCRIBER, client->topic);
    }

    clock_gettime(CLOCK_REALTIME, &ttime);
    response.response_status = OK; 
    printf("[%ld.%ld] Eliminado cliente (%d) Publicador/Suscriptor conectado : %s\n", ttime.tv_sec, ttime.tv_nsec, id, client->topic);
    print_topic_summary();

    if (client != NULL) {
        if (client->prev != NULL) {
            client->prev->next = client->next;
        }
        if (client->next != NULL) {
            client->next->prev = client->prev;
        }
        if (*list == client) {
            *list = client->next;
        }    
        free(client);
    }
    
    pthread_mutex_unlock(mutex);

    return response;
}

/* Searching the type of request */
void *handle_client(void *arg) {
    client_t *client = (client_t*)arg;
    int fd = client->fd;
    message_t message;
    response_t response;
    struct timespec ttime;
    memset(&message.topic, '\0', sizeof(message.topic));
    memset(&message.data.data, '\0', sizeof(message.data.data));

    while(1){
        // Esperamos a recibir un mensaje del cliente
        recv(fd, &message, sizeof(message_t), 0);
        // Procesamos el mensaje y enviamos la respuesta al cliente
        switch (message.action) {
            case REGISTER_PUBLISHER:
                response = register_client(PUBLISHER, fd, message.topic);
                send(fd, &response, sizeof(response_t), 0);
                if (response.response_status != OK) {
                    pthread_exit(NULL);
                }
                break;
            case UNREGISTER_PUBLISHER:
                response = unregister_client(PUBLISHER, message.id);
                send(fd, &response, sizeof(response_t), 0);
                close(fd);
                pthread_exit(NULL);
                break;
            case REGISTER_SUBSCRIBER:
                response = register_client(SUBSCRIBER, fd, message.topic);
                send(fd, &response, sizeof(response_t), 0);
                if (response.response_status != OK) {
                    pthread_exit(NULL);
                }
                break;
            case UNREGISTER_SUBSCRIBER:
                response = unregister_client(SUBSCRIBER, message.id);
                send(fd, &response, sizeof(response_t), 0);
                close(fd);
                pthread_exit(NULL);
                break;
            case PUBLISH_DATA:
                clock_gettime(CLOCK_REALTIME, &ttime);
                printf("[%ld.%ld] Recibido mensaje para publicar en topic: %s - mensaje: %s - Generó: [%ld.%ld]\n", ttime.tv_sec, ttime.tv_nsec,
                    message.topic, message.data.data, message.data.time_generated_data.tv_sec, message.data.time_generated_data.tv_nsec);
                publish_data(message);
                break;
        }
    }
}

/* Redirected message from Broker to Subscriber */
void *send_message(void *arg) {
    send_message_t *p = (send_message_t*) arg;
    int fd = p->fd;
    message_t msg;
    memset(&msg.topic, '\0', sizeof(msg.topic));
    memset(&msg.data.data, '\0', sizeof(msg.data.data));
                        
    strcpy(msg.topic, p->topic);
    msg.data = p->data;
    send(fd, &msg, sizeof(message_t), 0);

    free(p);
    pthread_exit(NULL);
}

void *send_message_seq(void *arg) {
    send_message_t *p = (send_message_t*) arg;
    int fd = p->fd;
    message_t msg;
    memset(&msg.topic, '\0', sizeof(msg.topic));
    memset(&msg.data.data, '\0', sizeof(msg.data.data));
                        
    strcpy(msg.topic, p->topic);
    msg.data = p->data;

    pthread_barrier_wait(&barrier);

    send(fd, &msg, sizeof(message_t), 0);

    free(p);
    pthread_exit(NULL);
}

/* Function to delete '\n' in msg */
void delete_jump(char *data) {
    int i;
    for (i = 0; i < strlen(data); i++) {
        if (data[i] == '\n') {
            data[i] = '\0';
            return;
        }
    }
}

/* Only for Publisher */
void read_and_send(){
    message_t msg;
    int fd;
    struct timespec ttime;
    memset(&msg.topic, '\0', sizeof(msg.topic));
    memset(&msg.data.data, '\0', sizeof(msg.data.data));

    fd = open("/proc/loadavg", O_RDONLY);
    if (fd == -1) {
        err(EXIT_FAILURE, "error: failure opening /proc/loadavg\n");
    }
    
    if (read(fd, msg.data.data, MAX_DATA_LEN) == -1) {
        err(EXIT_FAILURE, "error: failure reading /proc/loadavg\n");
    }

    close(fd);

    delete_jump(msg.data.data);

    strncpy(msg.topic, topic_, strlen(topic_));
    msg.action = PUBLISH_DATA;
    msg.id = priv_id_;
    clock_gettime(CLOCK_REALTIME, &msg.data.time_generated_data);

    if (send(sockfd_ps, &msg, sizeof(msg), 0) == -1) {
        err(EXIT_FAILURE, "error: failure sending message\n");
    }

    clock_gettime(CLOCK_REALTIME, &ttime);
    printf("[%ld.%ld] Publicado mensaje topic: %s - mensaje: %s - Generó: [%ld.%ld]\n", ttime.tv_sec, ttime.tv_nsec,
        msg.topic, msg.data.data, msg.data.time_generated_data.tv_sec, msg.data.time_generated_data.tv_nsec);
}

/* Only for Subscriber */
void recv_message_data(){
    message_t msg;
    struct timespec ttime;
    double latency;
    memset(&msg.topic, '\0', sizeof(msg.topic));
    memset(&msg.data.data, '\0', sizeof(msg.data.data));

    if (recv(sockfd_ps, &msg, sizeof(msg), 0) == -1) {
        err(EXIT_FAILURE, "error: failure receiving message\n");
    }

    clock_gettime(CLOCK_REALTIME, &ttime);

    latency = (ttime.tv_sec - msg.data.time_generated_data.tv_sec) + ((((double)ttime.tv_nsec) - ((double)msg.data.time_generated_data.tv_nsec)) / MILLION);

    printf("[%ld.%ld] Recibido mensaje topic: %s - mensaje: %s - Generó: [%ld.%ld] - Recibido: [%ld.%ld] - Latencia: %f\n", ttime.tv_sec,
        ttime.tv_nsec, msg.topic, msg.data.data, msg.data.time_generated_data.tv_sec, msg.data.time_generated_data.tv_nsec, ttime.tv_sec, ttime.tv_nsec, latency);
}

/* Only for Broker, decides type of communication */
void publish_data(message_t msg) {
    client_t *client;
    struct timespec ttime;
    int thread_count = 0;
    
    int j;
    clock_gettime(CLOCK_REALTIME, &ttime);
    topic_node* node = find_topic(msg.topic);
    pthread_t* threads = (pthread_t*)malloc(node->subscribers * sizeof(pthread_t));
    if (broker.n_subs > 0){
        printf("[%ld.%ld] Enviando mensaje en topic %s a %d suscriptores.\n", ttime.tv_sec, ttime.tv_nsec,
            msg.topic, node->subscribers);
    }

    switch (broker.mode) {
        case JUSTO:
            pthread_mutex_lock(&broker.subscribers_mutex);
            pthread_barrier_init(&barrier, NULL, node->subscribers);
            client = broker.subscribers;
            while (client != NULL) {
                if (strcmp(client->topic, msg.topic) == 0) {
                    send_message_t* send_data = (send_message_t*)malloc(sizeof(send_message_t));
                    send_data->data.time_generated_data = msg.data.time_generated_data;
                    strcpy(send_data->data.data, msg.data.data);
                    strcpy(send_data->topic, msg.topic);
                    send_data->fd = client->fd;
                    pthread_create(&threads[thread_count], NULL, &send_message_seq, send_data);
                    thread_count++;
                }
                client = client->next;
            }

            for (j = 0; j < thread_count; j++) {
                pthread_join(threads[j], NULL);
            }
            free(threads);
            pthread_barrier_destroy(&barrier);
            pthread_mutex_unlock(&broker.subscribers_mutex);
            break;
        case PARALLEL:
            pthread_mutex_lock(&broker.subscribers_mutex);
            client = broker.subscribers;
            while (client != NULL) {
                if (strcmp(client->topic, msg.topic) == 0) {
                    send_message_t* send_data = (send_message_t*)malloc(sizeof(send_message_t));
                    send_data->data.time_generated_data = msg.data.time_generated_data;
                    strcpy(send_data->data.data, msg.data.data);
                    strcpy(send_data->topic, msg.topic);
                    send_data->fd = client->fd;
                    pthread_create(&threads[thread_count], NULL, &send_message, send_data);
                    thread_count++;
                }
                client = client->next;
            }

            for (j = 0; j < thread_count; j++) {
                pthread_join(threads[j], NULL);
            }
            free(threads);
            pthread_mutex_unlock(&broker.subscribers_mutex);
            break;
        case SEQUENTIAL:
            pthread_mutex_lock(&broker.subscribers_mutex);
            client = broker.subscribers;
            while (client != NULL) {
                if (strcmp(client->topic, msg.topic) == 0) {
                    pthread_mutex_lock(&client->send_mutex);
                    send(client->fd, &msg, sizeof(message_t), 0);
                    pthread_mutex_unlock(&client->send_mutex);
                }
                client = client->next;
            }
            pthread_mutex_unlock(&broker.subscribers_mutex);
            free(threads);
            break;
        }
}
   
/*Exit clients*/
void exit_now() {
    message_t msg;
    response_t recv_rsp;

    struct timespec ttime;
    memset(&msg.topic, '\0', sizeof(msg.topic));
    strncpy(msg.topic, topic_, strlen(topic_));
    msg.id = priv_id_;

    if (type_ == PUBLISHER){
        msg.action = UNREGISTER_PUBLISHER;
    }else{
        msg.action = UNREGISTER_SUBSCRIBER;
    }
    
    if (send(sockfd_ps, &msg, sizeof(msg), 0) == -1) {
        err(EXIT_FAILURE, "error: failure sending message\n");
    }

    if (recv(sockfd_ps, &recv_rsp, sizeof(recv_rsp), 0) == -1) {
        err(EXIT_FAILURE, "error: failure receiving message\n");
    }

    if (recv_rsp.response_status == OK) {
        clock_gettime(CLOCK_REALTIME, &ttime);
        printf("[%ld.%ld] De-Registrado (%d) correctamente del broker.\n", ttime.tv_sec, ttime.tv_nsec, priv_id_);
        
        close(sockfd_ps);
        exit(EXIT_SUCCESS);
    } else {
        clock_gettime(CLOCK_REALTIME, &ttime);
        printf("[%ld.%ld] De-Registrado (%d) forzosamente del broker.\n", ttime.tv_sec, ttime.tv_nsec, priv_id_);
        
        close(sockfd_ps);
        exit(EXIT_FAILURE);
    } 
}

/* Broker configuration */
void broker_config(b_data_t broker_data) {
    int sockfd, connfd;
    unsigned int len;     /* length of client address */
    struct sockaddr_in servaddr, client;
    int enable = 1;
    
    ids_init();
    initialize_topics();
    broker_init(broker_data.mode);
    
    setbuf(stdout, NULL);
    //memset(&topics_.names, '\0', sizeof(topics_.names));

    /* socket creation */
    sockfd = socket(AF_INET, SOCK_STREAM, 0); 
    if (sockfd == -1) { 
        err(EXIT_FAILURE, "error: socket creation failed\n");
    } 

    //printf("Socket successfully created...\n"); 

    /* clear structure */
    memset(&servaddr, 0, sizeof(servaddr));
  
    /* assign IP, SERV_PORT, IPV4 */
    servaddr.sin_family      = AF_INET; 
    servaddr.sin_addr.s_addr = htonl(INADDR_ANY); 
    servaddr.sin_port        = htons(broker_data.port); 
    
    
    if (setsockopt(sockfd, SOL_SOCKET, SO_REUSEADDR, &enable, sizeof(int)) < 0) {
        err(EXIT_FAILURE, "error: setsockopt(SO_REUSEADDR) failed\n");
    }
    
    /* Bind socket */
    if ((bind(sockfd, (struct sockaddr *)&servaddr, sizeof(servaddr))) != 0) { 
        err(EXIT_FAILURE, "error: socket bind failed\n");
    } 
    /* Listen */
    if ((listen(sockfd, BACKLOG)) != 0) { 
        err(EXIT_FAILURE, "error: socket listen failed\n");
    }
    
    while(1) {  /* read data from a client socket till it is closed */               
        pthread_t thread;
        client_t* aux_client = (client_t*)malloc(sizeof(client_t));
        /* Accept the data from incoming socket*/
        len = sizeof(client);
        aux_client->fd = accept(sockfd, (struct sockaddr *)&client, &len); 

        if (aux_client->fd < 0) { 
            err(EXIT_FAILURE, "error: connection not accepted\n");
        }

        /*threads creation*/
        if (pthread_create(&thread, NULL, handle_client, aux_client) != 0) {
            err(EXIT_FAILURE, "error: cannot create thread\n");
        }
    }
}

/* Clients configuration */
int sp_config(sp_data_t sp_data) {
 
    struct sockaddr_in servaddr;
    message_t msg;
    response_t recv_rsp;
    struct timespec ttime;
    type_ = sp_data.type;
    memset(&msg.topic, '\0', sizeof(msg.topic));
    memset(&topic_, '\0', sizeof(topic_));

    if (type_ == PUBLISHER){
        msg.action = REGISTER_PUBLISHER;
    } else {
        msg.action = REGISTER_SUBSCRIBER;
    }
    strncpy(msg.topic,sp_data.topic, strlen(sp_data.topic));
    strncpy(topic_, sp_data.topic, strlen(sp_data.topic));

    setbuf(stdout, NULL);

    /* Socket creation */
    sockfd_ps = socket(AF_INET, SOCK_STREAM, 0); 
    if (sockfd_ps == -1) { 
        err(EXIT_FAILURE, "error: socket creation failed\n");
    } 
    
    //printf("Socket successfully created...\n"); 
    
    memset(&servaddr, 0, sizeof(servaddr));

    /* assign IP, PORT */
    servaddr.sin_family = AF_INET; 
    servaddr.sin_addr.s_addr = inet_addr(sp_data.ip); 
    servaddr.sin_port = htons(sp_data.port); 
  
    /* try to connect the client socket to server socket */
    if (connect(sockfd_ps, (struct sockaddr*)&servaddr, sizeof(servaddr)) != 0) { 
        err(EXIT_FAILURE, "error: connection with server failed\n");
    } 

    clock_gettime(CLOCK_REALTIME, &ttime);
    if (sp_data.type == PUBLISHER){
        printf("[%ld.%ld] Publisher conectado con broker correctamente.\n", ttime.tv_sec, ttime.tv_nsec);
    }else{
        printf("[%ld.%ld] Subscriber conectado con broker (%s:%d)\n", ttime.tv_sec, ttime.tv_nsec, sp_data.ip, sp_data.port);
    }

    /* send test sequences*/
    
    /* Register */
    if (send(sockfd_ps, &msg, sizeof(msg), 0) == -1) {
        err(EXIT_FAILURE, "error: failure sending message\n");
    }

    if (recv(sockfd_ps, &recv_rsp, sizeof(recv_rsp), 0) == -1) {
        err(EXIT_FAILURE, "error: failure receiving message\n");
    }
    
    priv_id_ = recv_rsp.id;
    
    clock_gettime(CLOCK_REALTIME, &ttime);
    if (recv_rsp.response_status == OK){
        printf("[%ld.%ld] Registrado correctamente con ID: %d para topic %s\n", ttime.tv_sec, ttime.tv_nsec, recv_rsp.id, msg.topic);
    } else {
        printf("[%ld.%ld] Error al hacer el registro: %d\n", ttime.tv_sec, ttime.tv_nsec, recv_rsp.response_status);
        close(sockfd_ps);
        return -1;
    }
    return 0;
}
