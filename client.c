#include <sys/stat.h>
#include <fcntl.h>
#include <errno.h>
#include <string.h>
#include <libgen.h> /* for basename */
#include <sys/time.h>
#include <inttypes.h>

#include "proj1.h"
#include "list.h"

#define MAX_CONN 4  /* Maximum number of connections a client will accept
                       (Including the server) */

/***** Packet buffer size *****/
#define PACKET_SIZE 1000
#define BUFLEN 1024


/********* Global Values ***********/
int registered = 0;        /* Flag indicating if client is registered to server */
static int recv_in_progress = 0;  /* Number of connections we are receiving files on */
static int send_in_progress = 0;  /* Number of connections we are sending files to */

struct list_node *connected_peer_list_head; /* Head of the linked list containing the
                                               details of connected peers */
int connected_peer_count = 0;               /* Number of peers connected (including the
                                               server */
int last_id = 0;                            /* Highest connection ID assigned so far */

struct available_peer_node *available_peers = NULL; /* List of available peers
                                                       sent by the server */
int num_available_peers = 0;                        /* Number of available peers
                                                       sent by the server */


static char buff[BUFLEN];   /* global buffer for receiving message */

/************ Forward declaration *************/
int handle_upload_request(struct connected_peer_node *node);
int handle_download_request(struct connected_peer_node *node);


/************ Function definitions *************/

/*
 * Function to add a connected peer to the linked list
 */
void add_to_peer_list(struct sockaddr_in peer_addr, char *hostname, int fd, int port)
{
    struct connected_peer_node *node = NULL;
    node = (struct connected_peer_node*) malloc(sizeof(struct connected_peer_node));
    if (!node) {
        printf("\nError in malloc\n");
        exit(1);
    }

    bzero(node,sizeof(struct connected_peer_node));
    node->id = ++last_id;
    node->fd = fd;
    node->addr = peer_addr;
    node->port = port;
    if (hostname)
        strcpy(node->hostname, hostname);

    /* Add to the end of the list */
    add_to_list_tail(&connected_peer_list_head, node);
    connected_peer_count++;
}

/*
 * Function to delete a connected peer from the linked list
 */
void cleanup_peer(struct connected_peer_node *node)
{
    printf("\nPeer %s:%d closed connection\n", 
            inet_ntoa(node->addr.sin_addr), node->port);

    /* Remove from peer list */
    delete_from_list(&connected_peer_list_head, node);
    connected_peer_count--;

    close(node->fd);

    /* update the readfd set and max fd */
    FD_CLR(node->fd, &readfds);
    if(node->fd == max_fd)
        update_maxfd();
}

/*
 * Function to display the list of connected peers
 */
void print_peer_list()
{
    struct connected_peer_node *node;
    struct list_node *tmp;
    if (connected_peer_list_head == NULL) {
        printf("Not connected to any peers\n");
        return;
    }

    printf("id:hostname\t\t\t\tIP Address\t\tPort No.\n");
    printf("-----------------------------------------------------------------------\n");
    for (tmp = connected_peer_list_head; tmp != NULL; tmp =  tmp->next) {
        node = (struct connected_peer_node *)tmp->container;
        printf("%d:%s\t\t%s\t\t%d\n", node->id, node->hostname,
                inet_ntoa(node->addr.sin_addr), node->port);
    }
}

/*
 * Function to display the list of available peers
 */
void display_available_peers()
{
    int i;
    struct in_addr tmp;

    if (!num_available_peers)
        return;

    printf("\nAvailable peers:\n");
    printf("IP\t\tport\n");
    printf("-----------------------------\n");
    for(i=0 ; i < num_available_peers; i++) {
        tmp = available_peers[i].ip;
        printf("%s\t%d\n",inet_ntoa(tmp), available_peers[i].port);
    }
}

/*
 * Function to check if a peer is present in the list of available
 * peers sent to us by server. Matches both IP Address and port.
 *
 * returns 1 if the peer is present in the available peer list
 * returns 0 if not found
 */
int address_in_available_peers(struct sockaddr_in addr)
{
    int i;
    for(i=0 ; i < num_available_peers; i++) {
        if (addr.sin_addr.s_addr == available_peers[i].ip.s_addr && 
                addr.sin_port == htons(available_peers[i].port)) {
            return 1;
        }
    }
    /* Not found */
    return 0;

}

/*
 * Function to check if a peer is present in the list of connected
 * peers. Matches both IP Address and port.
 *
 * returns 1 if the peer is present in the connected peer list
 * returns 0 if not found
 */
int address_in_connected_peers(struct sockaddr_in addr)
{
    struct list_node *cur;
    struct connected_peer_node *node = NULL;
    for (cur = connected_peer_list_head; cur != NULL; cur = cur->next) {
        node = (struct connected_peer_node *)(cur->container);
        printf("checking against: %s:%d\n", inet_ntoa(node->addr.sin_addr), node->port);
        if (addr.sin_addr.s_addr == node->addr.sin_addr.s_addr && 
                addr.sin_port == htons(node->port)) {
            return 1;
        }
    }

    /* Not found */
    return 0;
}

/* 
 * Function to lookup a peer in the connected peer list by file descriptor
 * Return the node if found, NULL otherwise
 */
struct connected_peer_node *lookup_peer_by_fd(struct list_node *head, int fd)
{
    struct list_node *cur;
    struct connected_peer_node *node = NULL;
    for (cur = head; cur != NULL; cur = cur->next) {
        node = (struct connected_peer_node *)(cur->container);
        if (node->fd == fd)
            return node;
    }
    /* Not found */
    return NULL;
}

/* 
 * Function to lookup a peer in the connected peer list by connection Id
 * Return the node if found, NULL otherwise
 */
