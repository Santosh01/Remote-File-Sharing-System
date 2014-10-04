#include <stdio.h>
#include "proj1.h"

/* 
 * Function to display the help for available commands
 */
int handle_cmd_help()
{
    printf("Available commands:\n");
    /* Clien and server are printed separately to maintain indentation
     * Client commands (like download/upload) requires arguments
     * so the description column are idented more */
    if (mode == client_mode) {
        printf("HELP:\t\t\t\t\t\tPrint this help information\n");
        printf("MYIP:\t\t\t\t\t\tDisplay the IP Address of this process\n");
        printf("MYPORT:\t\t\t\t\t\tDisplay the port listening for incoming connection\n");
        printf("REGISTER <server IP> <port>:\t\t\tRegister to the server and get the list of clients\n");
        printf("CONNECT <peer address> <port>:\t\t\tConnect to a peer\n");
        printf("LIST:\t\t\t\t\t\tDisplay a list of currently connected peers\n");
        printf("TERMINATE <connection id>:\t\t\tTerminate the connection from a peer identified by connection id\n");
        printf("EXIT:\t\t\t\t\t\tTermiate all connections and exit the program\n");
        printf("UPLOAD <conn id> <file>:\t\t\tUpload file to a peer identified by connection id\n");
        printf("DOWNLOAD <conn id> <file> <conn id> <file> ...:\tDownload files from one or more peers\n");
        printf("CREATOR:\t\t\t\t\tDisplay author information\n");
    } else {
        printf("HELP:\t\tPrint this help information\n");
        printf("MYIP:\t\tDisplay the IP Address of this process\n");
        printf("MYPORT:\t\tDisplay the port listening for incoming connection\n");
        printf("LIST:\t\tDisplay a list of currently registered clients\n");
        printf("EXIT:\t\tExit the program\n");
        printf("CREATOR:\tDisplay author information\n");
    }

    return 0;
}

/*
 * Function to print the author information
 */
int handle_cmd_creator()
{
    printf("Author:\n");
    printf("\tName:\t\tSANTOSH KUMAR DUBEY\n");
    printf("\tUBIT name:\tsdubey2\n");
    printf("\tUB email id:\tsdubey2@buffalo.edu\n");
    return 0;
}

/*
 * Function to print the port this process is listening on
 */
int handle_cmd_myport()
{
    printf("MY PORT: %d\n", listen_port);
    return 0;
}

/*
 * Function to print the IP Address this process is listening on
 */
int handle_cmd_myip()
{
    printf("MY IP Address: %s\n", inet_ntoa(myip));
    return 0;
}

/*
 * Function to handle the EXIT command
 */
int handle_cmd_exit()
{
    return handle_exit();
}

/*
 * Function to handle the LIST command
 */
int handle_cmd_list()
{
    if(mode == client_mode)
        print_peer_list();
    else
        print_client_list();

    return 0;
}

/*
 * Function to handle the PEERS command
 */
int handle_cmd_peers()
{
    display_available_peers();
    return 0;
}

/*
 * Function to handle the TERMINATE command
 */
int handle_cmd_terminate(char *cmd_ptr, int cmd_len)
{
    int conn_id;
    char *ptr = cmd_ptr;
    char id_str[255];
    int i = 0;

    /* Strip leading spaces */
    while (*ptr == ' ' || *ptr == '\t') ptr++;

    /* Get the id */
    while(*ptr != '\0' && *ptr != ' ' && *ptr != '\t') {
        id_str[i++] = *ptr;
        ptr++;
    }

    if (*ptr != '\0') {
        printf("Invalid command: unknown parameter ,Please type HELP\n");
        return -1;
    }
    id_str[i] = '\0';

    conn_id = strtol(id_str, NULL, 10);
    if (conn_id <= 0) {
        printf("Invalid connection ID\n");
        return -1;
    }

    return terminate_connection(conn_id);
}

/*
 * Function to handle the REGISTER command
 */
int handle_cmd_register(char *cmd_ptr, int cmd_len)
{
    char address[255], port_str[255];
    int i = 0;
    int port;
    char *ptr = cmd_ptr;

    /* Check if we are already registered */
    if (registered) {
        printf("REGISTER: Client already registered\n");
        return -1;
    }

    /* Strip leading spaces */
    while (*ptr == ' ' || *ptr == '\t') ptr++;

    /* Get the address */
    while(*ptr != '\0' && *ptr != ' ' && *ptr != '\t') {
        address[i++] = *ptr;
        ptr++;
    }
    
    if (*ptr == '\0') {
        printf("Invalid command: port number missing\n");
        return -1;
    }
    address[i] = '\0';

    /* Get the port */
    /* Strip leading spaces */
    while (*ptr == ' ' || *ptr == '\t') ptr++;
    i = 0;
    while(*ptr != '\0' && *ptr != ' ' && *ptr != '\t') {
        port_str[i++] = *(ptr++);
    }
    
    if (*ptr != '\0') {
        /* there is more argument, flag as invalid */
        printf("Invalid command: extra arguments %s\n", ptr);
        return -1;
    }
    port_str[i] = '\0';
    port = strtol(port_str, NULL, 10);
    if (port <=0 || port > 65535) {
        printf("invalid port\n");
        return -1;
    }

    /* Now register to the server */
    return register_to_server(address, (unsigned short)port);

}

