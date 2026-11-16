#include "render.h"

#include <stdio.h>
#include <string.h>

#include "font.h"

uint16_t fb_top[SCR_W * TOP_H];
uint16_t fb_map[SCR_W * MAPV_H];
uint16_t fb_bot[SCR_W * BOT_H];

#define RGB(r, g, b) ((uint16_t)((((r) & 0xF8) << 8) | (((g) & 0xFC) << 3) | ((b) >> 3)))

#define C_SEA RGB(44, 112, 178)
#define C_SEA_HI RGB(78, 146, 204)
#define C_SHOAL RGB(92, 176, 214)
#define C_FOAM RGB(214, 240, 248)
#define C_SAND RGB(238, 216, 162)
#define C_SAND_D RGB(214, 188, 130)
#define C_GRASS RGB(116, 190, 96)
#define C_GRASS_D RGB(94, 168, 80)
#define C_GRASS_L RGB(146, 212, 116)
#define C_RIVER RGB(84, 164, 224)
#define C_RIVER_HI RGB(150, 208, 244)
#define C_LAKE RGB(58, 132, 204)
#define C_ASH RGB(98, 90, 84)
#define C_ASH_D RGB(70, 64, 60)
#define C_FLOOD RGB(96, 150, 196)
#define C_SNOW RGB(232, 238, 246)
#define C_SNOW_D RGB(196, 210, 230)
#define C_SOIL RGB(150, 108, 70)
#define C_SOIL_D RGB(118, 82, 52)
#define C_LAVA RGB(232, 84, 30)
#define C_LAVA_HI RGB(255, 208, 70)
#define C_LAVA_D RGB(120, 40, 28)

#define C_BG RGB(22, 26, 42)
#define C_PANEL RGB(40, 46, 70)
#define C_LINE RGB(64, 72, 104)
#define C_TEXT RGB(236, 238, 244)
#define C_DIM RGB(150, 158, 184)
#define C_GOLD RGB(255, 214, 92)
#define C_RED RGB(240, 96, 88)

/* ---------- 字体 ---------- */

static int glyph_index(uint32_t cp)
{
    int lo = 0, hi = font_count - 1;
    while (lo <= hi) {
        int mid = (lo + hi) / 2;
        if (font_cp[mid] == cp) return mid;
        if (font_cp[mid] < cp) lo = mid + 1;
        else hi = mid - 1;
    }
    return -1;
}

static uint32_t utf8_next(const char **s)
{
    const unsigned char *p = (const unsigned char *)*s;
    uint32_t c = *p++;
    if (c >= 0xF0 && p[0] && p[1] && p[2]) {
        c = (c & 7) << 18 | (p[0] & 63) << 12 | (p[1] & 63) << 6 | (p[2] & 63);
        p += 3;
    } else if (c >= 0xE0 && p[0] && p[1]) {
        c = (c & 15) << 12 | (p[0] & 63) << 6 | (p[1] & 63);
        p += 2;
    } else if (c >= 0xC0 && p[0]) {
        c = (c & 31) << 6 | (p[0] & 63);
        p += 1;
    }
    *s = (const char *)p;
    return c;
}

int font_width(const char *s)
{
    int w = 0;
    while (*s) w += utf8_next(&s) < 0x80 ? 6 : 12;
    return w;
}

int font_draw(uint16_t *fb, int fbw, int fbh, int x, int y, const char *s, uint16_t color)
{
    while (*s) {
        uint32_t cp = utf8_next(&s);
        int adv = cp < 0x80 ? 6 : 12;
        int gi = glyph_index(cp);
        if (gi >= 0) {
            for (int r = 0; r < FONT_H; r++) {
                int yy = y + r;
                if (yy < 0 || yy >= fbh) continue;
                uint16_t bits = font_rows[gi][r];
                for (int c = 0; c < 12 && bits; c++, bits = (uint16_t)(bits << 1)) {
                    int xx = x + c;
                    if ((bits & 0x800) && xx >= 0 && xx < fbw) fb[yy * fbw + xx] = color;
                }
            }
        }
        x += adv;
    }
    return x;
}

/* ---------- 像素画 ---------- */

static uint16_t pal[128];

void render_init(void)
{
    pal['G'] = RGB(62, 148, 76);
    pal['g'] = RGB(104, 188, 96);
    pal['D'] = RGB(40, 108, 62);
    pal['S'] = RGB(78, 146, 70);
    pal['t'] = RGB(134, 94, 56);
    pal['T'] = RGB(98, 66, 40);
    pal['r'] = RGB(226, 62, 92);
    pal['p'] = RGB(246, 150, 180);
    pal['w'] = RGB(250, 250, 250);
    pal['k'] = RGB(36, 30, 36);
    pal['y'] = RGB(255, 226, 96);
    pal['o'] = RGB(250, 150, 50);
    pal['R'] = RGB(226, 70, 50);
    pal['M'] = RGB(160, 48, 40);
    pal['a'] = RGB(158, 160, 170);
    pal['A'] = RGB(112, 114, 128);
    pal['l'] = RGB(204, 206, 214);
    pal['s'] = RGB(250, 208, 170);
    pal['b'] = RGB(140, 96, 66);
    pal['B'] = RGB(98, 64, 46);
    pal['n'] = RGB(186, 136, 76);
    pal['N'] = RGB(134, 92, 50);
    pal['e'] = RGB(52, 46, 46);
    pal['u'] = RGB(70, 84, 130);
    pal['Y'] = RGB(226, 190, 100);
    pal['O'] = RGB(178, 140, 64);
    pal['q'] = RGB(150, 208, 244);
    pal['W'] = RGB(132, 136, 150);
    pal['V'] = RGB(88, 92, 108);
    pal['x'] = RGB(238, 230, 214);
    pal['X'] = RGB(196, 184, 164);
}

static const char *const SPR_TREE[11] = {"..DGGD..", ".DGGgGD.", "DGGgGGGD", "GGgGGGGG", "GGGGGgGD", "DGGGGGGD",
                                         ".DGGGGD.", "..DDDD..", "...tT...", "...tT...", "..STTS.."};
static const char *const SPR_YOUNG[8] = {"........", "...GG...", "..GgGG..", ".GGGGGD.",
                                         ".DGGGDD.", "..DDDD..", "...tT...", "...tT..."};
static const char *const SPR_SAPLING[8] = {"........", "........", "........", "...g....",
                                           "..gGg...", "...GD...", "...t....", "........"};
static const char *const SPR_STUMP[8] = {"........", "........", "........", "........",
                                         "........", "..tttt..", "..tTTt..", "..TTTT.."};
static const char *const SPR_BUSH[8] = {"........", "........", "..GGG...", ".GGgGGD.",
                                        "GGGGGGDD", "DGGGGDDD", ".DDDDDD.", "........"};
static const char *const SPR_BERRY[8] = {"........", "........", "..GrG...", ".GGgGrD.",
                                         "GrGGGGDD", "DGGrGDrD", ".DDDDDD.", "........"};
static const char *const SPR_FLOWER[8] = {"........", "........", "........", "..y.....",
                                          ".ywy.p..", "..y.pwp.", "..G..p..", ".....G.."};
static const char *const SPR_FIRE_A[8] = {"........", "...o....", "..oyo...", "..yyo.o.",
                                          ".oywyo..", ".oyyyo..", "tTtTtTt.", ".T.t.T.."};