struct connected_peer_node *lookup_peer_by_id(struct list_node *head, int id)
{
    struct list_node *cur;
    struct connected_peer_node *node = NULL;
    for (cur = head; cur != NULL; cur = cur->next) {
        node = (struct connected_peer_node *)(cur->container);
        if (node->id == id)
            return node;
    }
    /* Not found */
    return NULL;
}

/*
 * Function to send PACKET_SIZE amount of data from the file to a peer
 * Also prints the Tx summary if the file send is complete
 *
 * return 0 on success, -1 on failure
 */
int send_file_block(struct connected_peer_node *node)
{
    int bytes_sent = 0, bytes_read = 0, retval = 0;
    double tx_rate = 0.0;
    char buff[PACKET_SIZE];
    struct timeval start, end, diff;

    if(node->ctx.bytes_remaining) {
        bzero(buff, PACKET_SIZE);

        /* Get the start time */
        if (gettimeofday(&start, NULL) < 0) {
            printf("Error getting time: %s\n", strerror(errno));
            retval = -1;
            goto cleanup;
        }

        /* read PACKET_SIZE chunk of data from file */
        bytes_read = read(node->ctx.file_fd, buff, PACKET_SIZE);
        if (bytes_read < 0) {
            printf("Error reading from file: %s\n", strerror(errno));
            retval = -1;
            goto cleanup;
        }

        /* send the PACKET_SIZE chunk to peer */
        bytes_sent = send(node->fd, buff, bytes_read, 0);
        if (bytes_sent < bytes_read ) {
            printf("Error sending data to peer: %s\n", strerror(errno));
            retval = -1;
            goto cleanup;
        }

        /* Get the end time */
        if (gettimeofday(&end, NULL) < 0) {
            printf("Error getting time: %s\n", strerror(errno));
            retval = -1;
            goto cleanup;
        }

        /* Update the total_time */
        timersub(&end, &start, &diff);
        timeradd(&(node->ctx.total_time), &diff, &(node->ctx.total_time));

        if (node->ctx.bytes_remaining <= bytes_read)
            node->ctx.bytes_remaining = 0;
        else
            node->ctx.bytes_remaining -= bytes_read;
    }
    /* Check if the complete file has been sent */
    if (!node->ctx.bytes_remaining) {
        printf("\nSuccessfully sent file!!\n");

        /* Calculate the Tx rate */
        tx_rate = ((node->ctx.file_size * 8) / 
                   ((node->ctx.total_time.tv_sec * 1000000) + 
                    node->ctx.total_time.tv_usec));

        tx_rate *= 1000000;

        printf("Tx(%s): %s -> %s,\nFile Size: %" PRIu64 
                " Bytes,\nTime Taken: %ld.%06ld seconds, \nTx Rate: %f bits/second\n",
                my_hostname,my_hostname,node->hostname, 
                node->ctx.file_size, node->ctx.total_time.tv_sec, 
                node->ctx.total_time.tv_usec, tx_rate);

        print_prompt();
        retval = 0; /* success */
    } else {
        /* continue sending the file */
        return 0;
    }

cleanup:
    send_in_progress --;
    /* remove from writefds */
    FD_CLR(node->fd, &writefds);

    close(node->ctx.file_fd);

    /* reset the file transfer conetxt */
    node->ctx.file_fd = -1;
    node->ctx.status = idle;
    FREE(node->ctx.file_name);
    node->ctx.file_size = 0;

    return retval;
}

/*
 * Function to receive PACKET_SIZE amount of data from a peer
 * and write it to fle.
 * Also prints the Rx summary if the file receive is complete
 *
 * returns 0 on success, 
 *        -2 if the connection is closed,
 *        -1 on other failures failure
 */
int receive_file_block(struct connected_peer_node *node)
{
    int bytes_received = 0, bytes_written = 0, retval = 0;
    double rx_rate = 0.0;
    char buff[PACKET_SIZE];
    struct timeval start, end, diff;

    if(node->ctx.bytes_remaining) {
        bzero(buff, PACKET_SIZE);

        /* Get the start time */
        if (gettimeofday(&start, NULL) < 0) {
            printf("Error getting time: %s\n", strerror(errno));
            retval = -1;
            goto cleanup;
        }


        /* receive a PACKET_SIZE chunk from Peer */
        bytes_received = read(node->fd, buff, PACKET_SIZE);
        if (bytes_received < 0) {
            printf("Error receiving data from peer: %s\n", strerror(errno));
            retval = -1;
            goto cleanup;
        }

        if (bytes_received == 0) {
            /* connection to peer closed */
            retval = -2;
            goto cleanup;
        }

        /* write the PACKET_SIZE chunk of data to file */
        bytes_written = write(node->ctx.file_fd, buff, bytes_received);
        if (bytes_written < bytes_received) {
            printf("Error writing to file: %s\n", strerror(errno));
            retval = -1;
            goto cleanup;
        }

        /* Get the end time */
        if (gettimeofday(&end, NULL) < 0) {
            printf("Error getting time: %s\n", strerror(errno));
            retval = -1;
            goto cleanup;
        }

        /* Update the total_time */
        timersub(&end, &start, &diff);
        timeradd(&(node->ctx.total_time), &diff, &(node->ctx.total_time));

        if (node->ctx.bytes_remaining <= bytes_received)
            node->ctx.bytes_remaining = 0;
        else
            node->ctx.bytes_remaining -= bytes_received;
    }
    /* Check if the complete file has been received */
    if (!node->ctx.bytes_remaining) {
        printf("\nFile name : '%s' \nfrom : %s  :  %d\nSuccessfully received!!\n", 
                node->ctx.file_name, node->hostname, node->port);

        rx_rate = ((node->ctx.file_size * 8) / 
                   ((node->ctx.total_time.tv_sec * 1000000) + 
                    node->ctx.total_time.tv_usec));

        rx_rate *= 1000000;
        printf("Rx (%s): %s -> %s,\nFile Size: %" PRIu64 
                " Bytes,\nTime Taken: %ld.%06ld seconds, \nRx Rate: %f bits/second\n",
                my_hostname, node->hostname, my_hostname,
                node->ctx.file_size, node->ctx.total_time.tv_sec, 
                node->ctx.total_time.tv_usec, rx_rate);

        retval = 0; /* success */
        print_prompt();

    } else {
        /* else continue receiving the file */
        return 0;
    }

cleanup:
    recv_in_progress--;
    /* See if we are done downloading all file */
    if (recv_in_progress <= 0) {
        printf("\nAll downloads complete\n");
        print_prompt();
        /* start accepting commands again */
        FD_SET(fileno(stdin), &readfds);
    }

    close(node->ctx.file_fd);
    /* reset the file transfer sontext */
    node->ctx.file_fd = -1;
    node->ctx.status = idle;
    FREE(node->ctx.file_name);
    node->ctx.file_size = 0;
    return retval;
}

