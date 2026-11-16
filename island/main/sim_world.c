/*
 * 《造物主：孤岛》模拟核心（二）：地图生成、野兽、生态、天气、灾害、世代更替、神力。
 */
#include <string.h>

#include "sim_int.h"

#define DX SIM_DX
#define DY SIM_DY
#define rn sim_rn
#define cheb sim_cheb
#define inmap sim_inmap

static bool is_land(int t) { return t == T_SAND || t == T_GRASS || t == T_ASH; }
static int dist2(int idx, int cx, int cy)
{
    int dx = idx % MAP_W - cx, dy = idx / MAP_W - cy;
    return dx * dx + dy * dy;
}

/* ---------- 地图生成 ---------- */

static int hash2(uint32_t seed, int x, int y)
{
    uint32_t h = seed ^ ((uint32_t)x * 374761393u) ^ ((uint32_t)y * 668265263u);
    h = (h ^ (h >> 13)) * 1274126177u;
    return (int)((h ^ (h >> 16)) & 255);
}
static int vnoise(uint32_t seed, int x, int y, int cell)
{
    int gx = x / cell, gy = y / cell, fx = x % cell, fy = y % cell;
    int a = hash2(seed, gx, gy), b = hash2(seed, gx + 1, gy);
    int c = hash2(seed, gx, gy + 1), d = hash2(seed, gx + 1, gy + 1);
    int top = a * (cell - fx) + b * fx, bot = c * (cell - fx) + d * fx;
    return (top * (cell - fy) + bot * fy) / (cell * cell);
}

static bool near8(const world_t *w, int x, int y, int terr)
{
    for (int dy = -1; dy <= 1; dy++)
        for (int dx = -1; dx <= 1; dx++)
            if (inmap(x + dx, y + dy) && TERR(w->tiles[IDX(x + dx, y + dy)]) == terr) return true;
    return false;
}

bool sim_gen_map(world_t *w, uint32_t seed)
{
    static uint8_t elev[MAP_N];
    for (int y = 0; y < MAP_H; y++)
        for (int x = 0; x < MAP_W; x++) {
            int dx = x * 2 + 1 - MAP_W, dy = y * 2 + 1 - MAP_H;
            int d2 = dx * dx * 1000 / (MAP_W * MAP_W) + dy * dy * 1000 / (MAP_H * MAP_H);
            int e = 230 - d2 * 200 / 1000 + (vnoise(seed, x, y, 6) - 128) * 70 / 128;
            if (x == 0 || y == 0 || x == MAP_W - 1 || y == MAP_H - 1 || d2 > 900) e = 0;
            if (e < 0) e = 0;
            if (e > 255) e = 255;
            elev[IDX(x, y)] = (uint8_t)e;
            w->tiles[IDX(x, y)] = MKTILE(e < 105 ? T_SEA : T_GRASS, O_NONE);
        }
    /* 海岸：靠海的草地变沙滩，靠沙滩的海变浅滩 */
    for (int y = 0; y < MAP_H; y++)
        for (int x = 0; x < MAP_W; x++)
            if (TERR(w->tiles[IDX(x, y)]) == T_GRASS && near8(w, x, y, T_SEA)) w->tiles[IDX(x, y)] = MKTILE(T_SAND, O_NONE);
    for (int y = 0; y < MAP_H; y++)
        for (int x = 0; x < MAP_W; x++)
            if (TERR(w->tiles[IDX(x, y)]) == T_SEA && near8(w, x, y, T_SAND)) w->tiles[IDX(x, y)] = MKTILE(T_SHOAL, O_NONE);
    /* 山地 */
    for (int i = 0; i < MAP_N; i++)
        if (TERR(w->tiles[i]) == T_GRASS && elev[i] > 150 && vnoise(seed + 77, i % MAP_W, i / MAP_W, 4) > 180)
            w->tiles[i] = MKTILE(T_ROCK, O_NONE);
    /* 湖泊 + 一条流向海边的河 */
    for (int tries = 0; tries < 80; tries++) {
        int lx = 4 + rn(w, MAP_W - 8), ly = 4 + rn(w, MAP_H - 8);
        if (elev[IDX(lx, ly)] < 160 || cheb(lx, ly, MAP_W / 2, MAP_H / 2) < 4) continue;
        for (int dy = -2; dy <= 2; dy++)
            for (int dx = -2; dx <= 2; dx++) {
                int x = lx + dx, y = ly + dy;
                if (!inmap(x, y) || dx * dx + dy * dy > 2 + rn(w, 3)) continue;
                int t = TERR(w->tiles[IDX(x, y)]);
                if (t == T_GRASS || t == T_ROCK) w->tiles[IDX(x, y)] = MKTILE(T_LAKE, O_NONE);
            }
        int d; /* 朝离湖最近的地图边缘流去 */
        int dl = lx, dr = MAP_W - 1 - lx, du = ly, dd = MAP_H - 1 - ly;
        if (dl <= dr && dl <= du && dl <= dd) d = 1;
        else if (dr <= du && dr <= dd) d = 0;
        else if (du <= dd) d = 3;
        else d = 2;
        int x = lx, y = ly;
        for (int k = 0; k < 40; k++) {
            if (rn(w, 100) < 30) {
                int side = (d < 2) ? 2 + rn(w, 2) : rn(w, 2);
                x += DX[side];
                y += DY[side];
            } else {
                x += DX[d];
                y += DY[d];
            }
            if (!inmap(x, y)) break;
            int t = TERR(w->tiles[IDX(x, y)]);
            if (t == T_SEA || t == T_SHOAL) break;
            if (t != T_LAKE) w->tiles[IDX(x, y)] = MKTILE(T_RIVER, O_NONE);
        }
        break;
    }
    /* 营地：离中心最近、5x5 范围内都是平地的格子 */
    int best = -1, bestd = 9999;
    for (int y = 3; y < MAP_H - 3; y++)
        for (int x = 3; x < MAP_W - 3; x++) {
            bool ok = true;
            for (int dy = -2; dy <= 2 && ok; dy++)
                for (int dx = -2; dx <= 2; dx++) {
                    int t = TERR(w->tiles[IDX(x + dx, y + dy)]);
                    if (t != T_GRASS && t != T_SAND) { ok = false; break; }
                }
            int d = (x > MAP_W / 2 ? x - MAP_W / 2 : MAP_W / 2 - x) + (y > MAP_H / 2 ? y - MAP_H / 2 : MAP_H / 2 - y);
            if (ok && d < bestd) { bestd = d; best = IDX(x, y); }
        }
    if (best < 0) return false;
    w->camp_x = (uint8_t)(best % MAP_W);
    w->camp_y = (uint8_t)(best / MAP_W);
    /* 森林、浆果丛、野花 */
    int berries = 0;
    for (int i = 0; i < MAP_N; i++) {
        if (TERR(w->tiles[i]) != T_GRASS) continue;
        int x = i % MAP_W, y = i / MAP_W;
        if (cheb(x, y, w->camp_x, w->camp_y) <= 2) continue;
        int o = O_NONE, r = rn(w, 100);
        if (vnoise(seed + 191, x, y, 5) > 138) {
            if (r < 75) o = (r < 52) ? O_TREE : (r < 67) ? O_YOUNG : O_SAPLING;
        } else if (r < 4) {
            o = O_BERRY;
            berries++;
        } else if (r < 9) {
            o = O_FLOWER;
        } else if (r < 11) {
            o = O_TREE;
        }
        w->tiles[i] = MKTILE(T_GRASS, o);
    }
    for (int tries = 0; berries < 12 && tries < 400; tries++) {
        int i = rn(w, MAP_N);
        if (w->tiles[i] == MKTILE(T_GRASS, O_NONE) && cheb(i % MAP_W, i / MAP_W, w->camp_x, w->camp_y) > 2) {
            w->tiles[i] = MKTILE(T_GRASS, O_BERRY);
            berries++;
        }
    }
    w->tiles[best] = MKTILE(TERR(w->tiles[best]), O_FIRE);
    w->tiles[best + 1] = MKTILE(TERR(w->tiles[best + 1]), O_STORE);

    /* 校验：从营地出发要够得着足够的陆地、树、浆果和水边 */
    int sx, sy;
    if (!sim_camp_spot(w, 0, &sx, &sy)) return false;
    memset(sim_danger, 0, sizeof sim_danger);
    sim_set_self(-1);
    sim_bfs(w, sx, sy, sim_pred_none, NULL); /* 铺满可达区域 */
    int land = 0, trees = 0, bush = 0, shore = 0;
    for (int i = 0; i < MAP_N; i++) {
        if (!sim_par[i]) continue;
        land++;
        if (OBJ(w->tiles[i]) == O_TREE) trees++;
        if (OBJ(w->tiles[i]) == O_BERRY) bush++;
        if (sim_pred_shore(w, i)) shore++;
    }
    return land >= 200 && trees >= 25 && bush >= 6 && shore >= 6;
}