static const char *const SPR_FIRE_B[8] = {"....o...", "..o.....", "..oyoo..", ".oyyyo..",
                                          ".oywyo..", "..yyyo..", "tTtTtTt.", ".T.t.T.."};
static const char *const SPR_FIRE_OFF[8] = {"........", "........", "........", "........",
                                            "...a.a..", "..eaee..", "tTeTeTt.", ".T.t.T.."};
static const char *const SPR_FLAME_A[8] = {"...o....", "..oo.o..", ".oyo.oo.", ".oyyoyo.",
                                           "oyywyyo.", "oywwyyo.", ".oyyyo..", "..RRR..."};
static const char *const SPR_FLAME_B[8] = {".....o..", ".o.oo...", ".oooyo..", "oyoyyo..",
                                           "oyywyyo.", ".oywwyo.", ".oyyyoo.", "..RRRR.."};
static const char *const SPR_STORE[8] = {"..nnnn..", ".nNNNNn.", "nnnnnnnn", "nNnNNnNn",
                                         "nNnNNnNn", "nnnnnnnn", "nNnNNnNn", "NNNNNNNN"};
static const char *const SPR_GRAVE[8] = {"........", "..laa...", ".laaaA..", ".aaAaA..",
                                         ".aAAAA..", ".aaAaA..", ".aaAaA..", "SSSSSSS."};
static const char *const SPR_BURNT[8] = {"........", "...e....", "...e.e..", ".e.ee...",
                                         "..eee...", "...e....", "...e....", "..eee..."};
static const char *const SPR_ROCK[8] = {"...ll...", "..laaa..", ".laaaaA.", ".aaaaAA.",
                                        "laaaaaAA", "aaaaAAAA", "aaAAAAAA", ".AAAAAA."};
static const char *const SPR_HUT[9] = {"...YY...", "..YOYY..", ".YYYOYY.", "YOYYYYOY", "OYYOYYYO",
                                       ".nnnnnn.", ".nNTTNn.", ".nNTTNn.", ".nnTTnn."};
static const char *const SPR_HOUSE[10] = {".....A..", "..RRRA..", ".RRMRRR.", "RRRRRMRR", "RMRRRRRM",
                                          ".nnnnnn.", ".nqnTTn.", ".nqnTTn.", ".nnnTTn.", ".NNNTTN."};
static const char *const SPR_SITE[8] = {"........", ".t....t.", ".tttttt.", ".t....t.",
                                        ".t....t.", ".tnnnnt.", ".tnnnnt.", "........"};
static const char *const SPR_RUIN[8] = {"........", "........", ".e......", ".n...e..",
                                        ".nA..n..", ".nnaAnn.", "anAnnaAn", "........"};
/* 居民 6x8：h 头发 s 皮肤 c 上衣 u 裤子 */
static const char *const SPR_MAN_A[8] = {".hhhh.", "hhhhhh", "skssks", ".ssss.", "cccccc", "scccCs", ".uuuu.", ".u..u."};
static const char *const SPR_MAN_B[8] = {".hhhh.", "hhhhhh", "skssks", ".ssss.", "cccccc", "scccCs", ".uuuu.", "..uu.."};
static const char *const SPR_WOMAN_A[8] = {".hhhh.", "hhhhhh", "hksskh", "hssssh", "hccccC", "sccccs", "cccccC", ".s..s."};
static const char *const SPR_WOMAN_B[8] = {".hhhh.", "hhhhhh", "hksskh", "hssssh", "hccccC", "sccccs", "cccccC", "..ss.."};
static const char *const SPR_KID_A[6] = {".hh.", "hhhh", "kssk", "cccc", "cccC", "u..u"};
static const char *const SPR_KID_B[6] = {".hh.", "hhhh", "kssk", "cccc", "cccC", ".uu."};
static const char *const SPR_BABY[4] = {".ss.", "cssc", "cccc", ".cc."};
static const char *const SPR_SLEEP[4] = {".hh.....", "hsscccCc", "hssccccC", ".hh....."};
static const char *const SPR_BOAR_A[6] = {"........", ".BbbbB..", "bbbbbbkb", "bbbbbbbp", ".bbbbbb.", ".B..B..."};
static const char *const SPR_BOAR_B[6] = {"........", ".BbbbB..", "bbbbbbkb", "bbbbbbbp", ".bbbbbb.", "..B..B.."};
static const char *const SPR_RABBIT_A[5] = {".x.x..", ".x.x..", "xxkx..", "XxxxxX", ".X..X."};
static const char *const SPR_RABBIT_B[5] = {"..x.x.", ".x.x..", "xxkx..", "XxxxxX", "..XX.."};
static const char *const SPR_WOLF_A[6] = {"......V.", "V.WWWWWW", "VWWWWWkW", ".WWWWWWV", ".WWVWW..", ".V..V..."};
static const char *const SPR_WOLF_B[6] = {"......V.", "V.WWWWWW", "VWWWWWkW", ".WWWWWWV", ".WWVWW..", "..V..V.."};
static const char *const SPR_SHRINE[10] = {"...ll...", "..laal..", "..lyal..", "..aayA..", "..aaaA..",
                                           "..ayaA..", "..aaAA..", ".aaaaAA.", "aAAAAAAA", "SSSSSSSS"};
static const char *const ICON_MIC[8] = {"..ll....", ".llll...", ".llll...", ".llll...",
                                        "a.ll.a..", ".aaaa...", "..aa....", ".aaaa..."};
static const char *const SPR_SUN[8] = {"...y....", ".y.y.y..", "..yyy...", "yyywyyy.",
                                       "..yyy...", ".y.y.y..", "...y....", "........"};
static const char *const SPR_MOON[8] = {"..lll...", ".lll....", "lll.....", "lll.....",
                                        "lll...l.", ".lllll..", "..lll...", "........"};
/* 天气小图标 */
static const char *const ICON_W[W_COUNT][8] = {
    {"........", "........", "........", "........", "........", "........", "........", "........"},
    {"........", "..ll....", ".lllll..", "lllllll.", "lllllll.", ".lllll..", "........", "........"},
    {"..ll....", ".lllll..", "lllllll.", ".lllll..", "........", "q..q..q.", ".q..q...", "........"},
    {"..aa....", ".aaaaa..", "aaaaaaa.", ".aaaaa..", "q.q.q.q.", ".q.q.q..", "q.q.q.q.", "........"},
    {"..aa....", ".aaaaa..", "aaaaaaa.", ".aayaa..", "...yy...", "..yy....", "...y....", "..y....."},
    {"........", "llll....", "....l...", "lllll...", "........", "llllll..", "......l.", ".llll..."},
    {"...w....", ".w.w.w..", "..www...", "wwwwwww.", "..www...", ".w.w.w..", "...w....", "........"}};

static void spr(uint16_t *fb, int fbw, int fbh, int x, int y, const char *const *rows, int w, int h, bool flip)
{
    for (int r = 0; r < h; r++) {
        int yy = y + r;
        if (yy < 0 || yy >= fbh) continue;
        const char *row = rows[r];
        for (int c = 0; c < w; c++) {
            char ch = row[flip ? w - 1 - c : c];
            int xx = x + c;
            if (ch == '.' || xx < 0 || xx >= fbw) continue;
            fb[yy * fbw + xx] = pal[(unsigned char)ch];
        }
    }
}
#define MSPR(x, y, rows, w, h, flip) spr(fb_map, SCR_W, MAPV_H, x, y, rows, w, h, flip)

