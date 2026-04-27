#include <linux/prctl.h>
#include <openssl/md5.h>
#include <stdio.h>
#include <sys/types.h>
#include <errno.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <sys/stat.h>
#include <string.h>
#include <unistd.h>
#include <stdlib.h>
#include <arpa/inet.h>
#include <sys/file.h>
#include "util.h"
// for threads
#include <pthread.h>
#include <stdbool.h>
// for getting ip address
#include <ifaddrs.h>
// for ceil
#include <math.h>
#include <sys/prctl.h>
#include <signal.h>

#define MAXLINE 1024
#define BACKLOG_LENGTH 256
#define MAX_PEERS 64
#define MAX_THREADS 10

void* download_bytes(void* arg);


// shared folder path read from serverThreadConfig.cfg, used by peer_handler threads to serve file chunks
char shared_folder[256];
int n_seconds;

// should be long enough for address
//
char self_ip_addr[16];
int  server_port;

// args passed to each peer_handler thread (one per incoming peer connection)
typedef struct {
    int sock;
    struct sockaddr_in peer_addr;
} HandlerArgs;

// args passed to update_tracker when called after time
typedef struct {
    int tracker_sock;
    char* file_name;
    long start_bytes, end_bytes;
    char* ip_addr;
    int port_num;
} RepeatUpdateArgs;

typedef struct {
    char* file_name;
    long start_bytes;
    long end_bytes;
    char* ip_addr;
    int port_num;
} DownloadArgs;

void print_download_args(const DownloadArgs *args) {
    printf("DownloadArgs {\n");

    printf("  file_name   : %s\n",
           args->file_name ? args->file_name : "(null)");

    printf("  start_bytes  : %ld\n", args->start_bytes);
    printf("  end_bytes    : %ld\n", args->end_bytes);

    printf("  ip_addr      : %s\n",
           args->ip_addr ? args->ip_addr : "(null)");

    printf("  port_num     : %d\n", args->port_num);

    printf("}\n");
}

void read_client_thread_config(int* tracker_port, char* tracker_address) {
    // read client thread config
    FILE* client_thread_config;
    client_thread_config = fopen("clientThreadConfig.cfg", "r");

    // error if cannot find file
    if(client_thread_config == NULL){
        fprintf(stderr, "Could not open clientThreadConfig.cfg\n");
        exit(1);
    }

    fscanf(client_thread_config, "%d %s %d", tracker_port, tracker_address, &n_seconds);
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

    // line 1: listen port, line 2: shared folder name
    fscanf(server_thread_config, "%d %s", server_port, shared_folder);
    fclose(server_thread_config);
}

// gets the ip of the peer
void get_self_ip(char* addr) {
    struct ifaddrs *ifap, *ifa;
    struct sockaddr_in *sa;
    char* temp;

    getifaddrs(&ifap);
    for (ifa = ifap; ifa; ifa = ifa->ifa_next) {

        if (ifa->ifa_addr && ifa->ifa_addr->sa_family==AF_INET) {
            sa = (struct sockaddr_in *) ifa->ifa_addr;
            temp = inet_ntoa(sa->sin_addr);

            // check for local ip
            // WARN: change this if you want to find a certain type of ip address
            if (strstr(temp, "100.")) {
                strcpy(addr, temp);
                break;
            }
        }
    }

    freeifaddrs(ifap);
}