void sim_spawn_animal(world_t *w, int x, int y, int kind)
{
    for (int i = 0; i < MAX_ANI; i++)
        if (!w->ani[i].alive) {
            memset(&w->ani[i], 0, sizeof(animal_t));
            w->ani[i].alive = 1;
            w->ani[i].kind = (uint8_t)kind;
            w->ani[i].tx = w->ani[i].nx = (uint8_t)x;
            w->ani[i].ty = w->ani[i].ny = (uint8_t)y;
            w->ani[i].face = (uint8_t)rn(w, 2);
            return;
        }
}

/* 随机找一块离营地至少 mind 格的草地 */
int sim_rand_grass(world_t *w, int mind, bool empty_only)
{
    for (int tries = 0; tries < 200; tries++) {
        int i = rn(w, MAP_N);
        int t = TERR(w->tiles[i]), o = OBJ(w->tiles[i]);
        if (t != T_GRASS && t != T_ASH) continue;
        if (empty_only && o != O_NONE && o != O_FLOWER) continue;
        if (o == O_FIRE || o == O_STORE || o == O_GRAVE || o >= O_FARM) continue;
        if (cheb(i % MAP_W, i / MAP_W, w->camp_x, w->camp_y) < mind) continue;
        return i;
    }
    return -1;
}

static void recount(const world_t *w)
{
    sim_farms = sim_houses = 0;
    for (int i = 0; i < MAP_N; i++) {
        int o = OBJ(w->tiles[i]);
        sim_farms += o == O_FARM;
        sim_houses += sim_is_house(o);
    }
}

void sim_refresh(world_t *w)
{
    sim_calc_wdist(w);
    sim_calc_danger(w);
    recount(w);
}

void sim_new(world_t *w, uint32_t seed)
{
    if (seed == 0) seed = 0x1234567u;
    for (int attempt = 0; attempt < 64; attempt++) {
        memset(w, 0, sizeof *w);
        w->rng = seed + (uint32_t)attempt * 2654435761u;
        if (w->rng == 0) w->rng = 1;
        if (sim_gen_map(w, w->rng)) break;
    }
    memset(&g_log, 0, sizeof g_log);
    memset(g_fx, 0, sizeof g_fx);
    memset(g_tree, 0, sizeof g_tree);
    g_tree_dirty = true;
    w->shrine = NO_ID;
    w->faith = 20;
    w->sound_on = 1;
    w->tick = 7 * TICKS_PER_HOUR; /* 第一天早上 7 点开始 */
    w->food = 8;
    w->wood = 8;
    w->fish = 20;
    w->fire_lit = 1;
    w->next_uid = 1;
    w->max_gen = 1;
    w->weather = W_SUNNY;
    w->weather_h = 10;
    sim_refresh(w);
    /* 5 位初始居民，性格各不相同 */
    uint8_t tr[TR_COUNT] = {0, 1, 2, 3, 4};
    for (int i = TR_COUNT - 1; i > 0; i--) {
        int j = rn(w, i + 1);
        uint8_t t = tr[i];
        tr[i] = tr[j];
        tr[j] = t;
    }
    for (int i = 0; i < 5; i++) sim_spawn_resident(w, i & 1, tr[i], 18 + rn(w, 10), -1);
    for (int i = 0; i < 10; i++) { /* 6 头野猪、4 只兔子 */
        int t = sim_rand_grass(w, 5, false);
        if (t >= 0) sim_spawn_animal(w, t % MAP_W, t / MAP_W, i < 6 ? A_BOAR : A_RABBIT);
    }
    sim_logf("五位居民来到了这座孤岛");
}

/* ---------- 野兽 ---------- */

static bool ani_ok(const world_t *w, const animal_t *a, int x, int y)
{
    if (!inmap(x, y)) return false;
    int idx = IDX(x, y), t = TERR(w->tiles[idx]), o = OBJ(w->tiles[idx]);
    if (!sim_walkable(w, idx) || t == T_RIVER || sim_is_house(o) || sim_danger[idx] == 1) return false;
    /* 平时不进营地；已经在营地里的只许往外走 */
    int d = cheb(x, y, w->camp_x, w->camp_y);
    return a->flee || d > 3 || d >= cheb(a->tx, a->ty, w->camp_x, w->camp_y);
}

static void ani_step(animal_t *a, int d)
{
    a->nx = (uint8_t)(a->tx + DX[d]);
    a->ny = (uint8_t)(a->ty + DY[d]);
    if (DX[d]) a->face = DX[d] < 0;
    a->step = 1;
}

/* 朝 (gx,gy) 靠近（toward）或远离一步；走不通返回 false */
static bool ani_seek(world_t *w, animal_t *a, int gx, int gy, bool toward)
{
    int best = -1, bestv = toward ? 9999 : -1, d = rn(w, 4);
    for (int t = 0; t < 4; t++, d = (d + 1) & 3) {
        int nx = a->tx + DX[d], ny = a->ty + DY[d];
        if (!ani_ok(w, a, nx, ny)) continue;
        int v = (nx > gx ? nx - gx : gx - nx) + (ny > gy ? ny - gy : gy - ny);
        if (toward ? v < bestv : v > bestv) { bestv = v; best = d; }
    }
    if (best < 0) return false;
    ani_step(a, best);
    return true;
}