static void mpx(int x, int y, uint16_t c)
{
    if (x >= 0 && y >= 0 && x < SCR_W && y < MAPV_H) fb_map[y * SCR_W + x] = c;
}

static void rect(uint16_t *fb, int fbw, int fbh, int x, int y, int w, int h, uint16_t c)
{
    for (int yy = y < 0 ? 0 : y; yy < y + h && yy < fbh; yy++)
        for (int xx = x < 0 ? 0 : x; xx < x + w && xx < fbw; xx++) fb[yy * fbw + xx] = c;
}
#define MRECT(x, y, w, h, c) rect(fb_map, SCR_W, MAPV_H, x, y, w, h, c)

static uint32_t thash(int x, int y)
{
    uint32_t h = (uint32_t)x * 73856093u ^ (uint32_t)y * 19349663u;
    h ^= h >> 13;
    h *= 0x5bd1e995u;
    return h ^ (h >> 15);
}

static int terr_at(const world_t *w, int x, int y)
{
    if (x < 0 || y < 0 || x >= MAP_W || y >= MAP_H) return T_SEA;
    return TERR(w->tiles[y * MAP_W + x]);
}

static void tile_base(const world_t *w, int tx, int ty, uint32_t frame)
{
    int idx = ty * MAP_W + tx;
    int terr = terr_at(w, tx, ty);
    int x0 = tx * TILE, y0 = ty * TILE;
    uint32_t h = thash(tx, ty);
    bool snow = w->weather == W_SNOW;

    if (sim_flooded(w, idx)) { /* 被淹的陆地：浑浊的水面 */
        MRECT(x0, y0, TILE, TILE, C_FLOOD);
        int ph = (int)((frame / 2 + h) % 10);
        if (ph < 6) mpx(x0 + ph, y0 + 1 + (int)(h >> 5) % 6, C_FOAM), mpx(x0 + ph + 1, y0 + 1 + (int)(h >> 5) % 6, C_FOAM);
        return;
    }
    uint16_t base;
    switch (terr) {
    case T_SEA: base = C_SEA; break;
    case T_SHOAL: base = C_SHOAL; break;
    case T_SAND: base = snow ? C_SNOW : C_SAND; break;
    case T_RIVER: base = C_RIVER; break;
    case T_LAKE: base = C_LAKE; break;
    case T_ASH: base = C_ASH; break;
    case T_LAVA: base = C_LAVA; break;
    default: base = snow ? C_SNOW : C_GRASS; break;
    }
    MRECT(x0, y0, TILE, TILE, base);
    switch (terr) {
    case T_SEA:
    case T_LAKE: {
        int ph = (int)((h + frame / 3) % 24); /* 缓慢移动的波光 */
        if (ph < 5) {
            int yy = y0 + (int)(h >> 8) % 7, xx = x0 + ph;
            mpx(xx, yy, C_SEA_HI);
            mpx(xx + 1, yy, C_SEA_HI);
            mpx(xx + 2, yy, C_SEA_HI);
        }
        break;
    }
    case T_SHOAL: {
        int wob = (int)((frame / 6 + h) % 4) == 0; /* 贴着沙滩的浪花，轻轻进退 */
        if (terr_at(w, tx, ty - 1) == T_SAND) MRECT(x0, y0, TILE, 1 + wob, C_FOAM);
        if (terr_at(w, tx, ty + 1) == T_SAND) MRECT(x0, y0 + 7 - wob, TILE, 1 + wob, C_FOAM);
        if (terr_at(w, tx - 1, ty) == T_SAND) MRECT(x0, y0, 1 + wob, TILE, C_FOAM);
        if (terr_at(w, tx + 1, ty) == T_SAND) MRECT(x0 + 7 - wob, y0, 1 + wob, TILE, C_FOAM);
        break;
    }
    case T_SAND: {
        uint16_t d = snow ? C_SNOW_D : C_SAND_D;
        mpx(x0 + (int)(h % 7), y0 + (int)(h >> 4) % 7, d);
        mpx(x0 + (int)(h >> 9) % 7, y0 + (int)(h >> 13) % 7, d);
        break;
    }
    case T_RIVER: {
        int ph = (int)((frame / 2 + h) % 8);
        mpx(x0 + ph, y0 + 2 + (int)(h % 4), C_RIVER_HI);
        mpx(x0 + (ph + 4) % 8, y0 + 1 + (int)(h >> 5) % 6, C_RIVER_HI);
        break;
    }
    case T_ASH:
        mpx(x0 + (int)(h % 7), y0 + (int)(h >> 4) % 7, C_ASH_D);
        mpx(x0 + (int)(h >> 9) % 7, y0 + (int)(h >> 13) % 7, C_ASH_D);
        mpx(x0 + (int)(h >> 17) % 7, y0 + (int)(h >> 21) % 7, RGB(126, 118, 110));
        break;
    case T_LAVA: { /* 翻滚的岩浆：亮斑游走，暗色硬壳 */
        int ph = (int)((frame / 3 + h) % 6);
        MRECT(x0 + (int)(h % 5), y0 + ph, 3, 2, C_LAVA_HI);
        mpx(x0 + (int)(h >> 9) % 7, y0 + (int)(h >> 13) % 7, C_LAVA_D);
        mpx(x0 + (int)(h >> 17) % 7, y0 + (int)(h >> 21) % 7, C_LAVA_D);
        break;
    }
    default: { /* 草地 / 山地底色 */
        int ax = x0 + (int)(h % 6), ay = y0 + 1 + (int)(h >> 4) % 6;
        mpx(ax, ay, snow ? C_SNOW_D : C_GRASS_D);
        mpx(ax + 1, ay - 1, snow ? C_SNOW_D : C_GRASS_D);
        mpx(x0 + (int)(h >> 9) % 7, y0 + (int)(h >> 13) % 7, snow ? pal['w'] : C_GRASS_L);
        if (terr == T_ROCK) {
            MSPR(x0, y0, SPR_ROCK, 8, 8, h & 1);
            if (snow) mpx(x0 + 3, y0, pal['w']), mpx(x0 + 4, y0, pal['w']), mpx(x0 + 3, y0 + 1, pal['w']);
        }
        break;
    }
    }
}

static void draw_farm(int x0, int y0, int v, uint32_t h)
{
    MRECT(x0, y0, TILE, TILE, C_SOIL);
    for (int r = 1; r < TILE; r += 2) MRECT(x0, y0 + r, TILE, 1, C_SOIL_D);
    int stage = v >= 100 ? 3 : v >= 60 ? 2 : v >= 25 ? 1 : 0;
    for (int c = 1; c < TILE; c += 2) {
        int jig = (int)((h >> c) & 1);
        for (int r = 0; r < 3; r++) {
            int yy = y0 + 1 + r * 2 + (r == 1 ? 1 : 0) + jig;
            if (stage == 0) {
                if ((c + r) & 2) mpx(x0 + c, yy + 1, pal['g']);
            } else if (stage == 1) {
                mpx(x0 + c, yy + 1, pal['g']);
            } else if (stage == 2) {
                mpx(x0 + c, yy + 1, pal['G']);
                mpx(x0 + c, yy, pal['g']);
            } else {
                mpx(x0 + c, yy + 1, pal['O']);
                mpx(x0 + c, yy, pal['y']);
            }
        }
    }
}