// handles one incoming peer connection: serves a single file chunk then closes
// called as a detached pthread, unpacks HandlerArgs, frees it, then does the work
void* peer_handler(void* arg) {
    HandlerArgs* args = (HandlerArgs*) arg;
    int sock = args->sock;
    struct sockaddr_in peer_addr = args->peer_addr;
    free(args);

    // read incoming GET request: <GET filename start_byte end_byte>\n
    int length;
    char read_msg[MAXLINE];
    length = read(sock, read_msg, MAXLINE);
    if (length <= 0) {
        close(sock);
        return NULL;
    }
    read_msg[length] = '\0';
    printf("received message: %s\n", read_msg);

    char filename[256];
    long long start_byte, end_byte;
    // %*s skips "<GET", %lld stops at '>' so end_byte parses correctly
    if (sscanf(read_msg, "%*s %255s %lld %lld", filename, &start_byte, &end_byte) != 3) {
        send_msg(sock, "<GET invalid>\n");
        close(sock);
        return NULL;
    }

    // chunk size must be <= 1024 and range must be valid
    long long chunk_size = end_byte - start_byte + 1;
    if (chunk_size > 1024 || chunk_size <= 0 || start_byte < 0) {
        send_msg(sock, "<GET invalid>\n");
        close(sock);
        return NULL;
    }

    // open the requested file from this peer's shared folder
    char filepath[600];
    // snprintf(filepath, sizeof(filepath), "%s/%s", shared_folder, filename);
    snprintf(filepath, sizeof(filepath), "%s/%s", shared_folder, filename);
    printf("peer requested file: %s\n", filepath);

    // return error if filepath doesn't exist or can't be opened
    FILE *fp = fopen(filepath, "rb");
    if (fp == NULL) {
        printf("Error: file not found %s\n", filepath);
        send_msg(sock, "<GET invalid>\n");
        close(sock);
        return NULL;
    }

    // flock LOCK_SH, fseek to start_byte, fread chunk, flock LOCK_UN, fclose
    // flock guards against download threads (parent process) writing the same file
    // if bytes_read == 0, send "<GET invalid>\n" and close

    // fileno() converts to fd
    // lock it so file isn't written to while reading
    char chunk[MAXLINE];

    fread(chunk, chunk_size, 1, fp);
    int n;
    fseek(fp, start_byte, SEEK_SET);
    n = fread(chunk, 1, chunk_size, fp);
    if (n == 0) {
        send_msg(sock, "<GET invalid>\n");
        close(sock);
        return NULL;
    }

    // write raw bytes to sock, no wrapper, size already bounded to <= 1024
    // print: "Serving <start>-<end> of <filename> to <peer_ip>\n"

    send_data(sock, chunk, chunk_size);

    printf("Serving %lld-%lld of %s to %s:%d\n", start_byte, end_byte, filename, inet_ntoa(peer_addr.sin_addr), peer_addr.sin_port);
    close(sock);
    return NULL;
}

void start_server() {
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
    server_addr.sin_port = htons(server_port);
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

        // spawn a thread to handle this connection; thread owns sock_child and closes it
        HandlerArgs* args = malloc(sizeof(HandlerArgs));
        args->sock = sock_child;
        args->peer_addr = peer_addr;

        pthread_t tid;
        if (pthread_create(&tid, NULL, peer_handler, args) != 0) {
            printf("Failed to create handler thread\n");
            close(sock_child);
            free(args);
            continue;
        }
        // detach so thread cleans up automatically when done
        pthread_detach(tid);
    }
}

void handle_list_com(int tracker_sock){

    send_msg(tracker_sock, "req list");

    char msg[MAXLINE];

    ssize_t n;
    if((n = read(tracker_sock, msg, sizeof(msg))) < 0){// read what server has said
        printf("Read failure\n");
        exit(1);
    }

    fwrite(msg, 1, n, stdout);
    fflush(stdout);
}

void handle_create_tracker_com(int tracker_sock, char* file_name, char* description, char* ip_addr, int port_num){
    char filepath[600];
    snprintf(filepath, sizeof(filepath), "%s/%s", shared_folder, file_name);
    FILE* source_file = fopen(filepath, "r");

    if(source_file == NULL){
        printf("Could not open file %s\n", filepath);
    }

    struct stat st;
    stat(filepath, &st);

    int file_size = st.st_size;

    char* file_data = (char*) malloc (sizeof(char) * file_size + 1);
    file_data[file_size] = '\0';

    int pos = 0;
    int n;

    while((n = fread(file_data + pos, 1, 1024, source_file)) > 0){
        pos += n;
    }

    char md5_buf[33];
    compute_md5_of_string(file_data, file_size, md5_buf);

    char msg_buffer[256];

    // just need to pass file_name here since you don't need the full file path
    sprintf(
        msg_buffer,
        "<createtracker %s %d %s %s %s %d>\n",
        file_name,
        file_size,
        description,
        md5_buf,
        ip_addr,
        port_num
    );

    send_msg(tracker_sock, msg_buffer);

    memset(msg_buffer, 0, sizeof(msg_buffer));

    n = read(tracker_sock, msg_buffer, sizeof(msg_buffer));
    fwrite(msg_buffer, 1, n, stdout);
    fflush(stdout);
}