/* 
 * Function to register to the server
 * returns 0 on success, -1 on failure
 */
int register_to_server(char *address, unsigned short port)
{
    int fd = -1, len = 0, rc = 0, connected = 0;
    struct sockaddr_in server_addr;
    char *msg = NULL, *ptr;
    char msg_size = 0;
    char *hostname_ptr = NULL;
    char hostname[NI_MAXHOST];
    struct addrinfo *result, *rp, hints;

    /* Check whether an IP address was entered or a host name */
    bzero(&server_addr, sizeof(server_addr));
    rc = inet_pton(AF_INET, address, &(server_addr.sin_addr));
    if (rc == 1) {
        /* An IP address was entered */
        server_addr.sin_family = AF_INET;

        /* Get the host name of the server */
        getnameinfo((struct sockaddr *) &server_addr,
                sizeof(struct sockaddr_in), hostname, NI_MAXHOST, NULL, 0, 0);

        server_addr.sin_port = htons(port);

        fd = socket(AF_INET, SOCK_STREAM, 0);
        if (fd < 0) {
            printf("REGISTER: Error creating socket: %s\n", strerror(errno));
            return -1;
        }

        if (connect(fd, (struct sockaddr *)&server_addr, sizeof(server_addr)) < 0) {
            close(fd);
            printf("REGISTER: Error connecting to peer: %s\n", strerror(errno));
            return -1;
        }
        hostname_ptr = hostname;
    } else {
        /* A hostname was entered: resolve the hostname */
        memset(&hints, 0, sizeof(struct addrinfo));
        hints.ai_flags = AI_CANONNAME;
        hints.ai_socktype = SOCK_STREAM;
        hints.ai_family = AF_INET;

        /* Try to resolve the address */
        rc = getaddrinfo(address, NULL, NULL, &result);
        if (rc != 0 || result == NULL) {
            printf("REGISTER: Error resolving address '%s': %s\n", address, gai_strerror(rc));
            return -1;
        }

        /* Try to connect to the any of the resolved address */
        for (rp = result; rp != NULL; rp = rp->ai_next) {
            bzero(&server_addr, sizeof(server_addr));
            server_addr.sin_family = AF_INET;
            server_addr.sin_addr.s_addr = ((struct sockaddr_in *)(rp->ai_addr))->sin_addr.s_addr;
            server_addr.sin_port = htons(port);

            fd = socket(AF_INET, SOCK_STREAM, 0);
            if (fd < 0) {
                printf("REGISTER: Error creating socket: %s\n", strerror(errno));
                return -1;
            }

            if (connect(fd, (struct sockaddr *)&server_addr, sizeof(server_addr)) < 0) {
                close(fd);
                continue;
            }

            hostname_ptr = address;
            connected = 1;
            break;
        }
        freeaddrinfo(result);

        if (!connected) {
            printf("REGISTER: Unknown host '%s:%d, could not connect,Please type HELP'\n", address, port);
            return -1;
        }
    }

    /* Send the listening port
     * message format:
     * MSG_MYPORT | port
     */
    msg_size = sizeof(uint16_t) + sizeof(uint16_t);
    msg = (char *) malloc (msg_size);
    bzero(msg, msg_size);
    ptr = msg;
    *ptr = (uint16_t) MSG_MYPORT;
    ptr += sizeof(uint16_t);
    *(int *)ptr = listen_port;

    len = send(fd, msg, msg_size, 0);
    if (len < 0) {
        printf("\nREGISTER: error sending port information to server: %s\n",
                strerror(errno));
        close(fd);
        FREE(msg);
        return -1;
    }

    server_fd = fd;

    /* Mark as registered */
    registered = 1;

    printf("Registered to server %s:%d\n", address, port);

    /* store the server address in peer list */
    add_to_peer_list(server_addr, hostname_ptr, server_fd, port);

    /* Add it to select list for peer updates */
    FD_SET(server_fd, &readfds);
    if (max_fd < server_fd) max_fd = server_fd;
    FREE(msg);
    return 0;
}

/* 
 * Function to receive the available peer list update from the
 * server and update the local copy
 *
 * returns 0 on success, -1 otherwise
 */
