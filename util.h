#ifndef UTIL_H
#define UTIL_H

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <openssl/md5.h>
#include <unistd.h>

void compute_md5_of_string(const char *data, size_t len, char *out_hex);
void send_msg(int sock, const char *msg);

#endif // UTIL_H
