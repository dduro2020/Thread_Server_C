#include <time.h>
#include <semaphore.h>
#define MAX_DATA_LEN 100

typedef struct Node{
    char topic[100];
    int connections;
    int subscribers;
    int publishers;
    struct Node* next;
} topic_node;

typedef struct {
    topic_node* head;
    int topics_count;
} topics_t;

typedef enum {
    PUBLISHER = 0,
    SUBSCRIBER
} client_type_t;

typedef enum {
    SEQUENTIAL = 0,
    PARALLEL,
    JUSTO
} broker_mode_t;

typedef struct {
  int port;
  broker_mode_t mode; 
} b_data_t;

typedef struct {
  int port;
  char topic[50];
  char ip[16];
  client_type_t type;
} sp_data_t;

typedef enum {
    REGISTER_PUBLISHER = 0,
    UNREGISTER_PUBLISHER,
    REGISTER_SUBSCRIBER,
    UNREGISTER_SUBSCRIBER,
    PUBLISH_DATA
} operation_t;

typedef struct {
    struct timespec time_generated_data;
    char data[MAX_DATA_LEN];
} publish_t;

typedef struct {
    operation_t action;
    char topic[MAX_DATA_LEN];
    int id;
    publish_t data;
} message_t;

typedef enum {
    ERROR = 0,
    LIMIT,
    OK
} status_t;

typedef struct {
    status_t response_status;
    int id;
} response_t;

typedef struct client {
    int id;
    int fd;
    //int n_clients;
    char topic[MAX_DATA_LEN];
    pthread_mutex_t send_mutex;
    struct client *next;
    struct client *prev;
} client_t;

typedef struct broker {
    int n_subs;
    int n_pubs;
    broker_mode_t mode;
    pthread_mutex_t publishers_mutex;
    pthread_mutex_t subscribers_mutex;
    client_t *publishers;
    client_t *subscribers;
    sem_t semaphore;
} broker_t;

typedef struct {
    int p_ids;
    int s_ids;
} ids_t;

typedef struct {
    int fd;
    char topic[MAX_DATA_LEN];
    publish_t data;
} send_message_t;

void exit_now();
void ids_init();
void broker_init(broker_mode_t mode_);
response_t register_client(client_type_t type, int fd, char *topic);
response_t unregister_client(client_type_t type, int id);
void *handle_client(void *arg);
void *send_message(void *arg);
void *send_message_seq(void *arg);
void read_and_send();
void recv_message_data();
void publish_data(message_t msg);
void broker_config(b_data_t broker_data);
int sp_config(sp_data_t sp_data);
int handle_topics(operation_t action, char* topic);
topic_node* find_topic(char* topic);
void remove_topic(char* topic);
int add_topic(char* topic);
int is_empty(topic_node* head);
void initialize_topics();
void print_topic_summary();
void delete_jump(char *data);