int recv_update_from_server()
{
    char *buf = NULL, *ptr = NULL;
    uint8_t size = 0;
    uint16_t msg_type;
    int msglen = 0, len = 0, i = 0;
    struct available_peer_node n;
    struct connected_peer_node *node;
    struct list_node *tmp;

    /* msg format:
     * MSG_PEER_LIST | number of peers | list of IP-port pairs
     */

    /* First receive the message type */
    len = read(server_fd, (uint16_t *)&msg_type, sizeof(uint16_t));
    if (len < 0) {
        printf("\nError receiving update from server: %s\n",
                strerror(errno));
        return -1;
    }

    if (len == 0) {
        goto close;
    }

    if (msg_type != MSG_PEER_LIST) {
        printf("\nUnknown message %x\n", msg_type);
        return -1;
    }

    /* First receive the first byte to get the number of addresses */
    len = read(server_fd, (uint8_t *)&size, sizeof(uint8_t));
    if (len < 0) {
        printf("\nError receiving update from server: %s\n", strerror(errno));
        return -1;
    }

    if (len == 0) {
        goto close;
    }

    /* Now allocate the required space */
    msglen = UPDATE_MSG_SIZE(size);
    buf = (char *) malloc (msglen);

    if (buf == NULL) {
        printf("\nError in malloc\n");
        exit(1);
    }

    /* Now receive the entire message */
    len = read(server_fd, buf, msglen);
    if (len < 0) {
        printf("\nError receiving update from server: %s\n", strerror(errno));
        return -1;
    }

    if (len == 0) {
        goto close;
    }

    /* Remove the old available peer list */
    if (available_peers) free(available_peers);
    available_peers = (struct available_peer_node *) malloc (sizeof(struct available_peer_node)*size);

    ptr = buf;
    /* Assign this as the new available peer list */
    for(i=0; i < size; i++) {
        n = *(struct available_peer_node *)ptr;
        ptr += sizeof(n);
        available_peers[i].ip = n.ip;
        available_peers[i].port = n.port;
    }

    num_available_peers = size;

    FREE(buf);
    return 0;

close:
    /* Connection closed */
    printf("\nConnection to server closed\n");
    close(server_fd);

    FD_CLR(server_fd, &readfds);
    if(server_fd == max_fd) 
        update_maxfd();

    /* Mark as unregistered */
    registered = 0;

    /* Terminate connections to all clients */
    tmp = connected_peer_list_head;
    while (tmp) {
        node = (struct connected_peer_node *)tmp->container;
        FD_CLR(node->fd, &readfds);
        if(node->fd == max_fd) 
            update_maxfd();
        close(node->fd);
        tmp = tmp->next;
        delete_from_list(&connected_peer_list_head, node);
    }
  
    FREE(buf);
    return -1;
}

/*
 * Receive a connection request from a peer
 * 
 * Returns the listen port of the peer on success
 * -1 on failure
 */
int receive_peer_connect(int accept_fd, struct sockaddr_in accept_addr)
{
    int len = 0;
    uint16_t msg_type, port;
    char *ptr = NULL;
    char hostname[NI_MAXHOST];

    bzero(buff, BUFLEN);
    
    /* receive the entire message */
    len = read(accept_fd, buff, BUFLEN);
    if (len < 0) {
        printf("\nError reading from socket: %s\n", strerror(errno));
        return -1;
    }

    if (len == 0) {
        close(accept_fd);
        return -1;
    }

    /* see if we have reached the connection limit */
    if (connected_peer_count == MAX_CONN) {
        /* reject connection */
        close(accept_fd);
        return -1;
    }
    /* Parse the message:
     * message format:
     * MSG_CONNECT_REQUEST | port
     */
    ptr = buff;

    msg_type =  *(uint16_t *)ptr;
    if (msg_type != MSG_CONNECT_REQUEST) {
        printf("Unknown message from '%s'. Closing connection\n", 
                inet_ntoa(accept_addr.sin_addr));
        close(accept_fd);
        return -1;
    }

    ptr += sizeof(uint16_t);
    port = *(uint16_t *)ptr;

    /* Get the hostname of the peer */
    getnameinfo((struct sockaddr *) &accept_addr,
            sizeof(struct sockaddr_in), hostname, NI_MAXHOST, NULL, 0, 0);

    add_to_peer_list(accept_addr, hostname, accept_fd, port);

    FD_SET(accept_fd, &readfds);
    if (max_fd < accept_fd) max_fd = accept_fd;

    return port;
}

/* 
 * Function to send a connect request to a peer on address:port
 * The peer must be in the available peer list sent by the server
 *
 * returns 0 in success, -1 on failure
 */
