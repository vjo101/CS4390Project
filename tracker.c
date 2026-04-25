#include <stdio.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <errno.h>
#include <sys/dir.h>
#include <arpa/inet.h>
#include <string.h>
#include <stdlib.h>
// this is for constant INADDR_ANY?
#include <netinet/ip.h>
#include <netinet/in.h>
// for fork() and stuff
#include <unistd.h>
//for listening to torrent folder
#include <dirent.h>
// for mkdir and folder stuff
#include <sys/stat.h>
//timestamps
#include <time.h>
#include <stdint.h>
#include <openssl/evp.h>
#include "util.h"

// buffer length
#define BACKLOG_LENGTH 256
// max read length
#define MAXLINE 1024
//where .track files live
#define DEFAULT_TRACKER_DIR "torrents"
//time until peer is removed, 15 min
#define DEAD_PEER_INTERVAL 900
#define DEFAULT_PORT 3490

//global directory path
char TRACKER_DIR[256] = DEFAULT_TRACKER_DIR;

//prototypes
void peer_handler(int sock_child, struct sockaddr_in client_addr);
void handle_list_req(int sock);
void handle_get_req(int sock, char *msg);
void handle_createtracker_req(int sock, char* ip, int port, char *msg);
void handle_updatetracker_req(int sock, char* ip, int port, char *msg);
int  read_config(int *port);



int main() {
    // default value of server port
    int server_port = DEFAULT_PORT;
    pid_t pid;
    struct sockaddr_in server_addr, client_addr;
    //socket for spefic clients
    int sock_child;
    //main listening socket
    int sockid;

    // read config file from sconfig, use default if it fails
    if (read_config(&server_port) != 0) {
        printf("could not read sconfig file, using defaults: port=%d, dir=%s\n",
               DEFAULT_PORT, TRACKER_DIR);
        server_port = DEFAULT_PORT;
    }

    // make sure torrents dir exists
    if (mkdir(TRACKER_DIR, 0755) == -1 && errno != EEXIST) {
        printf("could not create tracker directory '%s'\n", TRACKER_DIR);
        exit(1);
    }

    //create TCP socket
    //AF_INET = use IPv4 addresses
    if ((sockid = socket(AF_INET,SOCK_STREAM,0)) < 0){ //create socket connection oriented
        printf("socket cannot be created \n");
        exit(1);
    }

    int opt = 1;
    setsockopt(sockid, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));

    //socket created at this stage
    //now associate the socket with local port to allow listening incoming connections
    // sin_family does not need to be network byte order (htons() does this). Only sin_port and sin_addr.s_addr need it
    server_addr.sin_family = AF_INET;
    server_addr.sin_port = htons(server_port);
    server_addr.sin_addr.s_addr = htons(INADDR_ANY);

    // bind and check error
    if (bind(sockid ,(struct sockaddr *) &server_addr,sizeof(server_addr)) ==-1){
        printf("bind  failure\n");
        exit(1);
    }

    //(parent) process listens at sockid and check error
    if (listen(sockid, BACKLOG_LENGTH) < 0){
        printf(" Tracker  SERVER CANNOT LISTEN\n");
        exit(1);
    }
    printf("Tracker SERVER READY TO LISTEN INCOMING REQUEST.... \n");

    //accept  connection from every requester client
    while(1) {
        socklen_t clilen = sizeof(client_addr);
        //wait until peer connects, then creates new socket (sock_child)
        if ((sock_child = accept(sockid ,(struct sockaddr *) &client_addr, &clilen))==-1){
        // accept connection and create a socket descriptor for actual work
            printf("Tracker Cannot accept...\n");
            //dont kill server on failure
            continue;
        }

        printf("new peer connected.\n");
        fflush(stdout);

        //New child process will serve the requester client. separate child will serve separate client
        if ((pid=fork())==0){
            close(sockid);   //child does not need listener
            peer_handler(sock_child, client_addr);//child is serving the client.
            close (sock_child);
            // printf("\n 1. closed");
            exit(0);         // kill the process. child process all done with work
        }
        // parent all done with client, only child will communicate with that client from now
        close(sock_child);

    }  //accept loop ends
} // main fun ends


 // function for file transfer. child process will call this function