static void wolf_think(world_t *w, int k)
{
    animal_t *a = &w->ani[k];
    /* 咬人：饿了的狼会袭击落单的人。篝火三格之内它不敢靠近 */
    if (a->hunger >= 12 && rn(w, 100) < 12)
        for (int i = 0; i < MAX_RES; i++) {
            resident_t *r = &w->res[i];
            if (!r->alive || sim_age(r) < AGE_CHILD || sim_in_house(w, r)) continue;
            if (cheb(a->tx, a->ty, r->tx, r->ty) > 1) continue;
            if (w->fire_lit && cheb(r->tx, r->ty, w->camp_x, w->camp_y) <= 3) continue;
            bool fighter = r->trait == TR_BRAVE && sim_age(r) >= AGE_ADULT && sim_age(r) < AGE_ELDER;
            if (fighter && rn(w, 100) < 55) {
                a->alive = 0;
                sim_logf("%s打死了一头狼", sim_name(r));
                return;
            }
            if (sim_hurt(w, i, (sim_age(r) < AGE_WORK || sim_age(r) >= AGE_ELDER) ? 320 : 240, D_WOLF)) {
                sim_logf("%s被狼咬伤了", sim_name(r));
                r->hurt_cd = 40;
            }
            sim_sfx(SFX_WOLF);
            a->hunger = (uint8_t)(a->hunger > 10 ? a->hunger - 10 : 0);
            a->flee = 6;
            return;
        }
    /* 捕食：追最近的兔子或野猪，贴上就吃掉 */
    int best = -1, bestd = 99;
    if (a->hunger >= 6)
        for (int j = 0; j < MAX_ANI; j++) {
            const animal_t *p = &w->ani[j];
            if (!p->alive || p->kind == A_WOLF || p->held) continue;
            int d = cheb(a->tx, a->ty, p->tx, p->ty);
            if (d < bestd) { bestd = d; best = j; }
        }
    if (best >= 0 && bestd <= 1) {
        w->ani[best].alive = 0;
        a->hunger = 0;
        return;
    }
    if (best >= 0 && bestd <= 12 && ani_seek(w, a, w->ani[best].tx, w->ani[best].ty, true)) return;
    if (rn(w, 30) == 0) {
        int d = rn(w, 4);
        if (ani_ok(w, a, a->tx + DX[d], a->ty + DY[d])) ani_step(a, d);
    }
}

void sim_ani_tick(world_t *w, int k)
{
    animal_t *a = &w->ani[k];
    if (!a->alive || a->held) return;
    if (a->step) {
        int sp = a->flee ? 2 : 1;
        if (a->kind == A_WOLF) sp = 2;
        if (a->kind == A_RABBIT && a->flee) sp = 2;
        a->step += sp;
        if (a->step < TILE) return;
        a->tx = a->nx;
        a->ty = a->ny;
        a->step = 0;
        if (a->flee) a->flee--;
    }
    int here = IDX(a->tx, a->ty);
    if (OBJ(w->tiles[here]) == O_FLAME || TERR(w->tiles[here]) == T_LAVA) { a->alive = 0; return; }
    if (sim_danger[here] && !a->flee) a->flee = 4; /* 野兽也怕火怕水 */
    if (a->kind == A_WOLF && !a->flee) { wolf_think(w, k); return; }
    if (a->kind == A_RABBIT && !a->flee && (w->tick + (uint32_t)k) % 4 == 0) {
        /* 兔子胆小：人或狼走到两格之内就跑 */
        for (int i = 0; i < MAX_RES; i++)
            if (w->res[i].alive && cheb(a->tx, a->ty, w->res[i].tx, w->res[i].ty) <= 2) {
                a->flee = 3;
                if (ani_seek(w, a, w->res[i].tx, w->res[i].ty, false)) return;
            }
        for (int j = 0; j < MAX_ANI; j++)
            if (w->ani[j].alive && w->ani[j].kind == A_WOLF && cheb(a->tx, a->ty, w->ani[j].tx, w->ani[j].ty) <= 3) {
                a->flee = 4;
                if (ani_seek(w, a, w->ani[j].tx, w->ani[j].ty, false)) return;
            }
    }
    if (!a->flee && rn(w, a->kind == A_RABBIT ? 25 : 45)) return;
    int d = rn(w, 4);
    for (int t = 0; t < 4; t++, d = (d + 1) & 3) {
        if (!ani_ok(w, a, a->tx + DX[d], a->ty + DY[d])) continue;
        ani_step(a, d);
        return;
    }
}

/* ---------- 火 ---------- */

static bool flammable(const world_t *w, int idx)
{
    int o = OBJ(w->tiles[idx]);
    if (sim_flooded(w, idx)) return false;
    if (o == O_FARM) return VAL(w->tiles[idx]) >= 30;
    return (o >= O_SAPLING && o <= O_FLOWER) || o == O_SITE || sim_is_house(o);
}

bool sim_ignite(world_t *w, int idx)
{
    if (!flammable(w, idx)) return false;
    int o = OBJ(w->tiles[idx]);
    bool house = sim_is_house(o);
    if (house) sim_house_lost(w, idx);
    w->tiles[idx] = MKT(TERR(w->tiles[idx]), O_FLAME, (house ? 64 : 0) | (6 + rn(w, 4)));
    if (!w->fires) sim_sfx(SFX_ALARM);
    w->fires++;
    return true;
}

static void burn_out(world_t *w, int i)
{
    int t = TERR(w->tiles[i]);
    bool house = VAL(w->tiles[i]) & 64;
    w->tiles[i] = MKTILE(t == T_GRASS ? T_ASH : t, house ? O_RUIN : O_BURNT);
}

/* 每 10 tick 推进一次火势：烧尽、被雨浇灭、向四邻蔓延 */
void sim_fire_step(world_t *w)
{
    static uint16_t ign[96];
    int nign = 0, count = 0;
    static const uint8_t SPREAD[W_COUNT] = {14, 12, 4, 2, 5, 30, 5}; /* 雷雨天带着雨，火不容易蔓延 */
    int p = SPREAD[w->weather % W_COUNT];
    bool wet = w->weather == W_RAIN || w->weather == W_STORM || w->weather == W_SNOW;
    for (int i = 0; i < MAP_N; i++) {
        if (OBJ(w->tiles[i]) != O_FLAME) continue;
        int v = VAL(w->tiles[i]), left = v & 63;
        if (left <= 1 || (wet && rn(w, 100) < 12) || sim_flooded(w, i)) {
            burn_out(w, i);
            continue;
        }
        w->tiles[i] = MKT(TERR(w->tiles[i]), O_FLAME, (v & 64) | (left - 1));
        count++;
        int x = i % MAP_W, y = i / MAP_W;
        for (int d = 0; d < 4; d++) {
            int nx = x + DX[d], ny = y + DY[d];
            if (!inmap(nx, ny)) continue;
            int n = IDX(nx, ny);
            if (flammable(w, n) && rn(w, 100) < p && nign < 96) ign[nign++] = (uint16_t)n;
        }
    }
    w->fires = (uint16_t)count;
    for (int k = 0; k < nign; k++) sim_ignite(w, ign[k]);
    if (!w->fires) sim_logf("大火熄灭了，留下一片焦土");
    sim_calc_danger(w);
}

static void burn_all_out(world_t *w)
{
    for (int i = 0; i < MAP_N; i++)
        if (OBJ(w->tiles[i]) == O_FLAME) burn_out(w, i);
    w->fires = 0;
}

/* ---------- 洪水 ---------- */

static void flood_apply(world_t *w)
{
    int farms = 0;
    for (int i = 0; i < MAP_N; i++) {
        if (!sim_flooded(w, i)) continue;
        int t = TERR(w->tiles[i]), o = OBJ(w->tiles[i]), v = VAL(w->tiles[i]);
        if (o == O_FARM && v > 1) {
            w->tiles[i] = MKT(t, O_FARM, 1);
            farms++;
        } else if (o == O_SAPLING || o == O_FLOWER || o == O_SITE) {
            w->tiles[i] = MKTILE(t, O_NONE);
        } else if (o == O_FLAME) {
            burn_out(w, i);
        } else if (sim_is_house(o)) {
            if (v <= 40) {
                sim_house_lost(w, i);
                w->tiles[i] = MKTILE(t, O_RUIN);
            } else {
                w->tiles[i] = MKT(t, o, v - 40);
            }
        }
    }
    if (farms) sim_logf("农田被洪水淹没，庄稼全毁了");
    if (sim_flooded(w, IDX(w->camp_x, w->camp_y))) w->fire_lit = 0;
    sim_calc_danger(w);
}

