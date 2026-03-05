#include <stdio.h>
#include <sys/stat.h>

int main(int argc, char *argv[]) {
    if (argc < 2) { fputs("Usage: stat <file>\n", stdout); return 1; }
    for (int i = 1; i < argc; i++) {
        struct stat st;
        if (stat(argv[i], &st) != 0) {
            fputs("stat: cannot stat: ", stdout);
            fputs(argv[i], stdout);
            fputs("\n", stdout);
            continue;
        }
        fputs(argv[i], stdout);
        fputs("\n  size: ", stdout);
        printf("%ld bytes\n", (long)st.st_size);
    }
    return 0;
}
