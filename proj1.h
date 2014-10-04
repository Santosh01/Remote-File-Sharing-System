#ifndef _PROJ1_H_
#define _PROJ1_H_

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <netdb.h>
#include <sys/types.h> 
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>

#define MAX_CONN 4  /* Including the server */

/* Size of server IP list update message:
 * msg_type (uint16_t)
 * number of IPs (uint8_t)
 * count * size of struct availabla_peer_node */
#define UPDATE_MSG_SIZE(count) ( sizeof(uint16_t) + sizeof(uint8_t) + ( sizeof (struct available_peer_node) ) * (count) )

/* List of available commands */
#define CMD_HELP        "help"
#define CMD_MYIP        "myip"
#define CMD_MYPORT      "myport"
#define CMD_REGISTER    "register"
#define CMD_CONNECT     "connect"
#define CMD_LIST        "list"
#define CMD_PEERS       "peers"
#define CMD_TERMINATE   "terminate"
#define CMD_EXIT        "exit"
#define CMD_UPLOAD      "upload"
#define CMD_DOWNLOAD    "download"
#define CMD_CREATOR     "creator"

/* Message types */
#define MSG_MYPORT              0x11 /* Used by client to send its port information */
#define MSG_PEER_LIST           0x12 /* Used by server to send the IP list */

#define MSG_CONNECT_REQUEST     0x21 /* Used by client to connect to peer */

#define MSG_DOWNLOAD_REQUEST    0x31 /* Used by client to request a file from peer */
#define MSG_DOWNLOAD_ACCEPT     0x32 /* Used by client to accept the download request from peer */
#define MSG_DOWNLOAD_REJECT     0x33 /* Used by client to reject the download request from peer*/

#define MSG_UPLOAD_REQUEST      0x41 /* Used by client to send an upload request to peer */
#define MSG_UPLOAD_ACCEPT       0x42 /* Used by client to accept an upload request from peer*/
#define MSG_UPLOAD_REJECT       0x43 /* Used by client to reject an upload request from peer*/


/* macro to safely free a pointer */
#define FREE(ptr)  { \
    if ( (ptr) ) { \
        free( (ptr) ); \
        (ptr) = NULL; \
    } \
  }


/********* data structures **********/

/* running  mode of this process */
typedef enum {
    client_mode,
    server_mode
} prog_mode_t;

/* structure to be used by server to maintain a list of available clients */
struct client_node {
    struct sockaddr_in clientaddr;
    unsigned short port;
    char hostname[NI_MAXHOST];
    int fd;
};

/* File transfer status for a peer */
typedef enum {
    idle,
    sending,
    receiving
} status_t;

/* structure to maintain the information required for file transfer with a peer */
struct file_transfer_context {
    status_t status;             /* Flag to indicated if we sending/receiving file from this peer */
    int file_fd;                 /* fd fo the file to read/write from, if we are sending/receiving file from this peer */
    char *file_name;             /* name of the file being transferred */
    struct timeval total_time;   /* total time spent in the transfer so far */
    uint64_t file_size;          /* size of the file being transferred */
    uint64_t bytes_remaining;    /* size of the file still remaining to be transferred */
};

/* structure to be used by client to maintain a list of connected peers */
struct connected_peer_node {
    int id;
    char hostname[NI_MAXHOST];
    struct sockaddr_in addr;
    unsigned short port;
    int fd;
    struct file_transfer_context ctx;
};

/* structure to be used by client to maintain a list of available peers */
struct available_peer_node {
    struct in_addr ip;
    unsigned short port;
};

/******* extern declarations *******/
extern char my_hostname[NI_MAXHOST];
extern int listen_port;
extern int listen_fd;
extern int mode;
extern int registered;
extern struct in_addr myip;
extern int server_fd;
extern fd_set readfds;
extern fd_set writefds;
extern int max_fd;

extern struct available_peer_node *available_peers;
extern int num_available_peers;

extern struct list_node *server_ip_list_head;
extern int server_ip_count;

extern struct list_node *connected_peer_list_head;
extern int connected_peer_count;


/********* function prototypes ************/

struct sockaddr_in getmyip();
int handle_cmd(char cmd[], int cmd_len);
void update_maxfd();
void print_prompt ();
int handle_exit();

void add_server_ip(struct sockaddr_in addr, int port, int fd);
int receive_client_connect(int accept_fd);
int send_ip_list_to_client();
void print_client_list();
void display_available_peers();

int recv_update_from_server();
void display_available_peers();
int receive_peer_connect(int accept_fd, struct sockaddr_in accept_addr);
int terminate_conection(int conn_id);
int register_to_server(char *address, unsigned short port);
int connect_to_peer(char *address, unsigned short port);
void print_peer_list();
int upload_to_peer(int conn_id, char *file_name);
int terminate_connection(int conn_id);
int receive_from_client(int fd);
int receive_data_from_peer(int fd);
int download_from_peer(int conn_id[], char file_name[][255], int count);
int handle_write(int fd);


#endif