void handle_update_tracker_com(int tracker_sock, char* file_name, long start_bytes, long end_bytes, char* ip_addr, int port_num) {
    //make copy of input string
    char* token;
    char* endptr;
    char msg[256];
    //strncpy(msg, str, sizeof(msg) - 1);
    //msg[sizeof(msg) - 1] = '\0';

    //check end more than start
    if (end_bytes < start_bytes) {
        printf("Ending byte smaller than start byte\n");
            exit(1);
    }

    sprintf(
        msg,
        "<updatetracker %s %ld %ld %s %d>\n",
        file_name,
        start_bytes,
        end_bytes,
        ip_addr,
        port_num
    );

    //send message to tracker
    send_msg(tracker_sock, msg);

    //receive message
    char confirm[256];
    ssize_t n;

    if ((n = read(tracker_sock, confirm, sizeof(confirm) - 1)) <= 0) {
        printf("Read failure\n");
        exit(1);
    }

    confirm[n] = '\0';

    if (strstr(confirm, "ferr")) {
        printf("Tracker file does not exist\n");
    } else if (strstr(confirm, "succ")) {
        printf("Tracker file updated successfully\n");
    } else {
        printf("Could not update tracker file\n");
    }

    fflush(stdout);
}

//handles periodically updating trackers
void* handle_repeat_update_tracker(void* args) {
    RepeatUpdateArgs* update_args = (RepeatUpdateArgs*)args;
    //could add command to terminate with a signal at end?
    while (true) {
        sleep(n_seconds);
        handle_update_tracker_com(update_args->tracker_sock, update_args->file_name, update_args->start_bytes, update_args->end_bytes, update_args->ip_addr, update_args->port_num);
    }

    return NULL;
}

// returns number of peers read
int read_tracker_file(char* filename, TrackerHeader* header, PeerEntry* peers) {
    printf("reading");
    FILE *fptr = fopen(filename, "r");

    //store peer lines here
    //each peer line: ip:port:start:end:timestamp
    int peer_count = 0;

    char line[256];
    int in_header = 1; //flag to chekc if still reading header

    while (fgets(line, sizeof(line), fptr) != NULL) {
        //header lines start with captial letter keyword or '#'
        if (in_header && (line[0] == '#' || line[0] == 'F' || line[0] == 'D' || line[0] == 'M')){
            if (strncmp(line, "Filename:", 9) == 0) {
                //sscanf with %s reads one whitespace delimited token after Filename:
                sscanf(line, "Filename: %255s", header->filename);
            } else if (strncmp(line, "Filesize:", 9) == 0) {
                char temp[64];
                sscanf(line, "Filesize: %63s", temp);
                header->filesize = atoll(temp);
            } else if (strncmp(line, "MD5:", 4) == 0) {
                sscanf(line, "MD5: %63s", header->md5);
            }
            continue;
        }

        //once line looks like peer, starting with digit or dot, then in peer section
        in_header = 0;

        //parse this line as peer entry
        //ip:port:start:end:timestamp
        PeerEntry pe;
        memset(&pe, 0, sizeof(pe));
        if (sscanf(line, "%63[^:]:%d:%lld:%lld:%ld", pe.ip, &pe.port, &pe.start, &pe.end, &pe.timestamp) == 5){
            //add to peers array if theres room
            //
            if (peer_count < MAX_PEERS) {
                peers[peer_count++] = pe;
            }
        }
        //lines not parsed as peers are skipped

    }
    fclose(fptr);
    return peer_count;
}