/*
 * Function to handle the CONNECT command
 */
int handle_cmd_connect (char cmd_buff[], int cmd_len)
{    
    char address[255], port_str[255];
    int i = 0;
    int port;
    char *ptr = cmd_buff;

    if (!registered) {
        printf("Please register to server before connecting to peers\n");
        return -1;
    }

    if (connected_peer_count == MAX_CONN) {
        printf("Cannot create more connection - maximum connection limit reached\n");
        return -1;
    }

    /* Strip leading spaces */
    while (*ptr == ' ' || *ptr == '\t') ptr++;

    /* Get the address */
    while(*ptr != '\0' && *ptr != ' ' && *ptr != '\t') {
        address[i++] = *ptr;
        ptr++;
    }
    
    if (*ptr == '\0') {
        printf("Invalid command: port number missing\n");
        return -1;
    }
    address[i] = '\0';

    /* Get the port */
    /* Strip leading spaces */
    while (*ptr == ' ' || *ptr == '\t') ptr++;
    i = 0;
    while(*ptr != '\0' && *ptr != ' ' && *ptr != '\t') {
        port_str[i++] = *(ptr++);
    }
    
    if (*ptr != '\0') {
        /* there is more argument, flag as invalid */
        printf("Invalid command: extra arguments %s\n", ptr);
        return -1;
    }
    port_str[i] = '\0';
    port = strtol(port_str, NULL, 10);
    if (port <=0 || port > 65535) {
        printf("invalid port\n");
        return -1;
    }

    /* Now connect to client */
    return connect_to_peer(address, port);
}

/*
 * Function to handle the UPLOAD command
 */
int handle_cmd_upload(char *cmd_ptr, int cmd_len)
{
    char file_name[255], id_str[255];
    int i = 0;
    int conn_id;
    char *ptr = cmd_ptr;

    if (!registered) {
        printf("Please register to server before connecting to peers\n");
        return -1;
    }

    if (connected_peer_count <= 1) {
        printf("Not connected to any peer\n");
        return -1;
    }

    /* Strip leading spaces */
    while (*ptr == ' ' || *ptr == '\t') ptr++;

    /* Get the id */
    while(*ptr != '\0' && *ptr != ' ' && *ptr != '\t') {
        id_str[i++] = *ptr;
        ptr++;
    }
    
    if (*ptr == '\0') {
        printf("Invalid command: file name missing\n");
        return -1;
    }
    id_str[i] = '\0';

    conn_id = strtol(id_str, NULL, 10);
    if (conn_id <= 0 ) {
        printf("Invalid connection ID\n");
        return -1;
    }

    /* Get the filename*/
    /* Strip leading spaces */
    while (*ptr == ' ' || *ptr == '\t') ptr++;
    i = 0;
    while(*ptr != '\0' && *ptr != ' ' && *ptr != '\t') {
        file_name[i++] = *(ptr++);
    }
    
    if (*ptr != '\0') {
        /* there is more argument, flag as invalid */
        printf("Invalid command: extra arguments %s\n", ptr);
        return -1;
    }
    file_name[i] = '\0';

    /* Send the file Peer */
    return upload_to_peer(conn_id, file_name);
}

/*
 * Function to handle the DOWNLOAD command
 */
int handle_cmd_download(char *cmd_ptr, int cmd_len)
{
    char file_name[3][255], id_str[255];
    int i = 0, count = 0;
    int conn_id[3];
    char *ptr = cmd_ptr;

    if (!registered) {
        printf("Please register to server before connecting to peers\n");
        return -1;
    }

    if (connected_peer_count <= 1) {
        printf("Not connected to any peer\n");
        return -1;
    }

    while (1) {
        bzero(id_str, sizeof(id_str));

        /* Strip leading spaces */
        while (*ptr == ' ' || *ptr == '\t') ptr++;

        /* Get the id */
        i = 0;
        while(*ptr != '\0' && *ptr != ' ' && *ptr != '\t') {
            id_str[i++] = *ptr;
            ptr++;
        }

        if (*ptr == '\0') {
            printf("Invalid command: file name missing\n");
            return -1;
        }
        id_str[i] = '\0';

        conn_id[count] = strtol(id_str, NULL, 10);
        if (conn_id[count] <= 0 ) {
            printf("Invalid connection ID\n");
            return -1;
        }

        /* Get the filename*/
        bzero(file_name[count], sizeof(file_name[count]));
        /* Strip leading spaces */
        while (*ptr == ' ' || *ptr == '\t') ptr++;
        i = 0;
        while(*ptr != '\0' && *ptr != ' ' && *ptr != '\t') {
            file_name[count][i++] = *(ptr++);
        }
        file_name[count][i] = '\0';

        count++;

        /* break if we have got three  hosts, or end of input */
        if (count == 3 || *ptr == '\0') {
            break;
        }
    }

    if (*ptr != '\0') {
        /* there is more argument, flag as invalid */
        printf("Invalid command: extra arguments %s\n", ptr);
        return -1;
    }

    /* request file from Peer */
    return download_from_peer(conn_id, file_name, count);
}

