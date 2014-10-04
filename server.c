#include <stdio.h>
#include <errno.h>
#include "list.h"
#include "proj1.h"

#define BUFLEN 1024

/***** Global Values *******/
struct list_node *server_ip_list_head;    /* head of the linked list containing
                                             the list of clients registered to this
                                             server */
int server_ip_count = 0;                  /* number of clients registered to this
                                             server */


/************ Function definitions **************/

/*
 * Function to add a client to the linked list of registered clients
 */
void add_server_ip(struct sockaddr_in addr, int port, int fd)
{
    struct client_node *node;
    node = (struct client_node *) malloc(sizeof(struct client_node));
    if (!node) {
        printf("\nError in malloc\n");
        exit(1);
    }
    bzero(node,sizeof(struct client_node));
    node->clientaddr = addr;
    node->port = port;
    node->fd = fd;

    /* Get the hostname */
    getnameinfo((struct sockaddr *) &addr,
            sizeof(struct sockaddr_in), node->hostname, NI_MAXHOST, NULL, 0, 0);

    add_to_list(&server_ip_list_head, node);
    server_ip_count++;
}

/*
 * Function to lookup a peer in the registered client list using the file descriptor
 *
 * Returns the client node if found, NULL otherwise
 */
struct client_node *lookup_client_by_fd(struct list_node *head, int fd)
{
        struct list_node *cur;
        struct client_node *node;
        for (cur = head; cur != NULL; cur = cur->next) {
            node = (struct client_node *)(cur->container);
            if (node->fd == fd)
                return node;
        }
        return NULL;
}


/*
 * Function to print the list of registered clients
 */
void print_client_list()
{
    struct list_node *tmp;
    struct client_node *node;

    if (server_ip_list_head == NULL) {
        printf("No clients registered\n");
        return;
   }
   printf("Registered Clients\n");
   printf("Hostname\t\t\t\tIP Address\t\tPort No.\n");
   printf("----------------------------------------------------------------------\n");
    for (tmp = server_ip_list_head; tmp != NULL; tmp =  tmp->next) {
        node = (struct client_node *)tmp->container;
        printf("%s\t\t%s\t\t%d\n", node->hostname,
                inet_ntoa(node->clientaddr.sin_addr), node->port);
    }

}


/*
 * Function to send the list of available peers to the registered clients
 *
 * returns 0 on success, -1 on failure
 */
int send_ip_list_to_client()
{
    char *msg, *ptr;
    struct list_node *tmp;
    struct client_node *node;
    int size, len;
    struct available_peer_node n;

    /* If no clients are connected, just return */
    if (server_ip_count == 0) {
        return 0;
    }

    /* Allocate the space required:
     * message format:
     * MSG_PEER_LIST | number of peers | ip-port pair for each peer
     */
    size = UPDATE_MSG_SIZE(server_ip_count);
    msg = (char *) malloc(size);
    bzero(msg, size);

    if (msg == NULL) {
        printf("\nError in malloc\n");
        exit(1);
    }

    /* Prepare the data to be sent */
    ptr = msg;

    /* Add the message type */
    *ptr = (uint16_t)MSG_PEER_LIST;
    ptr += sizeof(uint16_t);

    /* Add the count */
    *ptr = (uint8_t)server_ip_count;
    ptr += sizeof(uint8_t);

    /* Now add each IP and port */
    for (tmp = server_ip_list_head; tmp != NULL; tmp =  tmp->next) {
        node = (struct client_node *)tmp->container;
        n.ip = node->clientaddr.sin_addr;
        n.port = node->port;
        *(struct available_peer_node *)ptr = n;
        ptr += sizeof(n);
    }

    /* Now send this data to each client */
    for (tmp = server_ip_list_head; tmp != NULL; tmp =  tmp->next) {
        node = (struct client_node *)tmp->container;

        len = send(node->fd, msg, size, 0);
        if (len < 0) {
            printf("\nError sending server IP list to %s:%d\n", 
                    inet_ntoa(node->clientaddr.sin_addr), node->port);
        }
    }

    if(msg) free(msg);
    return 0;
}


/*
 * Function to handle incoming register request from client
 * and read the listening port
 *
 * returns 0 on success, -1 on failure
 */
int receive_client_connect(int accept_fd)
{
    char buff[BUFLEN];
    int len, client_port;
    uint16_t msg_type;
    char *ptr;

    bzero(buff, BUFLEN);
    len = read(accept_fd, buff, BUFLEN);
    if (len < 0) {
        printf("\nError reading from socket: %s\n", strerror(errno));
        return -1;
    }

    ptr = buff;

    msg_type =  *(uint16_t *)ptr;
    if (msg_type != MSG_MYPORT) {
        printf("Client did not send port information. Closing connection\n");
        close(accept_fd);
        return -1;
    }

    ptr += sizeof(uint16_t);
    client_port = *(uint16_t *)ptr;

    /* Add it to FD set */
    FD_SET(accept_fd, &readfds);
    if (max_fd < accept_fd) max_fd = accept_fd;

    return client_port;

}

/*
 * Function to receive from client
 * We don't actually expect anything to be received from the client.
 * But the select() may return this socket as ready to be read if the 
 * connection closes. This function handles that.
 * If any actual data is received, it is ignored
 *
 * returns 0 on success, -1 on failure
 */
int receive_from_client(int fd)
{
    uint16_t msg_type;
    int len;
    struct client_node *node;

    /* Lookup the peer from the peer list */
    node = lookup_client_by_fd(server_ip_list_head, fd);
    if (!node) {
        /* Ignore */
        close(fd);
        return 0;
    }

    /* receive the message type */
    len = read(fd, (uint16_t *)&msg_type, sizeof(uint16_t));
    if (len < 0) {
        printf("\nError receiving data from peer\n");
        return -1;
    }

    if (len == 0) {
        /* Client closed connection */
        printf("\nClient %s:%d closed connection\n", inet_ntoa(node->clientaddr.sin_addr), node->port);

        /* Remove from peer list */
        delete_from_list(&server_ip_list_head, node);
        server_ip_count --;

        close(fd);
        FD_CLR(fd, &readfds);
        if(fd == max_fd)
            update_maxfd();

        /* send the updated peer list to the registered client */
        send_ip_list_to_client();
        return -1;
    }

    /* We don't expect any data from client, so just discard the data */
    return 0;
}

