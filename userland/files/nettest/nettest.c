/*
 * nettest.c - ZenOS network syscall test
 * Runs on boot, tests TCP connect + HTTP GET, prints PASS/FAIL to stdout (serial)
 */
#include <stdint.h>
#include <stddef.h>
#include <string.h>
#include <stdio.h>
#include "../../userlib.h"

#define SYSCALL_NET_CONNECT 61
#define SYSCALL_NET_SEND    62
#define SYSCALL_NET_RECV    63
#define SYSCALL_NET_CLOSE   64

typedef struct { uint8_t ip[4]; uint16_t port; } net_connect_args_t;

static int net_connect(const uint8_t ip[4], uint16_t port) {
    net_connect_args_t a = {{ip[0],ip[1],ip[2],ip[3]}, port};
    return (int)(int64_t)_syscall1(SYSCALL_NET_CONNECT, (uint64_t)&a);
}
static int net_send(int id, const void *b, size_t l) {
    return (int)(int64_t)_syscall3(SYSCALL_NET_SEND,(uint64_t)id,(uint64_t)b,(uint64_t)l);
}
static int net_recv(int id, void *b, size_t l) {
    return (int)(int64_t)_syscall3(SYSCALL_NET_RECV,(uint64_t)id,(uint64_t)b,(uint64_t)l);
}
static void net_close(int id) { _syscall1(SYSCALL_NET_CLOSE,(uint64_t)id); }

static char rxbuf[4096];

int main(void) {
    puts("NETTEST: starting\n");

    /* QEMU host is always 10.0.2.2 in user networking */
    static const uint8_t host_ip[4] = {10,0,2,2};

    puts("NETTEST: connecting to 10.0.2.2:8080...\n");
    int conn = net_connect(host_ip, 8080);
    if (conn < 0) {
        puts("NETTEST: FAIL connect\n");
        return 1;
    }
    puts("NETTEST: connected\n");

    const char *req = "GET /test.txt HTTP/1.0\r\nHost: 10.0.2.2\r\nConnection: close\r\n\r\n";
    int sent = net_send(conn, req, strlen(req));
    if (sent < 0) {
        puts("NETTEST: FAIL send\n");
        net_close(conn);
        return 1;
    }
    printf("NETTEST: sent %d bytes\n", sent);

    int total = 0, n;
    while (total < (int)sizeof(rxbuf)-1) {
        n = net_recv(conn, rxbuf+total, sizeof(rxbuf)-1-total);
        if (n <= 0) break;
        total += n;
    }
    net_close(conn);
    rxbuf[total] = '\0';

    printf("NETTEST: received %d bytes\n", total);

    if (total == 0) {
        puts("NETTEST: FAIL no data\n");
        return 1;
    }

    /* check HTTP 200 */
    if (strncmp(rxbuf, "HTTP/1.", 7) != 0) {
        puts("NETTEST: FAIL not HTTP response\n");
        return 1;
    }

    /* find body after \r\n\r\n */
    char *body = strstr(rxbuf, "\r\n\r\n");
    if (!body) {
        puts("NETTEST: FAIL no HTTP body\n");
        return 1;
    }
    body += 4;

    printf("NETTEST: body = [%s]\n", body);

    if (strstr(body, "TEST_DOWNLOAD_OK")) {
        puts("NETTEST: PASS all good!\n");
    } else {
        puts("NETTEST: FAIL body mismatch\n");
        return 1;
    }

    return 0;
}
