#ifndef LIB_H
#define LIB_H

#include "userlib.h"

// ==================== TYPES ====================

typedef int FILE;
typedef long long ptrdiff_t;
// uintptr_t is provided by stdint.h (included via userlib.h)

#define EOF    (-1)
#define SEEK_SET 0
#define SEEK_CUR 1
#define SEEK_END 2

#define STDIN_FILENO  0
#define STDOUT_FILENO 1
#define STDERR_FILENO 2

#define O_RDONLY 0
#define O_WRONLY 1
#define O_RDWR   2
#define O_CREAT  0x40
#define O_TRUNC  0x200

// Errno-alike (last error, not thread-safe but fine for single-threaded)
static int errno = 0;

// ==================== FD TABLE ====================
// Maps integer fds to zfs_file_t structs.
// 0/1/2 = stdin/stdout/stderr (special-cased)
// 3+ = real files

#define _MAX_FDS 32
static zfs_file_t _fd_table[_MAX_FDS];
static int        _fd_used[_MAX_FDS];

static int _fd_alloc(void) {
    for (int i = 3; i < _MAX_FDS; i++) {
        if (!_fd_used[i]) {
            _fd_used[i] = 1;
            return i;
        }
    }
    return -1;
}

// ==================== LOW-LEVEL FILE I/O ====================

static inline int zopen(const char *path, int flags, int mode) {
    (void)mode;
    int fd = _fd_alloc();
    if (fd < 0) { errno = 24; return -1; } // EMFILE

    if (flags & O_CREAT) {
        create(path, 0); // ignore error if exists
    }

    // Call userlib's open directly (takes zfs_file_t*)
    int ret = syscall2(10, (uint64_t)path, (uint64_t)&_fd_table[fd]);
    if (ret < 0) {
        _fd_used[fd] = 0;
        errno = 2; // ENOENT
        return -1;
    }

    if (flags & O_TRUNC) {
        syscall3(14, (uint64_t)&_fd_table[fd], 0, SEEK_SET);
    }

    return fd;
}

static inline ssize_t zread(int fd, void *buf, size_t count) {
    if (fd == STDIN_FILENO) {
        // Read one char at a time from keyboard
        char *cbuf = (char*)buf;
        for (size_t i = 0; i < count; i++) {
            char c = 0;
            while (!c) c = getkey();
            cbuf[i] = c;
            if (c == '\n') return (ssize_t)(i + 1);
        }
        return (ssize_t)count;
    }
    if (fd < 3 || fd >= _MAX_FDS || !_fd_used[fd]) { errno = 9; return -1; }
    uint32_t bytes_read = 0;
    int ret = (int)syscall4(11, (uint64_t)&_fd_table[fd], (uint64_t)buf, count, (uint64_t)&bytes_read);
    if (ret < 0) { errno = 5; return -1; }
    return (ssize_t)bytes_read;
}

static inline ssize_t zwrite(int fd, const void *buf, size_t count) {
    if (fd == STDOUT_FILENO || fd == STDERR_FILENO) {
        // Use prints; it needs a null-terminated string, so chunk it
        const char *p = (const char*)buf;
        size_t remaining = count;
        char tmp[256];
        while (remaining > 0) {
            size_t chunk = remaining < 255 ? remaining : 255;
            for (size_t i = 0; i < chunk; i++) tmp[i] = p[i];
            tmp[chunk] = '\0';
            prints(tmp);
            p += chunk;
            remaining -= chunk;
        }
        return (ssize_t)count;
    }
    if (fd < 3 || fd >= _MAX_FDS || !_fd_used[fd]) { errno = 9; return -1; }
    int ret = (int)syscall3(12, (uint64_t)&_fd_table[fd], (uint64_t)buf, count);
    if (ret < 0) { errno = 5; return -1; }
    return (ssize_t)count;
}

static inline int zclose(int fd) {
    if (fd < 3) return 0;
    if (fd >= _MAX_FDS || !_fd_used[fd]) { errno = 9; return -1; }
    syscall1(13, (uint64_t)&_fd_table[fd]);
    _fd_used[fd] = 0;
    return 0;
}

static inline off_t zlseek(int fd, off_t offset, int whence) {
    if (fd < 3 || fd >= _MAX_FDS || !_fd_used[fd]) { errno = 9; return -1; }
    return (off_t)syscall3(14, (uint64_t)&_fd_table[fd], (uint64_t)offset, whence);
}

