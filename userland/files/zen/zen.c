/**
 * 
 * @file : zen.c
 * @brief : Zen - simple package manager for fetching and installing binaries over HTTP.
 * 
 * MIT License
 * 
 * Copyright (c) 2026 Rishies2010
 * 
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 * 
 * The above copyright notice and this permission notice shall be included in all
 * copies or substantial portions of the Software.
 * 
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 * SOFTWARE.
 * 
 * @author : Rishies2010
 * @copyright (c) 2026
 * 
 */

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

#define PKG_SERVER_HOST "zenos.nekoweb.org"
#define PKG_SERVER_PORT 80
#define PKG_BASE_PATH "/packages"
#define INSTALL_DIR "/mnt/drv0/bin"

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

#define RX_SIZE (4 * 1024 * 1024)
static char rx_buf[RX_SIZE];
static char req[512];
static char chunk[8192];

static int http_get(const char *path, char **body_out, int *body_len_out)
{
    int total = 0;

    for (int attempt = 0; attempt < 5; attempt++)
    {
        uint8_t ip[4];
        if (zen_gethostbyname4(PKG_SERVER_HOST, ip) < 0)
        {
            printf("zen: DNS failed for %s\n", PKG_SERVER_HOST);
            return -1;
        }

        int conn = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
        if (conn < 0)
        {
            puts("zen: socket failed\n");
            continue;
        }

        struct sockaddr_in addr;
        memset(&addr, 0, sizeof(addr));
        addr.sin_family = AF_INET;
        addr.sin_port = htons(PKG_SERVER_PORT);
        memcpy(&addr.sin_addr.s_addr, ip, 4);

        if (connect(conn, (const struct sockaddr *)&addr, sizeof(addr)) < 0)
        {
            puts("zen: cannot connect\n");
            close(conn);
            continue;
        }

        int rlen = snprintf(req, sizeof(req),
                            "GET %s HTTP/1.1\r\nHost: %s\r\nConnection: close\r\n\r\n",
                            path, PKG_SERVER_HOST);
        if (send(conn, req, (size_t)rlen, 0) < 0)
        {
            puts("zen: send failed\n");
            close(conn);
            continue;
        }

        total = 0;
        int n;
        while (total < RX_SIZE - 1)
        {
            n = recv(conn, chunk, sizeof(chunk), 0);
            if (n <= 0)
                break;
            if (total + n > RX_SIZE - 1)
                n = RX_SIZE - 1 - total;
            memcpy(rx_buf + total, chunk, n);
            total += n;
        }
        close(conn);
        rx_buf[total] = '\0';

        if (total > 0)
            break;
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

    write(STDOUT_FILENO, body, blen);

    return 0;
}

static int cmd_install(const char *name, const char *destdir)
{
    char path[256], dest[256];

    snprintf(path, sizeof(path), "%s/%s.txt", PKG_BASE_PATH, name);

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
