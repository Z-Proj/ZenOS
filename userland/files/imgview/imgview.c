#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <unistd.h>

#define STB_IMAGE_WRITE_IMPLEMENTATION
#define STB_IMAGE_IMPLEMENTATION
#define STB_IMAGE_RESIZE_IMPLEMENTATION

#include "../../include/harp_api.h"
#include "stb_image.h"
#include "stb_image_resize2.h"
#include "../harp/stb_image_write.h"

#define VIEW_W 640
#define MAX_FILES 256
#define MAX_PATH_LEN 256

typedef struct {
    harp_window_t *win;
    char file_names[MAX_FILES][MAX_PATH_LEN];
    int file_count;
    int file_index;
    uint8_t *orig_pixels;
    int orig_w;
    int orig_h;
    uint8_t *view_pixels;
    int view_w;
    int view_h;
    int win_w;
    int win_h;
    int dirty;
    int close_req;
} viewer_t;

static char lower_ascii(char c)
{
    if (c >= 'A' && c <= 'Z')
        return (char)(c - 'A' + 'a');
    return c;
}

static int str_ieq(const char *a, const char *b)
{
    while (*a && *b) {
        if (lower_ascii(*a) != lower_ascii(*b))
            return 0;
        a++;
        b++;
    }
    return *a == 0 && *b == 0;
}

static const char *path_basename(const char *path)
{
    const char *base = path;
    while (*path) {
        if (*path == '/' || *path == '\\')
            base = path + 1;
        path++;
    }
    return base;
}

static const char *path_ext(const char *path)
{
    const char *dot = NULL;
    while (*path) {
        if (*path == '/' || *path == '\\')
            dot = NULL;
        else if (*path == '.')
            dot = path + 1;
        path++;
    }
    return dot;
}

static int load_ext_supported(const char *path)
{
    const char *ext = path_ext(path);
    if (!ext)
        return 0;
    return str_ieq(ext, "jpg") || str_ieq(ext, "jpeg") ||
           str_ieq(ext, "png") || str_ieq(ext, "bmp") ||
           str_ieq(ext, "psd") || str_ieq(ext, "tga") ||
           str_ieq(ext, "gif") || str_ieq(ext, "hdr") ||
           str_ieq(ext, "pic") || str_ieq(ext, "pnm") ||
           str_ieq(ext, "ppm") || str_ieq(ext, "pgm");
}

static int save_ext_supported(const char *ext)
{
    if (!ext)
        return 0;
    return str_ieq(ext, "png") || str_ieq(ext, "bmp") || str_ieq(ext, "tga") ||
           str_ieq(ext, "jpg") || str_ieq(ext, "jpeg") || str_ieq(ext, "hdr");
}

static uint8_t *load_rgba_file(const char *path, int *w, int *h)
{
    int comp = 0;
    if (stbi_is_hdr(path)) {
        float *hdr = stbi_loadf(path, w, h, &comp, 4);
        if (!hdr)
            return NULL;
        size_t count = (size_t)(*w) * (size_t)(*h);
        uint8_t *rgba = (uint8_t *)malloc(count * 4);
        if (!rgba) {
            stbi_image_free(hdr);
            return NULL;
        }
        for (size_t i = 0; i < count; i++) {
            float r = hdr[i * 4 + 0];
            float g = hdr[i * 4 + 1];
            float b = hdr[i * 4 + 2];
            float a = hdr[i * 4 + 3];
            if (r < 0.0f) r = 0.0f; if (r > 1.0f) r = 1.0f;
            if (g < 0.0f) g = 0.0f; if (g > 1.0f) g = 1.0f;
            if (b < 0.0f) b = 0.0f; if (b > 1.0f) b = 1.0f;
            if (a < 0.0f) a = 0.0f; if (a > 1.0f) a = 1.0f;
            rgba[i * 4 + 0] = (uint8_t)(r * 255.0f + 0.5f);
            rgba[i * 4 + 1] = (uint8_t)(g * 255.0f + 0.5f);
            rgba[i * 4 + 2] = (uint8_t)(b * 255.0f + 0.5f);
            rgba[i * 4 + 3] = (uint8_t)(a * 255.0f + 0.5f);
        }
        stbi_image_free(hdr);
        return rgba;
    }
    return stbi_load(path, w, h, &comp, 4);
}

