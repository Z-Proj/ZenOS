#ifndef _SYS_IOCTL_H
#define _SYS_IOCTL_H

#include <sys/cdefs.h>

#ifdef __cplusplus
extern "C" {
#endif

#define TCGETS      0x5401UL
#define TCSETS      0x5402UL
#define TCSETSW     0x5403UL
#define TCSETSF     0x5404UL
#define TCSBRK      0x5409UL
#define TCXONC      0x540AUL
#define TCFLSH      0x540BUL
#define TIOCEXCL    0x540CUL
#define TIOCNXCL    0x540DUL
#define TIOCSCTTY   0x540EUL
#define TIOCGPGRP   0x540FUL
#define TIOCSPGRP   0x5410UL
#define TIOCOUTQ    0x5411UL
#define TIOCSTI     0x5412UL
#define TIOCGWINSZ  0x5413UL
#define TIOCSWINSZ  0x5414UL
#define TIOCMGET    0x5415UL
#define TIOCMBIS    0x5416UL
#define TIOCMBIC    0x5417UL
#define TIOCMSET    0x5418UL
#define TIOCGSOFTCAR 0x5419UL
#define TIOCSSOFTCAR 0x541AUL
#define FIONREAD    0x541BUL
#define TIOCLINUX   0x541CUL
#define TIOCCONS    0x541DUL
#define TIOCGSERIAL 0x541EUL
#define TIOCSSERIAL 0x541FUL
#define TIOCPKT     0x5420UL
#define FIONBIO     0x5421UL
#define TIOCNOTTY   0x5422UL
#define TIOCSETD    0x5423UL
#define TIOCGETD    0x5424UL
#define TCSBRKP     0x5425UL
#define TIOCSBRK    0x5427UL
#define TIOCCBRK    0x5428UL
#define TIOCGSID    0x5429UL
#define TIOCGRS485  0x542EUL
#define TIOCSRS485  0x542FUL
#define TIOCGPTN    0x80045430UL
#define TIOCSPTYGID 0x80045431UL
#define TIOCGPTLCK  0x80045439UL
#define TIOCSPTLCK  0x40045438UL
#define TIOCGPTPEER 0x5441UL

struct winsize {
    unsigned short ws_row;
    unsigned short ws_col;
    unsigned short ws_xpixel;
    unsigned short ws_ypixel;
};

int ioctl(int __fd, unsigned long __request, ...);

#ifdef __cplusplus
}
#endif

#endif