void handle_get_com(int tracker_sock, char* get_filename) {
    // WARN: did I do this right for snprintf? Or should i just use sprintf
    char req[MAXLINE];
    char tracker_filename[MAXLINE];
    // check if they added .track or not and add .track if not added.
    if (strstr(get_filename, ".track") == NULL) {
        snprintf(tracker_filename, MAXLINE, "%s.track", get_filename);
    }
    snprintf(req, MAXLINE, "GET %s", tracker_filename);

    send_msg(tracker_sock, req);

    char msg[MAXLINE];
    ssize_t n;
    int reading = 1;
    char sent_hash[33];
    char computed_hash[33];

    // fstream for writing
    FILE *fptr;

    while (reading) {

        if((n = read(tracker_sock, msg, sizeof(msg))) < 0){// read what server has said
            printf("Read failure\n");
            exit(1);
        }

        if(strstr(msg, "<GET invalid>") != NULL) {
            printf("Found no tracker for file %s\n", get_filename);
            return;
        }

        // TODO: add error handling
        // WARN: doesn't actually check if the first lin is "<REP GET BEGIN>"
        // if the beginning of transmission discard the beginning
        if(strstr(msg, "<REP GET BEGIN>\n") != NULL) {
            // WARN: hardcoded value for header length
            memmove(msg, msg + 16, n - 16);
            // add null terminator at end since it just copied message, it didn't remove the left overs.
            msg[n-16] = '\0';
            // update the length value
            n = n - 16;

            // we actaully have a tracker to write
            // want to do write mode to overwrite any old tracker file data
            fptr = fopen(tracker_filename, "w");
        }

        // check if the closing value is present. If so, extract md5 hash and remover tail.
        // WARN: what if the footer is split between two messages? can that happen?
        if(strstr(msg, "<REP GET END") != NULL) {
            // extract hash
            char* start_of_hash = msg + n - 34;
            memmove(sent_hash, start_of_hash, 32);
            sent_hash[32] = '\0';
            // remove tail
            // need to remove last 47 bytes to get rid of footer. Is there a better way to do this?
            memmove(msg, msg, n - 47);
            msg[n-47] = '\0';
            // update length after cutting it short
            n = n-47;
            // stop reading from pipe since got everything
            reading = 0;
        }

        // immediately save to file
        fprintf (fptr, "%s", msg);

        // check hash once done
        if (!reading) {
            compute_md5_of_string(msg, n, computed_hash);

            if (strcmp(computed_hash, sent_hash) != 0) {
                printf("Tracker file is corrupted. Please request it again\n");
                fclose(fptr);
                remove(tracker_filename);
            } else {
                printf("Tracker file successfully downloaded.\n");
                fclose(fptr);
            }
        }
    }

    PeerEntry peers[MAX_PEERS];

    TrackerHeader *header = malloc(sizeof(TrackerHeader));

    int peer_count = read_tracker_file(tracker_filename, header, peers);
    printf("read the tracker file...\n");

    // TODO: test if this actually sorts
    qsort(peers, peer_count, sizeof(PeerEntry), pe_compare);

    for (int i = 0; i < peer_count; i++) {
        printf("addr: %s:%d\ntimestamp: %ld\n", peers[i].ip, peers[i].port, peers[i].timestamp);
    }

    printf("ip: %s\n", peers[0].ip);
    printf("Filename = %s\n", header->filename);
    printf("Md5 = %s\n", header->md5);
    printf("Filesize = %lld\n", header->filesize);

    // Open torrent file for writing
    char filepath[600];
    snprintf(filepath, sizeof(filepath), "%s/%s", shared_folder, get_filename);

    FILE *torrented;
    torrented = fopen(filepath, "w");
    if(torrented == NULL){
        printf("Failed to create %s for torrenting.", header->filename);
        return;
    }

    // TODO: start requesting data from other peers
    // create array for MAX_THREADS with pthread_t
    pthread_t threads[MAX_THREADS];
    DownloadArgs threads_args[MAX_THREADS];
    int peer_assignment_idxs[MAX_THREADS];

    int broken_peer_idxs[MAX_PEERS];

    int num_broken_peers = 0;


    int total_segments = ceil((double) header->filesize / MAXLINE);
    int last_seg_bytes = header->filesize % MAXLINE;

    int req_seg = 0;
    int down_seg = 0;
    int num_launched_threads;

    bool finished = false;
    // open file
    while (!finished) {
        // send out 10 threads with their assignments
        num_launched_threads = 0;
        for (int i = 0; i < MAX_THREADS; i++) {

            if (req_seg == total_segments) {
                break;
            }

            DownloadArgs* download_args = &threads_args[i];

            download_args->file_name = header->filename;
            download_args->start_bytes = req_seg * MAXLINE;

            download_args->end_bytes = download_args->start_bytes + MAXLINE - 1;

            if(download_args->end_bytes > header->filesize) {
                download_args->end_bytes = header->filesize - 1;
            }

            // find ideal peer to download this offset
            // send out threads with offset, ip address, port, amount to download
            PeerEntry* pe = NULL;
            bool is_broken;

            for(int j = 0; j < peer_count; j++){

                is_broken = false;

                for(int k = 0; k < num_broken_peers; k++){
                    if(broken_peer_idxs[k] == j){
                        is_broken = true;
                    }
                }

                if(!is_broken && peers[j].start <= download_args->start_bytes && peers[j].end >= download_args->end_bytes){
                    pe = &peers[j];
                    peer_assignment_idxs[i] = j;
                    break;
                }

            }

            if(pe == NULL) {
                printf("Could not find available peers to torrent from.\n");

                // cancel and join amy started threads
                for(int j = 0; j < num_launched_threads; j++){
                    pthread_cancel(threads[j]);
                    pthread_join(threads[j], NULL);
                }
                fclose(torrented);
                return;
            }

            download_args->ip_addr = pe->ip;
            download_args->port_num = pe->port;

            pthread_t tid;
            pthread_create(&tid, NULL, download_bytes, (void*) download_args);

            threads[i] = tid;
            req_seg++;
            num_launched_threads++;
        }

        // wait for threads to come back
        for (int i = 0; i < MAX_THREADS; i++) {
            if (down_seg == total_segments) {
                finished = true;
                break;
            }

            // get the ptr back. If null, reassign.
            // if not null, save data to file
            // update tracker
            char* segment;
            pthread_join(threads[i], (void**) &segment);

            if(segment == NULL){
                // we mark peer as broken by adding to broken peer list if we return segment as null
                broken_peer_idxs[num_broken_peers] = peer_assignment_idxs[i];
                num_broken_peers++;

                // then retry starting from last successfully downloaded segment
                req_seg = down_seg;

                // cancel and join remaining threads in thread group
                for(int j = i + 1; j < num_launched_threads; j++){
                    pthread_cancel(threads[j]);
                    pthread_join(threads[j], NULL);
                }

                break;
            } else {
                if(down_seg + 1 == total_segments) {
                    fwrite(segment, 1, last_seg_bytes, torrented);
                } else {
                    fwrite(segment, 1, MAXLINE, torrented);
                }

                handle_update_tracker_com(tracker_sock, header->filename, 0, threads_args[i].end_bytes, self_ip_addr, server_port);
                free(segment);
            }

            down_seg++;
        }
    }
    fclose(torrented);
    // TODO: check md5. If incorrect, delete file and just recall this function
}

