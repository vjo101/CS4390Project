#include <stdio.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <stdlib.h>

int main(int argc,char *argv[]) {

    printf("Hello World");

    struct sockaddr_in server_addr;
    int port, sockfd;
    FILE* fptr;

    //open and read config files to get port number
    fptr = fopen("clientThreadConfig.cfg", "r");
    if (fptr == NULL) {
        perror("Error: Opening clientThreadConfig.cfg failed");
        exit(1);
    }
    
    fscanf(fptr, "%d", &port);

    //connect to tracker server
    //socket()
    sockfd = socket(AF_INET, SOCK_STREAM, 0);
    if (sockfd < 0) {
        perror("Error: Socket creation failed");
        exit(1);
    }

    server_addr.sin_family = AF_INET;
    server_addr.sin_port = htons(port);

    //connect
    if ((connect(sockfd,(struct sockaddr*)&server_addr, sizeof(struct sockaddr))) < 0) {
        perror("Error: Connect failed");
        exit(1);
    }

    //implement the 4 commands
    //not possible to let user pick in program through say switch?
    //implement message format
/*
    if(!strcmp(argv[1],"list"))\{// if this is LIST command
        int list_req=htons(LIST);
        if((write(sockid,(char *)&list_req,sizeof(list_req))) < 0)\{//inform the server of the list request
            printf("Send_request  failure\\n"); exit(0);
        }

        if((read(sockid,(char *)&msg,sizeof(msg)))< 0)\{// read what server has said
            printf("Read  failure\\n"); exit(0); 
        }
        
        close(sockid);
        printf("Connection closed\\n");
        exit(0);
    }//end close
*/    
}         
