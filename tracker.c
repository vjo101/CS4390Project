#include <stdio.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <errno.h>
#include <sys/dir.h>
#include <string.h>
#include <stdlib.h>
// this is for constance INADDR_ANY?
#include <netinet/ip.h>
// for fork() and stuff
#include <unistd.h>

// buffer length
#define BACKLOG_LENGTH 256
// max read length
#define MAXLINE 1024

void peer_handler(int);


int main() {
    // get this from config file eventually
    int server_port = 3490;
    pid_t pid;
    struct sockaddr_in server_addr, client_addr;   
    // WARN: is this needed? These seem to be variables that were undeclared in the skeleton file
    int sock_child;
    int sockid;

    if ((sockid = socket(AF_INET,SOCK_STREAM,0)) < 0){ //create socket connection oriented
        printf("socket cannot be created \n");
        exit(0); 
    }

    //socket created at this stage
    //now associate the socket with local port to allow listening incoming connections
    // sin_family does not need to be network byte order (htons() does this). Only sin_port and sin_addr.s_addr need it
    server_addr.sin_family = AF_INET;
    server_addr.sin_port = htons(server_port);
    server_addr.sin_addr.s_addr = htons(INADDR_ANY);
    
    // bind and check error
    if (bind(sockid ,(struct sockaddr *) &server_addr,sizeof(server_addr)) ==-1){
        printf("bind  failure\n");
        exit(0); 
    }
    
    printf("Tracker SERVER READY TO LISTEN INCOMING REQUEST.... \n");
    //(parent) process listens at sockid and check error
    if (listen(sockid, BACKLOG_LENGTH) < 0){
        printf(" Tracker  SERVER CANNOT LISTEN\n");
        exit(0);
    }                                        
    //accept  connection from every requester client
    while(1) {
        socklen_t clilen = sizeof(client_addr);
        if ((sock_child = accept(sockid ,(struct sockaddr *) &client_addr, &clilen))==-1){
        // accept connection and create a socket descriptor for actual work
            printf("Tracker Cannot accept...\n"); exit(0); 
        }
        //New child process will serve the requester client. separate child will serve separate client
        if ((pid=fork())==0){
            close(sockid);   //child does not need listener
            peer_handler(sock_child);//child is serving the client.           
            close (sock_child);
            // printf("\n 1. closed");
            exit(0);         // kill the process. child process all done with work
        }
        // parent all done with client, only child will communicate with that client from now
        close(sock_child);  
       
    }  //accept loop ends            
} // main fun ends  
     


 // function for file transfer. child process will call this function     
void peer_handler(int sock_child){
    //start handiling client request    
    int length;
    char read_msg[MAXLINE];
    length = read(sock_child, read_msg, MAXLINE);            
    read_msg[length]='\0';

    if((!strcmp(read_msg, "REQ LIST"))||(!strcmp(read_msg, "req list"))||(!strcmp(read_msg, "<REQ LIST>"))||(!strcmp(read_msg, "<REQ LIST>\n"))){//list command received
        // TODO: req list
        // handle_list_req(sock_child);// handle list request
        printf("list request handled.\n");
    }
    else if((strstr(read_msg,"get")!=NULL)||(strstr(read_msg,"GET")!=NULL)){// get command received
        // TODO: get function
        // xtrct_fname(read_msg, " ");// extract filename from the command        
        // handle_get_req(sock_child, fname);        
        printf("get request handled.\n");
    }
    else if((strstr(read_msg,"createtracker")!=NULL)||(strstr(read_msg,"Createtracker")!=NULL)||(strstr(read_msg,"CREATETRACKER")!=NULL)){// get command received
        // TODO: createtracker function
        // tokenize_createmsg(read_msg);
        // handle_createtracker_req(sock_child);
        printf("createtracker request handled.\n");
        
    }
    else if((strstr(read_msg,"updatetracker")!=NULL)||(strstr(read_msg,"Updatetracker")!=NULL)||(strstr(read_msg,"UPDATETRACKER")!=NULL)){// get command received
        // TODO: update tracker function
        // tokenize_updatemsg(read_msg);
        // handle_updatetracker_req(sock_child);        
        printf("updatetracker request handled.\n");
    }
}//end client handler function