static void tile_obj(const world_t *w, int tx, int ty, uint32_t frame)
{
    int x0 = tx * TILE, y0 = ty * TILE;
    uint16_t t = w->tiles[ty * MAP_W + tx];
    uint32_t h = thash(tx, ty);
    switch (OBJ(t)) {
    case O_SAPLING: MSPR(x0, y0, SPR_SAPLING, 8, 8, false); break;
    case O_YOUNG: MSPR(x0, y0, SPR_YOUNG, 8, 8, false); break;
    case O_TREE:
        MSPR(x0, y0 - 3, SPR_TREE, 8, 11, h & 4);
        if (w->weather == W_SNOW) MRECT(x0 + 2, y0 - 3, 4, 1, pal['w']), MRECT(x0 + 1, y0 - 2, 2, 1, pal['w']);
        break;
    case O_STUMP: MSPR(x0, y0, SPR_STUMP, 8, 8, false); break;
    case O_BUSH: MSPR(x0, y0, SPR_BUSH, 8, 8, false); break;
    case O_BERRY: MSPR(x0, y0, SPR_BERRY, 8, 8, false); break;
    case O_FLOWER: MSPR(x0, y0, SPR_FLOWER, 8, 8, h & 4); break;
    case O_FIRE:
        MSPR(x0, y0, !w->fire_lit ? SPR_FIRE_OFF : (frame / 3) & 1 ? SPR_FIRE_A : SPR_FIRE_B, 8, 8, false);
        break;
    case O_STORE:
        MSPR(x0, y0, SPR_STORE, 8, 8, false);
        if (w->food > 0) { /* 仓库顶上露出食物 */
            mpx(x0 + 2, y0 - 1, pal['R']);
            mpx(x0 + 3, y0 - 1, pal['y']);
            if (w->food > 6) mpx(x0 + 4, y0 - 1, pal['R']), mpx(x0 + 5, y0 - 1, pal['o']);
        }
        break;
    case O_GRAVE: MSPR(x0, y0, SPR_GRAVE, 8, 8, false); break;
    case O_BURNT: MSPR(x0, y0, SPR_BURNT, 8, 8, false); break;
    case O_FARM:
        if (!sim_flooded(w, ty * MAP_W + tx)) draw_farm(x0, y0, VAL(t), h);
        break;
    case O_SITE: MSPR(x0, y0, SPR_SITE, 8, 8, false); break;
    case O_HOUSE1:
        MSPR(x0, y0 - 1, SPR_HUT, 8, 9, false);
        if (VAL(t) < 70) mpx(x0 + 2, y0 + 1, pal['e']), mpx(x0 + 5, y0, pal['e']), mpx(x0 + 4, y0 + 2, pal['e']);
        break;
    case O_HOUSE2:
        MSPR(x0, y0 - 2, SPR_HOUSE, 8, 10, false);
        if (VAL(t) < 70) mpx(x0 + 2, y0, pal['e']), mpx(x0 + 5, y0 - 1, pal['e']), mpx(x0 + 4, y0 + 1, pal['e']);
        if (!((frame / 4 + h) % 3)) mpx(x0 + 5, y0 - 3 - (int)(frame / 4 % 2), pal['l']); /* 炊烟 */
        break;
    case O_RUIN: MSPR(x0, y0, SPR_RUIN, 8, 8, false); break;
    case O_SHRINE:
        MSPR(x0, y0 - 2, SPR_SHRINE, 8, 10, false);
        if (w->faith >= 50 && !((frame / 5 + h) % 4)) mpx(x0 + 3 + (int)(frame / 5 % 2), y0 - 4, pal['y']); /* 香火 */
        break;
    case O_FLAME: MSPR(x0, y0 - 1, ((frame / 2 + h) & 1) ? SPR_FLAME_A : SPR_FLAME_B, 8, 8, (frame / 4 + h) & 1); break;
    default: break;
    }
}

static const uint16_t SHIRT[MAX_RES] = {
    RGB(226, 78, 70),   RGB(74, 126, 226),  RGB(246, 200, 66),  RGB(160, 100, 214),
    RGB(60, 180, 170),  RGB(244, 140, 60),  RGB(240, 130, 180), RGB(240, 240, 236),
    RGB(150, 206, 70),  RGB(120, 150, 170), RGB(180, 60, 110),  RGB(40, 90, 160),
    RGB(200, 170, 120), RGB(110, 70, 160),  RGB(30, 130, 110),  RGB(250, 180, 150)};

static void res_pos(const resident_t *r, int *x, int *y)
{
    *x = r->tx * TILE + (r->nx - r->tx) * r->step + 1;
    *y = r->ty * TILE + (r->ny - r->ty) * r->step - 1;
}