// Shadow userlib's open/read/close with fd-based versions.
// userlib defines them as functions taking zfs_file_t*; we redefine
// the names here so c4 (and any POSIX-style caller) gets int fds.
// Must come AFTER the zopen/zread/zclose/zlseek definitions above.
#define open(path, flags)        zopen((path), (flags), 0)
#define read(fd, buf, size)      zread((fd), (buf), (size))
#define write(fd, buf, size)     zwrite((fd), (buf), (size))
#define close(fd)                zclose(fd)
#define lseek(fd, off, whence)   zlseek((fd), (off), (whence))
// FILE* is just int fd cast to FILE* for simplicity.

#define _FD(fp)  ((int)(size_t)(fp))
#define _FP(fd)  ((FILE*)(size_t)(fd))

static FILE *stdin  = _FP(0);
static FILE *stdout = _FP(1);
static FILE *stderr = _FP(2);

static inline FILE *fopen(const char *path, const char *mode) {
    int flags = O_RDONLY;
    if (mode[0] == 'w') flags = O_WRONLY | O_CREAT | O_TRUNC;
    else if (mode[0] == 'a') flags = O_WRONLY | O_CREAT;
    else if (mode[0] == 'r' && mode[1] == '+') flags = O_RDWR;
    int fd = zopen(path, flags, 0);
    if (fd < 0) return NULL;
    return _FP(fd);
}

static inline int fclose(FILE *fp) {
    return zclose(_FD(fp));
}

static inline size_t fread(void *ptr, size_t size, size_t nmemb, FILE *fp) {
    size_t total = size * nmemb;
    ssize_t got = zread(_FD(fp), ptr, total);
    if (got < 0) return 0;
    return (size_t)got / (size ? size : 1);
}

static inline size_t fwrite(const void *ptr, size_t size, size_t nmemb, FILE *fp) {
    size_t total = size * nmemb;
    ssize_t wrote = zwrite(_FD(fp), ptr, total);
    if (wrote < 0) return 0;
    return (size_t)wrote / (size ? size : 1);
}

static inline int fseek(FILE *fp, long offset, int whence) {
    return (zlseek(_FD(fp), (off_t)offset, whence) >= 0) ? 0 : -1;
}

static inline long ftell(FILE *fp) {
    return (long)zlseek(_FD(fp), 0, SEEK_CUR);
}

static inline void rewind(FILE *fp) {
    fseek(fp, 0, SEEK_SET);
}

static inline int feof(FILE *fp) {
    // Check if current position >= file size
    if (_FD(fp) < 3 || _FD(fp) >= _MAX_FDS) return 1;
    zfs_file_t *f = &_fd_table[_FD(fp)];
    return (int)(f->position >= f->size);
}

static inline int fgetc(FILE *fp) {
    unsigned char c;
    ssize_t r = zread(_FD(fp), &c, 1);
    return (r == 1) ? (int)c : EOF;
}

static inline int fputc(int c, FILE *fp) {
    unsigned char ch = (unsigned char)c;
    ssize_t r = zwrite(_FD(fp), &ch, 1);
    return (r == 1) ? (int)(unsigned char)c : EOF;
}

static inline char *fgets(char *s, int n, FILE *fp) {
    int i = 0;
    while (i < n - 1) {
        int c = fgetc(fp);
        if (c == EOF) { if (i == 0) return NULL; break; }
        s[i++] = (char)c;
        if (c == '\n') break;
    }
    s[i] = '\0';
    return s;
}

static inline int fputs(const char *s, FILE *fp) {
    size_t len = strlen(s);
    ssize_t r = zwrite(_FD(fp), s, len);
    return (r < 0) ? EOF : (int)r;
}

static inline int getc(FILE *fp)  { return fgetc(fp); }
static inline int putc(int c, FILE *fp) { return fputc(c, fp); }

// ==================== CONSOLE I/O ====================

static inline int getchar(void) {
    return fgetc(stdin);
}

static inline int puts(const char *s) {
    int r = fputs(s, stdout);
    fputc('\n', stdout);
    return r;
}

// ==================== STRING / NUMBER CONVERSIONS ====================