void peer_handler(int sock_child, struct sockaddr_in client_addr){
    //start handiling client request
    int length;
    char read_msg[MAXLINE];

    // client ip and port
    char *client_ip = inet_ntoa(client_addr.sin_addr);
    int client_port = ntohs(client_addr.sin_port);

    //read incoming message from peer
    //returns bytes read or -1 for error

    while((length = read(sock_child, read_msg, MAXLINE)) > 0){
        //null termiate string to use strcmp/strstr safely
        read_msg[length]='\0';

        printf("recieved message: %s\n", read_msg);

        if (strstr(read_msg, "REQ LIST") != NULL || strstr(read_msg, "req list") != NULL) {//list command received
            handle_list_req(sock_child);
            printf("list request handled.\n");
        }
        else if((strstr(read_msg,"get")!=NULL)||(strstr(read_msg,"GET")!=NULL)){// get command received
            handle_get_req(sock_child, read_msg);
            printf("get request handled.\n");
        }
        else if((strstr(read_msg,"createtracker")!=NULL)||(strstr(read_msg,"Createtracker")!=NULL)||(strstr(read_msg,"CREATETRACKER")!=NULL)){// get command received
            handle_createtracker_req(sock_child, client_ip, client_port, read_msg);
            printf("createtracker request handled.\n");

        }
        else if((strstr(read_msg,"updatetracker")!=NULL)||(strstr(read_msg,"Updatetracker")!=NULL)||(strstr(read_msg,"UPDATETRACKER")!=NULL)){// get command received
            handle_updatetracker_req(sock_child, client_ip, client_port, read_msg);
            printf("updatetracker request handled.\n");
        }
        else{
            //unknwon commands, let peer know
            printf("unknown command received: %s\n", read_msg);
            send_msg(sock_child, "ERROR: unknown command\n");
        }

    }
} //end client handler function

//sends the list of all tracker files
void handle_list_req(int sock) {
    DIR *dir;
    struct dirent *entry;
    char filepath[600];
    char line[256];
    char fname[256];
    char fsize[64];
    char fmd5[64];
    char response[MAXLINE];
    //file entries accumulate here
    char file_list[MAXLINE * 10];
    int count = 0;

    printf("handling LIST requests\n");

    //open shared dir where tracker files are
    dir = opendir(TRACKER_DIR);
    if (dir ==  NULL) {
        printf("cant open tracker directory: '%s'\n", TRACKER_DIR);
        send_msg(sock, "<REP LIST 0>\n<REP LIST END>\n");
        return;
    }

    //build file list entries
    memset(file_list, 0, sizeof(file_list));

    //give one directory entry at a time
    while ((entry = readdir(dir)) != NULL){

        //skip anything not ending in .track
        if (strstr(entry->d_name, ".track") == NULL){
            continue;
        }

        //build full path to current tracker file
        snprintf(filepath, sizeof(filepath), "%s/%s", TRACKER_DIR, entry->d_name);

        FILE *fp = fopen(filepath, "r");
        if (fp == NULL) {
            printf("WARNING: could not open %s, skipping\n", filepath);
            continue;
        }

        //reset extracted fields
        memset(fname, 0, sizeof(fname));
        memset(fsize, 0, sizeof(fsize));
        memset(fmd5,  0, sizeof(fmd5));

        //read tracker file line by line looking for filename, filezise, and md5
        while (fgets(line, sizeof(line), fp) != NULL) {
            if (strncmp(line, "Filename:", 9) == 0) {
                //sscanf with %s reads one whitespace delimited token after Filename:
                sscanf(line, "Filename: %255s", fname);
            } else if (strncmp(line, "Filesize:", 9) == 0) {
                sscanf(line, "Filesize: %63s", fsize);
            } else if (strncmp(line, "MD5:", 4) == 0) {
                sscanf(line, "MD5: %63s", fmd5);
            }

        }

        fclose(fp);

        count++;
        //build entry line for this file
        snprintf(response, sizeof(response), "<%d %s %s %s>\n", count, fname, fsize, fmd5);

        //append entry to running list
        strncat(file_list, response, sizeof(file_list) - strlen(file_list) - 1);
    }
    closedir(dir);

    //send full response in 3 parts
    //1. header with count
    //2. all file entries
    //3. end marker
    snprintf(response, sizeof(response), "<REP LIST %d>\n", count);
    send_msg(sock, response);
    send_msg(sock, file_list);
    send_msg(sock, "<REP LIST END>\n");

    printf("LIST response sent: %d tracker file(s) found\n", count);



}

