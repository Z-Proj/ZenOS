#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <unistd.h>
#include <fcntl.h>
#include <dirent.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <sys/mman.h>
#include "../userlib.h"

static int g_total = 0;
static int g_pass = 0;

static void report(const char *name, int ok)
{
    g_total++;
    if (ok) {
        g_pass++;
        fputs("[PASS] ", stdout);
    } else {
        fputs("[FAIL] ", stdout);
    }
    fputs(name, stdout);
    fputs("\n", stdout);
}

static void test_libc_memory(void)
{
    char *p = (char *)malloc(128);
    int ok = (p != NULL);
    if (ok) {
        memset(p, 0xAB, 128);
        for (int i = 0; i < 128; i++) {
            if ((unsigned char)p[i] != 0xAB) {
                ok = 0;
                break;
            }
        }
    }
    report("memory: malloc/memset", ok);

    ok = (p != NULL);
    if (ok) {
        char *q = (char *)realloc(p, 256);
        ok = (q != NULL);
        if (ok) {
            p = q;
            for (int i = 0; i < 128; i++) {
                if ((unsigned char)p[i] != 0xAB) {
                    ok = 0;
                    break;
                }
            }
        } else {
            p = NULL;
        }
    }
    report("memory: realloc preserve", ok);
    free(p);

    char *z = (char *)calloc(32, 4);
    ok = (z != NULL);
    if (ok) {
        for (int i = 0; i < 128; i++) {
            if (z[i] != 0) {
                ok = 0;
                break;
            }
        }
    }
    report("memory: calloc zero", ok);
    free(z);

    unsigned char *m = (unsigned char *)mmap(NULL, 4096, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    ok = (m != MAP_FAILED);
    if (ok) {
        for (int i = 0; i < 4096; i++) {
            m[i] = (unsigned char)(i & 0xFF);
        }
        for (int i = 0; i < 4096; i++) {
            if (m[i] != (unsigned char)(i & 0xFF)) {
                ok = 0;
                break;
            }
        }
    }
    report("memory: mmap read/write", ok);
    if (m != MAP_FAILED) {
        report("memory: munmap", munmap(m, 4096) == 0);
    } else {
        report("memory: munmap", 0);
    }
}

static void test_pipe_wait(void)
{
    int pfd[2];
    pid_t pid;
    int status = 0;
    char buf[64];
    ssize_t n;

    int pipe_ok = (pipe(pfd) == 0);
    report("pipe: create", pipe_ok);
    if (!pipe_ok) {
        return;
    }

    pid = fork();
    report("pipe: fork", pid >= 0);
    if (pid < 0) {
        close(pfd[0]);
        close(pfd[1]);
        return;
    }

    if (pid == 0) {
        close(pfd[0]);
        write(pfd[1], "pipe-ok", 7);
        close(pfd[1]);
        _exit(17);
    }

    close(pfd[1]);
    memset(buf, 0, sizeof(buf));
    n = read(pfd[0], buf, sizeof(buf));
    close(pfd[0]);

    report("pipe: read payload", n == 7 && memcmp(buf, "pipe-ok", 7) == 0);
    report("pipe: waitpid", waitpid(pid, &status, 0) == pid);
    report("pipe: child exit status", WIFEXITED(status) && WEXITSTATUS(status) == 17);
}

static void test_dup2_stdio(void)
{
    const char *path = "/coretest.redir";
    int fd;
    int status = 0;
    pid_t pid;
    char buf[64];
    ssize_t n;
    int pfd[2];

    unlink(path);
    report("dup2: create target", zen_create(path) == 0);
    pid = fork();
    report("dup2: fork for stdout redir", pid >= 0);
    if (pid == 0) {
        fd = open(path, O_WRONLY);
        if (fd < 0) _exit(110);
        if (zen_dup2(fd, 1) < 0) _exit(111);
        write(1, "redir-ok", 8);
        _exit(0);
    }
    if (pid > 0) {
        report("dup2: wait stdout child", waitpid(pid, &status, 0) == pid);
        report("dup2: stdout child exit", WIFEXITED(status) && WEXITSTATUS(status) == 0);
    }

    fd = open(path, O_RDONLY);
    report("dup2: reopen target", fd >= 0);
    if (fd >= 0) {
        memset(buf, 0, sizeof(buf));
        n = read(fd, buf, sizeof(buf));
        report("dup2: redirected stdout content", n >= 8 && memcmp(buf, "redir-ok", 8) == 0);
        close(fd);
    } else {
        report("dup2: redirected stdout content", 0);
    }
    unlink(path);

    int pipe_ok = (zen_pipe(pfd) == 0);
    report("dup2: create pipe", pipe_ok);
    if (!pipe_ok) {
        return;
    }

    pid = fork();
    report("dup2: fork for stdin redir", pid >= 0);
    if (pid < 0) {
        close(pfd[0]);
        close(pfd[1]);
        return;
    }

    if (pid == 0) {
        char inbuf[16];
        close(pfd[1]);
        if (zen_dup2(pfd[0], 0) < 0) _exit(112);
        close(pfd[0]);
        memset(inbuf, 0, sizeof(inbuf));
        n = read(0, inbuf, 8);
        if (n == 8 && memcmp(inbuf, "stdin-ok", 8) == 0) _exit(0);
        _exit(1);
    }

    close(pfd[0]);
    write(pfd[1], "stdin-ok", 8);
    close(pfd[1]);
    report("dup2: wait stdin child", waitpid(pid, &status, 0) == pid);
    report("dup2: stdin child exit", WIFEXITED(status) && WEXITSTATUS(status) == 0);
}

static void test_dirent(void)
{
    DIR *d = opendir("/");
    int saw_any = 0;
    int saw_bin = 0;
    int saw_sys = 0;

    report("dirent: opendir /", d != NULL);
    if (!d) {
        return;
    }

    struct dirent *ent;
    while ((ent = readdir(d)) != NULL) {
        if (ent->d_name[0] != '\0') {
            saw_any = 1;
        }
        if (strcmp(ent->d_name, "bin") == 0) {
            saw_bin = 1;
        }
        if (strcmp(ent->d_name, "sys") == 0) {
            saw_sys = 1;
        }
    }

    report("dirent: readdir any", saw_any);
    report("dirent: found /bin", saw_bin);
    report("dirent: found /sys", saw_sys);
    report("dirent: closedir", closedir(d) == 0);
}

static void test_sockets(void)
{
    const char *name = "coretest.sock";
    socket_file_t *sock = NULL;
    char buf[64];
    uint32_t got = 0;

    socket_delete(name);
    report("socket: create", socket_create(name) == 0);
    report("socket: open", socket_open(name, &sock) == 0 && sock != NULL);
    if (!sock) {
        socket_delete(name);
        return;
    }

    report("socket: write", socket_write(sock, "sock-ok", 7) == 7);
    report("socket: available", socket_available(sock) >= 7);

    memset(buf, 0, sizeof(buf));
    report("socket: read", socket_read(sock, buf, sizeof(buf) - 1, &got) >= 0 && got == 7);
    report("socket: verify payload", memcmp(buf, "sock-ok", 7) == 0);

    report("socket: close", socket_close(sock) == 0);
    report("socket: delete", socket_delete(name) == 0);
}

static void test_file_io(void)
{
    const char *path = "/coretest.tmp";
    const char *msg = "file-io-ok";
    char buf[64];
    int fd;
    struct stat st;

    unlink(path);
    report("file: create", zen_create(path) == 0);

    fd = open(path, O_RDWR);
    report("file: open", fd >= 0);
    if (fd < 0) {
        return;
    }

    report("file: write", write(fd, msg, strlen(msg)) == (ssize_t)strlen(msg));
    report("file: lseek", lseek(fd, 0, SEEK_SET) == 0);

    memset(buf, 0, sizeof(buf));
    report("file: read", read(fd, buf, strlen(msg)) == (ssize_t)strlen(msg));
    report("file: verify", strcmp(buf, msg) == 0);
    report("file: fstat", fstat(fd, &st) == 0 && st.st_size >= (off_t)strlen(msg));
    report("file: close", close(fd) == 0);
    report("file: unlink", unlink(path) == 0);
}

int main(void)
{
    setvbuf(stdout, NULL, _IONBF, 0);
    setvbuf(stderr, NULL, _IONBF, 0);

    fputs("\n[coretest] running syscall/libc smoke tests\n", stdout);

    test_libc_memory();
    test_pipe_wait();
    test_dup2_stdio();
    test_sockets();
    test_dirent();
    test_file_io();

    fputs("\n[coretest] summary: ", stdout);
    {
        char line[64];
        int n = snprintf(line, sizeof(line), "%d/%d passed\n", g_pass, g_total);
        if (n > 0) {
            fputs(line, stdout);
        }
    }

    if (g_pass == g_total) {
        int fd = open("/coretest.pass", O_WRONLY | O_CREAT);
        if (fd >= 0) {
            write(fd, "PASS\n", 5);
            close(fd);
        }
        unlink("/coretest.fail");
    } else {
        int fd = open("/coretest.fail", O_WRONLY | O_CREAT);
        if (fd >= 0) {
            write(fd, "FAIL\n", 5);
            close(fd);
        }
        unlink("/coretest.pass");
    }

    return (g_pass == g_total) ? 0 : 1;
}
