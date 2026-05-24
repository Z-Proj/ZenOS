#include <stdint.h>
#include <stddef.h>
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include "../../userlib.h"


static int my_atoi(const char *s){int v=0;while(*s>='0'&&*s<='9'){v=v*10+(*s-'0');s++;}return v;}


#define RX_SIZE (1024 * 1024)
static char rx_buf[RX_SIZE];
static char req_buf[512];
static char chunk[8192];

int main(int argc, char **argv) {
    if (argc < 4) {
        puts("usage: wget <host> <port> <path> [outfile]\n");
        return 1;
    }

    const char *host    = argv[1];
    uint16_t    port    = (uint16_t)my_atoi(argv[2]);
    const char *path    = argv[3];
    const char *outfile = (argc >= 5) ? argv[4] : NULL;

   
    uint8_t ip[4];
    if (zen_gethostbyname4(host, ip) < 0) {
        printf("wget: DNS failed for %s\n", host);
        return 1;
    }
    printf("wget: connecting to %d.%d.%d.%d:%d%s\n",
           ip[0],ip[1],ip[2],ip[3], port, path);

   
    int total = 0;
    for (int attempt = 0; attempt < 5; attempt++) {
       
        if (attempt > 0) {
            if (zen_gethostbyname4(host, ip) < 0) { puts("wget: DNS failed\n"); return 1; }
        }
        int conn = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
        if (conn < 0) { puts("wget: socket failed\n"); continue; }

        struct sockaddr_in addr;
        memset(&addr, 0, sizeof(addr));
        addr.sin_family = AF_INET;
        addr.sin_port = htons(port);
        memcpy(&addr.sin_addr.s_addr, ip, 4);

        if (connect(conn, (const struct sockaddr *)&addr, sizeof(addr)) < 0) {
            puts("wget: connection failed\n");
            close(conn);
            continue;
        }

        int rlen = snprintf(req_buf, sizeof(req_buf),
            "GET %s HTTP/1.1\r\nHost: %s\r\nConnection: close\r\n\r\n",
            path, host);
        if (send(conn, req_buf, (size_t)rlen, 0) < 0) {
            puts("wget: send failed\n"); close(conn); continue;
        }

        total = 0; int n;
        while (total < RX_SIZE - 1) {
            n = recv(conn, chunk, sizeof(chunk), 0);
            if (n <= 0) break;
            if (total + n > RX_SIZE - 1) n = RX_SIZE - 1 - total;
            memcpy(rx_buf + total, chunk, n);
            total += n;
        }
        close(conn);
        rx_buf[total] = '\0';

        if (total > 0) break;
        printf("wget: no data from %d.%d.%d.%d, retrying...\n",
               ip[0], ip[1], ip[2], ip[3]);
    }

    if (total == 0) { puts("wget: no data after 5 attempts\n"); return 1; }

   
    const char *sp = rx_buf;
    while (*sp && *sp != ' ') sp++;
    int status = my_atoi(sp + 1);
    if (status != 200) { printf("wget: HTTP %d\n", status); return 1; }

   
    char *body = NULL;
    for (int i = 0; i < total - 3; i++) {
        if (rx_buf[i]=='\r' && rx_buf[i+1]=='\n' &&
            rx_buf[i+2]=='\r' && rx_buf[i+3]=='\n') {
            body = rx_buf + i + 4; break;
        }
    }
    if (!body) { puts("wget: malformed HTTP response\n"); return 1; }
    int body_len = total - (int)(body - rx_buf);

   
    if (outfile) {
        zen_create(outfile);
        int fd = open(outfile, O_WRONLY, 0644);
        if (fd < 0) { printf("wget: cannot open %s for writing\n", outfile); return 1; }
        int written = write(fd, body, body_len);
        close(fd);
        if (written != body_len)
            printf("wget: warning: wrote %d/%d bytes\n", written, body_len);
        printf("wget: saved %d bytes to %s\n", body_len, outfile);
    } else {
        for (int i = 0; i < body_len; i++) putchar(body[i]);
        putchar('\n');
    }
    return 0;
}
