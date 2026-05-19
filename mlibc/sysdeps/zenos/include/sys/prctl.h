#ifndef _SYS_PRCTL_H
#define _SYS_PRCTL_H

#define PR_SET_PDEATHSIG 1
#define PR_GET_PDEATHSIG 2
#define PR_SET_NAME 15
#define PR_GET_NAME 16
#define PR_CAPBSET_READ 23
#define PR_CAPBSET_DROP 24
#define PR_SET_NO_NEW_PRIVS 38
#define PR_GET_NO_NEW_PRIVS 39
#define PR_CAP_AMBIENT 47
#define PR_CAP_AMBIENT_IS_SET 1
#define PR_CAP_AMBIENT_RAISE 2
#define PR_CAP_AMBIENT_LOWER 3
#define PR_CAP_AMBIENT_CLEAR_ALL 4

#ifdef __cplusplus
extern "C" {
#endif

int prctl(int option, ...);

#ifdef __cplusplus
}
#endif

#endif