static inline long strtol(const char *s, char **endptr, int base) {
    while (*s == ' ' || *s == '\t') s++;
    long result = 0;
    int sign = 1;
    if (*s == '-') { sign = -1; s++; }
    else if (*s == '+') s++;
    if (base == 0) {
        if (s[0] == '0' && (s[1] == 'x' || s[1] == 'X')) { base = 16; s += 2; }
        else if (s[0] == '0') { base = 8; s++; }
        else base = 10;
    } else if (base == 16 && s[0] == '0' && (s[1] == 'x' || s[1] == 'X')) {
        s += 2;
    }
    const char *start = s;
    while (1) {
        int digit;
        char c = *s;
        if (c >= '0' && c <= '9') digit = c - '0';
        else if (c >= 'a' && c <= 'z') digit = c - 'a' + 10;
        else if (c >= 'A' && c <= 'Z') digit = c - 'A' + 10;
        else break;
        if (digit >= base) break;
        result = result * base + digit;
        s++;
    }
    if (endptr) *endptr = (s == start) ? (char*)s : (char*)s;
    return sign * result;
}

static inline unsigned long strtoul(const char *s, char **endptr, int base) {
    return (unsigned long)strtol(s, endptr, base);
}

static inline long long atoll(const char *s) {
    return (long long)strtol(s, NULL, 10);
}

// ==================== MEMORY ====================

static inline void *calloc(size_t nmemb, size_t size) {
    size_t total = nmemb * size;
    void *p = malloc(total);
    if (p) memset(p, 0, total);
    return p;
}

static inline void *memmove(void *dest, const void *src, size_t n) {
    uint8_t *d = (uint8_t*)dest;
    const uint8_t *s = (const uint8_t*)src;
    if (d < s) {
        for (size_t i = 0; i < n; i++) d[i] = s[i];
    } else {
        for (size_t i = n; i > 0; i--) d[i-1] = s[i-1];
    }
    return dest;
}

static inline void *memchr(const void *s, int c, size_t n) {
    const uint8_t *p = (const uint8_t*)s;
    for (size_t i = 0; i < n; i++)
        if (p[i] == (uint8_t)c) return (void*)(p + i);
    return NULL;
}

// ==================== STRING ====================

static inline int strncmp(const char *s1, const char *s2, size_t n) {
    for (size_t i = 0; i < n; i++) {
        if (s1[i] != s2[i]) return (unsigned char)s1[i] - (unsigned char)s2[i];
        if (s1[i] == '\0') return 0;
    }
    return 0;
}

static inline char *strchr(const char *s, int c) {
    while (*s) {
        if (*s == (char)c) return (char*)s;
        s++;
    }
    return (c == '\0') ? (char*)s : NULL;
}

static inline char *strrchr(const char *s, int c) {
    const char *last = NULL;
    while (*s) {
        if (*s == (char)c) last = s;
        s++;
    }
    return (char*)last;
}

static inline char *strstr(const char *haystack, const char *needle) {
    size_t nlen = strlen(needle);
    if (nlen == 0) return (char*)haystack;
    while (*haystack) {
        if (strncmp(haystack, needle, nlen) == 0) return (char*)haystack;
        haystack++;
    }
    return NULL;
}

static inline char *strncat(char *dest, const char *src, size_t n) {
    char *d = dest;
    while (*d) d++;
    size_t i;
    for (i = 0; i < n && src[i]; i++) d[i] = src[i];
    d[i] = '\0';
    return dest;
}

static inline size_t strnlen(const char *s, size_t maxlen) {
    size_t i = 0;
    while (i < maxlen && s[i]) i++;
    return i;
}

static inline char *strdup(const char *s) {
    size_t len = strlen(s) + 1;
    char *p = (char*)malloc(len);
    if (p) memcpy(p, s, len);
    return p;
}

// ==================== CTYPE ====================

