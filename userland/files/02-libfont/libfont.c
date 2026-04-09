#include "libfont.h"
#include <ft2build.h>
#include FT_FREETYPE_H
#include FT_LCD_FILTER_H
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static FT_Library ft_lib = NULL;

#define GLYPH_CACHE_BITS 12
#define GLYPH_CACHE_SIZE (1u << GLYPH_CACHE_BITS)
#define SIZE_CACHE_MAX   8

typedef struct {
    uint8_t   used;
    uint32_t  codepoint;
    uint16_t  size;
    int16_t   bitmap_left;
    int16_t   bitmap_top;
    int16_t   advance_x;
    uint16_t  w;
    uint16_t  h;
    uint16_t  pitch;
    uint8_t  *bitmap;
} cached_glyph_t;

typedef struct {
    uint8_t used;
    int     size;
    int     ascent;
    int     descent;
    int     line_height;
    int     max_advance;
} cached_size_t;

struct font_face {
    FT_Face        face;
    cached_glyph_t slots[GLYPH_CACHE_SIZE];
    cached_size_t  sizes[SIZE_CACHE_MAX];
    int            current_size;
    uint8_t       *font_data;
    size_t         font_size;
};

static void ft_ensure_init(void)
{
    if (!ft_lib) {
        FT_Init_FreeType(&ft_lib);
        FT_Library_SetLcdFilter(ft_lib, FT_LCD_FILTER_DEFAULT);
    }
}

font_face_t *font_load(const char *path)
{
    ft_ensure_init();
    font_face_t *f = calloc(1, sizeof(*f));
    if (!f) return NULL;

    FILE *fp = fopen(path, "rb");
    if (!fp) {
        free(f);
        return NULL;
    }
    if (fseek(fp, 0, SEEK_END) != 0) {
        fclose(fp);
        free(f);
        return NULL;
    }
    long size = ftell(fp);
    if (size <= 0 || fseek(fp, 0, SEEK_SET) != 0) {
        fclose(fp);
        free(f);
        return NULL;
    }
    f->font_size = (size_t)size;
    f->font_data = malloc(f->font_size);
    if (!f->font_data) {
        fclose(fp);
        free(f);
        return NULL;
    }
    if (fread(f->font_data, 1, f->font_size, fp) != f->font_size) {
        fclose(fp);
        free(f->font_data);
        free(f);
        return NULL;
    }
    fclose(fp);

    if (FT_New_Memory_Face(ft_lib, f->font_data, (FT_Long)f->font_size, 0, &f->face)) {
        free(f->font_data);
        free(f);
        return NULL;
    }
    (void)FT_Select_Charmap(f->face, FT_ENCODING_UNICODE);
    return f;
}

void font_free(font_face_t *f)
{
    if (!f) return;
    for (int i = 0; i < GLYPH_CACHE_SIZE; i++)
        free(f->slots[i].bitmap);
    FT_Done_Face(f->face);
    free(f->font_data);
    free(f);
}

static inline uint32_t glyph_hash(uint32_t cp, int size)
{
    uint32_t x = cp ^ ((uint32_t)size * 0x9E3779B1u);
    x ^= x >> 16;
    x *= 0x7FEB352Du;
    x ^= x >> 15;
    x *= 0x846CA68Bu;
    x ^= x >> 16;
    return x;
}

static int ensure_size(font_face_t *f, int size)
{
    if (!f || size <= 0)
        return 0;
    if (f->current_size == size)
        return 1;
    if (FT_Set_Pixel_Sizes(f->face, 0, (FT_UInt)size))
        return 0;
    f->current_size = size;
    return 1;
}

static cached_size_t *get_size_cache(font_face_t *f, int size)
{
    for (int i = 0; i < SIZE_CACHE_MAX; i++) {
        if (f->sizes[i].used && f->sizes[i].size == size)
            return &f->sizes[i];
    }
    return NULL;
}

static cached_size_t *cache_size(font_face_t *f, int size)
{
    cached_size_t *slot = get_size_cache(f, size);
    if (slot)
        return slot;
    if (!ensure_size(f, size))
        return NULL;

    int best = 0;
    for (int i = 0; i < SIZE_CACHE_MAX; i++) {
        if (!f->sizes[i].used) {
            best = i;
            break;
        }
    }

    FT_Size_Metrics *m = &f->face->size->metrics;
    slot = &f->sizes[best];
    slot->used        = 1;
    slot->size        = size;
    slot->ascent      = (int)((m->ascender + 63) >> 6);
    slot->descent     = (int)((-m->descender + 63) >> 6);
    slot->line_height = (int)((m->height + 63) >> 6);
    slot->max_advance = (int)((m->max_advance + 63) >> 6);
    if (slot->line_height <= 0)
        slot->line_height = size;
    if (slot->max_advance <= 0)
        slot->max_advance = size;
    return slot;
}

static cached_glyph_t *cache_lookup(font_face_t *f, uint32_t cp, int size)
{
    uint32_t idx = glyph_hash(cp, size) & (GLYPH_CACHE_SIZE - 1);
    for (uint32_t probe = 0; probe < GLYPH_CACHE_SIZE; probe++) {
        cached_glyph_t *slot = &f->slots[(idx + probe) & (GLYPH_CACHE_SIZE - 1)];
        if (!slot->used)
            return NULL;
        if (slot->codepoint == cp && slot->size == (uint16_t)size)
            return slot;
    }
    return NULL;
}

