#pragma once
#include <stdint.h>

typedef struct font_face font_face_t;
typedef struct {
    int ascent;
    int descent;
    int line_height;
    int max_advance;
} font_metrics_t;

font_face_t *font_load(const char *path);
void         font_free(font_face_t *f);
int          font_draw(font_face_t *f, uint32_t *buf, int buf_w, int buf_h,
                       int x, int y, int size,
                       uint32_t fg, uint32_t bg,
                       const char *text);
int          font_draw_codepoint(font_face_t *f, uint32_t *buf, int buf_w, int buf_h,
                                 int x, int y, int size,
                                 uint32_t fg, uint32_t bg,
                                 uint32_t codepoint);
int          font_measure(font_face_t *f, int size, const char *text);
int          font_get_metrics(font_face_t *f, int size, font_metrics_t *out);
void         font_prime_ascii(font_face_t *f, int size);