static inline int isdigit(int c)  { return c >= '0' && c <= '9'; }
static inline int isalpha(int c)  { return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z'); }
static inline int isalnum(int c)  { return isdigit(c) || isalpha(c); }
static inline int isspace(int c)  { return c == ' ' || c == '\t' || c == '\n' || c == '\r' || c == '\f' || c == '\v'; }
static inline int isupper(int c)  { return c >= 'A' && c <= 'Z'; }
static inline int islower(int c)  { return c >= 'a' && c <= 'z'; }
static inline int isprint(int c)  { return c >= 0x20 && c < 0x7f; }
static inline int isxdigit(int c) { return isdigit(c) || (c >= 'a' && c <= 'f') || (c >= 'A' && c <= 'F'); }
static inline int toupper(int c)  { return islower(c) ? c - 32 : c; }
static inline int tolower(int c)  { return isupper(c) ? c + 32 : c; }

// ==================== PRINTF ENGINE ====================
// Supports: %d %i %u %x %X %o %s %c %p %% %ld %lld %lu %llu
// Width, precision, left-align (-), zero-pad (0)

static inline int _fmt_uint(char *buf, unsigned long long val, int base, int upper) {
    const char *digits = upper ? "0123456789ABCDEF" : "0123456789abcdef";
    char tmp[64];
    int len = 0;
    if (val == 0) { tmp[len++] = '0'; }
    while (val) { tmp[len++] = digits[val % base]; val /= base; }
    for (int i = 0; i < len; i++) buf[i] = tmp[len - 1 - i];
    return len;
}

typedef struct { char *buf; size_t pos; size_t cap; FILE *fp; int to_file; } _fmtctx;

static inline void _fmtput(char c, _fmtctx *ctx) {
    if (ctx->to_file) {
        fputc(c, ctx->fp);
        ctx->pos++;
    } else if (ctx->buf && ctx->pos + 1 < ctx->cap) {
        ctx->buf[ctx->pos++] = c;
    } else {
        ctx->pos++; // count even if no space (for snprintf return value)
    }
}

static inline void _fmtputs(const char *s, int width, int left, char pad, _fmtctx *ctx) {
    int len = (int)strlen(s);
    if (!left) for (int i = len; i < width; i++) _fmtput(pad, ctx);
    for (int i = 0; s[i]; i++) _fmtput(s[i], ctx);
    if (left)  for (int i = len; i < width; i++) _fmtput(' ', ctx);
}

static inline int _vfmt(const char *fmt, __builtin_va_list ap, _fmtctx *ctx) {
    while (*fmt) {
        if (*fmt != '%') { _fmtput(*fmt++, ctx); continue; }
        fmt++;
        if (!*fmt) break;

        // Flags
        int left = 0, zero = 0, alt = 0;
        while (*fmt == '-' || *fmt == '0' || *fmt == '#') {
            if (*fmt == '-') left = 1;
            if (*fmt == '0') zero = 1;
            if (*fmt == '#') alt = 1;
            fmt++;
        }

        // Width
        int width = 0;
        if (*fmt == '*') { width = __builtin_va_arg(ap, int); fmt++; }
        else while (*fmt >= '0' && *fmt <= '9') width = width * 10 + (*fmt++ - '0');

        // Precision
        int prec = -1;
        if (*fmt == '.') {
            fmt++;
            prec = 0;
            if (*fmt == '*') { prec = __builtin_va_arg(ap, int); fmt++; }
            else while (*fmt >= '0' && *fmt <= '9') prec = prec * 10 + (*fmt++ - '0');
        }

        // Length
        int lng = 0; // 1=long, 2=long long
        if (*fmt == 'l') { lng = 1; fmt++; if (*fmt == 'l') { lng = 2; fmt++; } }
        else if (*fmt == 'z') { lng = 1; fmt++; } // size_t

        char spec = *fmt++;
        char tmp[64];
        int tlen = 0;
        char pad = (zero && !left) ? '0' : ' ';
        (void)alt;

        switch (spec) {
        case 'd': case 'i': {
            long long v = (lng == 2) ? __builtin_va_arg(ap, long long)
                        : (lng == 1) ? (long long)__builtin_va_arg(ap, long)
                        : (long long)__builtin_va_arg(ap, int);
            if (v < 0) { tmp[tlen++] = '-'; v = -v; }
            tlen += _fmt_uint(tmp + tlen, (unsigned long long)v, 10, 0);
            tmp[tlen] = '\0';
            _fmtputs(tmp, width, left, pad, ctx);
            break;
        }
        case 'u': {
            unsigned long long v = (lng == 2) ? __builtin_va_arg(ap, unsigned long long)
                                 : (lng == 1) ? (unsigned long long)__builtin_va_arg(ap, unsigned long)
                                 : (unsigned long long)__builtin_va_arg(ap, unsigned int);
            tlen = _fmt_uint(tmp, v, 10, 0); tmp[tlen] = '\0';
            _fmtputs(tmp, width, left, pad, ctx);
            break;
        }
        case 'x': case 'X': {
            unsigned long long v = (lng == 2) ? __builtin_va_arg(ap, unsigned long long)
                                 : (lng == 1) ? (unsigned long long)__builtin_va_arg(ap, unsigned long)
                                 : (unsigned long long)__builtin_va_arg(ap, unsigned int);
            tlen = _fmt_uint(tmp, v, 16, spec == 'X'); tmp[tlen] = '\0';
            _fmtputs(tmp, width, left, pad, ctx);
            break;
        }
        case 'o': {
            unsigned long long v = (unsigned long long)__builtin_va_arg(ap, unsigned int);
            tlen = _fmt_uint(tmp, v, 8, 0); tmp[tlen] = '\0';
            _fmtputs(tmp, width, left, pad, ctx);
            break;
        }
        case 'p': {
            unsigned long long v = (unsigned long long)(uintptr_t)__builtin_va_arg(ap, void*);
            tmp[0] = '0'; tmp[1] = 'x';
            tlen = 2 + _fmt_uint(tmp + 2, v, 16, 0); tmp[tlen] = '\0';
            _fmtputs(tmp, width, left, ' ', ctx);
            break;
        }
        case 's': {
            const char *s = __builtin_va_arg(ap, const char*);
            if (!s) s = "(null)";
            // Apply precision as max length
            if (prec >= 0) {
                int slen = (int)strnlen(s, (size_t)prec);
                if (!left) for (int i = slen; i < width; i++) _fmtput(' ', ctx);
                for (int i = 0; i < slen; i++) _fmtput(s[i], ctx);
                if (left)  for (int i = slen; i < width; i++) _fmtput(' ', ctx);
            } else {
                _fmtputs(s, width, left, ' ', ctx);
            }
            break;
        }
        case 'c': {
            char c = (char)__builtin_va_arg(ap, int);
            tmp[0] = c; tmp[1] = '\0';
            _fmtputs(tmp, width, left, ' ', ctx);
            break;
        }
        case '%':
            _fmtput('%', ctx);
            break;
        default:
            _fmtput('%', ctx);
            _fmtput(spec, ctx);
            break;
        }
    }
    return (int)ctx->pos;
}

// ==================== PUBLIC PRINTF FAMILY ====================

static inline int vprintf(const char *fmt, __builtin_va_list ap) {
    _fmtctx ctx = {NULL, 0, 0, stdout, 1};
    return _vfmt(fmt, ap, &ctx);
}

static inline int vfprintf(FILE *fp, const char *fmt, __builtin_va_list ap) {
    _fmtctx ctx = {NULL, 0, 0, fp, 1};
    return _vfmt(fmt, ap, &ctx);
}

static inline int vsnprintf(char *buf, size_t size, const char *fmt, __builtin_va_list ap) {
    _fmtctx ctx = {buf, 0, size, NULL, 0};
    int r = _vfmt(fmt, ap, &ctx);
    if (buf && size > 0) buf[ctx.pos < size ? ctx.pos : size - 1] = '\0';
    return r;
}

static inline int vsprintf(char *buf, const char *fmt, __builtin_va_list ap) {
    return vsnprintf(buf, (size_t)-1, fmt, ap);
}

static inline int printf(const char *fmt, ...) {
    __builtin_va_list ap;
    __builtin_va_start(ap, fmt);
    int r = vprintf(fmt, ap);
    __builtin_va_end(ap);
    return r;
}

static inline int fprintf(FILE *fp, const char *fmt, ...) {
    __builtin_va_list ap;
    __builtin_va_start(ap, fmt);
    int r = vfprintf(fp, fmt, ap);
    __builtin_va_end(ap);
    return r;
}

static inline int sprintf(char *buf, const char *fmt, ...) {
    __builtin_va_list ap;
    __builtin_va_start(ap, fmt);
    int r = vsprintf(buf, fmt, ap);
    __builtin_va_end(ap);
    return r;
}

static inline int snprintf(char *buf, size_t size, const char *fmt, ...) {
    __builtin_va_list ap;
    __builtin_va_start(ap, fmt);
    int r = vsnprintf(buf, size, fmt, ap);
    __builtin_va_end(ap);
    return r;
}

// ==================== PROCESS ====================

static inline void abort(void) {
    exit(1);
}

static inline char *getenv(const char *name) {
    (void)name;
    return NULL; // No env on ZenOS
}

// ==================== ASSERT ====================

#define assert(expr) \
    do { if (!(expr)) { \
        fprintf(stderr, "assert failed: %s (%s:%d)\n", #expr, __FILE__, __LINE__); \
        abort(); \
    } } while(0)

#endif // LIB_H