/* ---------- 雷击、地震、火山 ---------- */

static void strike(world_t *w, int c)
{
    int cx = c % MAP_W, cy = c / MAP_W;
    for (int dy = -1; dy <= 1; dy++)
        for (int dx = -1; dx <= 1; dx++)
            if (inmap(cx + dx, cy + dy)) sim_ignite(w, IDX(cx + dx, cy + dy));
    if (TERR(w->tiles[c]) == T_GRASS && OBJ(w->tiles[c]) == O_NONE) w->tiles[c] = MKTILE(T_ASH, O_NONE);
    for (int k = 0; k < MAX_ANI; k++)
        if (w->ani[k].alive && cheb(w->ani[k].tx, w->ani[k].ty, cx, cy) <= 1) w->ani[k].alive = 0;
    sim_fx_add(FX_BOLT, cx, cy, 10);
    sim_sfx(SFX_THUNDER);
    for (int i = 0; i < MAX_RES; i++) {
        resident_t *r = &w->res[i];
        if (!r->alive || cheb(r->tx, r->ty, cx, cy) > 1) continue;
        if (sim_hurt(w, i, 450, D_BOLT)) sim_logf("%s被雷火灼伤了", sim_name(r));
    }
    sim_calc_danger(w);
}

static int pick_tree(world_t *w)
{
    for (int tries = 0; tries < 60; tries++) {
        int i = rn(w, MAP_N);
        if (OBJ(w->tiles[i]) == O_TREE) return i;
    }
    return sim_rand_grass(w, 0, false);
}

static void wreck_house(world_t *w, int i, int dmg)
{
    int t = TERR(w->tiles[i]), o = OBJ(w->tiles[i]), v = VAL(w->tiles[i]);
    if (!sim_is_house(o)) return;
    if (v <= dmg) {
        sim_house_lost(w, i);
        w->tiles[i] = MKTILE(t, O_RUIN);
    } else {
        w->tiles[i] = MKT(t, o, v - dmg);
    }
}

static void quake(world_t *w)
{
    for (int i = 0; i < MAP_N; i++) {
        int o = OBJ(w->tiles[i]);
        if (sim_is_house(o) && rn(w, 100) < 70) wreck_house(w, i, o == O_HOUSE2 ? 50 : 100);
        else if (o == O_SITE) w->tiles[i] = MKTILE(TERR(w->tiles[i]), O_NONE);
        else if (o == O_TREE && rn(w, 100) < 12) w->tiles[i] = MKTILE(TERR(w->tiles[i]), O_STUMP);
    }
    /* 地裂：一道乱石带隆起，永久改变地貌 */
    int c = sim_rand_grass(w, 4, false);
    if (c >= 0) {
        int x = c % MAP_W, y = c / MAP_W, d = rn(w, 4);
        for (int k = 0; k < 6; k++) {
            int i = IDX(x, y);
            if (inmap(x, y) && TERR(w->tiles[i]) == T_GRASS && !sim_occupied(w, i) && OBJ(w->tiles[i]) < O_FIRE &&
                cheb(x, y, w->camp_x, w->camp_y) > 2)
                w->tiles[i] = MKTILE(T_ROCK, O_NONE);
            if (rn(w, 100) < 35) d = (d + 2 + rn(w, 2)) & 3;
            x += DX[d];
            y += DY[d];
        }
    }
    sim_fx_add(FX_QUAKE, w->camp_x, w->camp_y, 14);
    sim_logf("大地震动，房屋倒塌");
    sim_sfx(SFX_RUMBLE);
    if (w->shrine != NO_ID && rn(w, 100) < 50) {
        w->tiles[w->shrine] = MKTILE(TERR(w->tiles[w->shrine]), O_RUIN);
        w->shrine = NO_ID;
    }
    for (int i = 0; i < MAX_RES; i++) {
        resident_t *r = &w->res[i];
        if (!r->alive || rn(w, 100) >= (sim_in_house(w, r) ? 60 : 20)) continue;
        if (sim_hurt(w, i, 250, D_QUAKE) && !r->hurt_cd) {
            sim_logf("%s在地震中受伤了", sim_name(r));
            r->hurt_cd = 40;
        }
    }
    sim_refresh(w);
}

static void to_lava(world_t *w, int i)
{
    int o = OBJ(w->tiles[i]);
    if (o == O_FIRE || o == O_STORE) return; /* 营地是岛民最后的落脚点，岩浆绕着走 */
    if (sim_is_house(o)) sim_house_lost(w, i);
    w->tiles[i] = MKTILE(T_LAVA, O_NONE);
    int x = i % MAP_W, y = i / MAP_W;
    for (int d = 0; d < 4; d++)
        if (inmap(x + DX[d], y + DY[d])) sim_ignite(w, IDX(x + DX[d], y + DY[d]));
}

static void erupt(world_t *w)
{
    int c = -1;
    for (int tries = 0; tries < 200 && c < 0; tries++) { /* 优先从山里喷出来 */
        int i = rn(w, MAP_N);
        if (TERR(w->tiles[i]) == T_ROCK && cheb(i % MAP_W, i / MAP_W, w->camp_x, w->camp_y) >= 5) c = i;
    }
    if (c < 0) c = sim_rand_grass(w, 6, false);
    if (c < 0) c = sim_rand_grass(w, 0, false);
    if (c < 0) return;
    int cx = c % MAP_W, cy = c / MAP_W;
    for (int i = 0; i < MAP_N; i++) {
        int t = TERR(w->tiles[i]), d2 = dist2(i, cx, cy);
        if (d2 <= 5 && (is_land(t) || t == T_ROCK)) {
            to_lava(w, i);
        } else if (d2 <= 49 && t == T_GRASS && rn(w, 100) < 60) { /* 火山灰 */
            int o = OBJ(w->tiles[i]), v = VAL(w->tiles[i]);
            if (o == O_FARM || o == O_FLOWER || o == O_SAPLING || o == O_BERRY) {
                o = (o == O_BERRY) ? O_BUSH : O_NONE; /* 庄稼和花草被火山灰埋掉 */
                v = 0;
            }
            if (o != O_FLAME) w->tiles[i] = MKT(T_ASH, o, v);
        }
    }
    w->crater = (uint16_t)c;
    w->volcano_h = 12;
    w->lava = 1;
    sim_fx_add(FX_QUAKE, cx, cy, 14);
    sim_logf("火山喷发了！岩浆涌向四周");
    sim_sfx(SFX_RUMBLE);
    sim_refresh(w);
}

