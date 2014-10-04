#include <stdio.h>
#include <unistd.h>
#include <stdlib.h>
#include <string.h>
#include <netdb.h>
#include <sys/types.h> 
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <errno.h>
#include <arpa/inet.h>
#include <sys/socket.h>
#include <netdb.h>
#include <ifaddrs.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <signal.h>
#include "proj1.h"


#define BUFLEN 1024

/* Global values */
int mode;            /* Whether the program is running in server or client mode */
int listen_port;     /* Port on which we are listening */
int listen_fd = -1;  /* File descriptor for the listening socket */
int server_fd = -1;  /* File descriptor of our socket connected to server */

fd_set readfds;      /* Set of file descriptors to test for reading */
fd_set writefds;     /* Set of file descriptors to test for reading */
int max_fd = -1;      /* Value of the highest file descriptor */

/* Function to handle SIGINT (Ctrl+C).
 * This function exits cleanly on receiving SIGINT (Ctrl+C) from the user */
void signal_handler(int signo)
{
    if (signo == SIGINT) {
        /* Ctrl+C was pressed, call the exit function */
        printf("\nExiting...\n");
        handle_exit();
    }
    /* Ignore other signals */
    return;
}

/* Function to print the command prompt */
void print_prompt ()
{
    printf("enter command > ");
    fflush(stdout);
}