static int load_image_dims(const char *path, int *w, int *h)
{
    int comp = 0;
    if (stbi_info(path, w, h, &comp))
        return 1;
    if (stbi_is_hdr(path)) {
        float *hdr = stbi_loadf(path, w, h, &comp, 0);
        if (!hdr)
            return 0;
        stbi_image_free(hdr);
        return 1;
    }
    return 0;
}

static void free_viewer_image(viewer_t *v)
{
    if (v->orig_pixels) {
        stbi_image_free(v->orig_pixels);
        v->orig_pixels = NULL;
    }
    if (v->view_pixels) {
        free(v->view_pixels);
        v->view_pixels = NULL;
    }
    v->orig_w = 0;
    v->orig_h = 0;
    v->view_w = 0;
    v->view_h = 0;
}

static void sort_files(viewer_t *v)
{
    for (int i = 0; i < v->file_count; i++) {
        for (int j = i + 1; j < v->file_count; j++) {
            if (strcmp(v->file_names[i], v->file_names[j]) > 0) {
                char tmp[MAX_PATH_LEN];
                memcpy(tmp, v->file_names[i], sizeof(tmp));
                memcpy(v->file_names[i], v->file_names[j], sizeof(tmp));
                memcpy(v->file_names[j], tmp, sizeof(tmp));
            }
        }
    }
}

static void add_listed_file(viewer_t *v, const char *name, size_t len)
{
    if (v->file_count >= MAX_FILES || len == 0)
        return;
    if (len >= MAX_PATH_LEN)
        len = MAX_PATH_LEN - 1;
    memcpy(v->file_names[v->file_count], name, len);
    v->file_names[v->file_count][len] = 0;
    if (!load_ext_supported(v->file_names[v->file_count]))
        return;
    v->file_count++;
}

static int scan_current_directory(viewer_t *v)
{
    v->file_count = 0;
    char *listing = (char *)malloc(16384);
    if (!listing)
        return 0;
    if (zen_ls(listing, 16384) != 0) {
        free(listing);
        return 0;
    }

    char *line = listing;
    while (*line) {
        char *next = strchr(line, '\n');
        size_t len = next ? (size_t)(next - line) : strlen(line);
        const char *start = line;
        const char *end = line + len;

        if (len != 0 &&
            strncmp(start, "Directory: ", 11) != 0 &&
            strcmp(start, "(empty)") != 0 &&
            strncmp(start, "[DIR]  ", 7) != 0)
        {
            while (start < end && (*start == ' ' || *start == '\t'))
                start++;
            const char *suffix = strstr(start, "  (");
            if (suffix && suffix < end)
                end = suffix;
            while (end > start && (end[-1] == ' ' || end[-1] == '\t'))
                end--;
            if (start < end && start[0] != '.')
                add_listed_file(v, start, (size_t)(end - start));
        }

        if (!next)
            break;
        line = next + 1;
    }

    free(listing);
    sort_files(v);
    return v->file_count > 0;
}

static int save_rgba_file(const char *path, const uint8_t *rgba, int w, int h)
{
    const char *ext = path_ext(path);
    if (!save_ext_supported(ext))
        return 0;
    if (str_ieq(ext, "png"))
        return stbi_write_png(path, w, h, 4, rgba, w * 4);
    if (str_ieq(ext, "bmp"))
        return stbi_write_bmp(path, w, h, 4, rgba);
    if (str_ieq(ext, "tga"))
        return stbi_write_tga(path, w, h, 4, rgba);
    if (str_ieq(ext, "jpg") || str_ieq(ext, "jpeg"))
        return stbi_write_jpg(path, w, h, 4, rgba, 90);
    if (str_ieq(ext, "hdr")) {
        float *hdr = (float *)malloc((size_t)w * (size_t)h * 3 * sizeof(float));
        if (!hdr)
            return 0;
        for (size_t i = 0, n = (size_t)w * (size_t)h; i < n; i++) {
            hdr[i * 3 + 0] = rgba[i * 4 + 0] / 255.0f;
            hdr[i * 3 + 1] = rgba[i * 4 + 1] / 255.0f;
            hdr[i * 3 + 2] = rgba[i * 4 + 2] / 255.0f;
        }
        int ok = stbi_write_hdr(path, w, h, 3, hdr);
        free(hdr);
        return ok;
    }
    return 0;
}

