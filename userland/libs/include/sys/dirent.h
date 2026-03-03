#ifndef _SYS_DIRENT_H
#define _SYS_DIRENT_H

#include <sys/cdefs.h>
#include <sys/_types.h>

#ifdef __cplusplus
extern "C" {
#endif

#ifndef _INO_T_DECLARED
typedef __ino_t ino_t;
#define _INO_T_DECLARED
#endif

#define DT_UNKNOWN  0
#define DT_FIFO     1
#define DT_CHR      2
#define DT_DIR      4
#define DT_BLK      6
#define DT_REG      8
#define DT_LNK      10
#define DT_SOCK     12

#define MAXNAMLEN 255

struct dirent {
    ino_t  d_ino;
    __uint8_t d_type;
    char   d_name[MAXNAMLEN + 1];
};

typedef struct {
    int   _dd_fd;
    struct dirent _dd_cur;
} DIR;

__BEGIN_DECLS
DIR           *opendir(const char *);
struct dirent *readdir(DIR *);
int            closedir(DIR *);
void           rewinddir(DIR *);
__END_DECLS

#ifdef __cplusplus
}
#endif

#endif
