#ifndef _SYS_SYSINFO_H
#define _SYS_SYSINFO_H

struct sysinfo {
	long uptime;
	unsigned long loads[3];
	unsigned long totalram;
	unsigned long freeram;
	unsigned long sharedram;
	unsigned long bufferram;
	unsigned long totalswap;
	unsigned long freeswap;
	unsigned short procs;
	unsigned long totalhigh;
	unsigned long freehigh;
	unsigned int mem_unit;
	char _f[20 - 2 * sizeof(long) - sizeof(int)];
};

#define SI_LOAD_SHIFT 16

#ifdef __cplusplus
extern "C" {
#endif

int sysinfo(struct sysinfo *info);
int get_nprocs(void);
int get_nprocs_conf(void);

#ifdef __cplusplus
}
#endif

#endif
