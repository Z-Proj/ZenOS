#include <fcntl.h>
#include <stdio.h>
#include <unistd.h>

static int copy_file(const char *src, const char *dst) {
    int in_fd = open(src, O_RDONLY);
    if (in_fd < 0)
        return 1;

    int out_fd = open(dst, O_WRONLY | O_CREAT | O_TRUNC, 0644);
    if (out_fd < 0) {
        close(in_fd);
        return 1;
    }

    char buf[4096];
    int failed = 0;
    for (;;) {
        ssize_t n = read(in_fd, buf, sizeof(buf));
        if (n < 0) {
            failed = 1;
            break;
        }
        if (n == 0)
            break;
        ssize_t off = 0;
        while (off < n) {
            ssize_t wr = write(out_fd, buf + off, (size_t)(n - off));
            if (wr <= 0) {
                failed = 1;
                break;
            }
            off += wr;
        }
        if (failed)
            break;
    }

    close(out_fd);
    close(in_fd);
    return failed;
}

int main(int argc, char *argv[]) {
    if (argc != 3) {
        fputs("Usage: cp <source> <destination>\n", stdout);
        return 1;
    }

    if (copy_file(argv[1], argv[2]) != 0) {
        fputs("cp: failed\n", stdout);
        return 1;
    }

    return 0;
}
