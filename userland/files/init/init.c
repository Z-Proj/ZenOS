#include "../../userlib.h"

#define MAX_LINE 512
#define MAX_ARGS 64

static void _write(const char *s, uint64_t n) { _syscall3(12, 2, (uint64_t)s, n); }
static void _exit_proc(int code)              { _syscall1(1, (uint64_t)code); }

#define MSG(s) _write(s, sizeof(s) - 1)

static int _strcmp(const char *a, const char *b)
{
    while (*a && *a == *b) { a++; b++; }
    return (unsigned char)*a - (unsigned char)*b;
}

static long _strtol(const char *s, char **end)
{
    long n = 0;
    while (*s >= '0' && *s <= '9') n = n * 10 + (*s++ - '0');
    if (end) *end = (char *)s;
    return n;
}

static char _buf[4096];
static int  _bfd  = -1;
static int  _bpos = 0;
static int  _blen = 0;

static int _nextc(void)
{
    if (_bpos >= _blen) {
        _blen = (int)_sc_ret(_syscall3(11, (uint64_t)(unsigned)_bfd, (uint64_t)_buf, sizeof(_buf)));
        _bpos = 0;
        if (_blen <= 0) return -1;
    }
    return (unsigned char)_buf[_bpos++];
}

static int _readline(char *dst, int cap)
{
    int n = 0, c;
    while ((c = _nextc()) >= 0 && c != '\n') {
        if (n < cap - 1) dst[n++] = (char)c;
    }
    if (c < 0 && n == 0) return -1;
    dst[n] = '\0';
    return n;
}

static int _parse_args(char *line, char *argv[], int max)
{
    int argc = 0;
    while (*line && argc < max - 1) {
        while (*line == ' ' || *line == '\t') line++;
        if (!*line) break;
        if (*line == '"') {
            line++;
            argv[argc++] = line;
            while (*line && *line != '"') line++;
            if (*line) *line++ = '\0';
        } else {
            argv[argc++] = line;
            while (*line && *line != ' ' && *line != '\t') line++;
            if (*line) *line++ = '\0';
        }
    }
    argv[argc] = NULL;
    return argc;
}

static void _handle_sleep(const char *arg)
{
    if (!arg || !*arg) { MSG("\x1b[38;2;255;165;0mInit: !sleep missing arg\x1b[0m\n"); return; }
    char *end;
    long secs = _strtol(arg, &end);
    if (*end != '\0' || secs < 0) { MSG("\x1b[38;2;255;165;0mInit: !sleep bad arg\x1b[0m\n"); return; }
    zen_sleep_ms((uint32_t)(secs * 1000));
}

int main(int argc, char *argv[])
{
    if (argc != 1 || _strcmp(argv[0], "kernel") != 0) {
        MSG("\x1b[38;2;255;50;50mInit: kernel-only. Err: INVALID_SIGN\x1b[0m\n");
        _exit_proc(1);
    }

    _bfd = (int)_sc_ret(_syscall3(10, (uint64_t)"/mnt/drv0/sys/init.run", 0, 0));
    if (_bfd < 0) {
        MSG("\x1b[38;2;255;50;50mInit: fatal: /sys/init.run not found\x1b[0m\n");
        _exit_proc(3);
    }

    char line[MAX_LINE];
    int launched = 0;

    while (_readline(line, MAX_LINE) >= 0) {
        char *p = line;
        while (*p == ' ' || *p == '\t') p++;
        if (*p == '\0' || *p == '#') continue;

        if (*p == '!') {
            p++;
            char *cmd = p;
            while (*p && *p != ' ' && *p != '\t') p++;
            if (*p) *p++ = '\0';
            while (*p == ' ' || *p == '\t') p++;
            char *arg = *p ? p : (char *)0;
            if (_strcmp(cmd, "sleep") == 0)
                _handle_sleep(arg);
            else
                MSG("\x1b[38;2;255;165;0mInit: unknown directive\x1b[0m\n");
            continue;
        }

        char *args[MAX_ARGS];
        int narg = _parse_args(p, args, MAX_ARGS);
        if (narg <= 0) continue;

        int pid = zen_spawn(args[0], (char *const *)args);
        if (pid < 0) {
            MSG("\x1b[38;2;255;50;50mInit: spawn failed\x1b[0m\n");
            continue;
        }

        zen_set_focus(pid);
        launched++;
    }

    _syscall1(13, (uint64_t)(unsigned)_bfd);

    while (launched > 0) {
        int st = 0;
        int dead = (int)_sc_ret(_syscall3(48, (uint64_t)(unsigned)-1, (uint64_t)&st, 0));
        if (dead < 0) break;
        launched--;
    }

    for (;;) {
        int st = 0;
        if ((int)_sc_ret(_syscall3(48, (uint64_t)(unsigned)-1, (uint64_t)&st, 0)) < 0) break;
    }

    _exit_proc(0);
    return 0;
}
