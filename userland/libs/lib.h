#ifndef LIB_H
#define LIB_H

#include "../userlib.h"

typedef int FILE;
typedef long ptrdiff_t;
typedef long intptr_t;



#define EOF (-1)

#define STDIN_FILENO  0
#define STDOUT_FILENO 1
#define STDERR_FILENO 2

#define _FD(fp) ((int)(size_t)(fp))
#define _FP(fd) ((FILE*)(size_t)(fd))

static FILE *stdin  = _FP(0);
static FILE *stdout = _FP(1);
static FILE *stderr = _FP(2);

static inline FILE *fopen(const char *path, const char *mode) {
    int flags = O_RDONLY;
    if (mode[0]=='w') flags = O_WRONLY|O_CREAT|O_TRUNC;
    else if (mode[0]=='a') flags = O_WRONLY|O_CREAT|O_APPEND;
    else if (mode[0]=='r'&&mode[1]=='+') flags = O_RDWR;
    else if (mode[0]=='w'&&mode[1]=='+') flags = O_RDWR|O_CREAT|O_TRUNC;
    int fd = open(path, flags);
    if (fd < 0) return NULL;
    return _FP(fd);
}

static inline int fclose(FILE *fp) {
    return close(_FD(fp));
}

static inline size_t fread(void *ptr, size_t size, size_t nmemb, FILE *fp) {
    size_t total = size * nmemb;
    ssize_t got = read(_FD(fp), ptr, total);
    if (got <= 0) return 0;
    return (size_t)got / (size ? size : 1);
}

static inline size_t fwrite(const void *ptr, size_t size, size_t nmemb, FILE *fp) {
    size_t total = size * nmemb;
    ssize_t wrote = write(_FD(fp), ptr, total);
    if (wrote <= 0) return 0;
    return (size_t)wrote / (size ? size : 1);
}

static inline int fseek(FILE *fp, long offset, int whence) {
    return (lseek(_FD(fp), (off_t)offset, whence) >= 0) ? 0 : -1;
}

static inline long ftell(FILE *fp) {
    return (long)lseek(_FD(fp), 0, SEEK_CUR);
}

static inline void rewind(FILE *fp) {
    lseek(_FD(fp), 0, SEEK_SET);
}

static inline int feof(FILE *fp) {
    int fd = _FD(fp);
    if (fd < 3) return 0;
    off_t cur = lseek(fd, 0, SEEK_CUR);
    off_t end = lseek(fd, 0, SEEK_END);
    if (cur >= 0 && end >= 0 && cur < end) lseek(fd, cur, SEEK_SET);
    return (cur >= end) ? 1 : 0;
}

static inline int ferror(FILE *fp) {
    (void)fp; return 0;
}

static inline void clearerr(FILE *fp) {
    (void)fp;
}

static inline int fflush(FILE *fp) {
    (void)fp; return 0;
}

static inline int fgetc(FILE *fp) {
    unsigned char c;
    ssize_t r = read(_FD(fp), &c, 1);
    return (r == 1) ? (int)c : EOF;
}

static inline int fputc(int c, FILE *fp) {
    unsigned char ch = (unsigned char)c;
    ssize_t r = write(_FD(fp), &ch, 1);
    return (r == 1) ? (int)(unsigned char)c : EOF;
}

static inline char *fgets(char *s, int n, FILE *fp) {
    int i = 0;
    while (i < n-1) {
        int c = fgetc(fp);
        if (c == EOF) { if (i==0) return NULL; break; }
        s[i++] = (char)c;
        if (c=='\n') break;
    }
    s[i] = '\0';
    return (i>0||n==1) ? s : NULL;
}

static inline int fputs(const char *s, FILE *fp) {
    size_t len = strlen(s);
    ssize_t r = write(_FD(fp), s, len);
    return (r < 0) ? EOF : (int)r;
}

static inline int getc(FILE *fp)     { return fgetc(fp); }
static inline int putc(int c, FILE *fp){ return fputc(c, fp); }

static inline int getchar(void) { return fgetc(stdin); }

static inline int putchar(int c) { return fputc(c, stdout); }

static inline int puts(const char *s) {
    int r = fputs(s, stdout);
    fputc('\n', stdout);
    return r < 0 ? EOF : r;
}

static inline int ungetc(int c, FILE *fp) {
    (void)fp; (void)c;
    return EOF;
}

static inline int _fmt_uint(char *buf, unsigned long long val, int base, int upper) {
    const char *digits = upper ? "0123456789ABCDEF" : "0123456789abcdef";
    char tmp[64]; int len=0;
    if (!val) { tmp[len++]='0'; }
    while (val) { tmp[len++]=digits[val%base]; val/=base; }
    for (int i=0;i<len;i++) buf[i]=tmp[len-1-i];
    return len;
}

typedef struct { char *buf; size_t pos; size_t cap; FILE *fp; int to_file; } _fmtctx;