static cached_glyph_t *cache_insert(font_face_t *f, uint32_t cp, int size)
{
    if (!ensure_size(f, size))
        return NULL;
    if (FT_Load_Char(f->face, cp, FT_LOAD_RENDER | FT_LOAD_TARGET_LIGHT))
        return NULL;

    uint32_t idx = glyph_hash(cp, size) & (GLYPH_CACHE_SIZE - 1);
    cached_glyph_t *slot = NULL;
    for (uint32_t probe = 0; probe < GLYPH_CACHE_SIZE; probe++) {
        cached_glyph_t *cur = &f->slots[(idx + probe) & (GLYPH_CACHE_SIZE - 1)];
        if (!cur->used) {
            slot = cur;
            break;
        }
    }
    if (!slot)
        slot = &f->slots[idx];

    free(slot->bitmap);
    memset(slot, 0, sizeof(*slot));

    FT_GlyphSlot gs = f->face->glyph;
    FT_Bitmap   *bmp = &gs->bitmap;
    slot->used        = 1;
    slot->codepoint   = cp;
    slot->size        = (uint16_t)size;
    slot->bitmap_left = (int16_t)gs->bitmap_left;
    slot->bitmap_top  = (int16_t)gs->bitmap_top;
    slot->advance_x   = (int16_t)(gs->advance.x >> 6);
    slot->w           = (uint16_t)bmp->width;
    slot->h           = (uint16_t)bmp->rows;
    slot->pitch       = (uint16_t)abs(bmp->pitch);

    int bytes = (int)slot->pitch * (int)slot->h;
    if (bytes > 0) {
        slot->bitmap = malloc((size_t)bytes);
        if (!slot->bitmap) {
            memset(slot, 0, sizeof(*slot));
            return NULL;
        }
        memcpy(slot->bitmap, bmp->buffer, (size_t)bytes);
    }
    return slot;
}

static inline cached_glyph_t *get_glyph(font_face_t *f, uint32_t cp, int size)
{
    cached_glyph_t *g = cache_lookup(f, cp, size);
    return g ? g : cache_insert(f, cp, size);
}

static void blit_cached(cached_glyph_t *cg, int gx, int gy,
                         uint32_t *buf, int bw, int bh,
                         uint32_t fg, uint32_t bg)
{
    (void)bg;
    if (!cg->bitmap || cg->w <= 0 || cg->h <= 0) return;
    uint32_t fg_rb = fg & 0x00FF00FFu;
    uint32_t fg_g  = fg & 0x0000FF00u;

    int row_start = gy < 0 ? -gy : 0;
    int row_end   = cg->h;
    if (gy + row_end > bh) row_end = bh - gy;

    for (int row = row_start; row < row_end; row++) {
        int py = gy + row;
        uint8_t  *src = cg->bitmap + row * cg->pitch;
        uint32_t *dst = buf + py * bw;

        int col_start = gx < 0 ? -gx : 0;
        int col_end   = cg->w;
        if (gx + col_end > bw) col_end = bw - gx;

        for (int col = col_start; col < col_end; col++) {
            uint8_t a = src[col];
            if (a == 0) continue;
            int px = gx + col;
            if (a == 255) {
                dst[px] = 0xFF000000 | fg;
                continue;
            }
            uint32_t db = dst[px];
            uint32_t ia = 255u - (uint32_t)a;
            uint32_t rb = ((((db & 0x00FF00FFu) * ia) + (fg_rb * a)) >> 8) & 0x00FF00FFu;
            uint32_t g  = ((((db & 0x0000FF00u) * ia) + (fg_g  * a)) >> 8) & 0x0000FF00u;
            dst[px] = 0xFF000000u | rb | g;
        }
    }
}

int font_draw(font_face_t *f, uint32_t *buf, int buf_w, int buf_h,
              int x, int y, int size,
              uint32_t fg, uint32_t bg,
              const char *text)
{
    if (!f || !text) return x;
    int pen_x = x;
    for (const unsigned char *s = (const unsigned char *)text; *s; s++) {
        cached_glyph_t *cg = get_glyph(f, *s, size);
        if (!cg) continue;
        blit_cached(cg,
                    pen_x + cg->bitmap_left,
                    y     - cg->bitmap_top,
                    buf, buf_w, buf_h, fg, bg);
        pen_x += cg->advance_x;
    }
    return pen_x;
}

int font_draw_codepoint(font_face_t *f, uint32_t *buf, int buf_w, int buf_h,
                        int x, int y, int size,
                        uint32_t fg, uint32_t bg,
                        uint32_t codepoint)
{
    if (!f) return x;
    cached_glyph_t *cg = get_glyph(f, codepoint, size);
    if (!cg) return x;
    blit_cached(cg,
                x + cg->bitmap_left,
                y - cg->bitmap_top,
                buf, buf_w, buf_h, fg, bg);
    return x + cg->advance_x;
}

int font_measure(font_face_t *f, int size, const char *text)
{
    if (!f || !text) return 0;
    int w = 0;
    for (const unsigned char *s = (const unsigned char *)text; *s; s++) {
        cached_glyph_t *cg = get_glyph(f, *s, size);
        if (cg) w += cg->advance_x;
    }
    return w;
}

int font_get_metrics(font_face_t *f, int size, font_metrics_t *out)
{
    if (!f || !out || size <= 0)
        return 0;
    cached_size_t *m = cache_size(f, size);
    if (!m)
        return 0;
    out->ascent      = m->ascent;
    out->descent     = m->descent;
    out->line_height = m->line_height;
    out->max_advance = m->max_advance;
    return 1;
}

void font_prime_ascii(font_face_t *f, int size)
{
    if (!f || size <= 0)
        return;
    (void)cache_size(f, size);
    for (uint32_t cp = 32; cp < 127; cp++)
        (void)get_glyph(f, cp, size);
}
