#ifndef UTIL_H
#define UTIL_H

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <openssl/md5.h>
#include <unistd.h>


void compute_md5_of_string(const char *data, size_t len, char *out_hex);
void send_msg(int sock, const char *msg);

typedef struct {
    char ip[64];
    int port;
    long long start;
    long long end;
    long timestamp;
} PeerEntry;

typedef struct {
    char filename[256];
    char description[256];
    char md5[64];
    long long filesize;
} TrackerHeader;

#endif // UTIL_H