//sends content of specific tracker file
void handle_get_req(int sock, char *msg) {

    char requested_file[256];
    char filepath[600];
    char file_content[MAXLINE * 20]; //tracker files are small
    char response[MAXLINE];
    char md5_hex[33];
    FILE *fp;
    size_t bytes_read;
    //buffer to hold the raw 16-byte binary hash from OpenSSL
    unsigned char hash[MD5_DIGEST_LENGTH];

    printf("handling GET request\n");

    //extract filename from the message
    if (sscanf(msg, "%*s %255s", requested_file) != 1){
        printf("ERROR:: could not parse filename from GET message: %s\n", msg);
        send_msg(sock, "<GET invalide>\n");
        return;
    }

    //gets rid of > for file name
    char *bracket = strchr(requested_file, '>');
    if (bracket) *bracket = '\0';

    //build path to the file
    snprintf(filepath, sizeof(filepath), "%s/%s", TRACKER_DIR, requested_file);
    printf("peer requested tracker file: %s\n", filepath);

    //open and read tracker file
    fp = fopen(filepath, "r");
    if (fp == NULL) {
        printf("Error: tracker file not found %s\n", filepath);
        send_msg(sock, "<GET invalid>\n");
        return;
    }

    //read entire file into memory
    bytes_read = fread(file_content, 1, sizeof(file_content) - 1, fp);
    fclose(fp);
    file_content[bytes_read]= '\0'; //null terminate
    printf("file content:\n%s", file_content);

    //compute md5 of tracker file content
    //generate raw 16-byte digest
    compute_md5_of_string((unsigned char*)file_content, bytes_read, md5_hex);

    //send the response
    send_msg(sock, "<REP GET BEGIN>\n");
    send_msg(sock, file_content);

    //use new hex string in footer
    snprintf(response, sizeof(response), "<REP GET END %s>\n", md5_hex);
    send_msg(sock, response);

    printf("GET response sent md5: %s\n", md5_hex);

}

//creates a new tracker file
void handle_createtracker_req(int sock, char* ip, int port, char *msg) {
    char filename[256], description[256], md5[64];
    long long filesize;
    char filepath[600];
    FILE *fp;


    printf("handling CREATETRACKER request\n");

    //parse msg, skipping <createtracker
    if (sscanf(msg, "%*s %255s %lld %255s %63s",
               filename, &filesize, description, md5) != 4) {
        printf("ERROR: failed to parse createtracker message: %s\n", msg);
        send_msg(sock, "<createtracker fail>\n");
        return;
    }

    //check if tracker file already exists
    //tracker file name = original filename + ".track"
    snprintf(filepath, sizeof(filepath), "%s/%s.track", TRACKER_DIR, filename);
    if (access(filepath, F_OK) == 0) {
        printf("createtracker FERR: tracker file '%s' already exists\n", filepath);
        send_msg(sock, "<createtracker ferr>\n");
        return;
    }

    //create tracker file
    fp = fopen(filepath, "w");
    if (fp == NULL) {
        printf("Error: could not create tracker file '%s'\n", filepath);
        send_msg(sock, "<createtracker fail>\n");
        return;
    }

    //write the header files
    fprintf(fp, "Filename: %s\n", filename);
    fprintf(fp, "Filesize: %lld\n", filesize);
    fprintf(fp, "Description: %s\n", description);
    fprintf(fp, "MD5: %s\n", md5);
    fprintf(fp, "#list of peers follows next\n");

    //write first peer entry
    //WARN: Changed to filesize-1 since bytes are 0 indexed
    fprintf(fp, "%s:%d:0:%lld:%ld\n", ip, port, filesize-1, time(NULL));
    fclose(fp);

    printf("createtracker SUCC: created '%s' (peer: %s:%d)\n", filepath, ip, port);

    send_msg(sock, "<createtracker succ>\n");

}