static int max_window_height(viewer_t *v)
{
    int max_h = 1;
    for (int i = 0; i < v->file_count; i++) {
        int w = 0;
        int h = 0;
        if (!load_image_dims(v->file_names[i], &w, &h) || w <= 0 || h <= 0)
            continue;
        int view_h = (int)((int64_t)h * VIEW_W / w);
        if (view_h > max_h)
            max_h = view_h;
    }
    return max_h;
}

static int convert_file(const char *input, const char *output)
{
    if (!load_ext_supported(input)) {
        fprintf(stderr, "imgview: unsupported input: %s\n", input);
        return 1;
    }
    if (!save_ext_supported(path_ext(output))) {
        fprintf(stderr, "imgview: unsupported output format: %s\n", output);
        return 1;
    }
    int w = 0;
    int h = 0;
    uint8_t *rgba = load_rgba_file(input, &w, &h);
    if (!rgba) {
        fprintf(stderr, "imgview: failed to load: %s\n", input);
        return 1;
    }
    int ok = save_rgba_file(output, rgba, w, h);
    stbi_image_free(rgba);
    if (!ok) {
        fprintf(stderr, "imgview: failed to save: %s\n", output);
        return 1;
    }
    return 0;
}

static int rename_file_cli(const char *old_path, const char *new_path)
{
    if (rename(old_path, new_path) != 0) {
        fprintf(stderr, "imgview: rename failed: %s -> %s\n", old_path, new_path);
        return 1;
    }
    return 0;
}

static int open_window_for_image(viewer_t *v, const char *title)
{
    if (v->win) {
        harp_retitle(v->win, title);
        v->dirty = 1;
        return 1;
    }
    v->win = harp_open(title, 120, 70, v->win_w, v->win_h);
    if (!v->win)
        return 0;
    v->dirty = 1;
    return 1;
}

static int load_viewer_image(viewer_t *v, int index)
{
    if (index < 0 || index >= v->file_count)
        return 0;
    int w = 0;
    int h = 0;
    uint8_t *orig = load_rgba_file(v->file_names[index], &w, &h);
    if (!orig)
        return 0;

    int view_h = (int)((int64_t)h * VIEW_W / (w > 0 ? w : 1));
    if (view_h < 1)
        view_h = 1;
    uint8_t *view = (uint8_t *)malloc((size_t)VIEW_W * (size_t)view_h * 4);
    if (!view) {
        stbi_image_free(orig);
        return 0;
    }

    if (w == VIEW_W && h == view_h)
        memcpy(view, orig, (size_t)w * (size_t)h * 4);
    else if (!stbir_resize_uint8_linear(orig, w, h, 0, view, VIEW_W, view_h, 0, STBIR_RGBA)) {
        free(view);
        stbi_image_free(orig);
        return 0;
    }

    free_viewer_image(v);
    v->orig_pixels = orig;
    v->orig_w = w;
    v->orig_h = h;
    v->view_pixels = view;
    v->view_w = VIEW_W;
    v->view_h = view_h;
    v->file_index = index;
    return open_window_for_image(v, path_basename(v->file_names[index]));
}

static uint32_t blend_rgba_over(uint32_t dst, const uint8_t *src)
{
    uint32_t a = src[3];
    uint32_t sr = src[0];
    uint32_t sg = src[1];
    uint32_t sb = src[2];
    if (a == 255)
        return 0xFF000000U | (sr << 16) | (sg << 8) | sb;
    if (a == 0)
        return dst;
    uint32_t dr = (dst >> 16) & 0xFF;
    uint32_t dg = (dst >> 8) & 0xFF;
    uint32_t db = dst & 0xFF;
    uint32_t ia = 255 - a;
    uint32_t r = (sr * a + dr * ia) / 255;
    uint32_t g = (sg * a + dg * ia) / 255;
    uint32_t b = (sb * a + db * ia) / 255;
    return 0xFF000000U | (r << 16) | (g << 8) | b;
}