static void draw_resident(const world_t *w, int i, uint32_t frame)
{
    const resident_t *r = &w->res[i];
    int x, y, age = sim_age(r);
    res_pos(r, &x, &y);
    uint16_t c = SHIRT[i];
    pal['c'] = c;
    pal['C'] = (uint16_t)(((c >> 1) & 0x7BEF)); /* 上衣暗部 */
    pal['h'] = r->sex ? RGB(150, 76, 40) : RGB(58, 42, 36);
    if (r->name & 1) pal['h'] = r->sex ? RGB(48, 36, 40) : RGB(112, 76, 44);
    if (age >= AGE_ELDER) pal['h'] = RGB(200, 200, 206); /* 老人白了头 */

    bool indoors = sim_in_house(w, r);
    if (indoors && (r->state == S_SLEEP || age < AGE_CHILD || (r->state == S_ACT && r->goal == G_SHELTER))) {
        if (r->state == S_SLEEP) { /* 人在屋里看不见，屋顶上飘着 z */
            int ph = (int)((frame / 4 + (uint32_t)i * 3) % 6);
            if (ph < 4) mpx(x + 5 + ph / 2, y - 3 - ph, pal['w']), mpx(x + 6 + ph / 2, y - 3 - ph, pal['w']);
        }
        return;
    }
    if (age < AGE_CHILD) {
        MSPR(x + 1, y + 4, SPR_BABY, 4, 4, false);
        return;
    }
    if (r->state == S_SLEEP) {
        MSPR(x - 1, y + 4, SPR_SLEEP, 8, 4, false);
        int ph = (int)((frame / 4 + (uint32_t)i * 3) % 6); /* 飘起来的 z */
        if (ph < 4) {
            int zx = x + 5 + ph / 2, zy = y + 2 - ph;
            mpx(zx, zy, pal['w']);
            mpx(zx + 1, zy, pal['w']);
            mpx(zx, zy + 1, pal['w']);
        }
        return;
    }
    bool walking = r->state == S_MOVE;
    bool acting = r->state == S_ACT && (r->goal == G_CHOP || r->goal == G_BERRY || r->goal == G_HUNT ||
                                        r->goal == G_EAT_BERRY || r->goal == G_BUILD || r->goal == G_UPGRADE ||
                                        r->goal == G_REPAIR || r->goal == G_TILL || r->goal == G_HARVEST ||
                                        r->goal == G_MINE || r->goal == G_CHAT || r->goal == G_SHRINE);
    bool fr = walking ? ((frame + (uint32_t)i) / 2) & 1 : false;
    int bob = acting && ((frame + (uint32_t)i) / 2 & 1) ? 1 : 0;
    int top = y;
    if (age < AGE_WORK) {
        MSPR(x + 1, y + 2 + bob, fr ? SPR_KID_B : SPR_KID_A, 4, 6, r->face);
        top = y + 2;
    } else {
        const char *const *s = r->sex ? (fr ? SPR_WOMAN_B : SPR_WOMAN_A) : (fr ? SPR_MAN_B : SPR_MAN_A);
        MSPR(x, y + bob, s, 6, 8, r->face);
    }

    int side = r->face ? -1 : 1, hx = r->face ? x - 1 : x + 6;
    if (r->state == S_ACT) {
        switch (r->goal) {
        case G_PRAY: /* 祈祷时头顶的微光 */
            if ((frame / 3 + (uint32_t)i) & 1) mpx(x + 2, top - 3, pal['y']), mpx(x + 3, top - 2, pal['y']), mpx(x + 4, top - 3, pal['y']);
            else mpx(x + 3, top - 4, pal['w']);
            break;
        case G_COWER: /* 发抖时的汗珠 */
            mpx(x + (r->face ? 0 : 5), top + 1 + (int)(frame / 2 & 1), pal['q']);
            break;
        case G_CHOP: case G_BUILD: case G_UPGRADE: case G_REPAIR: case G_MINE: case G_TILL: case G_SHRINE: /* 手里的工具 */
            mpx(hx, y + 3 + bob * 2, pal['l']);
            mpx(hx + side, y + 3 + bob * 2, pal['l']);
            mpx(hx, y + 4 + bob * 2, pal['t']);
            break;
        case G_FISH: /* 鱼竿和浮漂 */
            for (int k = 0; k < 4; k++) mpx(hx + side * k, y + 3 - k, pal['t']);
            mpx(hx + side * 4, y + 2 + (int)(frame / 4 & 1), pal['R']);
            break;
        case G_EAT_CAMP: mpx(hx, y + 3, pal['R']); break;
        case G_CHAT: /* 头顶的对话泡 */
            if ((frame / 4 + (uint32_t)i) & 1) MRECT(x + 4, top - 4, 4, 3, pal['w']), mpx(x + 4, top - 1, pal['w']);
            break;
        case G_HELP: case G_TEND:
            mpx(x + 2, top - 3, pal['r']), mpx(x + 4, top - 3, pal['r']), MRECT(x + 2, top - 2, 3, 1, pal['r']), mpx(x + 3, top - 1, pal['r']);
            break;
        default: break;
        }
    }
    if (r->carry_n) { /* 头顶扛着的东西 */
        uint16_t cc = r->carry == C_WOOD ? pal['t'] : r->carry == C_STONE ? pal['a'] : pal['R'];
        MRECT(x + 1, top - 3, 4, 2, cc);
        mpx(x + 2, top - 3, r->carry == C_WOOD ? pal['T'] : r->carry == C_STONE ? pal['l'] : pal['y']);
    } else if (r->sick) { /* 病人头顶冒绿点 */
        uint16_t gcol = RGB(120, 230, 90);
        mpx(x + 1, top - 2 - (int)(frame / 3 & 1), gcol);
        mpx(x + 4, top - 3 + (int)(frame / 3 & 1), gcol);
    } else if (r->goal == G_FLEE && (frame & 2)) { /* 逃命时的惊叹号 */
        MRECT(x + 3, top - 5, 1, 3, pal['R']);
        mpx(x + 3, top - 1, pal['R']);
    }
}

static void draw_animal(const animal_t *a, int k, uint32_t frame)
{
    int x = a->tx * TILE + (a->nx - a->tx) * a->step;
    int y = a->ty * TILE + (a->ny - a->ty) * a->step + 1;
    bool fr = a->step ? ((frame + (uint32_t)k) / 2) & 1 : false;
    if (a->held && (frame & 1)) x += 1; /* 被缠住时抖动 */
    if (a->kind == A_RABBIT) MSPR(x + 1, y + 1 - (fr ? 1 : 0), fr ? SPR_RABBIT_B : SPR_RABBIT_A, 6, 5, a->face);
    else if (a->kind == A_WOLF) MSPR(x, y, fr ? SPR_WOLF_B : SPR_WOLF_A, 8, 6, a->face);
    else MSPR(x, y, fr ? SPR_BOAR_B : SPR_BOAR_A, 8, 6, a->face);
}

static int night_level(const world_t *w)
{
    static const uint8_t LV[24] = {6, 6, 6, 6, 5, 3, 1, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 1, 3, 5, 6, 6};
    static const uint8_t DIM[W_COUNT] = {0, 1, 1, 3, 3, 0, 1}; /* 阴雨天整体暗一些 */
    int l = LV[sim_hour(w)] + DIM[w->weather % W_COUNT];
    return l > 7 ? 7 : l;
}

/* 夜色与阴天：整体压暗偏蓝，篝火周围留一圈暖光 */
static void shade_night(const world_t *w, int level)
{
    if (!level) return;
    int k[3][3];
    for (int z = 0; z < 3; z++) {
        int l = level - z * 3;
        if (l < 0) l = 0;
        k[z][0] = 32 - l * 3;
        k[z][1] = 32 - l * 5 / 2;
        k[z][2] = 32 - l;
    }
    int fx = w->camp_x * TILE + 4, fy = w->camp_y * TILE + 4;
    for (int y = 0; y < MAPV_H; y++) {
        uint16_t *p = fb_map + y * SCR_W;
        int dy = y - fy;
        bool near = w->fire_lit && dy > -30 && dy < 30;
        for (int x = 0; x < SCR_W; x++) {
            int z = 0;
            if (near) {
                int dx = x - fx, d2 = dx * dx + dy * dy;
                if (d2 < 16 * 16) z = 2;
                else if (d2 < 28 * 28) z = 1;
            }
            uint16_t c = p[x];
            int r = (c >> 11) * k[z][0] >> 5, g = ((c >> 5) & 63) * k[z][1] >> 5, b = (c & 31) * k[z][2] >> 5;
            p[x] = (uint16_t)(r << 11 | g << 5 | b);
        }
    }
}