int connect_to_peer(char *address, unsigned short port)
{
    int fd = -1, len = 0, rc  = 0, connected = 0, msg_size = 0;
    struct sockaddr_in peer_addr;
    char *msg = NULL, *ptr = NULL, *hostname_ptr = NULL;
    struct addrinfo *result, *rp, hints;
    char hostname[NI_MAXHOST];


    /* Check whether an IP address was entered or a host name */
    bzero(&peer_addr, sizeof(peer_addr));
    rc = inet_pton(AF_INET, address, &(peer_addr.sin_addr));
    if (rc == 1) {
        /* An IP address was entered */
        if (peer_addr.sin_addr.s_addr == myip.s_addr &&
                port == listen_port) {
            /* This is our own address : can't connect to self*/
            printf("CONNECT to self not allowed\n");
            return -1;
        }

        peer_addr.sin_family = AF_INET;

        /* Get the host name */
        getnameinfo((struct sockaddr *) &peer_addr,
                sizeof(struct sockaddr_in), hostname, NI_MAXHOST, NULL, 0, 0);

        peer_addr.sin_port = htons(port);

        /* Check if the address entered is in the available peer list */
        if (!address_in_available_peers(peer_addr)) {
            printf("CONNECT: Unknown peer %s:%d\n", address, port);
            return -1;
        }

        /* Check if we are already connected to this peer */
        if (address_in_connected_peers(peer_addr)) {
            printf("CONNECT: already connected to peer %s:%d\n", address, port);
            return -1;
        }


        fd = socket(AF_INET, SOCK_STREAM, 0);
        if (fd < 0) {
            printf("CONNECT: Error creating socket: %s\n", strerror(errno));
            return -1;
        }

        if (connect(fd, (struct sockaddr *)&peer_addr, sizeof(peer_addr)) < 0) {
            close(fd);
            printf("CONNECT: Error connecting to peer: %s\n", strerror(errno));
            return -1;
        }
        hostname_ptr = hostname;
    } else {
        /* A hostname was entered: resolve the hostname */
        memset(&hints, 0, sizeof(struct addrinfo));
        hints.ai_flags = AI_CANONNAME;
        hints.ai_socktype = SOCK_STREAM;
        hints.ai_family = AF_INET;

        /* Try to resolve the address */
        rc = getaddrinfo(address, NULL, NULL, &result);
        if (rc != 0 || result == NULL) {
            printf("CONNECT: Error resolving address '%s': %s\n", address, gai_strerror(rc));
            return -1;
        }

        /* Try to connect to the any of the resolved address */
        for (rp = result; rp != NULL; rp = rp->ai_next) {
            bzero(&peer_addr, sizeof(peer_addr));
            peer_addr.sin_family = AF_INET;
            peer_addr.sin_addr.s_addr = ((struct sockaddr_in *)(rp->ai_addr))->sin_addr.s_addr;
            peer_addr.sin_port = htons(port);

            /* Check if the address entered is in our available peer list */
            if (!address_in_available_peers(peer_addr)) {
                continue;
            }

            /* Check if we are already connected to this peer */
            if (address_in_connected_peers(peer_addr)) {
                printf("CONNECT: already connected to peer %s:%d\n", address, port);
                return -1;
            }

            if (peer_addr.sin_addr.s_addr == myip.s_addr &&
                    port == listen_port) {
                /* This is our own address: can't connect to self */
                printf("CONNECT to self not allowed\n");
                return -1;
            }

            fd = socket(AF_INET, SOCK_STREAM, 0);
            if (fd < 0) {
                printf("CONNECT: Error creating socket: %s\n", strerror(errno));
                return -1;
            }

            if (connect(fd, (struct sockaddr *)&peer_addr, sizeof(peer_addr)) < 0) {
                close(fd);
                printf("CONNECT: Error connecting to peer: %s\n", strerror(errno));
                return -1;
            }

            hostname_ptr = address;
            connected = 1;
            break;
        }
        freeaddrinfo(result);

        if (!connected) {
            printf("CONNECT: Unknown host '%s:%d'\n", address, port);
            return -1;
        }
    }


    msg_size = sizeof(uint16_t) + sizeof(uint16_t);
    msg = (char *) malloc (msg_size);
    bzero(msg, msg_size);
    ptr = msg;
    *ptr = (uint16_t) MSG_CONNECT_REQUEST;
    ptr += sizeof(uint16_t);
    *(int *)ptr = listen_port;

    len = send(fd, msg, msg_size, 0);
    if (len < 0) {
        printf("\nCONNECT: error sending connect request to peer: %s\n",
                strerror(errno));
        close(fd);
        FREE(msg);
        return -1;
    }

    FREE(msg);
    printf("Connected to peer %s : %d\n", address, port);

    /* Add it to select list for peer updates */
    add_to_peer_list(peer_addr, hostname_ptr, fd, ntohs(peer_addr.sin_port));
    FD_SET(fd, &readfds);
    if (max_fd < fd) max_fd = fd;
    return 0;
}

/*
 * Function to send data to peer
 * This is called, when a fd we are sending file to becomes 
 * ready to write
 *
 * return 0 on success, -1 otherwise
 */
int handle_write(int fd)
{
    struct connected_peer_node *node = NULL;

    /* Lookup the peer from the peer list */
    node = lookup_peer_by_fd(connected_peer_list_head, fd);
    if (!node) {
        printf("Unknown peer\n");
        FD_CLR(fd, &writefds);
        close(fd);
        return -1;
    }

    if (node->ctx.status != sending) {
        printf("Node not in sending mode\n");
        /* Remove from writefd set */
        FD_CLR(fd, &writefds);
        return -1;
    }
    return send_file_block(node);
}

/* Function to receive message from a peer
 *
 * returns 0 on success,
 *        -2 if the connection is closed
 *        -1 on failure
 */
int receive_data_from_peer(int fd)
{
    uint16_t msg_type;
    int len = 0, rc = 0;
    struct connected_peer_node *node = NULL;

    /* Lookup the peer from the peer list */
    node = lookup_peer_by_fd(connected_peer_list_head, fd);
    if (!node) {
        printf("Unknown peer\n");
        close(fd);
        return -1;
    }

    /* Check if we are already sending/receiving and call
     * appropriate handlers */
    if (node->ctx.status == receiving) {
        rc = receive_file_block(node);
        if (rc == -2) 
            goto close;
        return rc;
    } else if (node->ctx.status == sending) {
        rc = send_file_block(node);
        if (rc == -2) 
            goto close;
        return rc;
    }

    /* New message */
    /* First receive the message type */
    len = read(fd, (uint16_t *)&msg_type, sizeof(uint16_t));
    if (len < 0) {
        printf("\nError receiving msg type from peer\n");
        return -1;
    }

    if (len == 0) 
        goto close;

    if (msg_type == MSG_DOWNLOAD_REQUEST) {
        return handle_download_request(node);
    }

    if (msg_type == MSG_UPLOAD_REQUEST) {
        return handle_upload_request(node);
    }

    printf("\nUnknown message received from peer: %s:%d\n", 
            inet_ntoa(node->addr.sin_addr), node->port);
    return -1;

close:
    /* connection closed */
    printf("\nPeer %s:%d closed connection\n", inet_ntoa(node->addr.sin_addr), node->port);

    /* Remove from peer list */
    delete_from_list(&connected_peer_list_head, node);
    connected_peer_count--;

    close(fd);
    FD_CLR(fd, &readfds);
    if(fd == max_fd) 
        update_maxfd();
    return -2;
}

