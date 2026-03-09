#ifndef _SYS_TERMIOS_H
#define _SYS_TERMIOS_H

#include <sys/cdefs.h>
#include <sys/ioctl.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef unsigned int tcflag_t;
typedef unsigned char cc_t;
typedef unsigned int speed_t;

#define NCCS 19

struct termios {
    tcflag_t c_iflag;
    tcflag_t c_oflag;
    tcflag_t c_cflag;
    tcflag_t c_lflag;
    cc_t c_line;
    cc_t c_cc[NCCS];
    speed_t c_ispeed;
    speed_t c_ospeed;
};

#define VINTR    0
#define VQUIT    1
#define VERASE   2
#define VKILL    3
#define VEOF     4
#define VTIME    5
#define VMIN     6
#define VSWTC    7
#define VSTART   8
#define VSTOP    9
#define VSUSP    10
#define VEOL     11
#define VREPRINT 12
#define VDISCARD 13
#define VWERASE  14
#define VLNEXT   15
#define VEOL2    16

#define IGNBRK   0x00000001
#define BRKINT   0x00000002
#define IGNPAR   0x00000004
#define PARMRK   0x00000008
#define INPCK    0x00000010
#define ISTRIP   0x00000020
#define INLCR    0x00000040
#define IGNCR    0x00000080
#define ICRNL    0x00000100
#define IUCLC    0x00000200
#define IXON     0x00000400
#define IXANY    0x00000800
#define IXOFF    0x00001000
#define IMAXBEL  0x00002000

#define OPOST    0x00000001
#define OLCUC    0x00000002
#define ONLCR    0x00000004
#define OCRNL    0x00000008
#define ONOCR    0x00000010
#define ONLRET   0x00000020
#define OFILL    0x00000040
#define OFDEL    0x00000080
#define NLDLY    0x00000100
#define   NL0    0x00000000
#define   NL1    0x00000100
#define CRDLY    0x00000600
#define   CR0    0x00000000
#define   CR1    0x00000200
#define   CR2    0x00000400
#define   CR3    0x00000600
#define TABDLY   0x00001800
#define   TAB0   0x00000000
#define   TAB1   0x00000800
#define   TAB2   0x00001000
#define   TAB3   0x00001800
#define   XTABS  0x00001800
#define BSDLY    0x00002000
#define   BS0    0x00000000
#define   BS1    0x00002000
#define VTDLY    0x00004000
#define   VT0    0x00000000
#define   VT1    0x00004000
#define FFDLY    0x00008000
#define   FF0    0x00000000
#define   FF1    0x00008000

#define CBAUD    0x0000100F
#define CSIZE    0x00000030
#define   CS5    0x00000000
#define   CS6    0x00000010
#define   CS7    0x00000020
#define   CS8    0x00000030
#define CSTOPB   0x00000040
#define CREAD    0x00000080
#define PARENB   0x00000100
#define PARODD   0x00000200
#define HUPCL    0x00000400
#define CLOCAL   0x00000800
#define CBAUDEX  0x00001000
#define CRTSCTS  0x80000000

#define ISIG     0x00000001
#define ICANON   0x00000002
#define XCASE    0x00000004
#define ECHO     0x00000008
#define ECHOE    0x00000010
#define ECHOK    0x00000020
#define ECHONL   0x00000040
#define NOFLSH   0x00000080
#define TOSTOP   0x00000100
#define ECHOCTL  0x00000200
#define ECHOPRT  0x00000400
#define ECHOKE   0x00000800
#define FLUSHO   0x00001000
#define PENDIN   0x00004000
#define IEXTEN   0x00008000

#define TCSANOW   0
#define TCSADRAIN 1
#define TCSAFLUSH 2

#define TCIFLUSH  0
#define TCOFLUSH  1
#define TCIOFLUSH 2

#define TCOOFF 0
#define TCOON  1
#define TCIOFF 2
#define TCION  3

#define B0       0
#define B50      50
#define B75      75
#define B110     110
#define B134     134
#define B150     150
#define B200     200
#define B300     300
#define B600     600
#define B1200    1200
#define B1800    1800
#define B2400    2400
#define B4800    4800
#define B9600    9600
#define B19200   19200
#define B38400   38400
#define B57600   57600
#define B115200  115200
#define B230400  230400
#define B460800  460800
#define B500000  500000
#define B576000  576000
#define B921600  921600
#define B1000000 1000000
#define B1152000 1152000
#define B1500000 1500000
#define B2000000 2000000
#define B2500000 2500000
#define B3000000 3000000
#define B3500000 3500000
#define B4000000 4000000

static __inline int tcgetattr(int __fd, struct termios *__termios_p)
{
    return ioctl(__fd, TCGETS, __termios_p);
}

static __inline int tcsetattr(int __fd, int __optional_actions, const struct termios *__termios_p)
{
    unsigned long __req = TCSETS;
    if (__optional_actions == TCSADRAIN)
        __req = TCSETSW;
    else if (__optional_actions == TCSAFLUSH)
        __req = TCSETSF;
    return ioctl(__fd, __req, (void *)__termios_p);
}

static __inline int tcdrain(int __fd)
{
    (void)__fd;
    return 0;
}

static __inline int tcflush(int __fd, int __queue_selector)
{
    (void)__fd;
    (void)__queue_selector;
    return 0;
}

static __inline int tcflow(int __fd, int __action)
{
    (void)__fd;
    (void)__action;
    return 0;
}

static __inline void cfmakeraw(struct termios *__termios_p)
{
    if (!__termios_p)
        return;
    __termios_p->c_iflag &= ~(IGNBRK | BRKINT | PARMRK | ISTRIP | INLCR | IGNCR | ICRNL | IXON);
    __termios_p->c_oflag &= ~OPOST;
    __termios_p->c_lflag &= ~(ECHO | ECHOE | ECHOK | ECHONL | ICANON | ISIG | IEXTEN | XCASE);
    __termios_p->c_cflag &= ~(CSIZE | PARENB);
    __termios_p->c_cflag |= CS8;
    __termios_p->c_cc[VMIN]  = 1;
    __termios_p->c_cc[VTIME] = 0;
}

static __inline speed_t cfgetispeed(const struct termios *__termios_p)
{
    return __termios_p ? __termios_p->c_ispeed : 0;
}

static __inline speed_t cfgetospeed(const struct termios *__termios_p)
{
    return __termios_p ? __termios_p->c_ospeed : 0;
}

static __inline int cfsetispeed(struct termios *__termios_p, speed_t __speed)
{
    if (!__termios_p)
        return -1;
    __termios_p->c_ispeed = __speed;
    return 0;
}

static __inline int cfsetospeed(struct termios *__termios_p, speed_t __speed)
{
    if (!__termios_p)
        return -1;
    __termios_p->c_ospeed = __speed;
    return 0;
}

static __inline int cfsetspeed(struct termios *__termios_p, speed_t __speed)
{
    if (!__termios_p)
        return -1;
    __termios_p->c_ispeed = __speed;
    __termios_p->c_ospeed = __speed;
    return 0;
}

#ifdef __cplusplus
}
#endif

#endif