/* 雨丝、雪花、风线 */
static void draw_weather(const world_t *w, uint32_t frame)
{
    int wt = w->weather;
    if (wt == W_RAIN || wt == W_STORM || wt == W_THUNDER) {
        int n = wt == W_RAIN ? 70 : 170, len = wt == W_RAIN ? 3 : 5;
        uint16_t c = RGB(170, 206, 240);
        for (int i = 0; i < n; i++) {
            uint32_t h = thash(i, 77);
            int x = (int)((h + frame * 3) % (SCR_W + 40)) - 20, y = (int)(((h >> 9) + frame * 9) % (MAPV_H + 16)) - 8;
            for (int k = 0; k < len; k++) mpx(x - k / 2, y + k, c);
        }
    } else if (wt == W_SNOW) {
        for (int i = 0; i < 150; i++) {
            uint32_t h = thash(i, 99);
            int x = (int)((h + frame * (1 + (i & 1)) + (frame / 5 + (uint32_t)i) % 3) % SCR_W);
            int y = (int)(((h >> 9) + frame * (2 + (i % 3))) % MAPV_H);
            mpx(x, y, pal['w']);
            if (i & 4) mpx(x + 1, y, pal['w']);
        }
    } else if (wt == W_WIND) {
        for (int i = 0; i < 16; i++) {
            uint32_t h = thash(i, 55);
            int x = (int)((h + frame * 13) % (SCR_W + 60)) - 30, y = (int)((h >> 9) % MAPV_H);
            for (int k = 0; k < 12 + (int)(h & 7); k++) mpx(x + k, y + ((k / 5) & 1), RGB(226, 236, 240));
        }
    }
    if (wt == W_THUNDER && thash((int)(frame / 2), 7) % 23 == 0) /* 远处的闪电把天照亮一瞬 */
        for (int p = 0; p < SCR_W * MAPV_H; p++) fb_map[p] |= 0x4208;
}

static void draw_fx(uint32_t frame)
{
    for (int i = 0; i < FX_N; i++) {
        const sim_fx_t *f = &g_fx[i];
        if (!f->t) continue;
        int cx = f->x * TILE + 4, cy = f->y * TILE + 4;
        if (f->type == FX_HEAL) {
            uint16_t c = RGB(140, 255, 150);
            int yy = cy - 6 - (14 - f->t) / 2; /* 上升的绿色十字 */
            mpx(cx, yy, c), mpx(cx - 1, yy, c), mpx(cx + 1, yy, c), mpx(cx, yy - 1, c), mpx(cx, yy + 1, c);
        } else if (f->type == FX_SPARK) {
            static const int8_t D[8][2] = {{1, 0}, {-1, 0}, {0, 1}, {0, -1}, {1, 1}, {-1, 1}, {1, -1}, {-1, -1}};
            int rad = 3 + (14 - f->t);
            uint16_t c = (frame & 1) ? pal['y'] : pal['w'];
            for (int d = 0; d < 8; d++) {
                int m = d < 4 ? rad : rad * 3 / 4;
                mpx(cx + D[d][0] * m, cy + D[d][1] * m, c);
            }
            mpx(cx, cy, pal['w']);
        } else if (f->type == FX_BOLT) {
            if (f->t > 6) { /* 闪电本体 */
                int x = cx;
                for (int y = 0; y <= cy; y++) {
                    if (y % 6 == 5) x += (int)(thash(y, f->t) % 7) - 3;
                    if (cy - y < 8) x += (x < cx) - (x > cx);
                    mpx(x, y, pal['w']);
                    mpx(x + 1, y, pal['y']);
                }
            }
            for (int n = 0; n < 14; n++) { /* 火星 */
                uint32_t h = thash(n, (int)frame);
                mpx(cx + (int)(h % 33) - 16, cy + (int)(h >> 8) % 33 - 16, (h & 0x10000) ? pal['o'] : pal['y']);
            }
        }
    }
}

/* 火山口上方的烟柱 */
static void draw_smoke(const world_t *w, uint32_t frame)
{
    if (!w->volcano_h) return;
    int cx = w->crater % MAP_W * TILE + 4, cy = w->crater / MAP_W * TILE;
    for (int n = 0; n < 40; n++) {
        uint32_t h = thash(n, 31);
        int up = (int)((h + frame * 2) % 44);
        int x = cx + (int)(h >> 8) % (5 + up / 3) - (2 + up / 6), y = cy - up;
        uint16_t c = up < 8 ? pal['o'] : (h & 0x10000) ? pal['A'] : pal['e'];
        mpx(x, y, c);
        mpx(x + 1, y, c);
    }
}

#define MAPT(x, y, s, c) font_draw(fb_map, SCR_W, MAPV_H, x, y, s, c)

static const char *fmt_person(const world_t *w, const person_t *p, char *buf, size_t n)
{
    const resident_t *r = sim_resident_by_uid(w, p->uid);
    if (r) snprintf(buf, n, "%d岁", sim_age(r));
    else if (p->age == 0xFF) snprintf(buf, n, "下落不明");
    else snprintf(buf, n, "已故 享年%d", p->age);
    return buf;
}

static void render_tree(const world_t *w, const ui_t *ui)
{
    char buf[96], tmp[40];
    MRECT(0, 0, SCR_W, MAPV_H, C_BG);
    MAPT(12, 5, "家谱", C_GOLD);
    snprintf(buf, sizeof buf, "在册%d人 已传%d代", sim_tree_count(), w->max_gen);
    MAPT(228 - font_width(buf), 5, buf, C_DIM);

    const person_t *p = sim_person(ui->tree_uid);
    if (!p) {
        MAPT(12, 40, "这个人的记录已经湮没", C_DIM);
        return;
    }
    const resident_t *r = sim_resident_by_uid(w, p->uid);
    MRECT(8, 22, 224, 46, C_PANEL);
    MRECT(8, 22, 3, 46, C_GOLD);
    int x = MAPT(16, 26, sim_name_of(p->sex, p->name), C_GOLD);
    if (p->gen == 1) snprintf(buf, sizeof buf, " %s 第1代 登岛先民", p->sex ? "女" : "男");
    else snprintf(buf, sizeof buf, " %s 第%d代 生于第%d天", p->sex ? "女" : "男", p->gen, p->born);
    MAPT(x, 26, buf, C_TEXT);
    if (r) snprintf(buf, sizeof buf, "在世 %d岁 %s", sim_age(r), sim_trait(r));
    else if (p->age == 0xFF) snprintf(buf, sizeof buf, "下落不明");
    else snprintf(buf, sizeof buf, "享年%d岁 死于%s", p->age, sim_cause_name(p->cause));
    MAPT(16, 39, buf, r ? C_TEXT : C_DIM);
    int living = 0, total = sim_descendants(p->uid, &living);
    if (!total) snprintf(buf, sizeof buf, "没有后代");
    else if (!living) snprintf(buf, sizeof buf, "后代%d人 血脉已断", total);
    else snprintf(buf, sizeof buf, "后代%d人 在世%d人", total, living);
    MAPT(16, 52, buf, (total && !living) ? C_RED : C_DIM);

    uint16_t rel[TREE_ROWS];
    uint8_t role[TREE_ROWS];
    int n = sim_relatives(w, p->uid, rel, role, TREE_ROWS);
    if (!n) MAPT(16, 80, "没有登记在册的亲属", C_DIM);
    for (int k = 0; k < n; k++) {
        const person_t *q = sim_person(rel[k]);
        int y = 74 + k * 13;
        bool cur = k == ui->tree_cur;
        if (cur) {
            MRECT(8, y - 1, 224, 13, C_PANEL);
            for (int t = 0; t < 5; t++) MRECT(10, y + 3 + t, (t < 3 ? t : 4 - t) + 1, 1, C_GOLD);
        }
        const char *tag = role[k] == 0 ? "父" : role[k] == 1 ? "母" : role[k] == 2 ? "偶" : q->sex ? "女" : "子";
        MAPT(18, y, tag, C_DIM);
        MAPT(36, y, sim_name_of(q->sex, q->name), cur ? C_GOLD : C_TEXT);
        MAPT(68, y, fmt_person(w, q, tmp, sizeof tmp), sim_resident_by_uid(w, q->uid) ? C_TEXT : C_DIM);
        snprintf(buf, sizeof buf, "第%d代", q->gen);
        MAPT(228 - font_width(buf), y, buf, C_DIM);
    }
}

