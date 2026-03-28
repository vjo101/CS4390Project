#include "util.h"

//turns data into a hex md5 string
//md5 fills 16 byte array with the hash
//convert each byte to 2 hex chars
void compute_md5_of_string(const char *data, size_t len, char *out_hex) {
    unsigned char raw[MD5_DIGEST_LENGTH];
    MD5_CTX ctx;

    //setup internal state
    MD5_Init(&ctx);
    //update/ feed in data
    MD5_Update(&ctx, data, len);
    //finish and write
    MD5_Final(raw, &ctx);

    //convert to hex
    for (int i = 0; i < MD5_DIGEST_LENGTH; i++) {
        sprintf(out_hex + (i * 2), "%02x", raw[i]);
    }
    out_hex[32] = '\0';
}

//helper: write string to socket
//handles partial writes for possible busy networks
void send_msg(int sock, const char*msg) {
    size_t len = strlen(msg);
    size_t sent = 0;

    //keep writing until all bytes sent
    while (sent < len) {
        ssize_t n = write(sock, msg + sent, len - sent);
        if (n < 0){
            printf("ERROR: write() to socket failed\n");
            return;
        }
        sent += n;
    }
}
