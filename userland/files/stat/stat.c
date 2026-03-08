#include <stdio.h>
#include <sys/stat.h>
#include <time.h>

static void print_mode(mode_t m) {
    char buf[11];
    buf[0]  = S_ISDIR(m)  ? 'd' : S_ISCHR(m) ? 'c' : '-';
    buf[1]  = (m & S_IRUSR) ? 'r' : '-';
    buf[2]  = (m & S_IWUSR) ? 'w' : '-';
    buf[3]  = (m & S_IXUSR) ? 'x' : '-';
    buf[4]  = (m & S_IRGRP) ? 'r' : '-';
    buf[5]  = (m & S_IWGRP) ? 'w' : '-';
    buf[6]  = (m & S_IXGRP) ? 'x' : '-';
    buf[7]  = (m & S_IROTH) ? 'r' : '-';
    buf[8]  = (m & S_IWOTH) ? 'w' : '-';
    buf[9]  = (m & S_IXOTH) ? 'x' : '-';
    buf[10] = '\0';
    fputs(buf, stdout);
}

static void print_time(time_t t) {
    if (t == 0) { fputs("(none)", stdout); return; }
    struct tm *tm = gmtime(&t);
    if (!tm) { printf("%ld", (long)t); return; }
    printf("%04d-%02d-%02d %02d:%02d:%02d UTC",
           tm->tm_year + 1900, tm->tm_mon + 1, tm->tm_mday,
           tm->tm_hour, tm->tm_min, tm->tm_sec);
}

int main(int argc, char *argv[]) {
    if (argc < 2) { fputs("Usage: stat <file>\n", stdout); return 1; }
    for (int i = 1; i < argc; i++) {
        struct stat st;
        if (stat(argv[i], &st) != 0) {
            printf("stat: cannot stat: %s\n", argv[i]);
            continue;
        }
        printf("  File: %s\n",        argv[i]);
        printf("  Size: %ld bytes\n", (long)st.st_size);
        printf("Blocks: %ld (%ld byte blocks)\n",
               (long)st.st_blocks, (long)st.st_blksize);
        fputs("  Type: ", stdout);
        if      (S_ISREG(st.st_mode))  fputs("regular file\n", stdout);
        else if (S_ISDIR(st.st_mode))  fputs("directory\n",    stdout);
        else if (S_ISCHR(st.st_mode))  fputs("char device\n",  stdout);
        else                           fputs("other\n",         stdout);
        fputs("  Mode: ", stdout); print_mode(st.st_mode); fputs("\n", stdout);
        printf(" Inode: %lu\n",  (unsigned long)st.st_ino);
        printf(" Links: %u\n",   (unsigned)st.st_nlink);
        fputs(" Atime: ", stdout); print_time(st.st_atim.tv_sec); fputs("\n", stdout);
        fputs(" Mtime: ", stdout); print_time(st.st_mtim.tv_sec); fputs("\n", stdout);
        fputs(" Ctime: ", stdout); print_time(st.st_ctim.tv_sec); fputs("\n", stdout);
        if (i < argc - 1) fputs("\n", stdout);
    }
    return 0;
}
