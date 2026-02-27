#include "../userlib.h"
#include "../libs/gfx.h"

#define COLS     16
#define ROWS     16
#define CELL     22
#define OX       ((512 - COLS * CELL) / 2)
#define OY       58
#define MAX_LEN  (COLS * ROWS)

#define C_BG     0xFF0D1117
#define C_GRID   0xFF1A2332
#define C_WALL   0xFF2D4A7A
#define C_HEAD   0xFF00FF88
#define C_BODY   0xFF00CC66
#define C_FOOD   0xFFFF4466
#define C_TEXT   0xFFFFFFFF
#define C_SCORE  0xFF00DDFF

static int sx[MAX_LEN], sy[MAX_LEN];
static int slen, dx, dy, fx, fy, score;
static uint32_t rng;

static uint32_t rng_next(void) {
    rng ^= rng << 13; rng ^= rng >> 17; rng ^= rng << 5;
    return rng;
}

static void place_food(void) {
    while (1) {
        int x = rng_next() % COLS;
        int y = rng_next() % ROWS;
        int ok = 1;
        for (int i = 0; i < slen; i++)
            if (sx[i] == x && sy[i] == y) { ok = 0; break; }
        if (ok) { fx = x; fy = y; return; }
    }
}

static char nbuf[24];
static char *numstr(int n) {
    if (n == 0) { nbuf[0]='0'; nbuf[1]=0; return nbuf; }
    int i = 0; int tmp = n;
    while (tmp > 0) { nbuf[i++] = '0' + tmp % 10; tmp /= 10; }
    for (int a = 0, b = i-1; a < b; a++, b--) {
        char t = nbuf[a]; nbuf[a] = nbuf[b]; nbuf[b] = t;
    }
    nbuf[i] = 0;
    return nbuf;
}

static void draw_cell_bg(socket_file_t *g, int x, int y) {
    gfx_rect(g, OX + x*CELL+1, OY + y*CELL+1, CELL-2, CELL-2, C_GRID);
}

static void draw_cell_snake(socket_file_t *g, int x, int y, uint32_t col) {
    gfx_rect(g, OX + x*CELL+2, OY + y*CELL+2, CELL-4, CELL-4, col);
}

static void draw_food(socket_file_t *g) {
    gfx_circle(g, OX + fx*CELL + CELL/2, OY + fy*CELL + CELL/2, CELL/2-3, C_FOOD);
}

static void draw_score(socket_file_t *g) {
    gfx_rect(g, 4, 20, 50, 56, C_BG);
    gfx_text(g, 4, 40, C_SCORE, 1, numstr(score));
}

static void draw_full_scene(socket_file_t *g) {
    gfx_clear(g, C_BG);
    for (int y = 0; y < ROWS; y++)
        for (int x = 0; x < COLS; x++)
            gfx_rect(g, OX + x*CELL+1, OY + y*CELL+1, CELL-2, CELL-2, C_GRID);
    gfx_rect_outline(g, OX-2, OY-2, COLS*CELL+4, ROWS*CELL+4, C_WALL);
    draw_food(g);
    for (int i = slen-1; i >= 0; i--)
        draw_cell_snake(g, sx[i], sy[i], i == 0 ? C_HEAD : C_BODY);
    gfx_text(g, 4, 20,  C_TEXT, 1, "SNAKE");
    draw_score(g);
    gfx_text(g, 300, 20, C_TEXT, 1, "WASD / Q quit");
}

int main(int argc, char *argv[]) {
    (void)argc; (void)argv;
    socket_file_t *g;
    if (gfx_open(&g) != 0) {
        prints("\033[31msnake: no gfxserver\033[0m\n");
        exit(1);
    }
    slen = 4;
    sx[0]=COLS/2;   sy[0]=ROWS/2;
    sx[1]=COLS/2-1; sy[1]=ROWS/2;
    sx[2]=COLS/2-2; sy[2]=ROWS/2;
    sx[3]=COLS/2-3; sy[3]=ROWS/2;
    dx=1; dy=0; score=0;
    timeval_t tv; gettimeofday(&tv, NULL);
    rng = (uint32_t)(tv.tv_sec ^ tv.tv_usec) | 1;
    place_food();

    draw_full_scene(g);

    while (1) {
        for (int t = 0; t < 6; t++) {
            char k = getkey();
            if (k=='q'||k=='Q') goto done;
            if ((k=='w'||k=='W') && dy!=1)  { dx=0;  dy=-1; }
            if ((k=='s'||k=='S') && dy!=-1) { dx=0;  dy=1;  }
            if ((k=='a'||k=='A') && dx!=1)  { dx=-1; dy=0;  }
            if ((k=='d'||k=='D') && dx!=-1) { dx=1;  dy=0;  }
            yield();
        }

        int nx = sx[0]+dx, ny = sy[0]+dy;
        if (nx<0||nx>=COLS||ny<0||ny>=ROWS) goto dead;
        for (int i=1; i<slen; i++)
            if (sx[i]==nx && sy[i]==ny) goto dead;

        int ate = (nx==fx && ny==fy);

        if (!ate) {
            int tx = sx[slen-1], ty = sy[slen-1];
            for (int i=slen-1; i>0; i--) { sx[i]=sx[i-1]; sy[i]=sy[i-1]; }
            sx[0]=nx; sy[0]=ny;
            draw_cell_bg(g, tx, ty);
            draw_cell_snake(g, sx[1], sy[1], C_BODY);
            draw_cell_snake(g, sx[0], sy[0], C_HEAD);
        } else {
            if (slen < MAX_LEN) {
                for (int i=slen; i>0; i--) { sx[i]=sx[i-1]; sy[i]=sy[i-1]; }
                slen++;
            }
            sx[0]=nx; sy[0]=ny;
            score += 10;
            place_food();
            draw_cell_snake(g, sx[1], sy[1], C_BODY);
            draw_cell_snake(g, sx[0], sy[0], C_HEAD);
            draw_food(g);
            draw_score(g);
        }
        continue;

dead:
        gfx_clear(g, 0xFF110022);
        gfx_text(g, 140, 320, 0xFFFF3344, 2, "GAME OVER");
        gfx_text(g, 180, 360, 0xFF00DDFF, 1, "Score:");
        gfx_text(g, 260, 360, 0xFF00DDFF, 1, numstr(score));
        sleep(3000);
        goto done;
    }
done:
    gfx_clear(g, C_BG);
    gfx_close(g);
    exit(0);
    return 0;
}