/* 
 * Function to upload a file (file_name) to a peer identified by conn_id
 *
 * returns 0 on success, -1 on failure
 */

int upload_to_peer(int conn_id, char *file_name)
{
    struct connected_peer_node *node = NULL;
    struct stat st;
    int msg_size = 0, len = 0, bytes_read = 0, bytes_sent = 0, i = 0, file_fd = -1;
    char *msg = NULL, *ptr = NULL;
    uint64_t file_size = 0, bytes_remaining = 0;
    char buff[PACKET_SIZE];
    char *base_file_name = NULL, *file_name_dup =NULL;
    struct timeval start, end, diff, total_time = (struct timeval){0};
    double tx_rate = 0.0;
    uint16_t msg_type;

    /* Do not allow upload to server */
    if (conn_id == 1) {
        printf("UPLOAD to server not allowed\n");
        return -1;
    }

    node = lookup_peer_by_id(connected_peer_list_head, conn_id);
    if (!node) {
        printf("UPLOAD: Invalid connection ID\n");
        return -1;
    }

    if (stat(file_name, &st) < 0) {
        printf("UPLOAD: Error accessing file: %s\n", strerror(errno));
        return -1;
    }

    file_size = st.st_size;
    
    /* create a copy of the filename because basename may modify it */
    file_name_dup = strdup(file_name);
    if (!file_name_dup) {
        printf("Error in strdup()\n");
        exit(1);
    }

    /* get the base file name from the full file name */
    base_file_name = basename(file_name_dup);

    /* First send the upload command followed by the file size */
    /* Message format:
     * MSG_UPLOAD_REQUEST | filesize | filename size | filename 
     */
    msg_size = sizeof(uint16_t) + sizeof(uint64_t) + sizeof(uint64_t) + strlen(base_file_name);
    msg = (char *) malloc (msg_size);
    if (!msg) {
        printf("Error in malloc\n");
        exit(1);
    }

    bzero(msg, msg_size);
    ptr = msg;
    *ptr = (uint16_t) MSG_UPLOAD_REQUEST;
    ptr += sizeof(uint16_t);

    *(int *)ptr = file_size;
    ptr += sizeof(uint64_t);

    *(int *)ptr = strlen(base_file_name);
    ptr += sizeof(uint64_t);

   for (i = 0; i< strlen(file_name); ++i) {
      *ptr++ = base_file_name[i];
   } 

    len = send(node->fd, msg, msg_size, 0);
    if (len < 0) {
        printf("\nUPLOAD: error sending message to peer: %s\n", strerror(errno));
        FREE(msg);
        return -1;
    }

    FREE(msg);
    FREE(file_name_dup);

    /* Get the response MSG_UPLOAD_ACCEPT or MSG_UPLOAD_REJECT */
    len = read(node->fd, (uint16_t *)&msg_type, sizeof(uint16_t));
    if (len < 0) {
        printf("\nUPLOAD: Error receiving data from peer: %s\n", strerror(errno));
        return -1;
    }

    if (len == 0)  {
        cleanup_peer(node);
        return -1;
    }

    if (msg_type != MSG_UPLOAD_ACCEPT) {
        printf("\nUPLOAD: Peer %s rejected upload request\n", node->hostname);
        return -1;
    }

    /* Now start sending the file in chunks */

    /* Open the file for reading */
    file_fd = open(file_name, O_RDONLY);

    if (file_fd < 0) {
        printf("UPLOAD: Error opening file '%s': %s\n", file_name, strerror(errno));
        return -1;
    }

    printf("\nSending file...\nfile name : '%s'\nto :  %s  :  %d \n", file_name, node->hostname, node->port);

    bytes_remaining = file_size;
    while(bytes_remaining) {
        bzero(buff, PACKET_SIZE);
        /* Get the start time */
        if (gettimeofday(&start, NULL) < 0) {
            printf("UPLOAD: Error getting time: %s\n", strerror(errno));
            close(file_fd);
            return -1;
        }

        /* read PACKET_SIZE chunk of data from file */
        bytes_read = read(file_fd, buff, PACKET_SIZE);
        if (bytes_read < 0) {
            printf("UPLOAD: Error reading from file: %s\n", strerror(errno));
            close(file_fd);
            return -1;
        }

        /* Send the chunk to Peer */
        bytes_sent = send(node->fd, buff, bytes_read, 0);
        if (bytes_sent < bytes_read) {
            printf("UPLOAD: Error sending data to peer: %s\n", strerror(errno));
            close(file_fd);
            return -1;
        }

        /* Get the end time */
        if (gettimeofday(&end, NULL) < 0) {
            printf("UPLOAD: Error getting time: %s\n", strerror(errno));
            close(file_fd);
            return -1;
        }

        timersub(&end, &start, &diff);
        timeradd(&total_time, &diff, &total_time);

        if (bytes_remaining <= bytes_sent)
            bytes_remaining = 0;
        else
            bytes_remaining -= bytes_sent;
    }

    close(file_fd);
    printf("Successfully uploaded file!!\n");

    /* Calculate the Tx rate */
    tx_rate = ((file_size * 8) / 
            ((total_time.tv_sec * 1000000) + total_time.tv_usec));

    tx_rate *= 1000000;

    printf("\nTx (%s): %s -> %s, \nFile Size: %" PRIu64 
            " Bytes, \nTime Taken: %ld.%06ld seconds,\nTx Rate: %f bits/second\n",
            my_hostname, my_hostname, node->hostname, 
            file_size, total_time.tv_sec, total_time.tv_usec, tx_rate);

    return 0;
}


/*
 * Function to handle an upload request from a peer
 * This function receives a block of data and returns,
 * the rest of the file is downloaded when the data is available
 * and select returns
 *
 * returns 0 on success, -1 on failure
 */