static inline void _fmtput(char c, _fmtctx *ctx) {
    if (ctx->to_file) { fputc(c, ctx->fp); ctx->pos++; }
    else if (ctx->buf && ctx->pos+1 < ctx->cap) ctx->buf[ctx->pos++]=c;
    else ctx->pos++;
}

static inline void _fmtputs(const char *s, int width, int left, char pad, _fmtctx *ctx) {
    int len=(int)strlen(s);
    if (!left) for(int i=len;i<width;i++) _fmtput(pad,ctx);
    for (int i=0;s[i];i++) _fmtput(s[i],ctx);
    if (left) for(int i=len;i<width;i++) _fmtput(' ',ctx);
}

static inline int _vfmt(const char *fmt, __builtin_va_list ap, _fmtctx *ctx) {
    while (*fmt) {
        if (*fmt != '%') { _fmtput(*fmt++, ctx); continue; }
        fmt++; if (!*fmt) break;
        int left=0, zero=0, alt=0, plus=0, space=0;
        while (*fmt=='-'||*fmt=='0'||*fmt=='#'||*fmt=='+'||*fmt==' ') {
            if (*fmt=='-') left=1;
            if (*fmt=='0') zero=1;
            if (*fmt=='#') alt=1;
            if (*fmt=='+') plus=1;
            if (*fmt==' ') space=1;
            fmt++;
        }
        int width=0;
        if (*fmt=='*') { width=__builtin_va_arg(ap,int); if(width<0){left=1;width=-width;} fmt++; }
        else while(*fmt>='0'&&*fmt<='9') width=width*10+(*fmt++-'0');
        int prec=-1;
        if (*fmt=='.') {
            fmt++; prec=0;
            if (*fmt=='*') { prec=__builtin_va_arg(ap,int); fmt++; }
            else while(*fmt>='0'&&*fmt<='9') prec=prec*10+(*fmt++-'0');
        }
        int lng=0;
        while(*fmt=='l'||*fmt=='h'||*fmt=='z'||*fmt=='j'||*fmt=='t') {
            if(*fmt=='l') lng++;
            fmt++;
        }
        char spec=*fmt++;
        char tmp[80]; int tlen=0;
        char pad=(zero&&!left)?'0':' ';
        (void)alt; (void)plus; (void)space;
        switch(spec) {
        case 'd': case 'i': {
            long long v = (lng>=2) ? __builtin_va_arg(ap,long long)
                        : (lng==1) ? (long long)__builtin_va_arg(ap,long)
                                   : (long long)__builtin_va_arg(ap,int);
            if(v<0){tmp[tlen++]='-';v=-v;} else if(plus){tmp[tlen++]='+';} else if(space){tmp[tlen++]=' ';}
            int start=tlen; tlen+=_fmt_uint(tmp+tlen,(unsigned long long)v,10,0);
            if(prec>=0){int digs=tlen-start;while(digs++<prec){memmove(tmp+start+1,tmp+start,tlen-start);tmp[start]='0';tlen++;}}
            tmp[tlen]='\0'; _fmtputs(tmp,width,left,pad,ctx); break;
        }
        case 'u': {
            unsigned long long v = (lng>=2) ? __builtin_va_arg(ap,unsigned long long)
                                 : (lng==1) ? (unsigned long long)__builtin_va_arg(ap,unsigned long)
                                            : (unsigned long long)__builtin_va_arg(ap,unsigned int);
            tlen=_fmt_uint(tmp,v,10,0); tmp[tlen]='\0'; _fmtputs(tmp,width,left,pad,ctx); break;
        }
        case 'x': case 'X': {
            unsigned long long v = (lng>=2) ? __builtin_va_arg(ap,unsigned long long)
                                 : (lng==1) ? (unsigned long long)__builtin_va_arg(ap,unsigned long)
                                            : (unsigned long long)__builtin_va_arg(ap,unsigned int);
            if(alt&&v){tmp[tlen++]='0';tmp[tlen++]=(spec=='X')?'X':'x';}
            tlen+=_fmt_uint(tmp+tlen,v,16,spec=='X'); tmp[tlen]='\0'; _fmtputs(tmp,width,left,pad,ctx); break;
        }
        case 'o': {
            unsigned long long v = (lng>=2) ? __builtin_va_arg(ap,unsigned long long)
                                 : (lng==1) ? (unsigned long long)__builtin_va_arg(ap,unsigned long)
                                            : (unsigned long long)__builtin_va_arg(ap,unsigned int);
            tlen=_fmt_uint(tmp,v,8,0); tmp[tlen]='\0'; _fmtputs(tmp,width,left,pad,ctx); break;
        }
        case 'p': {
            unsigned long long v=(unsigned long long)(uintptr_t)__builtin_va_arg(ap,void*);
            tmp[0]='0'; tmp[1]='x'; tlen=2+_fmt_uint(tmp+2,v,16,0); tmp[tlen]='\0';
            _fmtputs(tmp,width,left,' ',ctx); break;
        }
        case 's': {
            const char *s=__builtin_va_arg(ap,const char*); if(!s) s="(null)";
            int slen=(prec>=0)?(int)strnlen(s,(size_t)prec):(int)strlen(s);
            if(!left) for(int i=slen;i<width;i++) _fmtput(' ',ctx);
            for(int i=0;i<slen;i++) _fmtput(s[i],ctx);
            if(left) for(int i=slen;i<width;i++) _fmtput(' ',ctx);
            break;
        }
        case 'c': { char c=(char)__builtin_va_arg(ap,int); tmp[0]=c; tmp[1]='\0'; _fmtputs(tmp,width,left,' ',ctx); break; }
        case 'n': { int *np=__builtin_va_arg(ap,int*); if(np) *np=(int)ctx->pos; break; }
        case '%': _fmtput('%',ctx); break;
        default:  _fmtput('%',ctx); _fmtput(spec,ctx); break;
        }
    }
    return (int)ctx->pos;
}

