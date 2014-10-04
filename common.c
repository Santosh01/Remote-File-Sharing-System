#include "list.h"
#include "proj1.h"

/* Macro to identify if an address is loopback address */
#define IS_LOOPBACK(addr)  ((((long int) (addr)) & htonl(0xff000000)) == htonl(0x7f000000)) /* in 127.0.0.0 network */

/******* Global values *******/
struct in_addr myip;           /* IP address the process in listening on */
char my_hostname[NI_MAXHOST];  /* Host name for this address */

/******** Function definitions *************/

/*
 * This function updates the max_fd
 * This needs to be called when an fd is removed from the readfd set
 * to make sure the max_fd is accurate
 */
void inline update_maxfd()
{
    int i;
    for(i = max_fd-1; i >= 0; --i) {
        if(FD_ISSET(i, &readfds)) {
            max_fd = i;
            break;
        }
    }
}


/*
 * Function to find out the IP address on which the process should listen
 * The IP Address should be the Public IP address of the system
 * This is found by connecting to a public DND server (8.8.8.8)
 * and reading the IP address of the socket
 *
 * Returns the sockaddr_in structure representin the address
 */
struct sockaddr_in getmyip()
{
    int sockfd;
    struct sockaddr_in servaddr, myaddr;
    socklen_t len;

    len = sizeof(struct sockaddr_in);
    sockfd=socket(AF_INET,SOCK_DGRAM,0);
    if(sockfd < 0) {
        printf("\nError is socket()\n");
        exit(1);
    }

    /* Connect to a Public DNS server */
    bzero(&servaddr,sizeof(servaddr));
    servaddr.sin_family = AF_INET;
    servaddr.sin_addr.s_addr=inet_addr("8.8.8.8");
    servaddr.sin_port=htons(53);

    if (connect(sockfd, (struct sockaddr *)&servaddr, sizeof(struct sockaddr_in)) < 0) {
        printf("\nError in connect()\n");
        exit(1);
    }

    if (getsockname(sockfd,(struct sockaddr *)&myaddr,&len) < 0) {
        printf("\nError in getsockname()\n");
        exit(1);
    }

    /* Get the hostname of the server */
    getnameinfo((struct sockaddr *) &myaddr,
            sizeof(struct sockaddr_in), my_hostname, NI_MAXHOST, NULL, 0, 0);

    myip = myaddr.sin_addr;

    return myaddr;
}

/*
 * Function to terminate all connection and cleanly exit
 */
int handle_exit()
{
    struct list_node *tmp;
    struct client_node *cnode;
    struct connected_peer_node *node;

    if (mode == server_mode) {
        /* disconnect from all the clients */
        for (tmp = server_ip_list_head; tmp != NULL; tmp =  tmp->next) {
            cnode = (struct client_node *)tmp->container;
            close(cnode->fd);
        }
    } else {
        /* Client mode */
        /* Disconnect from all Peers */
        for (tmp = connected_peer_list_head; tmp != NULL; tmp =  tmp->next) {
            node = (struct connected_peer_node *)tmp->container;
            /* Close socket */
            close(node->fd);
        }
    }
    /* Close the listening socket and exit */
    close(listen_fd);
    exit(0);
}