//update a peers entry in a tracker file
void handle_updatetracker_req(int sock, char* ip, int port, char *msg) {
    char filename[256];
    long long start_bytes, end_bytes;
    char filepath[600];
    char response[512];

    printf("Handling UPDATETRACKER request\n");

    //parse the message
    if (sscanf(msg, "%*s %255s %lld %lld",
               filename, &start_bytes, &end_bytes) != 3) {
        printf("ERROR: failed to parse updatetracker message: %s\n", msg);
        snprintf(response, sizeof(response), "<updatetracker %s fail>\n", filename);
        send_msg(sock, response);
        return;
    }

    //check tracker file exists
    snprintf(filepath, sizeof(filepath), "%s/%s.track", TRACKER_DIR, filename);
    if (access(filepath, F_OK) != 0){
        printf("update tracker FERR: '%s' not found\n", filepath);
        snprintf(response, sizeof(response), "<updatetracker %s ferr>\n", filename);
        send_msg(sock, response);
        return;
    }

    //read the existing tracker file into memeory
    FILE *fp = fopen(filepath, "r");
    if (fp ==NULL) {
        printf("ERROR: could not open '%s' for reading\n", filepath);
        snprintf(response, sizeof(response), "<updatetracker %s fail>\n", filename);
        send_msg(sock, response);
        return;
    }

    //store header portion
    //filename, filesize, description, md5, comments
    char header[MAXLINE * 4];
    memset(header, 0, sizeof(header));

    //store peer lines here
    //each peer line: ip:port:start:end:timestamp
    #define MAX_PEERS 64

    PeerEntry peers[MAX_PEERS];
    int peer_count = 0;

    char line[256];
    int in_header = 1; //flag to chekc if still reading header
    time_t now = time(NULL);

    while (fgets(line, sizeof(line), fp) != NULL) {
        //header lines start with captial letter keyword or '#'
        if (in_header && (line[0] == '#' || line[0] == 'F' || line[0] == 'D' || line[0] == 'M')){
        strncat(header, line, sizeof(header) - strlen(header) - 1);
        continue;
        }

        //once line looks like peer, starting with digit or dot, then in peer section
        in_header = 0;

        //parse this line as peer entry
        //ip:port:start:end:timestamp
        PeerEntry pe;
        memset(&pe, 0, sizeof(pe));
        if (sscanf(line, "%63[^:]:%d:%lld:%lld:%ld", pe.ip, &pe.port, &pe.start, &pe.end, &pe.timestamp) == 5){

            //check for dead peers
            if ((now - pe.timestamp) > DEAD_PEER_INTERVAL) {
                printf("removing dead peer %s:%d (last seen %ld seconds ago)\n",
                       pe.ip, pe.port, (long)(now - pe.timestamp));
                continue;
            }

            //add to peers array if theres room
            if (peer_count < MAX_PEERS) {
                peers[peer_count++] = pe;
            }
        }
        //lines not parsed as peers are skipped

    }
    fclose(fp);

    //add or update this peers entry
    //walk through peers arry, updating if theres a matching ip:port, otherwise append new entry
    int found = 0;
    for (int i = 0; i < peer_count; i++) {
        if (strcmp(peers[i].ip, ip) == 0 && peers[i].port == port) {
            peers[i].start = start_bytes;
            peers[i].end = end_bytes;
            peers[i].timestamp = now;
            found = 1;
            printf("updated existing peer %s:%d in '%s'\n", ip, port, filename);
            break;
        }
    }

    if (!found) {
        //add new peer
        if (peer_count < MAX_PEERS) {
            strncat(peers[peer_count].ip, ip, sizeof(peers[peer_count].ip) - 1);
            peers[peer_count].port = port;
            peers[peer_count].start = start_bytes;
            peers[peer_count].end = end_bytes;
            peers[peer_count].timestamp = now;
            peer_count++;
            printf("added new peer %s:%d to '%s'\n", ip, port, filename);
        } else {
            printf("WARNING: Max peers reached for '%s'\n", filename);
        }
    }

    //rewrite the tracker file
    fp = fopen(filepath, "w");
    if (fp == NULL) {
        printf("ERROR: could not open '%s' for writing\n", filepath);
        snprintf(response, sizeof(response), "<updatetracker %s fail>\n", filename);
        send_msg(sock, response);
        return;
    }

    //write header lines back first
    fputs(header, fp);

    //write non dead peer entires
    for (int i = 0; i < peer_count; i++) {
        fprintf(fp, "%s:%d:%lld:%lld:%ld\n",
            peers[i].ip,
            peers[i].port,
            peers[i].start,
            peers[i].end,
            peers[i].timestamp
        );
    }
    fclose(fp);

    //send success reponse
    snprintf(response, sizeof(response), "<updatetracker %s succ>\n", filename);
    send_msg(sock, response);

    printf("updatetracker SUCC for '%s'\n", filename);


}


//reads sconfig file
int read_config(int *port) {
    FILE *fp = fopen("sconfig", "r");
    if (fp == NULL) {
        printf(" WARNING: could not open 'sconfig'\n");
        return -1;
    }

    char line[256];

    //line 1 is port number
    if (fgets(line, sizeof(line), fp) == NULL) { fclose(fp); return -1; }
    *port = atoi(line); // atoi converts "3490\n" → 3490

    //line 2 is tracker shared directory written into global TRACKER_DIR
    if (fgets(line, sizeof(line), fp) == NULL) { fclose(fp); return -1; }
    //remove trailing newline
    line[strcspn(line, "\n")] = '\0';
    strncpy(TRACKER_DIR, line, sizeof(TRACKER_DIR) - 1);
    TRACKER_DIR[sizeof(TRACKER_DIR) - 1] = '\0';

    fclose(fp);
    printf("config loaded: port=%d, dir=%s\n", *port, TRACKER_DIR);
    return 0;
}
