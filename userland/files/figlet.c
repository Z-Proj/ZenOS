#include "../userlib.h"
#include "../libs/gfx.h"

#define FONT_HEIGHT 6
#define C_BG  0xFF0D1117
#define C_FG  0xFF00FFCC
#define C_SH  0xFF004433

static const char *font[59][FONT_HEIGHT] = {
    {" "," "," "," "," "," "},
    {"!","!","!","!"," ","!"},
    {"\"\"","\"\"","  ","  ","  ","  "},
    {" # # ","#####"," # # ","#####"," # # ","     "},
    {" $$$","$    "," $$$","    $"," $$$","     "},
    {"   ","   ","   ","   ","   ","   "},
    {" & ","( ) "," & ","(X) "," (X)","     "},
    {"'","'"," "," "," "," "},
    {" |","/"," |"," |"," \\"," |"},
    {"|"," \\"," |"," |","/", " |"},
    {"   ","X X"," X ","X X","   ","   "},
    {"   "," | ","---"," | ","   ","   "},
    {" "," "," "," ",",","/"},
    {"   ","   ","---","   ","   ","   "},
    {" "," "," "," ",".","."},
    {"  /"," / "," / ","/  ","   ","   "},
    {" 0 ","/ \\","/ \\"," \\ /"," 0 ","   "},
    {"  1"," 11","  1","  1","  1","   "},
    {" 2 ","   2"," 2 ","2  ","222","   "},
    {" 3 ","   3"," 33","   3"," 3 ","   "},
    {"  4 "," 44 ","4  4","44444","   4","    "},
    {"555","5  ","55 ","  5","55 ","   "},
    {" 6 ","6  ","66 ","6 6"," 6 ","   "},
    {"777","  7"," 7 "," 7 "," 7 ","   "},
    {" 8 ","8 8"," 8 ","8 8"," 8 ","   "},
    {" 9 ","9 9"," 99","  9"," 9 ","   "},
    {" "," ",":"," ",":"," "},
    {" "," "," ;"," "," ;","/"},
    {" <","/","<","\\"," <","  "},
    {"   ","===","   ","===","   ","   "},
    {"> "," \\"," >"," /",">","   "},
    {" ?? ","   ?"," ?? ","    "," ?  ","    "},
    {" @@ ","@  @","@ @@","@  @"," @@ ","    "},
    {" A ","A A","AAA","A A","A A","   "},
    {"BB ","B B","BB ","B B","BB ","   "},
    {" C ","C  ","C  ","C  "," C ","   "},
    {"DD ","D D","D D","D D","DD ","   "},
    {"EEE","E  ","EE ","E  ","EEE","   "},
    {"FFF","F  ","FF ","F  ","F  ","   "},
    {" GG","G  ","G G","G G"," GG","   "},
    {"H H","H H","HHH","H H","H H","   "},
    {"III"," I "," I "," I ","III","   "},
    {"  J","  J","  J","J J"," J ","   "},
    {"K K","K K","KK ","K K","K K","   "},
    {"L  ","L  ","L  ","L  ","LLL","   "},
    {"M M","MMM","M M","M M","M M","   "},
    {"N N","NNN","N N","N N","N N","   "},
    {" O ","O O","O O","O O"," O ","   "},
    {"PP ","P P","PP ","P  ","P  ","   "},
    {" Q ","Q Q","Q Q","Q Q"," QQ","   "},
    {"RR ","R R","RR ","R R","R R","   "},
    {" S ","S  "," S ","  S"," S ","   "},
    {"TTT"," T "," T "," T "," T ","   "},
    {"U U","U U","U U","U U"," U ","   "},
    {"V V","V V","V V","V V"," V ","   "},
    {"W W","W W","W W","WWW","W W","   "},
    {"X X","X X"," X ","X X","X X","   "},
    {"Y Y","Y Y"," Y "," Y "," Y ","   "},
    {"ZZZ","  Z"," Z ","Z  ","ZZZ","   "},
};

static char toupper_z(char c) { return (c>='a'&&c<='z') ? c-32 : c; }

static int charwidth(int idx) {
    int w = 0;
    for (int r = 0; r < FONT_HEIGHT; r++) {
        int l = 0;
        const char *row = font[idx][r];
        while (row[l]) l++;
        if (l > w) w = l;
    }
    return w;
}

int main(int argc, char *argv[]) {
    if (argc < 2) {
        prints("\033[33mUsage: figlet <text>\033[0m\n");
        exit(1);
    }

    static char buf[64];
    int pos = 0;
    for (int i = 1; i < argc && pos < 60; i++) {
        for (int j = 0; argv[i][j] && pos < 60; j++)
            buf[pos++] = toupper_z(argv[i][j]);
        if (i < argc-1 && pos < 60) buf[pos++] = ' ';
    }
    buf[pos] = '\0';

    socket_file_t *g;
    int has_gfx = (gfx_open(&g) == 0);

    if (has_gfx) {
        gfx_clear(g, C_BG);

        int cell_w = 10, cell_h = 14;
        int total_w = 0;
        for (int i = 0; buf[i]; i++) {
            int idx = buf[i] - 32;
            if (idx >= 0 && idx < 59) total_w += (charwidth(idx) + 1) * cell_w;
        }

        int start_x = (512 - total_w) / 2;
        if (start_x < 4) start_x = 4;
        int start_y = (768 - FONT_HEIGHT * cell_h) / 2;

        int cx = start_x;
        for (int ci = 0; buf[ci]; ci++) {
            int idx = buf[ci] - 32;
            if (idx < 0 || idx >= 59) { cx += cell_w * 4; continue; }
            int cw = charwidth(idx);
            for (int row = 0; row < FONT_HEIGHT; row++) {
                const char *r = font[idx][row];
                for (int col = 0; r[col]; col++) {
                    if (r[col] != ' ') {
                        gfx_rect(g, cx + col*cell_w + 2, start_y + row*cell_h + 2,
                                 cell_w-2, cell_h-2, C_SH);
                        gfx_rect(g, cx + col*cell_w, start_y + row*cell_h,
                                 cell_w-2, cell_h-2, C_FG);
                    }
                }
            }
            cx += (cw + 1) * cell_w;
        }

        gfx_text(g, 4, 18, 0xFF666688, 1, "figlet - press any key");
        while (getkey() == 0) { sleep(50); yield(); }
        gfx_clear(g, C_BG);
        gfx_close(g);
    } else {
        prints("\033[36m");
        for (int row = 0; row < FONT_HEIGHT; row++) {
            int i = 0;
            while (buf[i]) {
                int idx = buf[i] - 32;
                if (idx >= 0 && idx < 59) { prints(font[idx][row]); prints("  "); }
                i++;
            }
            prints("\n");
        }
        prints("\033[0m");
    }

    exit(0);
    return 0;
}
