#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <fcntl.h>
#include <unistd.h>
#include <dirent.h>
#include <sys/wait.h>
#include <sys/time.h>
#include "../userlib.h"

static int pass_count;
static int fail_count;

static void report(const char *name, int ok)
{
    if (ok)
    {
        pass_count++;
        printf("PASS %s\n", name);
    }
    else
    {
        fail_count++;
        printf("FAIL %s\n", name);
    }
}

static int test_memory(void)
{
    uint8_t *p = (uint8_t *)malloc(128);
    if (!p)
        return 0;
    for (int i = 0; i < 128; i++)
        p[i] = (uint8_t)(i ^ 0x5a);
    uint8_t *q = (uint8_t *)realloc(p, 4096);
    if (!q)
    {
        free(p);
        return 0;
    }
    for (int i = 0; i < 128; i++)
    {
        if (q[i] != (uint8_t)(i ^ 0x5a))
        {
            free(q);
            return 0;
        }
    }
    void *z = calloc(64, 4);
    if (!z)
    {
        free(q);
        return 0;
    }
    uint8_t *zb = (uint8_t *)z;
    for (int i = 0; i < 256; i++)
    {
        if (zb[i] != 0)
        {
            free(z);
            free(q);
            return 0;
        }
    }
    free(z);
    free(q);
    return 1;
}

static int test_libc_core(void)
{
    char a[64];
    char b[64];
    memset(a, 0, sizeof(a));
    memset(b, 0, sizeof(b));
    strcpy(a, "zen");
    strcat(a, "os");
    if (strcmp(a, "zenos") != 0)
        return 0;
    memcpy(b, a, strlen(a) + 1);
    if (memcmp(a, b, strlen(a) + 1) != 0)
        return 0;
    memmove(b + 1, b, strlen(b) + 1);
    if (strcmp(b, "zzenos") != 0)
        return 0;
    if (!strstr("hello zenos", "zenos"))
        return 0;
    return 1;
}

static int test_file_io_large(void)
{
    const char *path = "/home/coretest.bin";
    uint8_t wbuf[2048];
    uint8_t rbuf[2048];
    for (int i = 0; i < 2048; i++)
        wbuf[i] = (uint8_t)((i * 131u + 7u) & 0xffu);

    int fd = open(path, O_CREAT | O_TRUNC | O_RDWR, 0644);
    if (fd < 0)
        return 0;

    int total = 0;
    while (total < (int)sizeof(wbuf))
    {
        int n = (int)write(fd, wbuf + total, sizeof(wbuf) - (size_t)total);
        if (n <= 0)
        {
            close(fd);
            unlink(path);
            return 0;
        }
        total += n;
    }

    if (lseek(fd, 0, SEEK_SET) < 0)
    {
        close(fd);
        unlink(path);
        return 0;
    }

    total = 0;
    while (total < (int)sizeof(rbuf))
    {
        int n = (int)read(fd, rbuf + total, sizeof(rbuf) - (size_t)total);
        if (n <= 0)
        {
            close(fd);
            unlink(path);
            return 0;
        }
        total += n;
    }

    close(fd);
    unlink(path);

    return memcmp(wbuf, rbuf, sizeof(wbuf)) == 0;
}

static int test_dirent(void)
{
    DIR *d = opendir("/bin");
    if (!d)
        return 0;
    int seen = 0;
    struct dirent *de;
    while ((de = readdir(d)) != NULL)
    {
        if (de->d_name[0] == '\0')
            continue;
        seen++;
    }
    closedir(d);
    return seen > 0;
}

static int test_socket_local(void)
{
    const char *name = "coretest.sock";
    socket_delete(name);
    if (socket_create(name) < 0)
        return 0;

    socket_file_t *sf = NULL;
    if (socket_open(name, &sf) < 0 || !sf)
    {
        socket_delete(name);
        return 0;
    }

    const char msg[] = "socket-ok";
    if ((int)socket_write(sf, msg, (uint32_t)sizeof(msg)) != 0)
    {
        socket_close(sf);
        socket_delete(name);
        return 0;
    }

    char buf[32];
    memset(buf, 0, sizeof(buf));
    uint32_t br = 0;
    if ((int)socket_read(sf, buf, sizeof(buf), &br) != 0)
    {
        socket_close(sf);
        socket_delete(name);
        return 0;
    }

    int ok = (br == sizeof(msg)) && (memcmp(buf, msg, sizeof(msg)) == 0);
    socket_close(sf);
    socket_delete(name);
    return ok;
}

static int test_pipe_fork_wait(void)
{
    int pfd[2];
    if (pipe(pfd) < 0)
        return 0;

    pid_t pid = fork();
    if (pid < 0)
    {
        close(pfd[0]);
        close(pfd[1]);
        return 0;
    }

    if (pid == 0)
    {
        close(pfd[0]);
        const char msg[] = "pipe-ok";
        write(pfd[1], msg, sizeof(msg));
        close(pfd[1]);
        _exit(11);
    }

    close(pfd[1]);
    char buf[32];
    memset(buf, 0, sizeof(buf));
    int n = (int)read(pfd[0], buf, sizeof(buf));
    close(pfd[0]);

    int status = 0;
    pid_t w = waitpid(pid, &status, 0);

    if (w != pid)
        return 0;
    if (((status >> 8) & 0xff) != 11)
        return 0;
    if (n <= 0)
        return 0;
    return strcmp(buf, "pipe-ok") == 0;
}

static int test_waitpid_blocks_and_ps_filter(void)
{
    struct timeval t0;
    struct timeval t1;
    gettimeofday(&t0, NULL);

    pid_t pid = fork();
    if (pid < 0)
        return 0;

    if (pid == 0)
    {
        zen_sleep_ms(250);
        _exit(7);
    }

    int status = 0;
    pid_t w = waitpid(pid, &status, 0);
    gettimeofday(&t1, NULL);

    long elapsed_ms = (long)(t1.tv_sec - t0.tv_sec) * 1000L +
                      (long)(t1.tv_usec - t0.tv_usec) / 1000L;

    if (w != pid)
        return 0;
    if (((status >> 8) & 0xff) != 7)
        return 0;
    if (elapsed_ms < 150)
        return 0;

    task_info_t infos[64];
    int n = zen_list_tasks(infos, 64);
    if (n < 0)
        return 0;
    for (int i = 0; i < n; i++)
    {
        if ((pid_t)infos[i].pid == pid)
            return 0;
    }

    return 1;
}

int main(void)
{
    report("memory", test_memory());
    report("libc", test_libc_core());
    report("file_io_large", test_file_io_large());
    report("dirent", test_dirent());
    report("socket", test_socket_local());
    report("pipe_fork_wait", test_pipe_fork_wait());
    report("waitpid_block_ps", test_waitpid_blocks_and_ps_filter());

    printf("SUMMARY pass=%d fail=%d\n", pass_count, fail_count);
    return fail_count ? 1 : 0;
}
