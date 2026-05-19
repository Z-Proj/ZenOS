#ifndef _SYS_STATFS_H
#define _SYS_STATFS_H

#include <stdint.h>

typedef struct {
	int __val[2];
} fsid_t;

struct statfs {
	long f_type;
	long f_bsize;
	uint64_t f_blocks;
	uint64_t f_bfree;
	uint64_t f_bavail;
	uint64_t f_files;
	uint64_t f_ffree;
	fsid_t f_fsid;
	long f_namelen;
	long f_frsize;
	long f_flags;
	long f_spare[4];
};

typedef struct statfs statfs64;

#ifdef __cplusplus
extern "C" {
#endif

int statfs(const char *path, struct statfs *buf);
int fstatfs(int fd, struct statfs *buf);
int fstatfs64(int fd, statfs64 *buf);

#ifdef __cplusplus
}
#endif

#endif