static void lava_hour(world_t *w)
{
    static uint16_t add[64];
    int nadd = 0, count = 0;
    for (int i = 0; i < MAP_N; i++) {
        if (TERR(w->tiles[i]) != T_LAVA) continue;
        count++;
        if (w->volcano_h) {
            if (rn(w, 100) < 28 && nadd < 64) {
                int d = rn(w, 4), x = i % MAP_W + DX[d], y = i / MAP_W + DY[d];
                if (inmap(x, y)) {
                    int t = TERR(w->tiles[IDX(x, y)]);
                    if (is_land(t) || t == T_ROCK) add[nadd++] = (uint16_t)IDX(x, y);
                }
            }
        } else if (rn(w, 100) < 6) { /* 冷却成岩石：永久的地貌变化 */
            w->tiles[i] = MKTILE(T_ROCK, O_NONE);
            count--;
        }
    }
    for (int k = 0; k < nadd; k++)
        if (TERR(w->tiles[add[k]]) != T_LAVA) {
            to_lava(w, add[k]);
            count++;
        }
    if (w->volcano_h) {
        w->volcano_h--;
        for (int k = 0; k < 2; k++) { /* 火山弹 */
            int t = sim_rand_grass(w, 0, false);
            if (t >= 0 && dist2(t, w->crater % MAP_W, w->crater / MAP_W) <= 81) sim_ignite(w, t);
        }
        if (!w->volcano_h) sim_logf("火山平息了，岩浆慢慢冷却");
    }
    w->lava = (uint16_t)count;
    for (int k = 0; k < MAX_ANI; k++)
        if (w->ani[k].alive && TERR(w->tiles[IDX(w->ani[k].tx, w->ani[k].ty)]) == T_LAVA) w->ani[k].alive = 0;
    sim_refresh(w);
}

/* ---------- 天气 ---------- */

static void set_weather(world_t *w, int wt, int hours, bool say)
{
    static const char *const MSG[W_COUNT] = {"天放晴了",       "天阴了下来",   "下起了小雨", "暴雨倾盆而下",
                                             "天边电闪雷鸣", "刮起了大风", "暴风雪来了"};
    if (say && wt != w->weather) sim_logf("%s", MSG[wt]);
    w->weather = (uint8_t)wt;
    w->weather_h = (uint8_t)hours;
}

static void weather_hour(world_t *w)
{
    if (w->weather_h) w->weather_h--;
    if (!w->weather_h) { /* 自然变天：多数时候晴或阴，偶尔下雨刮风，极少暴雨雷电 */
        int k = rn(w, 100), wt, hrs = 5 + rn(w, 8);
        if (k < 46) wt = W_SUNNY;
        else if (k < 68) wt = W_CLOUDY;
        else if (k < 87) wt = W_RAIN;
        else if (k < 95) wt = W_WIND;
        else if (k < 98) wt = W_STORM, hrs = 3 + rn(w, 4);
        else wt = W_THUNDER, hrs = 2 + rn(w, 3);
        set_weather(w, wt, hrs, wt >= W_RAIN || w->weather >= W_RAIN);
    }
    if (w->weather == W_THUNDER && rn(w, 100) < 15) {
        int c = pick_tree(w);
        if (c >= 0) strike(w, c);
    }
    /* 暴雨持续 → 水位上涨；雨停 → 慢慢退去 */
    if (w->weather == W_STORM) {
        w->storm_h++;
        if (w->storm_h % 5 == 0 && w->flood < 3) {
            if (!w->flood) sim_logf("河水暴涨，漫上了岸");
            w->flood++;
            flood_apply(w);
        }
    } else {
        w->storm_h = 0;
        if (w->flood && sim_hour(w) % 6 == 0) {
            w->flood--;
            if (!w->flood) sim_logf("洪水退去了");
            sim_calc_danger(w);
        }
    }
    /* 风雨雪会慢慢损坏房屋 */
    if ((w->weather == W_STORM || w->weather == W_WIND || w->weather == W_SNOW) && sim_houses)
        for (int i = 0; i < MAP_N; i++)
            if (sim_is_house(OBJ(w->tiles[i])) && rn(w, 100) < 8)
                wreck_house(w, i, OBJ(w->tiles[i]) == O_HOUSE2 ? 8 : 15);
}

/* ---------- 生态：每小时一次 ---------- */

void sim_eco_hour(world_t *w)
{
    weather_hour(w);
    if (w->lava) lava_hour(w);

    static const int8_t GROW[W_COUNT] = {1, 1, 2, 2, 1, 1, -2}; /* 庄稼每小时的长势 */
    int grow = GROW[w->weather % W_COUNT];
    int trees = 0, land = 0, bushes = 0;
    for (int i = 0; i < MAP_N; i++) {
        int t = TERR(w->tiles[i]), o = OBJ(w->tiles[i]);
        if (t == T_GRASS || t == T_ASH) land++;
        if (o >= O_SAPLING && o <= O_TREE) trees++;
        if (o == O_BUSH || o == O_BERRY) bushes++;
        if (o == O_FARM && !sim_flooded(w, i)) {
            int v = VAL(w->tiles[i]) + grow;
            if (t == T_ASH) v += 1; /* 火山灰很肥 */
            w->tiles[i] = MKT(t, O_FARM, v < 1 ? 1 : v > 100 ? 100 : v);
        }
    }
    int samples = w->weather == W_RAIN ? 60 : 40; /* 下雨时草木长得快 */
    bool frozen = w->weather == W_SNOW;
    for (int k = 0; k < samples && !frozen; k++) {
        int i = rn(w, MAP_N);
        int t = TERR(w->tiles[i]), o = OBJ(w->tiles[i]);
        int p = rn(w, 100);
        if (t == T_ASH && p < 8 && o != O_BURNT && o != O_FLAME) {
            w->tiles[i] = MKT(T_GRASS, o, VAL(w->tiles[i])); /* 灰烬上重新长出草 */
            continue;
        }
        switch (o) {
        case O_SAPLING: if (p < 25) w->tiles[i] = MKTILE(t, O_YOUNG); break;
        case O_YOUNG: if (p < 20) w->tiles[i] = MKTILE(t, O_TREE); break;
        case O_STUMP: /* 树桩一半会重新发芽 */
            if (p < 20) w->tiles[i] = MKTILE(t, p < 10 ? O_SAPLING : O_NONE);
            break;
        case O_BURNT: if (p < 12) w->tiles[i] = MKTILE(t, p < 4 ? O_SAPLING : O_NONE); break;
        case O_RUIN: if (p < 6) w->tiles[i] = MKTILE(t, O_NONE); break;
        case O_BUSH: if (p < 30) w->tiles[i] = MKTILE(t, O_BERRY); break;
        case O_TREE:
            /* 成树向旁边的空草地播种（大风天传得更远更多）；森林占到陆地四成后不再扩张 */
            if (p < (w->weather == W_WIND ? 12 : 6) && trees * 10 < land * 4) {
                int d = rn(w, 4), x = i % MAP_W + DX[d], y = i / MAP_W + DY[d];
                if (inmap(x, y) && w->tiles[IDX(x, y)] == MKTILE(T_GRASS, O_NONE) &&
                    cheb(x, y, w->camp_x, w->camp_y) > 2)
                    w->tiles[IDX(x, y)] = MKTILE(T_GRASS, O_SAPLING);
            }
            break;
        default: break;
        }
    }
    /* 生态恢复：森林或浆果丛几乎灭绝时，风和鸟会带来新的种子 */
    if (trees < 12 && rn(w, 100) < 25) {
        int t = sim_rand_grass(w, 3, true);
        if (t >= 0 && TERR(w->tiles[t]) == T_GRASS) w->tiles[t] = MKTILE(T_GRASS, O_SAPLING);
    }
    if (bushes < 5 && rn(w, 100) < 15) {
        int t = sim_rand_grass(w, 3, true);
        if (t >= 0 && TERR(w->tiles[t]) == T_GRASS) w->tiles[t] = MKTILE(T_GRASS, O_BUSH);
    }

    int h = sim_hour(w);
    if (h % 4 == 0 && w->fish < 30) w->fish++;
    /* 篝火：夜里每 6 小时烧掉 1 根木柴 */
    if ((h >= 18 || h < 6) && h % 6 == 0 && w->wood > 0) w->wood--;
    bool lit = w->wood > 0 && !sim_flooded(w, IDX(w->camp_x, w->camp_y));
    if (w->fire_lit && !lit) sim_logf(w->wood ? "洪水浇灭了篝火" : "木柴烧完，篝火熄灭了");
    if (!w->fire_lit && lit) sim_logf("篝火重新燃起来了");
    w->fire_lit = lit;

    /* 洪水里的野兽 */
    if (w->flood >= 2)
        for (int k = 0; k < MAX_ANI; k++)
            if (w->ani[k].alive && sim_flooded(w, IDX(w->ani[k].tx, w->ani[k].ty)) && rn(w, 100) < 30)
                w->ani[k].alive = 0;

    /* 狼越来越饿，五天吃不到东西就饿死；兔子会啃庄稼 */
    for (int k = 0; k < MAX_ANI; k++) {
        animal_t *a = &w->ani[k];
        if (!a->alive) continue;
        if (a->kind == A_WOLF) {
            if (a->hunger < 250) a->hunger++;
            if (a->hunger > 120) a->alive = 0;
        } else if (a->kind == A_RABBIT && rn(w, 100) < 25) {
            for (int d = 0; d < 5; d++) {
                int x = a->tx + (d < 4 ? DX[d] : 0), y = a->ty + (d < 4 ? DY[d] : 0);
                if (!inmap(x, y)) continue;
                uint16_t t = w->tiles[IDX(x, y)];
                if (OBJ(t) != O_FARM || VAL(t) < 12) continue;
                w->tiles[IDX(x, y)] = MKT(TERR(t), O_FARM, VAL(t) - 10);
                break;
            }
        }
    }
    if (w->shrine != NO_ID && OBJ(w->tiles[w->shrine]) != O_SHRINE) w->shrine = NO_ID; /* 被岩浆吞没等 */

    /* 瘟疫：病人会传给两格以内的人 */
    for (int i = 0; i < MAX_RES; i++) {
        const resident_t *s = &w->res[i];
        if (!s->alive || !s->sick) continue;
        for (int j = 0; j < MAX_RES; j++) {
            resident_t *o = &w->res[j];
            if (!o->alive || o->sick || o->immune || cheb(s->tx, s->ty, o->tx, o->ty) > 2) continue;
            if (rn(w, 100) < 8) {
                sim_infect(w, j);
                if (rn(w, 100) < 50) sim_logf("%s也染上了瘟疫", sim_name(o));
            }
        }
    }
    recount(w);
}