static void render_viewer(viewer_t *v)
{
    if (!v->win || !v->view_pixels)
        return;
    for (int i = 0; i < v->win->w * v->win->h; i++)
        v->win->buf[i] = 0xFF000000U;
    for (int y = 0; y < v->view_h; y++) {
        uint32_t *dst = v->win->buf + y * v->win->w;
        const uint8_t *src = v->view_pixels + (size_t)y * (size_t)v->view_w * 4;
        for (int x = 0; x < v->view_w; x++)
            dst[x] = blend_rgba_over(0xFF000000U, src + x * 4);
    }
    harp_flush(v->win);
    v->dirty = 0;
}

static int try_load_direction(viewer_t *v, int delta)
{
    if (v->file_count <= 0)
        return 0;
    int start = v->file_index;
    int index = start;
    for (int tries = 0; tries < v->file_count; tries++) {
        index += delta;
        if (index < 0)
            index = v->file_count - 1;
        else if (index >= v->file_count)
            index = 0;
        if (load_viewer_image(v, index))
            return 1;
    }
    return 0;
}

static void handle_viewer_events(viewer_t *v)
{
    if (!v->win)
        return;
    harp_event_t ev;
    while (harp_poll_event(v->win, &ev)) {
        if (ev.type == HARP_EVENT_CLOSE_REQ) {
            v->close_req = 1;
        } else if (ev.type == HARP_EVENT_EXPOSE) {
            v->dirty = 1;
        } else if (ev.type == HARP_EVENT_KEY && ev.value != 0) {
            if (ev.key == KEY_ARROW_LEFT)
                try_load_direction(v, -1);
            else if (ev.key == KEY_ARROW_RIGHT)
                try_load_direction(v, 1);
        }
    }
}

static int run_viewer(const char *requested)
{
    viewer_t v;
    memset(&v, 0, sizeof(v));

    if (!scan_current_directory(&v)) {
        fprintf(stderr, "imgview: no supported images in current directory\n");
        return 1;
    }
    v.win_w = VIEW_W;
    v.win_h = max_window_height(&v);
    if (v.win_h < 1)
        v.win_h = 1;

    int start_index = 0;
    if (requested) {
        const char *base = path_basename(requested);
        for (int i = 0; i < v.file_count; i++) {
            if (strcmp(v.file_names[i], base) == 0) {
                start_index = i;
                break;
            }
        }
        if (!load_ext_supported(requested)) {
            fprintf(stderr, "imgview: unsupported image: %s\n", requested);
            return 1;
        }
    }

    if (!load_viewer_image(&v, start_index) && !try_load_direction(&v, 1)) {
        fprintf(stderr, "imgview: failed to load image\n");
        return 1;
    }

    while (!v.close_req) {
        handle_viewer_events(&v);
        if (v.dirty)
            render_viewer(&v);
        sched_yield();
    }

    free_viewer_image(&v);
    if (v.win)
        harp_close(v.win);
    return 0;
}

static void print_usage(void)
{
    fputs("usage:\n", stderr);
    fputs("  imgview [image]\n", stderr);
    fputs("  imgview --save-as <input> <output>\n", stderr);
    fputs("  imgview --convert <input> <output>\n", stderr);
    fputs("  imgview --rename <old> <new>\n", stderr);
}

int main(int argc, char **argv)
{
    if (argc == 1)
        return run_viewer(NULL);
    if (argc == 2 && strcmp(argv[1], "--help") == 0) {
        print_usage();
        return 0;
    }
    if (argc == 4 && strcmp(argv[1], "--save-as") == 0)
        return convert_file(argv[2], argv[3]);
    if (argc == 4 && strcmp(argv[1], "--convert") == 0)
        return convert_file(argv[2], argv[3]);
    if (argc == 4 && strcmp(argv[1], "--rename") == 0)
        return rename_file_cli(argv[2], argv[3]);
    if (argc == 2)
        return run_viewer(argv[1]);
    print_usage();
    return 1;
}