static inline int vprintf(const char *fmt, __builtin_va_list ap) {
    _fmtctx ctx={NULL,0,0,stdout,1}; return _vfmt(fmt,ap,&ctx);
}
static inline int vfprintf(FILE *fp, const char *fmt, __builtin_va_list ap) {
    _fmtctx ctx={NULL,0,0,fp,1}; return _vfmt(fmt,ap,&ctx);
}
static inline int vsnprintf(char *buf, size_t size, const char *fmt, __builtin_va_list ap) {
    _fmtctx ctx={buf,0,size,NULL,0}; int r=_vfmt(fmt,ap,&ctx);
    if(buf&&size>0) buf[ctx.pos<size?ctx.pos:size-1]='\0';
    return r;
}
static inline int vsprintf(char *buf, const char *fmt, __builtin_va_list ap) {
    return vsnprintf(buf,(size_t)-1,fmt,ap);
}
static inline int vsscanf(const char *str, const char *fmt, __builtin_va_list ap) {
    (void)str; (void)fmt; (void)ap; return 0;
}

static inline int printf(const char *fmt, ...) {
    __builtin_va_list ap; __builtin_va_start(ap,fmt);
    int r=vprintf(fmt,ap); __builtin_va_end(ap); return r;
}
static inline int fprintf(FILE *fp, const char *fmt, ...) {
    __builtin_va_list ap; __builtin_va_start(ap,fmt);
    int r=vfprintf(fp,fmt,ap); __builtin_va_end(ap); return r;
}
static inline int sprintf(char *buf, const char *fmt, ...) {
    __builtin_va_list ap; __builtin_va_start(ap,fmt);
    int r=vsprintf(buf,fmt,ap); __builtin_va_end(ap); return r;
}
static inline int snprintf(char *buf, size_t size, const char *fmt, ...) {
    __builtin_va_list ap; __builtin_va_start(ap,fmt);
    int r=vsnprintf(buf,size,fmt,ap); __builtin_va_end(ap); return r;
}
static inline int sscanf(const char *str, const char *fmt, ...) {
    (void)str; (void)fmt; return 0;
}

static inline void perror(const char *s) {
    if (s && *s) { fputs(s, stderr); fputs(": ", stderr); }
    fputs("error\n", stderr);
}

static inline void abort(void) { _exit(134); }

static inline char *getenv(const char *name) { (void)name; return NULL; }

static inline int setenv(const char *n, const char *v, int ow) { (void)n;(void)v;(void)ow; return 0; }
static inline int unsetenv(const char *n) { (void)n; return 0; }

static inline int system(const char *cmd) {
    if (!cmd) return 1;
    return execv(cmd, NULL);
}

static inline int abs(int n) { return n<0?-n:n; }
static inline long labs(long n) { return n<0?-n:n; }
static inline long long llabs(long long n) { return n<0?-n:n; }

static inline int rand(void) {
    static unsigned long long _rng = 12345;
    _rng = _rng * 6364136223846793005ULL + 1442695040888963407ULL;
    return (int)((_rng >> 33) & 0x7fffffff);
}
static inline void srand(unsigned int seed) { (void)seed; }

static inline double strtod(const char *s, char **end) {
    double r=0.0; int sign=1;
    while(*s==' ') s++;
    if(*s=='-'){sign=-1;s++;} else if(*s=='+') s++;
    while(*s>='0'&&*s<='9'){r=r*10+(*s-'0');s++;}
    if(*s=='.') {
        s++; double frac=0.1;
        while(*s>='0'&&*s<='9'){r+=(*s-'0')*frac;frac*=0.1;s++;}
    }
    if(end) *end=(char*)s;
    return sign*r;
}
static inline float strtof(const char *s, char **e) { return (float)strtod(s,e); }

#define assert(expr) \
    do { if(!(expr)) { fprintf(stderr,"assert failed: %s (%s:%d)\n",#expr,__FILE__,__LINE__); abort(); } } while(0)

#ifndef NDEBUG
#define _ASSERT_ENABLED 1
#endif

#endif