/* ---------- 每天一次：衰老、生育、疾病、野兽繁衍 ---------- */

static void birth(world_t *w, int m)
{
    resident_t *mo = &w->res[m];
    resident_t *fa = mo->partner != NO_SLOT ? &w->res[mo->partner] : NULL;
    int at = (mo->home != NO_ID && sim_is_house(OBJ(w->tiles[mo->home]))) ? mo->home : IDX(mo->tx, mo->ty);
    int trait = rn(w, 100) < 70 ? (rn(w, 2) && fa ? fa->trait : mo->trait) : rn(w, TR_COUNT);
    uint16_t fuid = fa ? fa->uid : NO_ID; /* 先记下来：新生儿可能复用别的槽位，但不会覆盖父母 */
    /* 新生儿的性别略微偏向岛上年轻人里较少的一方，免得一代人全是同性而绝后 */
    int boys = 0, girls = 0;
    for (int i = 0; i < MAX_RES; i++)
        if (w->res[i].alive && sim_age(&w->res[i]) < 40) {
            if (w->res[i].sex) girls++;
            else boys++;
        }
    int sex = rn(w, 100) < (boys > girls ? 70 : boys < girls ? 30 : 50);
    int s = sim_spawn_resident(w, sex, trait, 0, at);
    if (s < 0) return;
    resident_t *c = &w->res[s];
    c->mother = mo->uid;
    c->mname = mo->name;
    c->father = fuid;
    c->fname = fa ? fa->name : NO_SLOT;
    c->home = mo->home;
    c->gen = (uint8_t)((fa && fa->gen > mo->gen ? fa->gen : mo->gen) + 1);
    c->food = 800;
    if (c->gen > w->max_gen) w->max_gen = c->gen;
    sim_tree_put(w, c);
    sim_sfx(SFX_BIRTH);
    mo->kids++;
    if (fa) fa->kids++;
    mo->last_birth = (uint16_t)sim_day(w);
    w->births++;
    if (fa) sim_logf("%s和%s家迎来了新生儿%s", sim_name(fa), sim_name(mo), sim_name(c));
    else sim_logf("%s生下了%s", sim_name(mo), sim_name(c));
    sim_fx_add(FX_HEAL, c->tx, c->ty, 14);
}

