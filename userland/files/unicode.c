#include "userlib.h"

extern char _binary_FreeSansB_sfn_start;
extern char _binary_FreeSansB_sfn_end;

#define SSFN_IMPLEMENTATION
#define SSFN_MAXLINES 4096
#define SSFN_memcmp memcmp
#define SSFN_memset memset
#define SSFN_memcpy memcpy
#define SSFN_realloc realloc
#define SSFN_free free
#include "ssfn.h"

// ---- bump allocator ----
static uint8_t heap[256 * 1024];
static uint32_t heap_pos = 0;

void *malloc(size_t sz) {
    sz = (sz + 7) & ~7ULL;
    if (heap_pos + sz > sizeof(heap)) return NULL;
    void *p = &heap[heap_pos];
    heap_pos += sz;
    return p;
}
void free(void *p) { (void)p; }
void *realloc(void *p, size_t sz) {
    void *n = malloc(sz);
    if (p && n) memcpy(n, p, sz);
    return n;
}

// ---- globals ----
static ssfn_t    ctx;
static ssfn_buf_t buf;
static int cur_x, cur_y;

// ---- itoa ----
static void print_int_inline(char *out, int64_t val) {
    if (val < 0) { *out++ = '-'; val = -val; }
    char tmp[20]; int i = 0;
    if (!val) tmp[i++] = '0';
    while (val) { tmp[i++] = '0' + val % 10; val /= 10; }
    for (int a=0,b=i-1; a<b; a++,b--) { char t=tmp[a]; tmp[a]=tmp[b]; tmp[b]=t; }
    tmp[i] = 0;
    strcpy(out, tmp);
}

// ---- render helpers ----
static void nl(int line_h) {
    cur_x = 8;
    cur_y += line_h + 3;
}

static void put(const char *str, uint32_t fg, int size) {
    ssfn_select(&ctx, SSFN_FAMILY_SANS, NULL, SSFN_STYLE_REGULAR, size);
    buf.x  = cur_x;
    buf.y  = cur_y;
    buf.fg = fg;
    const char *p = str;
    while (*p) {
        int r = ssfn_render(&ctx, &buf, p);
        if (r <= 0) break;
        p += r;
    }
    cur_x = buf.x;
    cur_y = buf.y;
}

static void hline(uint32_t *fbp, uint32_t pitch_px, uint32_t half, uint32_t right_w, int y, uint32_t col) {
    for (uint32_t x = 0; x < right_w; x++)
        fbp[y * pitch_px + half + x] = col;
}

int main() {
    // ---- framebuffer ----
    fb_info_t fb;
    if (fbinfo(&fb) != 0) { prints("fbinfo failed\n"); exit(1); }

    uint32_t *fbp      = (uint32_t *)(uintptr_t)fb.addr;
    uint32_t  half     = fb.width / 2;
    uint32_t  right_w  = fb.width - half;
    uint32_t  pitch_px = fb.pitch / 4;

    // clear right half
    for (uint32_t y = 0; y < fb.height; y++)
        for (uint32_t x = 0; x < right_w; x++)
            fbp[y * pitch_px + half + x] = 0xFF12121E;

    // left border line
    for (uint32_t y = 0; y < fb.height; y++)
        fbp[y * pitch_px + half] = 0xFF3333FF;

    // ---- ssfn setup ----
    memset(&ctx, 0, sizeof(ctx));
    if (ssfn_load(&ctx, (const void *)&_binary_FreeSansB_sfn_start) != SSFN_OK) {
        prints("ssfn_load failed\n"); exit(1);
    }

    buf.ptr = (uint8_t *)&fbp[half + 1];
    buf.p   = fb.pitch;
    buf.w   = (int)right_w - 1;
    buf.h   = (int)fb.height;
    buf.bg  = 0;

    cur_x = 8; cur_y = 16;

    // ======== TITLE ========
    put("ZenOS Unicode Test", 0xFFFFD700, 18); // gold, size 18
    nl(18);
    put("FreeSansB \xE2\x80\x94 SSFN2 Renderer", 0xFF888888, 10);
    nl(11);

    // separator
    hline(fbp, pitch_px, half+1, right_w-2, cur_y, 0xFF333355);
    cur_y += 5; cur_x = 8;

    // ======== LATIN ========
    put("Latin", 0xFF00FFCC, 12);
    nl(12);
    put("The quick brown fox jumps", 0xFFFFFFFF, 11);
    nl(11);
    put("over the lazy dog. 0123456789", 0xFFFFFFFF, 11);
    nl(13);

    // ======== GREEK ========
    put("Greek", 0xFF00FFCC, 12);
    nl(12);
    // α β γ δ ε ζ η θ ι κ λ μ ν ξ ο π ρ σ τ υ φ χ ψ ω
    put("\xCE\xB1\xCE\xB2\xCE\xB3\xCE\xB4\xCE\xB5\xCE\xB6\xCE\xB7\xCE\xB8\xCE\xB9\xCE\xBA\xCE\xBB\xCE\xBC\xCE\xBD\xCE\xBE\xCE\xBF\xCF\x80\xCF\x81\xCF\x83\xCF\x84\xCF\x85\xCF\x86\xCF\x87\xCF\x88\xCF\x89", 0xFFFFFFFF, 11);
    nl(13);

    // ======== CYRILLIC ========
    put("Cyrillic", 0xFF00FFCC, 12);
    nl(12);
    // Привет мир
    put("\xD0\x9F\xD1\x80\xD0\xB8\xD0\xB2\xD0\xB5\xD1\x82 \xD0\xBC\xD0\xB8\xD1\x80", 0xFFFFFFFF, 11);
    nl(13);

    // ======== MATH ========
    put("Math", 0xFF00FFCC, 12);
    nl(12);
    // ∞ ∑ ∫ √ ≠ ≤ ≥ ÷ × ±
    put("\xE2\x88\x9E \xE2\x88\x91 \xE2\x88\xAB \xE2\x88\x9A \xE2\x89\xA0 \xE2\x89\xA4 \xE2\x89\xA5 \xC3\xB7 \xC3\x97 \xC2\xB1", 0xFFFFFFFF, 13);
    nl(15);

    // ======== MISC ========
    put("Misc", 0xFF00FFCC, 12);
    nl(12);
    // © ® ™ § ¶ • … † ‡
    put("\xC2\xA9 \xC2\xAE \xE2\x84\xA2 \xC2\xA7 \xC2\xB6 \xE2\x80\xA2 \xE2\x80\xA6 \xE2\x80\xA0 \xE2\x80\xA1", 0xFFFFFFFF, 13);
    nl(15);

    // ======== SIZE DEMO ========
    put("Size Demo", 0xFF00FFCC, 12);
    nl(12);
    put("Aa", 0xFFFFFFFF,  9); cur_x += 6;
    put("Aa", 0xFFFFFFFF, 11); cur_x += 6;
    put("Aa", 0xFFFFFFFF, 14); cur_x += 6;
    put("Aa", 0xFFFFFFFF, 17); cur_x += 6;
    put("Aa", 0xFFFFFFFF, 21);
    nl(24);

    // separator
    hline(fbp, pitch_px, half+1, right_w-2, cur_y, 0xFF333355);
    cur_y += 4; cur_x = 8;

    // ======== FOOTER ========
    put("ZenOS \xE2\x80\x94 OSDev \xE2\x80\x94 ssfn2 \xE2\x80\x94 Unicode \xE2\x9C\x93", 0xFF555577, 9);

    ssfn_free(&ctx);
    prints("unicode_test: done! press any key\n");
    getkey();
    exit(0);
    return 0;
}