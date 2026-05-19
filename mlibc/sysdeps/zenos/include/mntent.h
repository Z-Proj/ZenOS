#ifndef _MNTENT_H
#define _MNTENT_H

#include <stdio.h>

#define MOUNTED "/etc/mtab"
#define MNTOPT_DEFAULTS "defaults"
#define MNTOPT_RO "ro"
#define MNTOPT_RW "rw"
#define MNTOPT_SUID "suid"
#define MNTOPT_NOSUID "nosuid"
#define MNTOPT_NOAUTO "noauto"

struct mntent {
	char *mnt_fsname;
	char *mnt_dir;
	char *mnt_type;
	char *mnt_opts;
	int mnt_freq;
	int mnt_passno;
};

#ifdef __cplusplus
extern "C" {
#endif

FILE *setmntent(const char *filename, const char *type);
struct mntent *getmntent(FILE *f);
int addmntent(FILE *f, const struct mntent *mnt);
int endmntent(FILE *f);
char *hasmntopt(const struct mntent *mnt, const char *opt);
struct mntent *getmntent_r(FILE *f, struct mntent *mnt, char *linebuf, int buflen);

#ifdef __cplusplus
}
#endif

#endif