void sim_eco_day(world_t *w)
{
    int day = sim_day(w), pop = sim_pop(w);
    for (int i = 0; i < MAX_RES; i++) {
        resident_t *r = &w->res[i];
        if (!r->alive) continue;
        r->age_days++;
        int age = sim_age(r);
        if (r->immune) r->immune--;
        if (r->sick && --r->sick == 0) {
            r->immune = 20;
            sim_logf("%s的病好了", sim_name(r));
        }
        /* 老死：60 岁起每天有概率，年纪越大越高 */
        if (age >= AGE_OLD && rn(w, 100) < (age - AGE_OLD + 2) * 2) {
            sim_die(w, i, D_AGE);
            continue;
        }
        if (r->sex != 1) continue;
        if (r->preg) {
            if (--r->preg == 0) birth(w, i);
            continue;
        }
        /* 怀孕：有伴侣、有房子、不闹饥荒、岛上还住得下 */
        if (r->partner == NO_SLOT || !w->res[r->partner].alive || age < AGE_ADULT || age > 42) continue;
        if (r->home == NO_ID || !sim_is_house(OBJ(w->tiles[r->home]))) continue;
        if (pop >= MAX_RES - 1 || w->food < pop || r->kids >= 4 || r->sick) continue;
        if (r->last_birth && day - r->last_birth < 10) continue;
        int chance = 30 + w->faith / 10 - w->fear / 4; /* 人心惶惶时不敢要孩子 */
        if (rn(w, 100) < (chance < 5 ? 5 : chance)) r->preg = 3;
    }

    /* 野兽繁衍：野猪慢，兔子快，狼要有足够的猎物才生 */
    int boars = sim_kind_count(w, A_BOAR), rabbits = sim_kind_count(w, A_RABBIT), wolves = sim_kind_count(w, A_WOLF);
    static const struct { uint8_t kind, min, cap, pct; } BREED[] = {{A_BOAR, 2, 8, 50}, {A_RABBIT, 2, 10, 75}, {A_RABBIT, 2, 6, 50}};
    for (unsigned b = 0; b < sizeof BREED / sizeof BREED[0]; b++) {
        int n = sim_kind_count(w, BREED[b].kind);
        if (n < BREED[b].min || n >= BREED[b].cap || sim_animals(w) >= MAX_ANI || rn(w, 100) >= BREED[b].pct) continue;
        int k = rn(w, MAX_ANI);
        for (int t = 0; t < MAX_ANI; t++, k = (k + 1) % MAX_ANI)
            if (w->ani[k].alive && w->ani[k].kind == BREED[b].kind) {
                sim_spawn_animal(w, w->ani[k].tx, w->ani[k].ty, BREED[b].kind);
                break;
            }
    }
    if (wolves >= 2 && wolves < 4 && boars + rabbits >= 8 && sim_animals(w) < MAX_ANI && rn(w, 100) < 12)
        for (int k = 0; k < MAX_ANI; k++)
            if (w->ani[k].alive && w->ani[k].kind == A_WOLF) {
                sim_spawn_animal(w, w->ani[k].tx, w->ani[k].ty, A_WOLF);
                break;
            }
    /* 快绝迹时偶尔有新的游上岸；猎物多了会把狼引来 */
    for (int kind = A_BOAR; kind <= A_RABBIT; kind++)
        if (sim_kind_count(w, kind) < 2 && rn(w, 100) < 15) {
            int t = sim_rand_grass(w, 5, false);
            if (t >= 0) {
                sim_spawn_animal(w, t % MAP_W, t / MAP_W, kind);
                if (kind == A_BOAR) sim_logf("一头野猪游上了岸");
            }
        }
    if (wolves == 0 && boars + rabbits >= 13 && sim_animals(w) <= MAX_ANI - 2 && rn(w, 100) < 5) {
        for (int k = 0; k < 2; k++) {
            int t = sim_rand_grass(w, 7, false);
            if (t >= 0) sim_spawn_animal(w, t % MAP_W, t / MAP_W, A_WOLF);
        }
        sim_logf("猎物多了，两头狼游上了岛");
        sim_sfx(SFX_WOLF);
    }

    /* 信仰与恐惧都会随时间淡去；恐惧压过信仰太多，岛民会推倒祭石 */
    if (w->faith > (w->shrine != NO_ID ? 30 : 0)) w->faith--; /* 立了祭石，信仰就有了根 */
    if (w->fear > 0) w->fear = (uint8_t)(w->fear > 2 ? w->fear - 2 : 0);
    if (w->shrine != NO_ID && w->fear >= 60 && w->fear > w->faith + 20 && rn(w, 100) < 30) {
        w->tiles[w->shrine] = MKTILE(TERR(w->tiles[w->shrine]), O_RUIN);
        w->shrine = NO_ID;
        sim_devotion(w, -10, 0);
        sim_logf("岛民推倒了祭石，不再相信神的仁慈");
    }
    if (w->fear >= 50 && rn(w, 100) < 30) sim_logf("岛民惶恐不安，不知何处触怒了神");
    if (w->food == 0)
        for (int i = 0; i < MAX_RES; i++)
            if (w->res[i].alive && w->res[i].food < 300) {
                sim_logf("岛上出现了食物短缺");
                break;
            }
}

void sim_tick(world_t *w)
{
    w->tick++;
    for (int i = 0; i < MAX_RES; i++)
        if (w->res[i].alive) sim_res_tick(w, i);
    for (int k = 0; k < MAX_ANI; k++) sim_ani_tick(w, k);
    if (w->fires && w->tick % 10 == 0) sim_fire_step(w);
    if (w->tick % TICKS_PER_HOUR == 0) sim_eco_hour(w);
    if (w->tick % TICKS_PER_DAY == 0) sim_eco_day(w);
}

/* ---------- 神力 ---------- */

static void blob(world_t *w, int c, int r2, int terr)
{
    int cx = c % MAP_W, cy = c / MAP_W;
    for (int i = 0; i < MAP_N; i++) {
        if (dist2(i, cx, cy) > r2 + rn(w, 2)) continue;
        int t = TERR(w->tiles[i]), o = OBJ(w->tiles[i]);
        if ((t != T_GRASS && t != T_ASH) || o == O_FIRE || o == O_STORE || o == O_GRAVE || o >= O_SITE) continue;
        if (sim_occupied(w, i) || cheb(i % MAP_W, i / MAP_W, w->camp_x, w->camp_y) < 3) continue;
        w->tiles[i] = MKTILE(terr, O_NONE);
    }
    sim_fx_add(FX_SPARK, cx, cy, 14);
    sim_refresh(w);
}

static bool anyone_hurt(const world_t *w)
{
    for (int i = 0; i < MAX_RES; i++)
        if (w->res[i].alive && (w->res[i].hp < 600 || w->res[i].sick)) return true;
    return false;
}