/*
 * Function to parse the incoming command and call appropriate handler
 */
int handle_cmd (char cmd_buff[], int cmd_len)
{
    int i;
    char *cmd_ptr;
    char cmd[10]; // length of the largest command

    cmd_ptr = cmd_buff;
    /* strip leading and trailing spaces, if any */
    while (*cmd_ptr == ' ' || *cmd_ptr == '\t') {
        cmd_ptr++;
        cmd_len--;
    }
    while(cmd_ptr[cmd_len-1] == ' '  || cmd_ptr[cmd_len-1] == '\t') {
        cmd_len --;
    }
    cmd_ptr[cmd_len] = '\0';
    

    /* get the command */
    i = 0;
    while (*cmd_ptr != ' ' && *cmd_ptr != '\t' && *cmd_ptr != '\0' && i <= sizeof(cmd) -1) {
        cmd[i] = *cmd_ptr;
        cmd_ptr++;
        cmd_len --;
        i++;
    }

    if (i > sizeof(cmd) -1) {
        printf ("Invalid command\n");
        return -1;
    }
    cmd[i] = '\0';

    /* HELP Command */
    if (strcasecmp(cmd, CMD_HELP) == 0) {
        /* This command does not take any argument */

        if (*cmd_ptr != '\0') {
            printf("Invalid argument to 'HELP' command\n");
            return -1;
        }
        handle_cmd_help();
        return 0;
    }

    /* MYIP Command */
    if (strcasecmp(cmd, CMD_MYIP) == 0) {
        /* This command does not take any argument */
        if (*cmd_ptr != '\0') {
            printf("Invalid argument to 'MYIP' command\n");
            return -1;
        }
        handle_cmd_myip();
        return 0;
    }

    /* MYPORT Command */
    if (strcasecmp(cmd, CMD_MYPORT) == 0) {
        /* This command does not take any argument */
        if (*cmd_ptr != '\0') {
            printf("Invalid argument to 'MYPORT' command\n");
            return -1;
        }
        handle_cmd_myport();
        return 0;
    }

    /* REGISTER Command */
    if (strcasecmp(cmd, CMD_REGISTER) == 0) {
        if (mode == server_mode) {
            printf("REGISTER command not available when running in server mode\n");
            return -1;
        }
        return handle_cmd_register(cmd_ptr, cmd_len);
    }

    /* CONNECT Command */
    if (strcasecmp(cmd, CMD_CONNECT) == 0) {
        if (mode == server_mode) {
            printf("CONNECT command not available when running in server mode\n");
            return -1;
        }
        return handle_cmd_connect(cmd_ptr, cmd_len);
    }

    /* LIST Command */
    if (strcasecmp(cmd, CMD_LIST) == 0) {
        /* This command does not take any argument */
        if (*cmd_ptr != '\0') {
            printf("Invalid argument to 'LIST' command\n");
            return -1;
        }
        return handle_cmd_list();
    }

    /* PEERS Command  (lists all available peers */
    if (strcasecmp(cmd, CMD_PEERS) == 0) {
        if (mode == server_mode) {
            printf("PEERS command not available when running in server mode\n");
            return -1;
        }
        /* This command does not take any argument */
        if (*cmd_ptr != '\0') {
            printf("Invalid argument to 'PEERS' command\n");
            return -1;
        }
        return handle_cmd_peers();
    }

    /* TERMINATE Command */
    if (strcasecmp(cmd, CMD_TERMINATE) == 0) {
        if (mode == server_mode) {
            printf("TERMINATE command not available when running in server mode\n");
            return -1;
        }
        return handle_cmd_terminate(cmd_ptr, cmd_len);
    }

    /* EXIT Command */
    if (strcasecmp(cmd, CMD_EXIT) == 0) {
        /* This command does not take any argument */
        if (*cmd_ptr != '\0') {
            printf("Invalid argument to 'EXIT' command\n");
            return -1;
        }
        handle_exit();
        return 0;
    }

    /* UPLOAD Command */
    if (strcasecmp(cmd, CMD_UPLOAD) == 0) {
        if (mode == server_mode) {
            printf("UPLOAD command not available when running in server mode\n");
            return -1;
        }
        return handle_cmd_upload(cmd_ptr, cmd_len);
    }

    /* DOWNLOAD Command */
    if (strcasecmp(cmd, CMD_DOWNLOAD) == 0) {
        if (mode == server_mode) {
            printf("DOWNLOAD command not available when running in server mode\n");
            return -1;
        }
        return handle_cmd_download(cmd_ptr, cmd_len);
    }

    /* CREATOR Command */
    if (strcasecmp(cmd, CMD_CREATOR) == 0) {
        /* This command does not take any argument */
        if (*cmd_ptr != '\0') {
            printf("Invalid argument to 'CREATOR' command\n");
            return -1;
        }
        return handle_cmd_creator();
    }

    printf("Invalid Command '%s'\n", cmd);
    return 0;
}


