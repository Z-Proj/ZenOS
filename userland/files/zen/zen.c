#include <stdint.h>
#include <stddef.h>
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <fcntl.h>
#include <unistd.h>
#include "../../userlib.h"

#define PKG_SERVER_HOST "zen-pkg.byethost17.com"
#define PKG_SERVER_PORT 80
#define PKG_BASE_PATH "/packages"
#define INSTALL_DIR "/mnt/drv0/bin"

#define SYSCALL_NET_CONNECT 61
#define SYSCALL_NET_SEND 62
#define SYSCALL_NET_RECV 63
#define SYSCALL_NET_CLOSE 64
#define SYSCALL_DNS_RESOLVE 66

typedef struct
{
    uint8_t ip[4];
    uint16_t port;
} net_connect_args_t;

static int net_connect(const uint8_t ip[4], uint16_t port)
{
    net_connect_args_t a;
    a.ip[0] = ip[0];
    a.ip[1] = ip[1];
    a.ip[2] = ip[2];
    a.ip[3] = ip[3];
    a.port = port;
    return (int)(int64_t)_syscall1(SYSCALL_NET_CONNECT, (uint64_t)&a);
}
static int net_send(int id, const void *b, size_t l) { return (int)(int64_t)_syscall3(SYSCALL_NET_SEND, (uint64_t)id, (uint64_t)b, (uint64_t)l); }
static int net_recv(int id, void *b, size_t l) { return (int)(int64_t)_syscall3(SYSCALL_NET_RECV, (uint64_t)id, (uint64_t)b, (uint64_t)l); }
static void net_close(int id) { _syscall1(SYSCALL_NET_CLOSE, (uint64_t)id); }

static int dns_resolve(const char *host, uint8_t ip_out[4])
{
    return (int)(int64_t)_syscall2(SYSCALL_DNS_RESOLVE, (uint64_t)host, (uint64_t)ip_out);
}

static int my_atoi(const char *s)
{
    int v = 0;
    while (*s >= '0' && *s <= '9')
    {
        v = v * 10 + (*s - '0');
        s++;
    }
    return v;
}

#define RX_SIZE (512 * 1024)
static char rx_buf[RX_SIZE];
static char req[512];
static char chunk[8192];

static int http_get(const char *path, char **body_out, int *body_len_out)
{
    int total = 0;

    for (int attempt = 0; attempt < 5; attempt++)
    {
        uint8_t ip[4];
        if (dns_resolve(PKG_SERVER_HOST, ip) < 0)
        {
            printf("zen: DNS failed for %s\n", PKG_SERVER_HOST);
            return -1;
        }

        int conn = net_connect(ip, PKG_SERVER_PORT);
        if (conn < 0)
        {
            puts("zen: cannot connect\n");
            continue;
        }

        int rlen = snprintf(req, sizeof(req),
                            "GET %s HTTP/1.1\r\nHost: %s\r\nConnection: close\r\n\r\n",
                            path, PKG_SERVER_HOST);
        net_send(conn, req, (size_t)rlen);

        total = 0;
        int n;
        while (total < RX_SIZE - 1)
        {
            n = net_recv(conn, chunk, sizeof(chunk));
            if (n <= 0)
                break;
            if (total + n > RX_SIZE - 1)
                n = RX_SIZE - 1 - total;
            memcpy(rx_buf + total, chunk, n);
            total += n;
        }
        net_close(conn);
        rx_buf[total] = '\0';

        if (total > 0)
            break;
        printf("zen: no data from %d.%d.%d.%d, retrying...\n",
               ip[0], ip[1], ip[2], ip[3]);
    }

    if (total == 0)
    {
        puts("zen: no response after 5 attempts\n");
        return -1;
    }

    const char *sp = rx_buf;
    while (*sp && *sp != ' ')
        sp++;
    int status = my_atoi(sp + 1);
    if (status != 200)
    {
        printf("zen: server returned HTTP %d for %s\n", status, path);
        return -1;
    }

    char *body = NULL;
    for (int i = 0; i < total - 3; i++)
    {
        if (rx_buf[i] == '\r' && rx_buf[i + 1] == '\n' &&
            rx_buf[i + 2] == '\r' && rx_buf[i + 3] == '\n')
        {
            body = rx_buf + i + 4;
            break;
        }
    }
    if (!body)
    {
        puts("zen: bad HTTP response\n");
        return -1;
    }

    *body_out = body;
    *body_len_out = total - (int)(body - rx_buf);
    return 0;
}

static int cmd_list(void)
{
    char path[128];
    snprintf(path, sizeof(path), "%s/list.txt.txt", PKG_BASE_PATH);

    char *body;
    int blen;
    if (http_get(path, &body, &blen) < 0)
        return 1;

    puts("Available packages:\n");
    int i = 0;
    while (i < blen)
    {
        int j = i;
        while (j < blen && body[j] != '\n' && body[j] != '\r')
            j++;
        if (j > i)
        {
            body[j] = '\0';
            printf("  %s\n", body + i);
        }
        while (j < blen && (body[j] == '\n' || body[j] == '\r'))
            j++;
        i = j;
    }
    return 0;
}

static int cmd_install(const char *name, const char *destdir)
{
    char path[256], dest[256];

    /* fetch the .txt version from server */
    snprintf(path, sizeof(path), "%s/%s.txt", PKG_BASE_PATH, name);

    /* save to bin/ without .txt extension */
    if (destdir)
        snprintf(dest, sizeof(dest), "%s/%s", destdir, name);
    else
        snprintf(dest, sizeof(dest), "%s/%s", INSTALL_DIR, name);

    printf("zen: installing %s -> %s\n", name, dest);

    char *body;
    int blen;
    if (http_get(path, &body, &blen) < 0)
        return 1;

    zen_create(dest);
    int fd = open(dest, O_WRONLY, 0644);
    if (fd < 0)
    {
        printf("zen: cannot open %s for writing\n", dest);
        return 1;
    }
    int written = write(fd, body, blen);
    close(fd);

    if (written != blen)
    {
        printf("zen: write incomplete (%d/%d bytes)\n", written, blen);
        return 1;
    }
    printf("zen: installed %s (%d bytes)\n", name, blen);
    return 0;
}

int main(int argc, char **argv)
{
    if (argc < 2)
    {
        puts("Zen - ZenOS Package Manager\n");
        puts("  zen list\n");
        puts("  zen install <pkg> [destdir]\n");
        return 0;
    }
    if (strcmp(argv[1], "list") == 0)
        return cmd_list();
    else if (strcmp(argv[1], "install") == 0)
    {
        if (argc < 3)
        {
            puts("zen: install needs a package name\n");
            return 1;
        }
        return cmd_install(argv[2], argc >= 4 ? argv[3] : NULL);
    }
    printf("zen: unknown command '%s'\n", argv[1]);
    return 1;
}