int handle_upload_request(struct connected_peer_node *node)
{
    int len = 0;
    char *ptr = NULL;
    uint64_t file_size = 0;
    int file_fd = -1;
    uint64_t file_name_len = 0;
    char buff[PACKET_SIZE];
    char file_name[255];
    mode_t mode;
    int rc = 0;
    uint16_t msg_type;

    /* 
     * Message format:
     * MSG_UPLOAD_REQUEST | file size | file name size | file name
     */

    /* we have already received the msg type, now receive the file size
     * and file name size */
    len = read(node->fd, &buff, sizeof(uint64_t) + sizeof(uint64_t));
    if (len < 0) {
        printf("\nError receiving data from peer\n");
        return -1;
    }

    if (len == 0) {
        goto close;
    }

    ptr = buff;

    file_size = *(uint64_t *)ptr;
    ptr+= sizeof(uint64_t);
    file_name_len = *(uint64_t *)ptr;

    /* Now receive the file name */
    len = read(node->fd, &file_name, file_name_len);
    if (len < 0) {
        printf("\nError receiving data from peer\n");
        return -1;
    }

    if (len == 0) {
        goto close;
    }

    file_name[file_name_len] = '\0';

    /* Open the file for creating */
    mode = S_IRUSR | S_IWUSR | S_IRGRP | S_IROTH;
    file_fd = open(file_name, O_WRONLY | O_CREAT | O_TRUNC, mode);

    if (file_fd < 0) {
        printf("\nError creating file: %s\n", strerror(errno));

        /* Send the UPLOAD_REJECT message */
        msg_type = (uint16_t) MSG_UPLOAD_REJECT;
        len = send(node->fd, (char *)&msg_type, sizeof(msg_type), 0);
        if (len < 0) {
            printf("\nError sending message to peer\n");
        }
        return -1;
    }

    /* Everything fine so far - Send the UPLOAD_ACCEPT message */
    msg_type = (uint16_t) MSG_UPLOAD_ACCEPT;
    len = send(node->fd, (char *)&msg_type, sizeof(msg_type), 0);
    if (len < 0) {
        printf("\nError sending message to peer\n");
        return -1;
    }

    printf("\nReceiving file... \n");
    print_prompt();

    /* Add to the file transfer context for the node */
    node->ctx.status = receiving;
    node->ctx.file_fd = file_fd;
    node->ctx.file_name = strdup(file_name);
    node->ctx.bytes_remaining = node->ctx.file_size = file_size;
    node->ctx.total_time = (struct timeval){0};

    rc = receive_file_block(node);
    if (rc == -2) {
        goto close;
    }
    
    return rc;

close:

    if (file_fd != -1) close(file_fd);
    cleanup_peer(node);

    return -1;
}

/*
 * Function to send download file request to peers (called as a result 
 * of DOWNLOAD command)
 * This function sends the MSG_DOWNLOAD_REQUEST to all the requested peers and
 * receives the response.
 * If accpeted, it adds the fd to write fd set to send the data
 * If rejected, it prints a message and skips
 *
 * returns 0 on success, -1 on failure
 */
int download_from_peer(int conn_id[], char file_name[][255], int count)
{
    int i, j, len, success = 0;
    uint16_t msg_type;
    struct connected_peer_node *node;
    uint64_t file_size;
    mode_t mode;
    int msg_size;
    char *msg = NULL, *ptr = NULL;

    mode = S_IRUSR | S_IWUSR | S_IRGRP | S_IROTH;

    for (i = 0; i < count; ++i) {
        if (conn_id[i] == 1) {
            printf("DOWLOAD from server not allowed , Please type HELP,\nskipping..\n");
            continue;
        }
        node = lookup_peer_by_id(connected_peer_list_head, conn_id[i]);
        if (!node) {
            printf("DOWNLOAD: Invalid connection ID %d\n", conn_id[i]);
            continue;
        }

        /* First send the upload command followed by the file name */
        /* Message format:
         * MSG_DOWNLOAD | filename size | filename 
         */
        msg_size = sizeof(uint16_t) + sizeof(uint64_t) + strlen(file_name[i]);
        msg = (char *) malloc (msg_size);

        if (!msg) {
            printf("Error in malloc\n");
            exit(1);
        }

        bzero(msg, msg_size);
        ptr = msg;
        *ptr = (uint16_t) MSG_DOWNLOAD_REQUEST;
        ptr += sizeof(uint16_t);

        *(int *)ptr = strlen(file_name[i]);
        ptr += sizeof(uint64_t);

        for (j = 0; j< strlen(file_name[i]); ++j) {
            *ptr++ = file_name[i][j];
        } 

        len = send(node->fd, msg, msg_size, 0);
        if (len < 0) {
            printf("\nDOWNLOAD: error sending message to peer: %s\n",
                    strerror(errno));
            FREE(msg);
            continue;
        }
        FREE(msg);

        /* Now receive the response */
        /* Response Format:
         * MSG_DOWNLOAD_ACCEPT | filesize
         * or
         * MSG_DOWNLAOD_REJECT
         */

        /* First receive the message type */
        len = read(node->fd, (uint16_t *)&msg_type, sizeof(uint16_t));
        if (len < 0) {
            printf("\nDOWNLOAD: Error receiving update from server: %s\n",
                    strerror(errno));
            continue;
        }

        if (len == 0) {
            cleanup_peer(node);
            continue;
        }

        if (msg_type == MSG_DOWNLOAD_REJECT) {
            /* Peer rejected the download */
            printf("DOWNLOAD: Download requested rejected by Peer\n");
            continue;
        }
        if (msg_type != MSG_DOWNLOAD_ACCEPT) {
            printf("Unknown message\n");
            continue;
        }

        /* We have received an MSG_DOWNLOAD_ACCEPT message */

        /* Now receive the size of the file */
        len = read(node->fd, (char *)&file_size, sizeof(uint64_t));
        if (len < 0) {
            printf("DOWNLOAD: Error receiving data from peer\n");
            continue;
        }
       
        if (len == 0) {
            cleanup_peer(node);
            continue;
        }

        /* create the file to be downloaded */
        node->ctx.file_fd = open(file_name[i], O_WRONLY | O_CREAT | O_TRUNC, mode);

        if (node->ctx.file_fd < 0) {
            printf("DOWNLOAD: Error creating file: %s\n", strerror(errno));
            continue;
        }

        /* create the file transfer context for the node */
        node->ctx.status = receiving;
        node->ctx.file_name = strdup(file_name[i]);
        node->ctx.bytes_remaining = node->ctx.file_size = file_size;
        node->ctx.total_time = (struct timeval){0};

        recv_in_progress++;
        printf("\nReceiving file..\n");
        success = 1;
        node = NULL;
    }
    if (!success) {
        /* none of the files could be downloaded */
        printf("DOWNLOAD: No files could be downloaded\n");
        return -1;
    }
    /* remove the stdin from select FD set, until all the downloads are complete */
    FD_CLR(fileno(stdin), &readfds);

    return 0;
}

