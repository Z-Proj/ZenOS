#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>
#include <dirent.h>
#include <sys/stat.h>
#include <sys/wait.h>
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

static void test_libc_core(void)
{
    char s[64];
    char b[16];
    char overlap[8] = {0, 1, 2, 3, 4, 5, 6, 7};

    strcpy(s, "zen");
    strcat(s, "os");
    report("libc: strcpy/strcat/strcmp", strcmp(s, "zenos") == 0);
    report("libc: strlen", strlen(s) == 5);

    memset(b, 0x5A, sizeof(b));
    report("libc: memset", (unsigned char)b[0] == 0x5A && (unsigned char)b[15] == 0x5A);

    memcpy(b, "abc", 4);
    report("libc: memcpy", strcmp(b, "abc") == 0);

    memmove(overlap + 1, overlap, 6);
    report("libc: memmove overlap", overlap[1] == 0 && overlap[6] == 5);

    report("libc: atoi", atoi("12345") == 12345);

    {
        char numbuf[32];
        int n = snprintf(numbuf, sizeof(numbuf), "%d", 42);
        report("libc: snprintf", n == 2 && strcmp(numbuf, "42") == 0);
    }

    {
        char *p = (char *)malloc(64);
        int ok = (p != NULL);
        if (ok) {
            memset(p, 'A', 64);
            p = (char *)realloc(p, 128);
            ok = (p != NULL);
            if (ok) {
                ok = (p[0] == 'A' && p[63] == 'A');
            }
        }
        report("libc: malloc/realloc/free", ok);
        free(p);
    }
}

static void test_syscalls_file(void)
{
    const char *path = "/syscalltest.tmp";
    const char *msg = "syscall-file-io";
    char buf[64];
    struct stat st;
    int fd;

    unlink(path);
    report("syscall: create", zen_create(path) == 0);

    fd = open(path, O_RDWR);
    report("syscall: open", fd >= 0);
    if (fd < 0) return;

    report("syscall: write", write(fd, msg, strlen(msg)) == (ssize_t)strlen(msg));
    report("syscall: lseek", lseek(fd, 0, 0) == 0);

    memset(buf, 0, sizeof(buf));
    report("syscall: read", read(fd, buf, strlen(msg)) == (ssize_t)strlen(msg));
    report("syscall: read verify", strcmp(buf, msg) == 0);

    report("syscall: fstat", fstat(fd, &st) == 0 && st.st_size >= (off_t)strlen(msg));
    report("syscall: close", close(fd) == 0);
    report("syscall: stat", stat(path, &st) == 0 && st.st_size >= (off_t)strlen(msg));
    report("syscall: unlink", unlink(path) == 0);

    errno = 0;
    fd = open(path, O_RDONLY);
    report("syscall: errno on missing file", fd < 0 && errno != 0);
}

static void test_syscalls_dir(void)
{
    const char *dir = "/syscalltest_dir";
    DIR *d;
    int saw_entry = 0;

    rmdir(dir);
    report("syscall: mkdir", mkdir(dir, 0755) == 0);

    d = opendir("/");
    report("syscall: opendir", d != NULL);
    if (d) {
        struct dirent *ent;
        while ((ent = readdir(d)) != NULL) {
            if (ent->d_name[0] != '\0') {
                saw_entry = 1;
                break;
            }
        }
        report("syscall: readdir", saw_entry);
        report("syscall: closedir", closedir(d) == 0);
    }

    report("syscall: rmdir", rmdir(dir) == 0);
}

static void test_syscalls_proc_wait_exec(void)
{
    int pfd[2];
    pid_t pid;
    int status = 0;
    char msg[32];
    ssize_t n;

    report("syscall: pipe", zen_pipe(pfd) == 0);

    pid = fork();
    report("syscall: fork", pid >= 0);

    if (pid == 0) {
        close(pfd[0]);
        write(pfd[1], "child-ok", 8);
        close(pfd[1]);
        zen_sleep_ms(150);
        _exit(23);
    }

    if (pid > 0) {
        close(pfd[1]);
        memset(msg, 0, sizeof(msg));
        n = read(pfd[0], msg, sizeof(msg));
        close(pfd[0]);

        report("syscall: pipe read", n == 8 && memcmp(msg, "child-ok", 8) == 0);
        report("syscall: waitpid", waitpid(pid, &status, 0) == pid);
        report("syscall: wait status", WIFEXITED(status) && WEXITSTATUS(status) == 23);
    }

    {
        pid_t pid2 = fork();
        report("syscall: fork for exec", pid2 >= 0);
        if (pid2 == 0) {
            char *argv[] = {"/bin/hello", NULL};
            execv("/bin/hello", argv);
            _exit(111);
        }
        if (pid2 > 0) {
            int st2 = 0;
            pid_t w = waitpid(pid2, &st2, 0);
            report("syscall: waitpid exec-child", w == pid2);
            report("syscall: exec child exit", WIFEXITED(st2) && WEXITSTATUS(st2) == 0);
        }
    }

    {
        task_info_t tasks[32];
        int count = zen_list_tasks(tasks, 32);
        int found_self = 0;
        pid_t self = getpid();
        int i;

        for (i = 0; i < count; i++) {
            if ((pid_t)tasks[i].pid == self) {
                found_self = 1;
                break;
            }
        }

        report("syscall: list tasks", count > 0);
        report("syscall: getpid in list", found_self);
    }
}

int main(void)
{
    fputs("\n[syscalltest] Running syscall + libc checks\n", stdout);

    test_libc_core();
    test_syscalls_file();
    test_syscalls_dir();
    test_syscalls_proc_wait_exec();

    fputs("\n[syscalltest] Summary: ", stdout);
    {
        char buf[64];
        int len = snprintf(buf, sizeof(buf), "%d/%d passed\n", g_pass, g_total);
        if (len > 0) fputs(buf, stdout);
    }

    return (g_pass == g_total) ? 0 : 1;
}
