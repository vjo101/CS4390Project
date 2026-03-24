#include <stdio.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <string.h>
#include <unistd.h>
#include <stdlib.h>
#include <arpa/inet.h>

#define MAXLINE 100
#define BACKLOG_LENGTH 256

void read_client_thread_config(int* tracker_port, char* tracker_address, int* n_seconds) {
    // read client thread config
    FILE* client_thread_config;
    client_thread_config = fopen("clientThreadConfig.cfg", "r");

    // error if cannot find file
    if(client_thread_config == NULL){
        fprintf(stderr, "Could not open clientThreadConfig.cfg\n");
        exit(1);
    }

    fscanf(client_thread_config, "%d %s %d", tracker_port, tracker_address, n_seconds);
    fclose(client_thread_config);
}

void read_server_thread_config(int* server_port){
    // read server thread config
    FILE* server_thread_config;
    server_thread_config = fopen("serverThreadConfig.cfg", "r");

    // error if cannot find file
    if(server_thread_config == NULL){
        printf("Could not open serverThreadConfig.cfg\n");
        exit(1);
    }

    fscanf(server_thread_config, "%d", server_port);
    fclose(server_thread_config);
}

void peer_handler(int sock) {
    // TODO: Fill this in
}

void start_server(int port) {
    struct sockaddr_in server_addr, peer_addr;

    int sock_child;
    int sockid;

    if ((sockid = socket(AF_INET,SOCK_STREAM,0)) < 0){ //create socket connection oriented
        printf("socket cannot be created \n");
        exit(1);
    }

    int opt = 1;
    setsockopt(sockid, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));

    server_addr.sin_family = AF_INET;
    server_addr.sin_port = htons(port);
    server_addr.sin_addr.s_addr = htons(INADDR_ANY);

    // bind and check error
    if (bind(sockid ,(struct sockaddr *) &server_addr,sizeof(server_addr)) ==-1){
        printf("bind failure\n");
        exit(1);
    }

    //(parent) process listens at sockid and check error
    if (listen(sockid, BACKLOG_LENGTH) < 0){
        printf("CANNOT LISTEN\n");
        exit(1);
    }

    printf("LISTENING FOR INCOMING REQUESTS.... \n");

    while(1) {
        socklen_t peerlen = sizeof(peer_addr);
        //wait until peer connects, then creates new socket (sock_child)
        if ((sock_child = accept(sockid ,(struct sockaddr *) &peer_addr, &peerlen))==-1){
        // accept connection and create a socket descriptor for actual work
            printf("Cannot accept...\n");
            //dont kill server on failure
            continue;
        }

        //New child process will serve the requester client. separate child will serve separate client
        if (fork()==0){
            //child does not need listener
            close(sockid);
            peer_handler(sock_child);
            close (sock_child);
            // kill the process. child process all done with work
            exit(0);
        }

        // parent all done with client, only child will communicate with that client from now
        close(sock_child);
    }
}

void handle_list_com(int tracker_sock){
    char* req = "req list";

    if((write(tracker_sock, req, strlen(req))) < 0){// inform the server of the list request
        printf("Send_request  failure\\n");
        exit(1);
    }

    char msg[256];

    ssize_t n;
    if((n = read(tracker_sock, msg, sizeof(msg))) < 0){// read what server has said
        printf("Read  failure\\n");
        exit(1);
    }

    fwrite(msg, 1, n, stdout);
    fflush(stdout);
}

void handle_create_tracker_com(){
    // TODO: create tracker
}

void handle_update_tracker_com(){
    // TODO: update tracerk
}

void handle_get_com(){

}

void handle_command(char* str, int tracker_sock) {
    char* command = strtok(str, " ");

    if(strcmp(command, "list") == 0){
        handle_list_com(tracker_sock);
    } else if(strcmp(command, "create_tracker") == 0){
        handle_create_tracker_com();
    } else if(strcmp(command, "update_tracker") == 0){
        handle_update_tracker_com();
    } else if(strcmp(command, "get") == 0) {
        handle_get_com();
    } else {
        printf("Unkown command: %s", command);
    }
}

int connect_tracker_server(char* tracker_address, int tracker_port){
    int sockid;
    struct sockaddr_in tracker_addr;

    //create socket
    if ((sockid = socket(AF_INET,SOCK_STREAM,0))==-1){
        printf("Socket cannot be created\\n");
        exit(0);
    }

    tracker_addr.sin_family = AF_INET; //host byte order
    tracker_addr.sin_port = htons(tracker_port); // convert to network byte order

    inet_pton(AF_INET, tracker_address, &tracker_addr.sin_addr);

    //connect and error check
    if (connect(sockid ,(struct sockaddr *) &tracker_addr,sizeof(struct sockaddr))==-1){

        printf("Cannot connect to tracker server\n");
        exit(1);
    }

    printf("Connected to tracker sever %s:%d\n", tracker_address, tracker_port);

    return sockid;
}

int main(int argc,char *argv[]) {
    int tracker_port;
    char tracker_address[16];
    int  n_seconds;
    int server_port;

    read_client_thread_config(&tracker_port, tracker_address, &n_seconds);
    read_server_thread_config(&server_port);

    int tracker_sock = connect_tracker_server(tracker_address, tracker_port);

    // fork peer so it can server any other peers that request stuff
    if(fork() == 0){
        start_server(server_port);
    }

    char line[MAXLINE];
    while(true){
        printf(">");
        fflush(stdout);
        if(fgets(line, MAXLINE, stdin) != NULL){
            char* newline = strchr(line, '\n');
            *newline = '\0';
            handle_command(line, tracker_sock);
        }
    }
}