// TODO: function to send download request to
    // download threads must send: <GET filename start_byte end_byte>\n
    // where end_byte - start_byte + 1 <= 1024
    // response is raw bytes on success, or "<GET invalid>\n" on error

// Could probably allocate what bytes the threads are going to get before called here? Would be modification in thread creation at end of handle_get_com
void* download_bytes(void* arg) {

    // make cancelable
    pthread_setcanceltype(PTHREAD_CANCEL_DEFERRED, NULL);

    DownloadArgs* download_args = (DownloadArgs*) arg;

    printf("Fetching bytes %ld %ld of %s from %s:%d\n",
           download_args->start_bytes,
           download_args->end_bytes,
           download_args->file_name,
           download_args->ip_addr,
           download_args->port_num);

    int sock;
    struct sockaddr_in peer_addr;

    if ((sock = socket(AF_INET, SOCK_STREAM, 0)) < 0) {
        printf("Socket creation failed.\n");
        return NULL;
    }

    peer_addr.sin_family = AF_INET;
    peer_addr.sin_port = htons(download_args->port_num);
    inet_pton(AF_INET, download_args->ip_addr, &peer_addr.sin_addr);

    // establish connection to other peer
    if (connect(sock, (struct sockaddr *) &peer_addr, sizeof(peer_addr)) < 0) {
        printf("Socket connection failed.\n");
        return NULL;
    }

    long total_bytes = download_args->end_bytes - download_args->start_bytes + 1;
    long received_bytes = 0;

    // make sure will only get a valid amount of bytes
    if (total_bytes <= 0 || total_bytes > 1024) {
        printf("Incorrect range of bytes.\n");
        close(sock);
        return NULL;
    }

    // request proper segment from the peer
    char msg[256];
    snprintf(msg, sizeof(msg), "<GET %s %ld %ld>\n", download_args->file_name, download_args->start_bytes, download_args->end_bytes);
    write(sock, msg, strlen(msg));

    //done to check if the get was invalid (is this the best way to do it?)
    char check[64];
    int start = read(sock, check, sizeof(check) - 1);

    if (start <= 0) {
        printf("Nothing from peer.\n");
        close(sock);
        return NULL;
    }
    check[start] = '\0';

    // thread returns since it was an invalid command so nothing gets stored
    if (strstr(check, "GET invalid") != NULL) {
        printf("Invalid GET command to peer.\n");
        close(sock);
        return NULL;
    }

    // allocates for the segment downloaded
    char* segment = malloc(total_bytes);

    // adds in the first data read
    int check_end = start;
    if (check_end > 0) {
        memcpy(segment, check, check_end);
        received_bytes += check_end;
    }

    // read in segment from peer

    while (received_bytes < total_bytes) {

        int current;
        if ((current = read(sock, segment + received_bytes, total_bytes - received_bytes)) < 0) {
            printf("Reading failed.\n");
            close(sock);
            return NULL;
        }

        received_bytes += current;
    }

    if (received_bytes != total_bytes) {
        printf("Received incorrect amount of bytes.\n");
        free(segment);
        close(sock);
        return NULL;
    }

    // return pointer to downloaded stuff or NULL if failed
    close(sock);
    return segment;
};

