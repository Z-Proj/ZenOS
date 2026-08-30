/**
 *
 * @file : settings.c
 * @brief : ZenOS Settings - Nuklear app
 *          for managing boot apps, the Harp wallpaper + dock, power, and
 *          basic system info.
 *
 * MIT License
 * 
 * Copyright (c) 2026 Rishies2010
 * 
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 * 
 * The above copyright notice and this permission notice shall be included in all
 * copies or substantial portions of the Software.
 * 
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 * SOFTWARE.
 * 
 * @author : Rishies2010
 * @copyright (c) 2026
 * 
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <sys/stat.h>
#include <sys/sysinfo.h>

#include "../../userlib.h"
#include "nk_harp.h"
#include "stb_image.h"
#include "stb_image_resize2.h"

#define WIN_W 800
#define WIN_H 600
#define NAV_W 150

#define DRIVE_ROOT "/mnt/drv0"
#define BOOT_PATH DRIVE_ROOT "/sys/init.run"
#define HARP_DOCK_CFG DRIVE_ROOT "/lib/harp/harp.cfg"
#define HARP_BG_CFG DRIVE_ROOT "/lib/harp/wallpaper.cfg"
#define HARP_BG_DEFAULT DRIVE_ROOT "/lib/harp/bg.png"

#define MAX_BOOT 40
#define MAX_DOCK 8
#define THUMB_W 240
#define THUMB_H 135

static char *trim(char *s)
{
    while (*s == ' ' || *s == '\t')
        s++;
    size_t n = strlen(s);
    while (n > 0 && (s[n - 1] == '\n' || s[n - 1] == '\r' ||
                     s[n - 1] == ' ' || s[n - 1] == '\t'))
        s[--n] = 0;
    return s;
}

static int hex_nibble(char c)
{
    if (c >= '0' && c <= '9')
        return c - '0';
    if (c >= 'a' && c <= 'f')
        return c - 'a' + 10;
    if (c >= 'A' && c <= 'F')
        return c - 'A' + 10;
    return -1;
}

static int parse_hex_color(const char *s, uint32_t *out)
{
    if (!s || !*s)
        return 0;
    if (s[0] == '0' && (s[1] == 'x' || s[1] == 'X'))
        s += 2;
    if (*s == '#')
        s++;
    uint32_t value = 0;
    int digits = 0;
    while (*s)
    {
        int v = hex_nibble(*s++);
        if (v < 0)
            return 0;
        value = (value << 4) | (uint32_t)v;
        digits++;
    }
    if (digits == 6)
    {
        *out = 0xFF000000u | value;
        return 1;
    }
    if (digits == 8)
    {
        *out = value;
        return 1;
    }
    return 0;
}

static struct nk_color u32_to_nkcolor(uint32_t c)
{
    return nk_rgb((c >> 16) & 0xFF, (c >> 8) & 0xFF, c & 0xFF);
}

static void ensure_lib_harp_dir(void)
{
    mkdir(DRIVE_ROOT "/lib", 0755);
    mkdir(DRIVE_ROOT "/lib/harp", 0755);
}

static void clamp_text_len(char *s, int *len, size_t cap)
{
    if (!s || !len || cap == 0)
        return;
    if (*len < 0)
        *len = 0;
    if ((size_t)*len >= cap)
        *len = (int)cap - 1;
    s[*len] = 0;
    *len = (int)strlen(s);
}

static void clean_cfg_field(char *s, int *len, size_t cap, int allow_semicolon)
{
    clamp_text_len(s, len, cap);
    for (int i = 0; s[i]; i++)
    {
        if (s[i] == '\r' || s[i] == '\n' || (!allow_semicolon && s[i] == ';'))
            s[i] = ' ';
    }
    char *p = trim(s);
    if (p != s)
        memmove(s, p, strlen(p) + 1);
    *len = (int)strlen(s);
}

enum
{
    PAGE_BOOT = 0,
    PAGE_WALLPAPER,
    PAGE_DOCK,
    PAGE_POWER,
    PAGE_ABOUT,
    PAGE_COUNT
};

static const char *page_names[PAGE_COUNT] = {
    "Boot Apps", "Wallpaper", "Dock", "Power", "About PC"};

static int page = PAGE_BOOT;
static int about_redraw_tick = 0;

typedef struct
{
    char text[220];
    int len;
    int enabled;
    int is_comment;
} boot_entry_t;

static boot_entry_t boot_entries[MAX_BOOT];
static int boot_count = 0;
static char boot_status[80] = "";

static void load_boot(void)
{
    boot_count = 0;
    FILE *fp = fopen(BOOT_PATH, "r");
    if (!fp)
    {
        snprintf(boot_status, sizeof(boot_status), "No init.run found yet.");
        return;
    }

    char line[256];
    while (boot_count < MAX_BOOT && fgets(line, sizeof(line), fp))
    {
        char *p = trim(line);
        if (!*p)
            continue;

        boot_entry_t *e = &boot_entries[boot_count];
        memset(e, 0, sizeof(*e));

        if (*p == '#')
        {
            p++;
            while (*p == ' ' || *p == '\t')
                p++;
            if (*p == '/' || *p == '!')
            {
                e->enabled = 0;
                e->is_comment = 0;
                strncpy(e->text, p, sizeof(e->text) - 1);
            }
            else
            {
                e->enabled = 1;
                e->is_comment = 1;
                strncpy(e->text, p, sizeof(e->text) - 1);
            }
        }
        else
        {
            e->enabled = 1;
            e->is_comment = 0;
            strncpy(e->text, p, sizeof(e->text) - 1);
        }
        e->len = (int)strlen(e->text);
        boot_count++;
    }
    fclose(fp);
    snprintf(boot_status, sizeof(boot_status), "Loaded %d entries.", boot_count);
}

static void save_boot(void)
{
    mkdir(DRIVE_ROOT "/sys", 0755);
    FILE *fp = fopen(BOOT_PATH, "w");
    if (!fp)
    {
        snprintf(boot_status, sizeof(boot_status), "Save failed.");
        return;
    }
    for (int i = 0; i < boot_count; i++)
    {
        boot_entry_t *e = &boot_entries[i];
        e->text[e->len] = 0;
        if (e->is_comment)
            fprintf(fp, "# %s\n", e->text);
        else if (e->enabled)
            fprintf(fp, "%s\n", e->text);
        else
            fprintf(fp, "# %s\n", e->text);
    }
    fclose(fp);
    snprintf(boot_status, sizeof(boot_status), "Saved. Applies on reboot.");
}

static void render_boot(nk_harp_t *nh)
{
    struct nk_context *ctx = &nh->ctx;

    nk_layout_row_dynamic(ctx, 20, 1);
    nk_label(ctx, "Startup Applications (/sys/init.run)", NK_TEXT_LEFT);
    nk_layout_row_dynamic(ctx, 16, 1);
    nk_label_colored(ctx, "Order matters. Unchecked entries are saved disabled.",
                     NK_TEXT_LEFT, nk_rgb(150, 150, 165));

    nk_layout_row_dynamic(ctx, 4, 1);
    nk_spacing(ctx, 1);

    int act_up = -1, act_down = -1, act_remove = -1;

    nk_layout_row_dynamic(ctx, 230, 1);
    if (nk_group_begin(ctx, "boot_list", NK_WINDOW_BORDER))
    {
        for (int i = 0; i < boot_count; i++)
        {
            boot_entry_t *e = &boot_entries[i];
            nk_layout_row_template_begin(ctx, 26);
            nk_layout_row_template_push_static(ctx, 22);
            nk_layout_row_template_push_static(ctx, 22);
            nk_layout_row_template_push_static(ctx, 22);
            nk_layout_row_template_push_dynamic(ctx);
            nk_layout_row_template_push_static(ctx, 22);
            nk_layout_row_template_end(ctx);

            if (nk_button_symbol(ctx, NK_SYMBOL_TRIANGLE_UP) && i > 0)
                act_up = i;
            if (nk_button_symbol(ctx, NK_SYMBOL_TRIANGLE_DOWN) && i < boot_count - 1)
                act_down = i;
            if (e->is_comment)
                nk_label(ctx, "#", NK_TEXT_CENTERED);
            else
                nk_checkbox_label(ctx, "", &e->enabled);
            nk_edit_string(ctx, NK_EDIT_FIELD, e->text, &e->len,
                           (int)sizeof(e->text) - 1, nk_filter_default);
            if (nk_button_symbol(ctx, NK_SYMBOL_X))
                act_remove = i;
        }
        nk_group_end(ctx);
    }

    if (act_up >= 0)
    {
        boot_entry_t tmp = boot_entries[act_up];
        boot_entries[act_up] = boot_entries[act_up - 1];
        boot_entries[act_up - 1] = tmp;
    }
    else if (act_down >= 0)
    {
        boot_entry_t tmp = boot_entries[act_down];
        boot_entries[act_down] = boot_entries[act_down + 1];
        boot_entries[act_down + 1] = tmp;
    }
    else if (act_remove >= 0)
    {
        for (int i = act_remove; i < boot_count - 1; i++)
            boot_entries[i] = boot_entries[i + 1];
        boot_count--;
    }

    nk_layout_row_dynamic(ctx, 4, 1);
    nk_spacing(ctx, 1);

    nk_layout_row_template_begin(ctx, 26);
    nk_layout_row_template_push_static(ctx, 140);
    nk_layout_row_template_push_static(ctx, 100);
    nk_layout_row_template_push_dynamic(ctx);
    nk_layout_row_template_end(ctx);

    if (nk_button_label(ctx, "+ Add Entry") && boot_count < MAX_BOOT)
    {
        boot_entry_t *e = &boot_entries[boot_count];
        memset(e, 0, sizeof(*e));
        strncpy(e->text, DRIVE_ROOT "/bin/", sizeof(e->text) - 1);
        e->len = (int)strlen(e->text);
        e->enabled = 1;
        boot_count++;
    }
    if (nk_button_label(ctx, "Save"))
        save_boot();
    nk_label(ctx, boot_status, NK_TEXT_LEFT);
}

static char wall_path[128] = HARP_BG_DEFAULT;
static int wall_path_len = (int)sizeof(HARP_BG_DEFAULT) - 1;
static char wall_status[100] = "";

static uint32_t thumb_pixels[THUMB_W * THUMB_H];
static int thumb_valid = 0;

static struct nk_rect g_preview_rect;
static int g_preview_show = 0;

static void generate_thumbnail(const char *path)
{
    thumb_valid = 0;
    int iw = 0, ih = 0, comp = 0;
    uint8_t *img = stbi_load(path, &iw, &ih, &comp, 4);
    if (!img)
        return;

    uint8_t *resized = (uint8_t *)malloc((size_t)THUMB_W * THUMB_H * 4);
    if (!resized)
    {
        stbi_image_free(img);
        return;
    }

    if (!stbir_resize_uint8_linear(img, iw, ih, 0, resized, THUMB_W, THUMB_H, 0, STBIR_RGBA))
    {
        stbi_image_free(img);
        free(resized);
        return;
    }
    stbi_image_free(img);

    for (int i = 0; i < THUMB_W * THUMB_H; i++)
    {
        uint8_t *p = resized + (size_t)i * 4;
        thumb_pixels[i] = 0xFF000000u | ((uint32_t)p[0] << 16) | ((uint32_t)p[1] << 8) | p[2];
    }
    free(resized);
    thumb_valid = 1;
}

static void load_wall(void)
{
    strncpy(wall_path, HARP_BG_DEFAULT, sizeof(wall_path) - 1);
    FILE *fp = fopen(HARP_BG_CFG, "r");
    if (fp)
    {
        char line[128];
        if (fgets(line, sizeof(line), fp))
        {
            char *p = trim(line);
            if (*p)
                strncpy(wall_path, p, sizeof(wall_path) - 1);
        }
        fclose(fp);
    }
    wall_path[sizeof(wall_path) - 1] = 0;
    wall_path_len = (int)strlen(wall_path);
}

static void apply_wallpaper(nk_harp_t *nh)
{
    wall_path[wall_path_len] = 0;
    ensure_lib_harp_dir();
    FILE *fp = fopen(HARP_BG_CFG, "w");
    if (!fp)
    {
        snprintf(wall_status, sizeof(wall_status), "Failed to save wallpaper config.");
        return;
    }
    fprintf(fp, "%s\n", wall_path);
    fclose(fp);

    generate_thumbnail(wall_path);
    harp_reload_wallpaper(nh->win);

    if (thumb_valid)
        snprintf(wall_status, sizeof(wall_status), "Applied.");
    else
        snprintf(wall_status, sizeof(wall_status),
                 "Image couldn't be read. Applied fallback background.");
}

static void render_wallpaper(nk_harp_t *nh)
{
    struct nk_context *ctx = &nh->ctx;

    nk_layout_row_dynamic(ctx, 20, 1);
    nk_label(ctx, "Desktop Wallpaper", NK_TEXT_LEFT);
    nk_layout_row_dynamic(ctx, 4, 1);
    nk_spacing(ctx, 1);

    nk_layout_row_template_begin(ctx, 26);
    nk_layout_row_template_push_static(ctx, 50);
    nk_layout_row_template_push_dynamic(ctx);
    nk_layout_row_template_end(ctx);
    nk_label(ctx, "Path", NK_TEXT_LEFT);
    nk_edit_string(ctx, NK_EDIT_FIELD, wall_path, &wall_path_len,
                   (int)sizeof(wall_path) - 1, nk_filter_default);

    nk_layout_row_dynamic(ctx, 6, 1);
    nk_spacing(ctx, 1);

    nk_layout_row_static(ctx, 26, 130, 2);
    if (nk_button_label(ctx, "Preview"))
    {
        wall_path[wall_path_len] = 0;
        generate_thumbnail(wall_path);
        if (!thumb_valid)
            snprintf(wall_status, sizeof(wall_status), "Couldn't read image - check the path.");
        else
            snprintf(wall_status, sizeof(wall_status), "Preview updated.");
    }
    if (nk_button_label(ctx, "Apply & Reload"))
        apply_wallpaper(nh);

    nk_layout_row_dynamic(ctx, 4, 1);
    nk_spacing(ctx, 1);
    nk_layout_row_dynamic(ctx, 16, 1);
    nk_label_colored(ctx, wall_status, NK_TEXT_LEFT, nk_rgb(150, 200, 150));

    nk_layout_row_dynamic(ctx, 6, 1);
    nk_spacing(ctx, 1);

    if (thumb_valid)
    {
        nk_layout_row_static(ctx, THUMB_H, THUMB_W, 1);
        struct nk_rect b;
        if (nk_widget(&b, ctx) != NK_WIDGET_INVALID)
        {
            g_preview_rect = b;
            g_preview_show = 1;
        }
    }
    else
    {
        nk_layout_row_dynamic(ctx, 18, 1);
        nk_label_colored(ctx, "No preview available for this path.", NK_TEXT_LEFT, nk_rgb(200, 140, 140));
    }
}

typedef struct
{
    char path[128];
    int path_len;
    char color[16];
    int color_len;
    char label[8];
    int label_len;
    char args[96];
    int args_len;
} dock_entry_t;

static dock_entry_t dock_entries[MAX_DOCK];
static int dock_count = 0;
static char dock_status[96] = "";

static void load_dock(void)
{
    dock_count = 0;
    FILE *fp = fopen(HARP_DOCK_CFG, "r");
    if (!fp)
    {
        snprintf(dock_status, sizeof(dock_status), "No dock config found yet.");
        return;
    }
    char line[256];
    while (dock_count < MAX_DOCK && fgets(line, sizeof(line), fp))
    {
        char *p = trim(line);
        if (!*p || *p == '#')
            continue;

        char *sep1 = strchr(p, ';');
        if (!sep1)
            continue;
        *sep1++ = 0;
        char *sep2 = strchr(sep1, ';');
        if (!sep2)
            continue;
        *sep2++ = 0;
        char *sep3 = strchr(sep2, ';');
        char *args = "";
        if (sep3)
        {
            *sep3++ = 0;
            args = trim(sep3);
        }

        char *path = trim(p);
        char *color = trim(sep1);
        char *label = trim(sep2);
        if (!*path)
            continue;

        dock_entry_t *e = &dock_entries[dock_count];
        memset(e, 0, sizeof(*e));
        strncpy(e->path, path, sizeof(e->path) - 1);
        strncpy(e->color, color, sizeof(e->color) - 1);
        strncpy(e->label, label, sizeof(e->label) - 1);
        strncpy(e->args, args, sizeof(e->args) - 1);
        e->path_len = (int)strlen(e->path);
        e->color_len = (int)strlen(e->color);
        e->label_len = (int)strlen(e->label);
        e->args_len = (int)strlen(e->args);
        dock_count++;
    }
    fclose(fp);
    snprintf(dock_status, sizeof(dock_status), "Loaded %d dock app(s).", dock_count);
}

static void save_dock(nk_harp_t *nh)
{
    ensure_lib_harp_dir();
    FILE *fp = fopen(HARP_DOCK_CFG, "w");
    if (!fp)
    {
        snprintf(dock_status, sizeof(dock_status), "Save failed.");
        return;
    }
    for (int i = 0; i < dock_count; i++)
    {
        dock_entry_t *e = &dock_entries[i];
        clean_cfg_field(e->path, &e->path_len, sizeof(e->path), 0);
        clean_cfg_field(e->color, &e->color_len, sizeof(e->color), 0);
        clean_cfg_field(e->label, &e->label_len, sizeof(e->label), 0);
        clean_cfg_field(e->args, &e->args_len, sizeof(e->args), 1);
        if (e->label_len > 3)
        {
            e->label_len = 3;
            e->label[3] = 0;
        }
        if (!*e->color)
            strncpy(e->color, "0x3C3C50", sizeof(e->color) - 1);
        if (!*e->label)
            strncpy(e->label, "APP", sizeof(e->label) - 1);
        e->color_len = (int)strlen(e->color);
        e->label_len = (int)strlen(e->label);
        fprintf(fp, "%s;%s;%s;%s\n", e->path, e->color, e->label, e->args);
    }
    fclose(fp);
    harp_reload_dock(nh->win);
    snprintf(dock_status, sizeof(dock_status), "Saved.");
}

static void render_dock(nk_harp_t *nh)
{
    struct nk_context *ctx = &nh->ctx;

    nk_layout_row_dynamic(ctx, 20, 1);
    nk_label(ctx, "Harp Dock", NK_TEXT_LEFT);
    nk_layout_row_dynamic(ctx, 16, 1);
    nk_label_colored(ctx, "Up to 8 icons. Args are optional launch parameters.",
                     NK_TEXT_LEFT, nk_rgb(150, 150, 165));

    nk_layout_row_dynamic(ctx, 4, 1);
    nk_spacing(ctx, 1);

    int act_up = -1, act_down = -1, act_remove = -1;

    nk_layout_row_dynamic(ctx, 350, 1);
    if (nk_group_begin(ctx, "dock_list", NK_WINDOW_BORDER))
    {
        if (dock_count == 0)
        {
            nk_layout_row_dynamic(ctx, 20, 1);
            nk_label(ctx, "No dock apps configured.", NK_TEXT_LEFT);
        }
        for (int i = 0; i < dock_count; i++)
        {
            dock_entry_t *e = &dock_entries[i];
            nk_layout_row_template_begin(ctx, 24);
            nk_layout_row_template_push_static(ctx, 20);
            nk_layout_row_template_push_static(ctx, 20);
            nk_layout_row_template_push_static(ctx, 20);
            nk_layout_row_template_push_dynamic(ctx);
            nk_layout_row_template_push_static(ctx, 82);
            nk_layout_row_template_push_static(ctx, 44);
            nk_layout_row_template_push_static(ctx, 20);
            nk_layout_row_template_end(ctx);

            if (nk_button_symbol(ctx, NK_SYMBOL_TRIANGLE_UP) && i > 0)
                act_up = i;
            if (nk_button_symbol(ctx, NK_SYMBOL_TRIANGLE_DOWN) && i < dock_count - 1)
                act_down = i;

            clamp_text_len(e->color, &e->color_len, sizeof(e->color));
            e->color[e->color_len] = 0;
            uint32_t swatch;
            if (!parse_hex_color(e->color, &swatch))
                swatch = 0xFF3C3C50u;
            nk_button_color(ctx, u32_to_nkcolor(swatch));

            nk_edit_string(ctx, NK_EDIT_FIELD, e->path, &e->path_len,
                           (int)sizeof(e->path) - 1, nk_filter_default);
            nk_edit_string(ctx, NK_EDIT_FIELD, e->color, &e->color_len,
                           (int)sizeof(e->color) - 1, nk_filter_default);
            if (e->label_len > 3)
                e->label_len = 3;
            nk_edit_string(ctx, NK_EDIT_FIELD, e->label, &e->label_len,
                           4, nk_filter_default);

            if (nk_button_symbol(ctx, NK_SYMBOL_X))
                act_remove = i;

            nk_layout_row_template_begin(ctx, 24);
            nk_layout_row_template_push_static(ctx, 60);
            nk_layout_row_template_push_dynamic(ctx);
            nk_layout_row_template_end(ctx);
            nk_label(ctx, "Args", NK_TEXT_RIGHT);
            nk_edit_string(ctx, NK_EDIT_FIELD, e->args, &e->args_len,
                           (int)sizeof(e->args) - 1, nk_filter_default);
        }
        nk_group_end(ctx);
    }

    if (act_up >= 0)
    {
        dock_entry_t tmp = dock_entries[act_up];
        dock_entries[act_up] = dock_entries[act_up - 1];
        dock_entries[act_up - 1] = tmp;
    }
    else if (act_down >= 0)
    {
        dock_entry_t tmp = dock_entries[act_down];
        dock_entries[act_down] = dock_entries[act_down + 1];
        dock_entries[act_down + 1] = tmp;
    }
    else if (act_remove >= 0)
    {
        for (int i = act_remove; i < dock_count - 1; i++)
            dock_entries[i] = dock_entries[i + 1];
        dock_count--;
    }

    nk_layout_row_dynamic(ctx, 4, 1);
    nk_spacing(ctx, 1);

    nk_layout_row_template_begin(ctx, 26);
    nk_layout_row_template_push_static(ctx, 140);
    nk_layout_row_template_push_static(ctx, 160);
    nk_layout_row_template_push_dynamic(ctx);
    nk_layout_row_template_end(ctx);

    if (nk_button_label(ctx, "+ Add App") && dock_count < MAX_DOCK)
    {
        dock_entry_t *e = &dock_entries[dock_count];
        memset(e, 0, sizeof(*e));
        strncpy(e->path, DRIVE_ROOT "/bin/", sizeof(e->path) - 1);
        strncpy(e->color, "0x3C3C50", sizeof(e->color) - 1);
        strncpy(e->label, "APP", sizeof(e->label) - 1);
        e->path_len = (int)strlen(e->path);
        e->color_len = (int)strlen(e->color);
        e->label_len = (int)strlen(e->label);
        dock_count++;
    }
    if (nk_button_label(ctx, "Save & Reload Dock"))
        save_dock(nh);
    nk_label(ctx, dock_status, NK_TEXT_LEFT);
}

static int power_confirm = 0;

static void render_power(nk_harp_t *nh)
{
    (void)nh;
    struct nk_context *ctx = &nh->ctx;

    nk_layout_row_dynamic(ctx, 20, 1);
    nk_label(ctx, "Power", NK_TEXT_LEFT);
    nk_layout_row_dynamic(ctx, 16, 1);
    nk_spacing(ctx, 1);

    if (power_confirm == 0)
    {
        nk_layout_row_static(ctx, 34, 160, 2);
        if (nk_button_label(ctx, "Shut Down"))
            power_confirm = 1;
        if (nk_button_label(ctx, "Reboot"))
            power_confirm = 2;
    }
    else
    {
        nk_layout_row_dynamic(ctx, 20, 1);
        nk_label(ctx, power_confirm == 1 ? "Shut down ZenOS now?" : "Reboot ZenOS now?", NK_TEXT_LEFT);
        nk_layout_row_dynamic(ctx, 6, 1);
        nk_spacing(ctx, 1);
        nk_layout_row_static(ctx, 30, 120, 2);
        if (nk_button_label(ctx, "Confirm"))
        {
            if (power_confirm == 1)
                zen_shutdown();
            else
                zen_reboot();
        }
        if (nk_button_label(ctx, "Cancel"))
            power_confirm = 0;
    }
}

static void about_row(struct nk_context *ctx, const char *k, const char *v)
{
    nk_layout_row_static(ctx, 22, 130, 2);
    nk_label(ctx, k, NK_TEXT_LEFT);
    nk_label(ctx, v, NK_TEXT_LEFT);
}

static void render_about(nk_harp_t *nh)
{
    struct nk_context *ctx = &nh->ctx;

    utsname_t u;
    memset(&u, 0, sizeof(u));
    uname(&u);
    fb_info_t fb;
    memset(&fb, 0, sizeof(fb));
    zen_fbinfo(&fb);
    struct sysinfo si;
    memset(&si, 0, sizeof(si));
    sysinfo(&si);
    int ncpu = get_nprocs();

    char line_res[64];
    snprintf(line_res, sizeof(line_res), "%llu x %llu, %u bpp",
             (unsigned long long)fb.width, (unsigned long long)fb.height, fb.bpp);

    long up = (long)si.uptime;
    char line_up[48];
    snprintf(line_up, sizeof(line_up), "%ldh %ldm %lds",
             up / 3600, (up % 3600) / 60, up % 60);

    unsigned long total_mb = si.totalram / 1024 / 1024;
    unsigned long free_mb = si.freeram / 1024 / 1024;
    unsigned long used_mb = total_mb > free_mb ? total_mb - free_mb : 0;

    char line_ram[64];
    snprintf(line_ram, sizeof(line_ram), "%lu MB used / %lu MB total", used_mb, total_mb);
    char line_cpu[24];
    snprintf(line_cpu, sizeof(line_cpu), "%d", ncpu);
    char line_procs[24];
    snprintf(line_procs, sizeof(line_procs), "%u", si.procs);

    nk_layout_row_dynamic(ctx, 20, 1);
    nk_label(ctx, "About This PC", NK_TEXT_LEFT);
    nk_layout_row_dynamic(ctx, 6, 1);
    nk_spacing(ctx, 1);

    nk_layout_row_dynamic(ctx, 300, 1);
    if (nk_group_begin(ctx, "about_group", NK_WINDOW_BORDER))
    {
        about_row(ctx, "System", u.sysname);
        about_row(ctx, "Host name", u.nodename);
        about_row(ctx, "Release", u.release);
        about_row(ctx, "Version", u.version);
        about_row(ctx, "Machine", u.machine);
        about_row(ctx, "Display", line_res);
        about_row(ctx, "CPUs", line_cpu);
        about_row(ctx, "Memory", line_ram);
        about_row(ctx, "Uptime", line_up);
        about_row(ctx, "Processes", line_procs);
        nk_group_end(ctx);
    }
}

static void render_nav(nk_harp_t *nh)
{
    struct nk_context *ctx = &nh->ctx;
    nk_layout_row_dynamic(ctx, 30, 1);
    for (int i = 0; i < PAGE_COUNT; i++)
    {
        char buf[24];
        snprintf(buf, sizeof(buf), "%s %s", page == i ? "- " : "", page_names[i]);
        if (nk_button_label(ctx, buf))
            page = i;
    }
}

static void blit_thumbnail(nk_harp_t *nh, struct nk_rect b)
{
    int x0 = (int)b.x, y0 = (int)b.y;
    int ww = nh->win->w, wh = nh->win->h;
    for (int y = 0; y < THUMB_H; y++)
    {
        int dy = y0 + y;
        if (dy < 0 || dy >= wh)
            continue;
        uint32_t *dst = nh->win->buf + (size_t)dy * ww;
        const uint32_t *src = thumb_pixels + (size_t)y * THUMB_W;
        for (int x = 0; x < THUMB_W; x++)
        {
            int dx = x0 + x;
            if (dx < 0 || dx >= ww)
                continue;
            dst[dx] = src[x];
        }
    }
}

int main(void)
{
    load_wall();
    generate_thumbnail(wall_path);
    load_boot();
    load_dock();

    nk_harp_t *nh = nk_harp_init("Settings", 90, 50, WIN_W, WIN_H,
                                 "/mnt/drv0/lib/fonts/default.ttf");
    if (!nh)
        return 1;

    while (!nh->close_req)
    {
        nk_harp_feed_events(nh);
        g_preview_show = 0;

        if (page == PAGE_ABOUT)
        {
            if (++about_redraw_tick >= 50)
            {
                about_redraw_tick = 0;
                nk_harp_request_redraw(nh);
            }
        }
        else
        {
            about_redraw_tick = 0;
        }

        if (nk_begin(&nh->ctx, "Settings",
                     nk_rect(0, 0, WIN_W, WIN_H),
                     NK_WINDOW_BORDER | NK_WINDOW_NO_SCROLLBAR))
        {

            nk_layout_row_template_begin(&nh->ctx, WIN_H - 16);
            nk_layout_row_template_push_static(&nh->ctx, NAV_W);
            nk_layout_row_template_push_dynamic(&nh->ctx);
            nk_layout_row_template_end(&nh->ctx);

            if (nk_group_begin(&nh->ctx, "nav", NK_WINDOW_BORDER))
            {
                render_nav(nh);
                nk_group_end(&nh->ctx);
            }

            if (nk_group_begin(&nh->ctx, "content", NK_WINDOW_BORDER))
            {
                switch (page)
                {
                case PAGE_BOOT:
                    render_boot(nh);
                    break;
                case PAGE_WALLPAPER:
                    render_wallpaper(nh);
                    break;
                case PAGE_DOCK:
                    render_dock(nh);
                    break;
                case PAGE_POWER:
                    render_power(nh);
                    break;
                case PAGE_ABOUT:
                    render_about(nh);
                    break;
                }
                nk_group_end(&nh->ctx);
            }
        }
        nk_end(&nh->ctx);

        nk_harp_render(nh);

        if (page == PAGE_WALLPAPER && g_preview_show && thumb_valid)
            blit_thumbnail(nh, g_preview_rect);

        harp_flush(nh->win);
        zen_sleep_ms(10);
    }

    nk_harp_free(nh);
    return 0;
}