int main (int argc, char *argv[])
{
    int i, rc, len;
    struct timeval tv;
    char cmd_buff[BUFLEN];
    fd_set temp_rfds, temp_wfds;
    int stdin_fd = fileno(stdin);
    int accept_fd, client_port, sockopt;
    socklen_t accept_len;
    struct sockaddr_in listen_addr, accept_addr;

    /* Accept only two arguments */
    if (argc != 3) {
        printf("Usage:\n%s <mode> <listen port>\n", argv[0]);
        printf("Where:\nmode: c -> to run in client mode\n");
        printf("\ts -> to run in server mode\n");
        exit(1);
    }

    /* Get the command line parameters */
    if (strcasecmp(argv[1], "c") == 0) {
        /* Client mode */
        mode = client_mode;
    } else if (strcasecmp(argv[1], "s") == 0) {
        /* Server mode */
        mode = server_mode;
    } else {
        printf("Invalid mode, should be 's' or 'c'\n");
        exit(1);
    }

    listen_port = strtol(argv[2], NULL, 10);
    /* Allow the program to listen on non-standard port only */
    /* This also takes care of invalid integer */
    if (listen_port <= 1024 || listen_port > 65535 ) {
        printf("Invalid port number. Should be between 1025-65535\n");
        exit(1);
    }

    /* Register the signal handler to handle Ctrl+C */
    if (signal(SIGINT, signal_handler) == SIG_ERR) {
        printf("\nError registering signal handler\n");
        /* Continue even if there is an error */
    }

    /* Get the IP address to listen on */
    bzero(&listen_addr,sizeof(listen_addr));
    listen_addr = getmyip();
    listen_addr.sin_family = AF_INET;
    listen_addr.sin_port=htons(listen_port);

    /* Create the listening socket */
    if ((listen_fd = socket(AF_INET, SOCK_STREAM, 0)) < 0) {
        printf("Error in creating listening socket: %s\n", strerror(errno));
        exit(1);
    }

    /* Enable port re-use, so that we can listen on the same port on starting
     * immediately after quitting */
    sockopt = 1;
    if (setsockopt(listen_fd, SOL_SOCKET, SO_REUSEADDR, 
                (const void *)&sockopt , sizeof(sockopt)) < 0) {
        printf("Error in setting socket option: %s\n", strerror(errno));
        exit(1);
    }

    /* bind the socket to the given port */
    if (bind(listen_fd, (struct sockaddr *)&listen_addr, 
                sizeof(listen_addr)) < 0) {
        printf("Error in binding socket to listening port: %s\n", 
                strerror(errno));
        exit(1);
    }

    /* Start listening on the socket, for incoming connections */
    if (listen(listen_fd, 10) < 0) {
        printf("Error in listening: %s\n", strerror(errno));
        exit(1);
    }

    FD_ZERO(&readfds);
    FD_ZERO(&writefds);
    FD_ZERO(&temp_rfds);
    FD_ZERO(&temp_wfds);

    /* Add the interested fds to read fd set */
    FD_SET(stdin_fd, &readfds);
    FD_SET(listen_fd, &readfds);

    max_fd = listen_fd;

    print_prompt();

    while (1) {
        /* Get a copy of the read and write fd sets */
        temp_rfds = readfds;
        temp_wfds = writefds;

        /* Set select timeout of 5 seconds 
         * Need to do this before every select, because linux may modify the
         * timeout argument on returning from select */
        tv.tv_sec = 5;
        tv.tv_usec = 0;

        rc = select(max_fd + 1, &temp_rfds, &temp_wfds, NULL, &tv);

        if (rc < 0) {
            /* Check if we were inturrepted by signal */
            if (errno != EINTR) {
                printf("\nError in select\n");
            }
        } else if (rc == 0) {
            /* Select timeout */
        } else {
            /* Go through the fds to see if anything is ready to read */
            for (i = 0; i <= max_fd; i++) {
                if (FD_ISSET(i, &temp_rfds)) {

                    /* Check a command was entered */
                    if (i == stdin_fd) {
                        /* Read command */
                        len = read(stdin_fd, cmd_buff, sizeof(cmd_buff));
                        if (len > 0) {
                            /* NULL terminate the string */
                            cmd_buff[len] = '\0';
                            /* strip the trainling '\n' if any */
                            if (cmd_buff[len-1] == '\n') {
                                cmd_buff[len-1] = '\0';
                                len--;
                            }

                            /* handle the command if it is non-empty */
                            if (cmd_buff[0] != '\0') {
                                handle_cmd(cmd_buff, len);
                            }
                        } else {
                            printf("Error reading command\n");
                        }
                        print_prompt();
                    } else if (i == listen_fd) {
                        /* accept incoming connection */
                        bzero(&accept_addr,sizeof(accept_addr));
                        accept_len = sizeof(accept_addr);
                        accept_fd = accept(listen_fd, (struct sockaddr *) 
                                &accept_addr, &accept_len);
                        if (accept_fd < 0) {
                            printf("\nError accepting new connection: %s\n", 
                                    strerror(errno));
                        } else {
                            /* A new connection is received */
                            
                            /* If we are in server mode, add the new address to
                             * the server_ip_list */                       
                            if (mode == server_mode) {
                                client_port = receive_client_connect(accept_fd);
                                if (client_port > 0) {
                                    printf("\nClient %s:%d registered\n", 
                                            inet_ntoa(accept_addr.sin_addr), client_port);

                                    /* Add to the list of registered clients */
                                    add_server_ip(accept_addr, client_port, accept_fd);

                                    /* Send updates to all clients */
                                    if (send_ip_list_to_client() < 0) {
                                        printf("\nError sending server IP List to clients\n");
                                    }
                                } else {
                                    /* error already printed in receive_client_connect() */
                                }
                            } else {
                                /* client_mode */
                                client_port = receive_peer_connect(accept_fd, accept_addr);
                                if (client_port > 0) {
                                    printf("\nPeer %s:%d connected\n", 
                                            inet_ntoa(accept_addr.sin_addr), client_port);
                                } else {
                                    /* error already printed in receive_peer_connect() */
                                }
                            }
                            print_prompt();
                        }
                    } else if(mode == client_mode && i == server_fd) {
                        /* Client received an update from the server */
                        if (recv_update_from_server() < 0 ) {
                            /* Error already printed recv_update_from_server()*/
                        } else {
                            /* Display the updated list */
                            display_available_peers();
                        }
                        print_prompt();
                    } else {
                        if (mode == client_mode) {
                            /* Received some data from peer */
                            if (receive_data_from_peer(i) < 0) {
                                /* error already printed in receive_data_from_peer() */
                                print_prompt();
                            }
                        } else {
                            /* We have received something from client */
                            if (receive_from_client(i) < 0) {
                                /* error already printed in receive_from_client() */
                                print_prompt();
                            }
                        }
                    }
                } /* end of FD_ISSET */
                /* check if the FD was ready to write (we are sending file) */
                if (FD_ISSET(i, &temp_wfds)) {
                    if (handle_write(i) < 0) {
                        /* error already printed in handle_write() */
                        print_prompt();
                    }
                }
            } /* end of for (i = 0; i <= max_fd; i++) */
        } /* end of rc > 0 */
    } /* end of while (1) */
}