void handle_command(char* str, int tracker_sock) {
    char* command = strtok(str, " ");

    if(command == NULL){
        return;
    }

    if(strcmp(command, "list") == 0){
        handle_list_com(tracker_sock);
    } else if(strcmp(command, "create_tracker") == 0){
        char* file_name = strtok(NULL, " ");
        if (file_name == NULL) {
            printf("No filename provided");
            return;
        }
        char* description = strtok(NULL, " ");
        if (description == NULL) {
            printf("No description provided");
            return;
        }
        char* ip_addr = strtok(NULL, " ");

        if (ip_addr == NULL) {
            printf("No IP Address provided.");
            return;
        }
        // easy way to get self ip addr when manually inputting
        printf("global ip = %s\n", self_ip_addr);
        if (strcmp(ip_addr, "0") == 0) {
            ip_addr = self_ip_addr;
        }
        int w, x, y, z, ip_len;
        if (!(sscanf(ip_addr, "%d.%d.%d.%d%n", &w, &x, &y, &z, &ip_len) == 4 &&
            ip_addr[ip_len] == '\0' &&
            w >= 0 && w <= 255 &&
            x >= 0 && x <= 255 &&
            y >= 0 && y <= 255 &&
            z >= 0 && z <= 255)) {
            printf("IP Address is invalid.");
            return;
        }
        char* temp = strtok(NULL, " ");
        if (temp == NULL) {
            printf("No port number provided.");
            return;
        }
        int port_num = atoi(temp);
        // eady way to get port number
        if (port_num == 0) {
            read_server_thread_config(&port_num);
        }
        if (port_num < 0 || port_num > 65535) {
            printf("Port number is invalid.");
            return;
        }

        handle_create_tracker_com(tracker_sock, file_name, description, ip_addr, port_num);

        // TODO: this is not gonna work
        // pthread_t timed_update;
        //
        // RepeatUpdateArgs update_args;
        // update_args.tracker_sock = tracker_sock;
        // update_args.file_name = file_name;
        //
        // update_args.start_bytes = start_bytes;
        // update_args.end_bytes = end_bytes;
        //
        // update_args.ip_addr = ip_addr;
        // update_args.port_num = port_num;
        //
        // pthread_create(&timed_update, NULL, handle_repeat_update_tracker, &update_args);
        // pthread_join(timed_update, NULL);

    } else if(strcmp(command, "update_tracker") == 0) {
        char* endptr;
        char* file_name = strtok(NULL, " ");
        if (file_name == NULL) {
            printf("No filename provided");
            return;
        }
        char* temp = strtok(NULL, " ");
        if (temp == NULL) {
            printf("No start byte provided.");
            return;
        }
        long start_bytes = strtol(temp, &endptr, 10);
        if (temp == endptr || *endptr != '\0') {
            printf("No valid start byte provided.");
            return;
        }
        temp = strtok(NULL, " ");
        if (temp == NULL) {
            printf("No end byte provided.");
            return;
        }
        long end_bytes = strtol(temp, &endptr, 10);
        if (temp == endptr || *endptr != '\0') {
            printf("No valid end byte provided.");
            return;
        }
        char* ip_addr = strtok(NULL, " ");
        if (ip_addr == NULL) {
            printf("No IP Address provided.");
            return;
        }
        // easy way to get self ip addr when manually inputting
        if (strcmp(ip_addr, "0") == 0) {
            ip_addr = self_ip_addr;
        }
        int w, x, y, z, ip_len;
        if (!(sscanf(ip_addr, "%d.%d.%d.%d%n", &w, &x, &y, &z, &ip_len) == 4 &&
            ip_addr[ip_len] == '\0' &&
            w >= 0 && w <= 255 &&
            x >= 0 && x <= 255 &&
            y >= 0 && y <= 255 &&
            z >= 0 && z <= 255)) {
            printf("IP Address is invalid.");
            return;
        }
        temp = strtok(NULL, " ");
        if (temp == NULL) {
            printf("No port number provided.");
            return;
        }
        int port_num = atoi(temp);
        // eady way to get port number
        if (port_num == 0) {
            read_server_thread_config(&port_num);
        }
        if (port_num < 0 || port_num > 65535) {
            printf("Port number is invalid.");
            return;
        }

        handle_update_tracker_com(tracker_sock, file_name, start_bytes, end_bytes, ip_addr, port_num);
    } else if(strcmp(command, "get") == 0) {
        char* get_filename = strtok(NULL, " ");
        handle_get_com(tracker_sock, get_filename);
    } else {
        printf("Unkown command: %s\n", command);
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

    read_client_thread_config(&tracker_port, tracker_address);
    read_server_thread_config(&server_port);
    // get the ip
    get_self_ip(self_ip_addr);

    printf("Peer server IP = %s\n", self_ip_addr);
    int tracker_sock = connect_tracker_server(tracker_address, tracker_port);

    // create or ensure exists a downloads directory
    if (mkdir(shared_folder, 0755) == -1 && errno != EEXIST) {
        printf("could not create tracker directory '%s'\n", shared_folder);
        exit(1);
    }

    // fork peer so it can server any other peers that request stuff
    if(fork() == 0){
        prctl(PR_SET_PDEATHSIG, SIGTERM);
        start_server();
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