void sim_cast(world_t *w, int power)
{
    if (power < 0 || power >= P_COUNT) return;
    if (w->power_used[power] < 0xFFFF) w->power_used[power]++;
    /* 岛民看得见神做了什么：雪中送炭最得人心，降灾则让人恐惧 */
    {
        int pop = sim_pop(w), cat = sim_power_cat_id(power);
        static const int8_t FEAR[] = {8, 15, 20, 20, 30, 25}; /* 雷火 大火 洪水 地震 火山 瘟疫 */
        if (!pop) {
            /* 没有人看着 */
        } else if (power == P_FOOD) {
            sim_devotion(w, w->food < pop * 2 ? 14 : 3, 0);
        } else if (power == P_HEAL) {
            sim_devotion(w, anyone_hurt(w) ? 16 : 3, -5);
        } else if (power == P_CALM) {
            sim_devotion(w, (w->fires || w->flood || w->lava || w->weather >= W_STORM) ? 14 : 2, -10);
        } else if (cat == 1 && power != P_WOLF) {
            sim_devotion(w, 3, 0);
        } else if (power == P_GROW) {
            sim_devotion(w, 6, 0);
        } else if (cat == 4) {
            sim_devotion(w, -FEAR[power - P_BOLT] / 2, FEAR[power - P_BOLT]);
        } else if (power == P_W_STORM || power == P_W_THUNDER || power == P_WOLF) {
            sim_devotion(w, -2, 6);
        } else if (power == P_W_SNOW) {
            sim_devotion(w, -4, 12);
        }
        if (cat == 1 || cat == 2) sim_sfx(SFX_BLESS);
    }
    switch (power) {
    case P_FOREST: {
        int c = sim_rand_grass(w, 5, false);
        if (c < 0) { sim_logf("找不到可以造林的土地"); break; }
        int cx = c % MAP_W, cy = c / MAP_W;
        for (int i = 0; i < MAP_N; i++) {
            if (dist2(i, cx, cy) > 5) continue;
            int t = TERR(w->tiles[i]), o = OBJ(w->tiles[i]);
            if ((t != T_GRASS && t != T_ASH) || cheb(i % MAP_W, i / MAP_W, w->camp_x, w->camp_y) < 3) continue;
            if (o == O_NONE || o == O_FLOWER || o == O_STUMP || o == O_BURNT || o == O_SAPLING || o == O_YOUNG)
                w->tiles[i] = MKTILE(T_GRASS, rn(w, 100) < 80 ? O_TREE : O_YOUNG);
        }
        sim_fx_add(FX_SPARK, cx, cy, 14);
        sim_logf("一片森林拔地而起");
        break;
    }
    case P_BEAST: {
        int made = 0;
        for (int k = 0; k < 3 && sim_animals(w) < MAX_ANI; k++) {
            int t = sim_rand_grass(w, 5, false);
            if (t < 0) continue;
            sim_spawn_animal(w, t % MAP_W, t / MAP_W, A_BOAR);
            sim_fx_add(FX_SPARK, t % MAP_W, t / MAP_W, 12);
            made++;
        }
        sim_logf(made ? "几头野猪出现在岛上" : "岛上的野兽已经够多了");
        break;
    }
    case P_RABBIT:
    case P_WOLF: {
        int made = 0, want = power == P_RABBIT ? 5 : 2;
        for (int k = 0; k < want && sim_animals(w) < MAX_ANI; k++) {
            int t = sim_rand_grass(w, power == P_WOLF ? 7 : 4, false);
            if (t < 0) continue;
            sim_spawn_animal(w, t % MAP_W, t / MAP_W, power == P_RABBIT ? A_RABBIT : A_WOLF);
            sim_fx_add(FX_SPARK, t % MAP_W, t / MAP_W, 12);
            made++;
        }
        if (!made) sim_logf("岛上的野兽已经够多了");
        else if (power == P_RABBIT) sim_logf("一窝兔子蹦进了草丛");
        else {
            sim_logf("狼群出现在岛的边缘");
            sim_sfx(SFX_WOLF);
        }
        break;
    }
    case P_FISH:
        w->fish = (uint16_t)(w->fish + 12 > 60 ? 60 : w->fish + 12);
        for (int tries = 0; tries < 100; tries++) {
            int i = rn(w, MAP_N);
            if (TERR(w->tiles[i]) == T_SHOAL) {
                sim_fx_add(FX_SPARK, i % MAP_W, i / MAP_W, 14);
                break;
            }
        }
        sim_logf("海里游来了一群鱼");
        break;
    case P_FOOD:
        w->food = (uint16_t)(w->food + 10 > 999 ? 999 : w->food + 10);
        sim_fx_add(FX_SPARK, w->camp_x + 1, w->camp_y, 14);
        sim_logf("食物从天而降，落进仓库");
        break;
    case P_HUMAN: {
        int s = sim_spawn_resident(w, rn(w, 2), rn(w, TR_COUNT), 16 + rn(w, 10), -1);
        if (s < 0) { sim_logf("岛上已经住满了"); break; }
        sim_fx_add(FX_SPARK, w->res[s].tx, w->res[s].ty, 14);
        sim_logf("新居民%s来到了岛上", sim_name(&w->res[s]));
        break;
    }
    case P_ORE: {
        int c = sim_rand_grass(w, 4, false);
        if (c < 0) break;
        blob(w, c, 1, T_ROCK);
        sim_logf("一座石山从地里隆起");
        break;
    }
    case P_WATER: {
        int c = sim_rand_grass(w, 5, false);
        if (c < 0) break;
        blob(w, c, 2, T_LAKE);
        sim_logf("大地裂开，涌出一汪清泉");
        break;
    }
    case P_CROP: {
        int made = 0;
        for (int i = 0; i < MAP_N; i++)
            if (OBJ(w->tiles[i]) == O_FARM) w->tiles[i] = MKT(TERR(w->tiles[i]), O_FARM, 100);
        for (int tries = 0; tries < 300 && made < 3; tries++) {
            int i = rn(w, MAP_N), d = cheb(i % MAP_W, i / MAP_W, w->camp_x, w->camp_y);
            if (w->tiles[i] != MKTILE(T_GRASS, O_NONE) || d < 5 || d > 8 || sim_wdist[i] < 2) continue;
            w->tiles[i] = MKT(T_GRASS, O_FARM, 100);
            sim_fx_add(FX_SPARK, i % MAP_W, i / MAP_W, 12);
            made++;
        }
        sim_refresh(w);
        sim_logf("田里的庄稼一夜之间成熟了");
        break;
    }
    case P_HEAL:
        for (int i = 0; i < MAX_RES; i++) {
            resident_t *r = &w->res[i];
            if (!r->alive) continue;
            r->hp = 1000;
            r->sick = 0;
            r->mood = (uint16_t)(r->mood + 200 > 1000 ? 1000 : r->mood + 200);
            sim_fx_add(FX_HEAL, r->tx, r->ty, 14);
        }
        sim_logf("神的祝福治愈了所有人，疾病消散");
        break;
    case P_GROW:
        for (int i = 0; i < MAP_N; i++) {
            int t = TERR(w->tiles[i]), o = OBJ(w->tiles[i]);
            if (t == T_ASH) t = T_GRASS;
            if (o == O_SAPLING || o == O_YOUNG) o = O_TREE;
            else if (o == O_STUMP || o == O_BURNT) o = O_SAPLING;
            else if (o == O_BUSH) o = O_BERRY;
            w->tiles[i] = MKT(t, o, o == O_FARM ? 100 : (o == O_TREE || o == O_SAPLING || o == O_BERRY) ? 0 : VAL(w->tiles[i]));
        }
        sim_fx_add(FX_SPARK, w->camp_x, w->camp_y, 14);
        sim_logf("万物生长，大地回春");
        break;
    case P_CALM:
        burn_all_out(w);
        w->flood = 0;
        w->storm_h = 0;
        w->volcano_h = 0;
        set_weather(w, W_SUNNY, 24, false);
        sim_refresh(w);
        sim_logf("风停雨歇，天地恢复平静");
        break;
    case P_W_SUNNY: case P_W_CLOUDY: case P_W_RAIN: case P_W_STORM:
    case P_W_THUNDER: case P_W_WIND: case P_W_SNOW: {
        int wt = power - P_W_SUNNY;
        set_weather(w, wt, wt == W_STORM ? 14 : wt == W_THUNDER ? 8 : 18, true);
        break;
    }
    case P_BOLT: {
        int c = pick_tree(w);
        if (c < 0) break;
        sim_logf("天雷劈下，燃起了火");
        strike(w, c);
        break;
    }
    case P_WILDFIRE: {
        int lit = 0;
        for (int k = 0; k < 3; k++) {
            int c = pick_tree(w);
            if (c < 0) continue;
            for (int d = 0; d < 4; d++) {
                int x = c % MAP_W + DX[d], y = c / MAP_W + DY[d];
                if (inmap(x, y)) lit += sim_ignite(w, IDX(x, y));
            }
            lit += sim_ignite(w, c);
        }
        sim_calc_danger(w);
        sim_logf(lit ? "森林燃起了大火" : "没有什么可烧的了");
        break;
    }
    case P_FLOOD:
        set_weather(w, W_STORM, 12, false);
        w->flood = 3;
        sim_logf("洪水滔天，淹没了低地");
        flood_apply(w);
        break;
    case P_QUAKE: quake(w); break;
    case P_VOLCANO: erupt(w); break;
    case P_PLAGUE: {
        int n = 0, i = rn(w, MAX_RES);
        for (int k = 0; k < MAX_RES && n < 3; k++, i = (i + 1) % MAX_RES)
            if (w->res[i].alive && !w->res[i].sick) {
                w->res[i].immune = 0;
                sim_infect(w, i);
                n++;
            }
        sim_logf(n ? "瘟疫开始在岛上蔓延" : "岛上没有人可以染病");
        break;
    }
    case P_SPEED: w->speed_idx = (uint8_t)((w->speed_idx + 1) % 3); break;
    case P_SOUND: w->sound_on = !w->sound_on; break;
    case P_VOICE:
        w->voice_on = !w->voice_on;
        sim_logf(w->voice_on ? "神在倾听：拍手降雷，吹气起风" : "神不再倾听");
        break;
    default: break;
    }
}