/*
 * Function to receive and process the download request from peer
 * This function receives the request and sends the response
 * either MSG_DOWNLOAD_ACCEPT if everything is all right
 * or MSG_DOWLNOAD_REJECT if something goes wrong
 * then adds the socket file descriptor to the writefds to send the
 * files in paralllel
 *
 * returns 0 on success, -1 on failure
 */
int handle_download_request(struct connected_peer_node *node)
{
    int len = 0, msg_size = 0;
    char *ptr = NULL, *msg = NULL;
    uint64_t file_size = 0, file_name_len = 0;
    char file_name[255];
    struct stat st;
    uint16_t msg_type;

    /* receive the file name size */
    len = read(node->fd, &file_name_len, sizeof(uint64_t));
    if (len < 0) {
        printf("\nError receiving data from peer: %s\n", strerror(errno));
        return -1;
    }

    if (len == 0) {
        goto close;
    }

    /* Now receive the file name */
    len = read(node->fd, &file_name, file_name_len);
    if (len < 0) {
        printf("\nError receiving data from peer: %s\n", strerror(errno));
        return -1;
    }

    if (len == 0) {
        goto close;
    }

    file_name[file_name_len] = '\0';

    /* Check if the file exists, and readable
     * if no, send MSG_DOWNLOAD_REJECT
     * else send MSG_DOWNLOAD_ACCEPT along with the file size */

    /* Check if the file exists and we have read permission on the file */
    if (access(file_name, R_OK) < 0){
        printf("File not found OR No read permission on requested file '%s'\n", file_name);
        goto reject;
    }
    if (stat(file_name, &st) < 0) {
        printf("Error accessing file: %s\n", strerror(errno));
        goto reject;
    }

    file_size = st.st_size;
    /* Check if this is a regular file */
    if (!S_ISREG(st.st_mode)) {
        printf("Reqested file '%s' not a regular file\n", file_name);
        goto reject;
    }

    /* open the file to be sent */
    node->ctx.file_fd = open(file_name, O_RDONLY);

    if (node->ctx.file_fd < 0) {
        printf("Error opening requested file '%s': %s\n", 
                file_name, strerror(errno));
        goto reject;
    }

    /* Now send the MSG_DOWNLOAD_ACCEPT response */
    msg_size = sizeof(uint16_t) + sizeof(uint64_t);
    msg = (char *) malloc (msg_size);
    bzero(msg, msg_size);
    ptr = msg;
    *ptr = (uint16_t) MSG_DOWNLOAD_ACCEPT;
    ptr += sizeof(uint16_t);

    *(uint64_t *)ptr = file_size;

    len = send(node->fd, msg, msg_size, 0);
    if (len < 0) {
        printf("\nError sending message to peer: %s\n",
                strerror(errno));
        FREE(msg);
        return -1;
    }

    printf("\nSending file...\nfile name : '%s' \nto : %s  :  %d\n", file_name, node->hostname, node->port);
    print_prompt();

    /* create the file transfer context for the node */
    node->ctx.status = sending;
    node->ctx.file_name = strdup(file_name);
    node->ctx.bytes_remaining = node->ctx.file_size = file_size;
    node->ctx.total_time = (struct timeval){0};

    FREE(msg);
    /* Now add the socket to write fd set */
    FD_SET(node->fd, &writefds);
    send_in_progress++;
    return 0;

reject:
    msg_type = (uint16_t) MSG_DOWNLOAD_REJECT;
    len = send(node->fd, (char *)&msg_type, sizeof(msg_type), 0);
    if (len < 0) {
        printf("\nError sending message to peer: %s\n", strerror(errno));
        return -1;
    }
    return 0;

close:
    if (node->ctx.file_fd != -1) close(node->ctx.file_fd);
    cleanup_peer(node);

    return -1;
}

/*
 * Function to terminate a connection with a peer identifief by conn_id
 * closes the socket fd, and removes the peer from the peer list
 *
 * returns 0 on success,-1 on failure
 */
int terminate_connection(int conn_id)
{
    struct connected_peer_node *node = NULL;

    /* Lookup the peer from the peer list */
    node = lookup_peer_by_id(connected_peer_list_head, conn_id);
    if (!node) {
        printf("TERMINATE: Invalid connection ID , Please type HELP\n");
        return -1;
    }

    printf("Terminated connection to %s  :  %d\n", inet_ntoa(node->addr.sin_addr), node->port);

    close(node->fd);
    FD_CLR(node->fd, &readfds);
    if(node->fd == max_fd) 
        update_maxfd();

    /* Remove from peer list */
    delete_from_list(&connected_peer_list_head, node);
    connected_peer_count--;

    return 0;
}