void render_map(const world_t *w, const ui_t *ui)
{
    uint32_t frame = ui->frame;
    if (ui->inspect == 4) { render_tree(w, ui); return; }
    for (int ty = 0; ty < MAP_H; ty++)
        for (int tx = 0; tx < MAP_W; tx++) tile_base(w, tx, ty, frame);
    /* 自上而下逐行画：先地物，再站在这一行的生物，下一行的树会自然压住它们 */
    for (int ty = 0; ty < MAP_H; ty++) {
        for (int tx = 0; tx < MAP_W; tx++) tile_obj(w, tx, ty, frame);
        for (int k = 0; k < MAX_ANI; k++)
            if (w->ani[k].alive && w->ani[k].ty == ty) draw_animal(&w->ani[k], k, frame);
        for (int i = 0; i < MAX_RES; i++)
            if (w->res[i].alive && w->res[i].ty == ty) draw_resident(w, i, frame);
    }
    shade_night(w, night_level(w));
    draw_weather(w, frame);
    draw_smoke(w, frame);
    bool quake = false;
    for (int i = 0; i < FX_N; i++) {
        if (!g_fx[i].t) continue;
        if (g_fx[i].type == FX_QUAKE) quake = true;
        if (g_fx[i].type == FX_BOLT && g_fx[i].t > 8) /* 闪电瞬间把整张地图照亮 */
            for (int p = 0; p < SCR_W * MAPV_H; p++) fb_map[p] |= 0x8410;
    }
    draw_fx(frame);
    if (ui->inspect == 1 && w->res[ui->who].alive) { /* 被观察者头顶的箭头 */
        int x, y;
        res_pos(&w->res[ui->who], &x, &y);
        y -= 8 + (int)(frame / 3 & 1);
        for (int r = 0; r < 3; r++)
            for (int c = r; c < 5 - r; c++) mpx(x + c, y + r, C_GOLD);
        mpx(x + 2, y + 3, C_GOLD);
    }
    if (quake) { /* 地震：整屏左右错位 */
        for (int y = 0; y < MAPV_H; y++) {
            int s = (int)(thash(y / 6, (int)frame) % 7) - 3;
            uint16_t *p = fb_map + y * SCR_W;
            if (s > 0) memmove(p + s, p, (size_t)(SCR_W - s) * 2);
            else if (s < 0) memmove(p, p - s, (size_t)(SCR_W + s) * 2);
        }
    }
}

/* ---------- 顶栏与底部面板 ---------- */

#define TOPT(x, y, s, c) font_draw(fb_top, SCR_W, TOP_H, x, y, s, c)
#define BOTT(x, y, s, c) font_draw(fb_bot, SCR_W, BOT_H, x, y, s, c)
#define BRECT(x, y, w, h, c) rect(fb_bot, SCR_W, BOT_H, x, y, w, h, c)

void render_top(const world_t *w, const ui_t *ui)
{
    (void)ui;
    char buf[40];
    rect(fb_top, SCR_W, TOP_H, 0, 0, SCR_W, TOP_H, C_BG);
    rect(fb_top, SCR_W, TOP_H, 0, TOP_H - 1, SCR_W, 1, C_LINE);
    int h = sim_hour(w);
    spr(fb_top, SCR_W, TOP_H, 14, 8, (h >= 6 && h < 19) ? SPR_SUN : SPR_MOON, 8, 8, false);
    spr(fb_top, SCR_W, TOP_H, 24, 8, ICON_W[w->weather % W_COUNT], 8, 8, false);
    snprintf(buf, sizeof buf, "第%d天 %d时", sim_day(w), h);
    TOPT(w->weather == W_SUNNY ? 26 : 35, 6, buf, C_TEXT);
    if (w->paused) snprintf(buf, sizeof buf, "已暂停");
    else snprintf(buf, sizeof buf, "%d倍速", sim_speed(w));
    TOPT(136, 6, buf, (w->paused || sim_speed(w) > 1) ? C_GOLD : C_DIM);
    snprintf(buf, sizeof buf, "人口%d", sim_pop(w));
    TOPT(224 - font_width(buf), 6, buf, C_TEXT);
    if (w->voice_on) spr(fb_top, SCR_W, TOP_H, 176, 8, ICON_MIC, 8, 8, false);
}

static void bar(int x, int y, int wpx, int v, uint16_t c)
{
    BRECT(x, y, wpx, 8, C_LINE);
    BRECT(x + 1, y + 1, (wpx - 2) * v / 1000, 6, c);
}

static void hint(const char *s) { BOTT(120 - font_width(s) / 2, 73, s, C_DIM); }

static void panel_resident(const world_t *w, const ui_t *ui)
{
    char buf[96];
    const resident_t *r = &w->res[ui->who];
    BRECT(10, 4, 4, 10, SHIRT[ui->who]);
    int x = BOTT(18, 3, sim_name(r), C_GOLD);
    snprintf(buf, sizeof buf, " %s %d岁 %s", r->sex ? "女" : "男", sim_age(r), sim_trait(r));
    x = BOTT(x, 3, buf, C_TEXT);
    if (r->sick) x = BOTT(x + 6, 3, "染病", C_RED);
    if (r->preg) BOTT(x + 6, 3, "有孕", RGB(240, 130, 190));
    snprintf(buf, sizeof buf, "正在：%s", sim_status(w, r));
    BOTT(10, 16, buf, C_TEXT);
    /* 家庭：两行信息轮流显示 */
    if ((ui->frame / 35) & 1) {
        if (r->fname != NO_SLOT || r->mname != NO_SLOT)
            snprintf(buf, sizeof buf, "父%s 母%s", r->fname != NO_SLOT ? sim_name_of(0, r->fname) : "不详",
                     r->mname != NO_SLOT ? sim_name_of(1, r->mname) : "不详");
        else snprintf(buf, sizeof buf, "初代居民 %s", r->home != NO_ID ? "有房" : "住在营地");
    } else {
        int n = 0;
        if (r->partner != NO_SLOT && w->res[r->partner].alive)
            n = snprintf(buf, sizeof buf, "伴侣%s ", sim_name(&w->res[r->partner]));
        else n = snprintf(buf, sizeof buf, "单身 ");
        snprintf(buf + n, sizeof buf - (size_t)n, "子女%d 第%d代", r->kids, r->gen);
    }
    BOTT(10, 29, buf, C_DIM);
    BOTT(10, 43, "生命", C_DIM);
    bar(38, 45, 72, r->hp, C_RED);
    BOTT(124, 43, "饱食", C_DIM);
    bar(152, 45, 72, r->food, RGB(246, 170, 70));
    BOTT(10, 57, "体力", C_DIM);
    bar(38, 59, 72, r->energy, RGB(90, 170, 240));
    BOTT(124, 57, "心情", C_DIM);
    bar(152, 59, 72, r->mood, RGB(240, 130, 190));
    hint("上下切换居民 确认返回");
}

static void meter(int x, int y, int v, uint16_t c)
{
    BRECT(x, y, 44, 8, C_LINE);
    BRECT(x + 1, y + 1, 42 * v / 100, 6, c);
}

static void panel_records(const world_t *w)
{
    char buf[96];
    snprintf(buf, sizeof buf, "逝者%d人 新生%d人 已传%d代", w->deaths, w->births, w->max_gen);
    BOTT(10, 3, buf, C_GOLD);
    BOTT(10, 17, "信仰", C_DIM);
    meter(38, 19, w->faith, C_GOLD);
    BOTT(92, 17, "恐惧", C_DIM);
    meter(120, 19, w->fear, C_RED);
    BOTT(174, 17, w->shrine != NO_ID ? "有祭石" : "无祭石", C_DIM);
    int shown = 0;
    for (int k = 1; k <= MAX_DEAD && shown < 3; k++) { /* 最近去世的排在最前 */
        const dead_t *d = &w->dead[(uint8_t)(w->dead_n - k) % MAX_DEAD];
        if (!d->used) continue;
        snprintf(buf, sizeof buf, "%s %s %d岁 %s 第%d代", sim_name_of(d->sex, d->name), d->sex ? "女" : "男", d->age,
                 sim_cause_name(d->cause), d->gen);
        BOTT(10, 31 + shown * 13, buf, shown ? C_DIM : C_TEXT);
        shown++;
    }
    if (!shown) BOTT(10, 36, "岛上还没有人离世", C_DIM);
    hint("确认返回");
}

/* 离线补算的进度 */
static void panel_catchup(const world_t *w, const ui_t *ui)
{
    char buf[96];
    BOTT(10, 6, "你不在的时候，岛上的日子照常过", C_GOLD);
    snprintf(buf, sizeof buf, "第%d天  人口%d", sim_day(w), sim_pop(w));
    BOTT(10, 24, buf, C_TEXT);
    BRECT(10, 44, 220, 10, C_LINE);
    uint32_t done = ui->catch_total - ui->catch_left;
    BRECT(11, 45, (int)(218 * (uint64_t)done / (ui->catch_total ? ui->catch_total : 1)), 8, C_GOLD);
    hint("确认键跳过");
}

/* 归来纪事 */
static void panel_chronicle(const world_t *w, const ui_t *ui)
{
    char buf[128];
    const chron_t *c = &ui->chron;
    uint32_t h = c->away_s / 3600, m = c->away_s % 3600 / 60;
    int n = 0;
    if (h) n = snprintf(buf, sizeof buf, "你离开了%u小时", (unsigned)h);
    else n = snprintf(buf, sizeof buf, "你离开了%u分钟", (unsigned)m);
    snprintf(buf + n, sizeof buf - (size_t)n, " 岛上过了%d天", c->days);
    BOTT(10, 3, buf, C_GOLD);
    snprintf(buf, sizeof buf, "新生%d人 去世%d人 结伴%d对", c->births, c->deaths, c->marriages);
    BOTT(10, 18, buf, C_TEXT);
    snprintf(buf, sizeof buf, "新盖房屋%d间 现有%d人 第%d代", c->built, sim_pop(w), w->max_gen);
    BOTT(10, 32, buf, C_TEXT);
    /* 期间离世的人里最近的两位 */
    n = 0;
    int shown = 0;
    buf[0] = 0;
    for (int k = 1; k <= MAX_DEAD && shown < 2; k++) {
        const dead_t *d = &w->dead[(uint8_t)(w->dead_n - k) % MAX_DEAD];
        if (!d->used || d->day < c->from_day) continue;
        n += snprintf(buf + n, sizeof buf - (size_t)n, "%s%s%d岁%s", shown ? " " : "送别：", sim_name_of(d->sex, d->name),
                      d->age, sim_cause_name(d->cause));
        shown++;
    }
    if (!shown) snprintf(buf, sizeof buf, "%s", sim_pop(w) ? "这段日子无人离世" : "岛上已无人烟");
    BOTT(10, 46, buf, C_DIM);
    hint("确认继续");
}

void render_bot(const world_t *w, const ui_t *ui)
{
    char buf[96];
    BRECT(0, 0, SCR_W, BOT_H, C_BG);
    BRECT(0, 0, SCR_W, 1, C_LINE);

    if (ui->catching) { panel_catchup(w, ui); return; }
    if (ui->inspect == 1 && w->res[ui->who].alive) { panel_resident(w, ui); return; }
    if (ui->inspect == 2) { panel_records(w); return; }
    if (ui->inspect == 3) { panel_chronicle(w, ui); return; }
    if (ui->inspect == 4) {
        BOTT(10, 10, "上下：选择一位亲属", C_TEXT);
        BOTT(10, 26, "确认：转到此人的家谱", C_TEXT);
        BOTT(10, 42, "长按确认：返回岛屿", C_TEXT);
        return;
    }

    snprintf(buf, sizeof buf, "粮%d 木%d 石%d 猪%d 兔%d 狼%d", w->food, w->wood, w->stone, sim_kind_count(w, A_BOAR),
             sim_kind_count(w, A_RABBIT), sim_kind_count(w, A_WOLF));
    BOTT(10, 4, buf, C_TEXT);
    const char *wn = sim_weather_name(w->weather);
    BOTT(230 - font_width(wn), 4, wn, w->weather >= W_STORM ? C_GOLD : C_DIM);
    /* 最近两条事件，旧的在上、颜色更暗 */
    if (g_log.seq >= 2) BOTT(10, 19, g_log.msg[(g_log.seq - 2) % LOG_N], C_DIM);
    if (g_log.seq >= 1) BOTT(10, 33, g_log.msg[(g_log.seq - 1) % LOG_N], C_GOLD);

    /* 神力选择条：类别 · 名称 */
    BRECT(40, 50, 160, 19, C_PANEL);
    BRECT(40, 50, 160, 1, C_LINE);
    BRECT(40, 68, 160, 1, C_LINE);
    const char *cat = sim_power_cat(ui->sel);
    if (ui->sel == P_SPEED) snprintf(buf, sizeof buf, "流速%d倍", sim_speed(w));
    else if (ui->sel == P_SOUND) snprintf(buf, sizeof buf, "音效 %s", w->sound_on ? "开" : "关");
    else if (ui->sel == P_VOICE) snprintf(buf, sizeof buf, "声控神力 %s", w->voice_on ? "开" : "关");
    else snprintf(buf, sizeof buf, "%s", sim_power_name(ui->sel));
    int total = font_width(cat) + 14 + font_width(buf);
    int x = 120 - total / 2;
    x = BOTT(x, 54, cat, C_DIM);
    BRECT(x + 6, 59, 2, 2, C_DIM);
    BOTT(x + 14, 54, buf, sim_power_cat_id(ui->sel) == 4 ? C_RED : C_TEXT);
    for (int r = 0; r < 5; r++) { /* 左右两个小三角 */
        int half = r < 3 ? r : 4 - r;
        BRECT(31 - half, 57 + r, half + 1, 1, C_GOLD);
        BRECT(207, 57 + r, half + 1, 1, C_GOLD);
    }
    if ((ui->frame / 40) & 1) hint("长按上下换类别 长按确认暂停");
    else hint(ui->sel <= P_RECORDS ? "上下选择 确认查看" : ui->sel >= P_SPEED ? "上下选择 确认切换" : "上下选择 确认施放");
}
