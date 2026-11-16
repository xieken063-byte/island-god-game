/*
 * 《造物主：孤岛》模拟核心（一）：基础工具、寻路、居民的需求与状态机。
 * 世界侧（地图生成、生态、天气、灾害、神力）在 sim_world.c。
 * 只依赖 C 标准库。所有随机数来自 world_t.rng，因此同一存档可复现。
 */
#include <stdarg.h>
#include <stdio.h>
#include <string.h>

#include "sim_int.h"

sim_log_t g_log;
sim_fx_t g_fx[FX_N];
person_t g_tree[TREE_N];
bool g_tree_dirty;

static uint8_t s_sfx_q[8];
static uint8_t s_sfx_head, s_sfx_n;

void sim_sfx(int id)
{
    for (int k = 0; k < s_sfx_n; k++) /* 同一种音效不排两次 */
        if (s_sfx_q[(s_sfx_head + k) % 8] == id) return;
    if (s_sfx_n >= 8) return;
    s_sfx_q[(s_sfx_head + s_sfx_n) % 8] = (uint8_t)id;
    s_sfx_n++;
}
int sim_sfx_pop(void)
{
    if (!s_sfx_n) return SFX_NONE;
    int id = s_sfx_q[s_sfx_head];
    s_sfx_head = (uint8_t)((s_sfx_head + 1) % 8);
    s_sfx_n--;
    return id;
}

#define NAME_N 20
static const char *const NAMES_M[NAME_N] = {"阿山", "大石", "小虎", "青松", "铁牛", "阿海", "柱子",
                                            "木生", "石头", "长风", "阿福", "大川", "小满", "云生",
                                            "阿木", "春生", "二牛", "阿树", "水生", "冬青"};
static const char *const NAMES_F[NAME_N] = {"阿月", "小禾", "春芽", "青杏", "云朵", "阿兰", "小梅",
                                            "桃子", "秋叶", "星儿", "阿花", "小雨", "杏儿", "月牙",
                                            "阿朵", "小溪", "梨花", "青禾", "阿霞", "小鹿"};
static const char *const TRAITS[TR_COUNT] = {"勤劳", "懒散", "勇敢", "胆小", "善良"};
static const char *const WEATHERS[W_COUNT] = {"晴", "阴", "小雨", "暴雨", "雷电", "大风", "暴风雪"};
static const char *const CAUSES[D_COUNT] = {"饿死", "老死", "雷击", "火灾", "洪水", "岩浆", "地震", "瘟疫", "冻死", "狼袭"};
static const char *const POWERS[P_COUNT] = {
    "观察居民", "家谱", "岛屿纪事",
    "降下森林", "创造野猪", "创造兔子", "放出野狼", "召来鱼群", "赐予食物", "创造居民", "创造矿石", "创造水源", "创造庄稼",
    "治愈众生", "万物生长", "平息灾祸",
    "晴天", "阴天", "小雨", "暴雨", "雷电", "大风", "暴风雪",
    "天降雷火", "森林大火", "洪水", "地震", "火山喷发", "瘟疫",
    "时间流速", "音效", "声控神力"};
static const char *const CATS[6] = {"观察", "创造", "祝福", "天气", "灾害", "设置"};

const int8_t SIM_DX[4] = {1, -1, 0, 0};
const int8_t SIM_DY[4] = {0, 0, 1, -1};
#define DX SIM_DX
#define DY SIM_DY

uint8_t sim_wdist[MAP_N];
uint8_t sim_danger[MAP_N];
uint8_t sim_par[MAP_N];
int sim_farms, sim_houses;
static bool s_hazard;

/* 营地周围的落脚点（篝火在 0,0，仓库在 1,0） */
static const int8_t SPOT[MAX_RES][2] = {{-1, 1},  {0, 1},  {1, 1},  {2, 1},  {-1, 0}, {2, 0},
                                        {-1, -1}, {0, -1}, {1, -1}, {2, -1}, {-1, 2}, {0, 2},
                                        {1, 2},   {2, 2},  {-2, 1}, {-2, -1}};

/* ---------- 基础工具 ---------- */

uint32_t sim_rnd(world_t *w)
{
    uint32_t x = w->rng;
    x ^= x << 13;
    x ^= x >> 17;
    x ^= x << 5;
    return w->rng = x;
}
int sim_rn(world_t *w, int n) { return (int)(sim_rnd(w) % (uint32_t)n); }
#define rn sim_rn
static int iabs(int v) { return v < 0 ? -v : v; }
int sim_cheb(int ax, int ay, int bx, int by)
{
    int dx = iabs(ax - bx), dy = iabs(ay - by);
    return dx > dy ? dx : dy;
}
#define cheb sim_cheb
bool sim_inmap(int x, int y) { return x >= 0 && y >= 0 && x < MAP_W && y < MAP_H; }
#define inmap sim_inmap

void sim_logf(const char *fmt, ...)
{
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(g_log.msg[g_log.seq % LOG_N], LOG_LEN, fmt, ap);
    va_end(ap);
    g_log.seq++;
}

void sim_fx_add(int type, int x, int y, int dur)
{
    int best = 0;
    for (int i = 0; i < FX_N; i++) {
        if (g_fx[i].t == 0) { best = i; break; }
        if (g_fx[i].t < g_fx[best].t) best = i;
    }
    g_fx[best] = (sim_fx_t){(uint8_t)type, (uint8_t)x, (uint8_t)y, (uint8_t)dur};
}
void sim_fx_step(void)
{
    for (int i = 0; i < FX_N; i++)
        if (g_fx[i].t) g_fx[i].t--;
}

bool sim_is_water(int terr) { return terr == T_SHOAL || terr == T_LAKE || terr == T_RIVER; }
bool sim_is_house(int obj) { return obj == O_HOUSE1 || obj == O_HOUSE2; }

bool sim_walkable(const world_t *w, int idx)
{
    int t = TERR(w->tiles[idx]), o = OBJ(w->tiles[idx]);
    if (t != T_SAND && t != T_GRASS && t != T_RIVER && t != T_ASH) return false;
    return o != O_FIRE && o != O_STORE && o != O_FLAME;
}

bool sim_flooded(const world_t *w, int idx)
{
    if (!w->flood) return false;
    int t = TERR(w->tiles[idx]);
    return (t == T_SAND || t == T_GRASS || t == T_ASH) && sim_wdist[idx] <= w->flood;
}

bool sim_occupied(const world_t *w, int idx)
{
    int x = idx % MAP_W, y = idx / MAP_W;
    for (int i = 0; i < MAX_RES; i++)
        if (w->res[i].alive && ((w->res[i].tx == x && w->res[i].ty == y) || (w->res[i].nx == x && w->res[i].ny == y)))
            return true;
    for (int k = 0; k < MAX_ANI; k++)
        if (w->ani[k].alive && w->ani[k].tx == x && w->ani[k].ty == y) return true;
    return false;
}

int sim_pop(const world_t *w)
{
    int n = 0;
    for (int i = 0; i < MAX_RES; i++) n += w->res[i].alive;
    return n;
}
int sim_animals(const world_t *w)
{
    int n = 0;
    for (int i = 0; i < MAX_ANI; i++) n += w->ani[i].alive;
    return n;
}
int sim_kind_count(const world_t *w, int kind)
{
    int n = 0;
    for (int i = 0; i < MAX_ANI; i++) n += w->ani[i].alive && w->ani[i].kind == kind;
    return n;
}

/* ---------- 家谱 ---------- */

const person_t *sim_person(uint16_t uid)
{
    if (uid == 0 || uid == NO_ID) return NULL;
    const person_t *p = &g_tree[uid % TREE_N];
    return p->uid == uid ? p : NULL;
}
const resident_t *sim_resident_by_uid(const world_t *w, uint16_t uid)
{
    for (int i = 0; i < MAX_RES; i++)
        if (w->res[i].alive && w->res[i].uid == uid) return &w->res[i];
    return NULL;
}
void sim_tree_put(const world_t *w, const resident_t *r)
{
    person_t *p = &g_tree[r->uid % TREE_N];
    if (p->uid != r->uid) {
        memset(p, 0, sizeof *p);
        p->uid = r->uid;
        p->born = (uint16_t)(sim_day(w) > r->age_days ? sim_day(w) - r->age_days : 0);
        p->age = 0xFF;
    }
    p->father = r->father;
    p->mother = r->mother;
    p->name = r->name;
    p->sex = r->sex;
    p->gen = r->gen;
    g_tree_dirty = true;
}
int sim_tree_count(void)
{
    int n = 0;
    for (int i = 0; i < TREE_N; i++) n += g_tree[i].uid != 0;
    return n;
}
int sim_relatives(const world_t *w, uint16_t uid, uint16_t *out, uint8_t *role, int max)
{
    const person_t *p = sim_person(uid);
    int n = 0;
    if (!p) return 0;
    if (sim_person(p->father) && n < max) out[n] = p->father, role[n++] = 0;
    if (sim_person(p->mother) && n < max) out[n] = p->mother, role[n++] = 1;
    /* 配偶：在世的看现在的伴侣，否则看孩子的另一位父母 */
    uint16_t sp = NO_ID;
    const resident_t *r = sim_resident_by_uid(w, uid);
    if (r && r->partner != NO_SLOT && w->res[r->partner].alive) sp = w->res[r->partner].uid;
    for (int i = 0; i < TREE_N && sp == NO_ID; i++) {
        const person_t *c = &g_tree[i];
        if (!c->uid) continue;
        if (c->father == uid && sim_person(c->mother)) sp = c->mother;
        else if (c->mother == uid && sim_person(c->father)) sp = c->father;
    }
    if (sp != NO_ID && sim_person(sp) && n < max) out[n] = sp, role[n++] = 2;
    /* 子女按出生先后 */
    int first = n;
    for (int i = 0; i < TREE_N && n < max; i++) {
        const person_t *c = &g_tree[i];
        if (!c->uid || (c->father != uid && c->mother != uid)) continue;
        int k = n;
        while (k > first && sim_person(out[k - 1])->born > c->born) {
            out[k] = out[k - 1];
            role[k] = 3;
            k--;
        }
        out[k] = c->uid;
        role[k] = 3;
        n++;
    }
    return n;
}
int sim_descendants(uint16_t uid, int *living)
{
    static uint8_t mark[TREE_N];
    memset(mark, 0, sizeof mark);
    bool grew = true;
    int total = 0, alive = 0;
    while (grew) { /* 一代一代往下标记 */
        grew = false;
        for (int i = 0; i < TREE_N; i++) {
            const person_t *c = &g_tree[i];
            if (!c->uid || mark[i]) continue;
            const person_t *f = sim_person(c->father), *m = sim_person(c->mother);
            bool hit = c->father == uid || c->mother == uid || (f && mark[c->father % TREE_N]) ||
                       (m && mark[c->mother % TREE_N]);
            if (!hit) continue;
            mark[i] = 1;
            grew = true;
            total++;
            alive += c->age == 0xFF;
        }
    }
    if (living) *living = alive;
    return total;
}

void sim_devotion(world_t *w, int dfaith, int dfear)
{
    int f = w->faith + dfaith, e = w->fear + dfear;
    w->faith = (uint8_t)(f < 0 ? 0 : f > 100 ? 100 : f);
    w->fear = (uint8_t)(e < 0 ? 0 : e > 100 ? 100 : e);
}
int sim_day(const world_t *w) { return (int)(w->tick / TICKS_PER_DAY) + 1; }
int sim_hour(const world_t *w) { return (int)(w->tick % TICKS_PER_DAY) * 24 / TICKS_PER_DAY; }
bool sim_is_night(const world_t *w)
{
    int h = sim_hour(w);
    return h >= 21 || h < 5;
}
int sim_speed(const world_t *w)
{
    static const uint8_t SPD[3] = {1, 4, 16};
    return SPD[w->speed_idx % 3];
}
int sim_age(const resident_t *r) { return r->age_days / DAYS_PER_YEAR; }
bool sim_in_house(const world_t *w, const resident_t *r)
{
    return r->step == 0 && sim_is_house(OBJ(w->tiles[IDX(r->tx, r->ty)]));
}

const char *sim_name_of(int sex, int name) { return (sex ? NAMES_F : NAMES_M)[name % NAME_N]; }
const char *sim_name(const resident_t *r) { return sim_name_of(r->sex, r->name); }
const char *sim_trait(const resident_t *r) { return TRAITS[r->trait % TR_COUNT]; }
const char *sim_power_name(int p) { return POWERS[p % P_COUNT]; }
const char *sim_weather_name(int wt) { return WEATHERS[wt % W_COUNT]; }
const char *sim_cause_name(int c) { return CAUSES[c % D_COUNT]; }
int sim_power_cat_id(int p)
{
    if (p <= P_RECORDS) return 0;
    if (p <= P_CROP) return 1;
    if (p <= P_CALM) return 2;
    if (p <= P_W_SNOW) return 3;
    if (p <= P_PLAGUE) return 4;
    return 5;
}
const char *sim_power_cat(int p) { return CATS[sim_power_cat_id(p)]; }

const char *sim_status(const world_t *w, const resident_t *r)
{
    if (sim_age(r) < AGE_CHILD) return r->state == S_SLEEP ? "睡觉" : "在家里玩";
    if (r->state == S_SLEEP) return sim_in_house(w, r) ? "在家睡觉" : "露天睡觉";
    bool moving = r->state == S_MOVE;
    switch (r->goal) {
    case G_EAT_CAMP: return moving ? "回营地吃饭" : (w->fire_lit ? "烤东西吃" : "吃饭");
    case G_EAT_BERRY: return moving ? "去找浆果吃" : "吃浆果";
    case G_BERRY: return moving ? "去采浆果" : "采集浆果";
    case G_CHOP: return moving ? "去砍树" : "砍伐树木";
    case G_HUNT:
        if (w->ani[r->tgt % MAX_ANI].kind == A_RABBIT) return moving ? "追兔子" : "抓兔子";
        return moving ? "追猎野猪" : "与野猪搏斗";
    case G_FISH: return moving ? "去水边" : "捕鱼";
    case G_HAUL: return r->carry == C_WOOD ? "搬木柴回营地" : r->carry == C_STONE ? "搬石料回营地" : "带食物回营地";
    case G_SLEEP: return "回去睡觉";
    case G_REST: return "休息";
    case G_FLEE: return "逃离危险";
    case G_CHAT: return moving ? "去找人聊天" : "聊天";
    case G_HELP: return moving ? "赶去救助同伴" : "照料伤者";
    case G_TEND: return moving ? "回家看孩子" : "照顾孩子";
    case G_BUILD: return moving ? "去选地盖房" : "盖房子";
    case G_UPGRADE: return moving ? "回家翻修" : "翻修房屋";
    case G_REPAIR: return moving ? "回家修房" : "修缮房屋";
    case G_TILL: return moving ? "去开垦" : "开垦农田";
    case G_HARVEST: return moving ? "去收庄稼" : "收割庄稼";
    case G_MINE: return moving ? "去采石" : "采石";
    case G_SHELTER: return moving ? "回家避风雨" : "在家躲避风雨";
    case G_SHRINE: return moving ? "去立祭石" : "垒起祭石";
    case G_PRAY: return moving ? "去祭石前" : "向神祈祷";
    case G_COWER: return "惶恐不安";
    default: return sim_age(r) < AGE_WORK ? "玩耍" : "闲逛";
    }
}

/* ---------- 派生数据：到水的距离、危险区 ---------- */

void sim_calc_wdist(const world_t *w)
{
    static uint16_t q[MAP_N];
    int head = 0, tail = 0;
    for (int i = 0; i < MAP_N; i++) {
        int t = TERR(w->tiles[i]);
        if (sim_is_water(t) || t == T_SEA) {
            sim_wdist[i] = 0;
            q[tail++] = (uint16_t)i;
        } else {
            sim_wdist[i] = 255;
        }
    }
    while (head < tail) {
        int c = q[head++], cx = c % MAP_W, cy = c / MAP_W;
        for (int d = 0; d < 4; d++) {
            int nx = cx + DX[d], ny = cy + DY[d];
            if (!inmap(nx, ny)) continue;
            int n = IDX(nx, ny);
            if (sim_wdist[n] != 255) continue;
            sim_wdist[n] = (uint8_t)(sim_wdist[c] + 1);
            q[tail++] = (uint16_t)n;
        }
    }
    for (int i = 0; i < MAP_N; i++)
        if (sim_wdist[i] > 15) sim_wdist[i] = 15;
}

void sim_calc_danger(const world_t *w)
{
    memset(sim_danger, 0, sizeof sim_danger);
    s_hazard = false;
    if (!w->fires && !w->lava && !w->flood) return;
    for (int i = 0; i < MAP_N; i++) {
        bool hot = OBJ(w->tiles[i]) == O_FLAME || TERR(w->tiles[i]) == T_LAVA;
        if (!hot && !sim_flooded(w, i)) continue;
        s_hazard = true;
        sim_danger[i] = 1;
        if (!hot) continue;
        int cx = i % MAP_W, cy = i / MAP_W;
        for (int dy = -3; dy <= 3; dy++)
            for (int dx = -3; dx <= 3; dx++) {
                if (!inmap(cx + dx, cy + dy)) continue;
                int n = IDX(cx + dx, cy + dy);
                int lvl = (iabs(dx) <= 1 && iabs(dy) <= 1) ? 2 : 3;
                if (sim_danger[n] == 0 || (sim_danger[n] == 3 && lvl == 2)) sim_danger[n] = (uint8_t)lvl;
            }
    }
}

/* ---------- 寻路：780 格上的 BFS，不保存路径，每走一格重算一步 ---------- */

static uint16_t s_q[MAP_N];
static int s_self;     /* 当前决策的居民下标，供谓词使用 */
static int s_goal_idx; /* pred_goal 的目标格 */

void sim_set_self(int i) { s_self = i; }

int sim_bfs(const world_t *w, int sx, int sy, sim_pred_t pred, int *dir_out)
{
    memset(sim_par, 0, sizeof sim_par);
    int s = IDX(sx, sy), head = 0, tail = 0;
    sim_par[s] = 5;
    s_q[tail++] = (uint16_t)s;
    while (head < tail) {
        int c = s_q[head++];
        if (pred(w, c)) {
            if (dir_out) {
                int d = -1, k = c;
                while (k != s) {
                    d = sim_par[k] - 1;
                    k -= DX[d] + DY[d] * MAP_W;
                }
                *dir_out = d;
            }
            return c;
        }
        int cx = c % MAP_W, cy = c / MAP_W;
        int rot = ((cx + cy) & 1) * 2; /* 交替先横后纵，走出斜向的折线 */
        for (int k = 0; k < 4; k++) {
            int d = (k + rot) & 3;
            int nx = cx + DX[d], ny = cy + DY[d];
            if (!inmap(nx, ny)) continue;
            int n = IDX(nx, ny);
            if (sim_par[n] || !sim_walkable(w, n)) continue;
            sim_par[n] = (uint8_t)(d + 1);
            s_q[tail++] = (uint16_t)n;
        }
    }
    return -1;
}
#define bfs sim_bfs

bool sim_pred_none(const world_t *w, int idx)
{
    (void)w;
    (void)idx;
    return false;
}
static bool pred_goal(const world_t *w, int idx)
{
    (void)w;
    return idx == s_goal_idx;
}

/* 目标是否已被别的居民占了（避免五个人抢同一棵树） */
static bool taken(const world_t *w, int idx)
{
    int x = idx % MAP_W, y = idx / MAP_W;
    for (int i = 0; i < MAX_RES; i++) {
        const resident_t *r = &w->res[i];
        if (i == s_self || !r->alive || r->gx != x || r->gy != y) continue;
        switch (r->goal) {
        case G_BERRY: case G_EAT_BERRY: case G_CHOP: case G_FISH:
        case G_BUILD: case G_TILL: case G_HARVEST: case G_MINE: case G_SHRINE: return true;
        default: break;
        }
    }
    return false;
}
static bool free_tile(const world_t *w, int idx) { return !sim_danger[idx] && !taken(w, idx); }
static bool near4(const world_t *w, int idx, int terr, int obj)
{
    int x = idx % MAP_W, y = idx / MAP_W;
    for (int d = 0; d < 4; d++) {
        int nx = x + DX[d], ny = y + DY[d];
        if (!inmap(nx, ny)) continue;
        uint16_t t = w->tiles[IDX(nx, ny)];
        if ((terr >= 0 && TERR(t) == terr) || (obj >= 0 && OBJ(t) == obj)) return true;
    }
    return false;
}
static int camp_dist(const world_t *w, int idx) { return cheb(idx % MAP_W, idx / MAP_W, w->camp_x, w->camp_y); }

static bool pred_berry(const world_t *w, int idx) { return OBJ(w->tiles[idx]) == O_BERRY && free_tile(w, idx); }
static bool pred_tree(const world_t *w, int idx) { return OBJ(w->tiles[idx]) == O_TREE && free_tile(w, idx); }
static bool pred_ripe(const world_t *w, int idx)
{
    return OBJ(w->tiles[idx]) == O_FARM && VAL(w->tiles[idx]) >= 100 && free_tile(w, idx);
}
bool sim_pred_shore(const world_t *w, int idx)
{
    if (TERR(w->tiles[idx]) == T_RIVER || !free_tile(w, idx)) return false;
    return near4(w, idx, T_SHOAL, -1) || near4(w, idx, T_LAKE, -1) || near4(w, idx, T_RIVER, -1);
}
static bool pred_mine(const world_t *w, int idx) { return free_tile(w, idx) && near4(w, idx, T_ROCK, -1); }
static bool pred_safe(const world_t *w, int idx)
{
    (void)w;
    return sim_danger[idx] == 0;
}
static bool empty_grass(const world_t *w, int idx)
{
    int o = OBJ(w->tiles[idx]);
    return TERR(w->tiles[idx]) == T_GRASS && (o == O_NONE || o == O_FLOWER);
}
/* 宅基地：离营地 3~10 格、离水至少 3 格、不和别的房子挨着 */
static bool pred_site(const world_t *w, int idx)
{
    if (!free_tile(w, idx)) return false;
    if (OBJ(w->tiles[idx]) == O_SITE) return true; /* 接手没盖完的工地 */
    if (!empty_grass(w, idx) || sim_wdist[idx] < 3) return false;
    int d = camp_dist(w, idx);
    if (d < 3 || d > 10) return false;
    int x = idx % MAP_W, y = idx / MAP_W;
    for (int dy = -1; dy <= 1; dy++)
        for (int dx = -1; dx <= 1; dx++) {
            if (!inmap(x + dx, y + dy)) continue;
            int o = OBJ(w->tiles[IDX(x + dx, y + dy)]);
            if (sim_is_house(o) || o == O_SITE) return false;
        }
    return true;
}
static bool pred_till_any(const world_t *w, int idx)
{
    if (!free_tile(w, idx) || !empty_grass(w, idx) || sim_wdist[idx] < 2) return false;
    int d = camp_dist(w, idx);
    return d >= 5 && d <= 8 && !near4(w, idx, -1, O_HOUSE1) && !near4(w, idx, -1, O_HOUSE2);
}
static bool pred_shrine(const world_t *w, int idx)
{
    return free_tile(w, idx) && empty_grass(w, idx) && camp_dist(w, idx) == 3 && !sim_occupied(w, idx);
}
static bool pred_till_adj(const world_t *w, int idx) { return pred_till_any(w, idx) && near4(w, idx, -1, O_FARM); }

/* ---------- 居民：出生、死亡、受伤 ---------- */

bool sim_camp_spot(const world_t *w, int i, int *x, int *y)
{
    for (int k = 0; k < MAX_RES; k++) {
        int j = (i + k) % MAX_RES;
        int sx = w->camp_x + SPOT[j][0], sy = w->camp_y + SPOT[j][1];
        if (inmap(sx, sy) && sim_walkable(w, IDX(sx, sy))) {
            *x = sx;
            *y = sy;
            return true;
        }
    }
    return false;
}

int sim_spawn_resident(world_t *w, int sex, int trait, int age_years, int at_idx)
{
    int slot = -1;
    for (int i = 0; i < MAX_RES; i++)
        if (!w->res[i].alive) { slot = i; break; }
    if (slot < 0) return -1;
    /* 挑一个在世居民没用过的名字 */
    int name = rn(w, NAME_N);
    for (int k = 0; k < NAME_N; k++) {
        bool used = false;
        for (int i = 0; i < MAX_RES; i++)
            if (w->res[i].alive && w->res[i].sex == sex && w->res[i].name == name) used = true;
        if (!used) break;
        name = (name + 1) % NAME_N;
    }
    resident_t *r = &w->res[slot];
    memset(r, 0, sizeof *r);
    r->alive = 1;
    r->name = (uint8_t)name;
    r->sex = (uint8_t)sex;
    r->trait = (uint8_t)trait;
    r->uid = w->next_uid++;
    r->age_days = (uint16_t)(age_years * DAYS_PER_YEAR + (age_years ? rn(w, DAYS_PER_YEAR) : 0));
    r->hp = 1000;
    r->food = (uint16_t)(600 + rn(w, 300));
    r->energy = (uint16_t)(700 + rn(w, 300));
    r->mood = 600;
    r->partner = NO_SLOT;
    r->home = NO_ID;
    r->father = r->mother = NO_ID;
    r->fname = r->mname = NO_SLOT;
    r->gen = 1;
    int x = w->camp_x, y = w->camp_y + 1;
    if (at_idx >= 0) {
        x = at_idx % MAP_W;
        y = at_idx / MAP_W;
    } else {
        sim_camp_spot(w, slot, &x, &y);
    }
    r->tx = r->nx = (uint8_t)x;
    r->ty = r->ny = (uint8_t)y;
    for (int j = 0; j < MAX_RES; j++) w->rel[slot][j] = w->rel[j][slot] = 0;
    sim_tree_put(w, r);
    return slot;
}

void sim_die(world_t *w, int i, int cause)
{
    resident_t *r = &w->res[i];
    if (!r->alive) return;
    r->alive = 0;
    w->deaths++;
    for (int k = 0; k < MAX_ANI; k++)
        if (w->ani[k].held == i + 1) w->ani[k].held = 0;
    if (r->partner != NO_SLOT && w->res[r->partner].partner == i) w->res[r->partner].partner = NO_SLOT;
    int idx = IDX(r->tx, r->ty);
    int o = OBJ(w->tiles[idx]), t = TERR(w->tiles[idx]);
    if (t != T_LAVA && (o == O_NONE || o == O_FLOWER || o == O_STUMP || o == O_BURNT))
        w->tiles[idx] = MKTILE(t, O_GRAVE);
    dead_t *d = &w->dead[w->dead_n % MAX_DEAD];
    w->dead_n++;
    *d = (dead_t){r->name, r->sex, (uint8_t)sim_age(r), r->gen, (uint8_t)cause, 1, (uint16_t)sim_day(w)};
    person_t *pr = &g_tree[r->uid % TREE_N];
    if (pr->uid == r->uid) {
        pr->age = (uint8_t)(sim_age(r) > 250 ? 250 : sim_age(r));
        pr->cause = (uint8_t)cause;
        g_tree_dirty = true;
    }
    sim_sfx(SFX_DEATH);
    /* 天灾夺命，岛民会觉得是神在发怒；饿死人，则是神不眷顾 */
    if (cause == D_STARVE) sim_devotion(w, -3, 0);
    else if (cause != D_AGE && cause != D_WOLF) sim_devotion(w, 0, 4);
    const char *n = sim_name(r);
    switch (cause) {
    case D_STARVE: sim_logf("%s饿死了", n); break;
    case D_AGE: sim_logf("%s因年老去世，享年%d岁", n, sim_age(r)); break;
    case D_BOLT: sim_logf("%s被雷火夺去了生命", n); break;
    case D_FIRE: sim_logf("%s葬身火海", n); break;
    case D_FLOOD: sim_logf("%s被洪水卷走了", n); break;
    case D_LAVA: sim_logf("%s葬身岩浆", n); break;
    case D_QUAKE: sim_logf("%s在地震中遇难", n); break;
    case D_PLAGUE: sim_logf("%s病死了", n); break;
    case D_WOLF: sim_logf("%s被狼咬死了", n); break;
    default: sim_logf("%s在风雪中冻死了", n); break;
    }
    if (sim_pop(w) == 0) sim_logf("岛上已无人烟");
}

bool sim_hurt(world_t *w, int i, int amount, int cause)
{
    resident_t *r = &w->res[i];
    if (r->hp <= amount) {
        sim_die(w, i, cause);
        return false;
    }
    r->hp = (uint16_t)(r->hp - amount);
    return true;
}

void sim_infect(world_t *w, int i)
{
    resident_t *r = &w->res[i];
    if (!r->alive || r->sick || r->immune) return;
    r->sick = (uint8_t)(4 + rn(w, 3));
}

void sim_house_lost(world_t *w, int idx)
{
    const char *who = NULL;
    for (int i = 0; i < MAX_RES; i++)
        if (w->res[i].alive && w->res[i].home == idx) {
            w->res[i].home = NO_ID;
            if (!who || sim_age(&w->res[i]) >= AGE_ADULT) who = sim_name(&w->res[i]);
        }
    if (who) sim_logf("%s家的房子毁了", who);
}

static bool home_ok(const world_t *w, const resident_t *r)
{
    return r->home != NO_ID && sim_is_house(OBJ(w->tiles[r->home]));
}

/* ---------- 居民：决策 ---------- */

static void drop_task(world_t *w, int i)
{
    for (int k = 0; k < MAX_ANI; k++)
        if (w->ani[k].held == i + 1) w->ani[k].held = 0;
}

static void set_idle(resident_t *r, int wait)
{
    r->state = S_IDLE;
    r->goal = G_NONE;
    r->timer = (uint16_t)wait;
    r->step = 0;
    r->nx = r->tx;
    r->ny = r->ty;
}

static void go(resident_t *r, int goal, int idx)
{
    r->goal = (uint8_t)goal;
    r->gx = (uint8_t)(idx % MAP_W);
    r->gy = (uint8_t)(idx / MAP_W);
    r->state = S_MOVE;
    r->timer = 0;
}

static bool reachable(world_t *w, int i, int idx)
{
    s_self = i;
    s_goal_idx = idx;
    return bfs(w, w->res[i].tx, w->res[i].ty, pred_goal, NULL) >= 0;
}

static int camp_idx(const world_t *w, int i)
{
    int x, y;
    if (!sim_camp_spot(w, i, &x, &y)) return IDX(w->res[i].tx, w->res[i].ty);
    return IDX(x, y);
}
static bool camp_safe(const world_t *w, int i) { return sim_danger[camp_idx(w, i)] == 0; }
static void go_camp(world_t *w, int i, int goal) { go(&w->res[i], goal, camp_idx(w, i)); }

static void go_sleep(world_t *w, int i)
{
    resident_t *r = &w->res[i];
    if (home_ok(w, r) && !sim_danger[r->home] && reachable(w, i, r->home)) go(r, G_SLEEP, r->home);
    else if (camp_safe(w, i)) go_camp(w, i, G_SLEEP);
    else go(r, G_SLEEP, IDX(r->tx, r->ty));
}

static bool try_find(world_t *w, int i, sim_pred_t pred, int goal)
{
    resident_t *r = &w->res[i];
    s_self = i;
    int idx = bfs(w, r->tx, r->ty, pred, NULL);
    if (idx < 0) return false;
    go(r, goal, idx);
    return true;
}

/* small_only：只抓兔子（胆小的人、少年和老人也敢） */
static bool try_hunt(world_t *w, int i, bool small_only)
{
    resident_t *r = &w->res[i];
    int best = -1, bestd = 999;
    int hunters[A_KINDS] = {0};
    for (int j = 0; j < MAX_RES; j++)
        if (w->res[j].alive && w->res[j].goal == G_HUNT) hunters[w->ani[w->res[j].tgt % MAX_ANI].kind % A_KINDS]++;
    for (int k = 0; k < MAX_ANI; k++) {
        const animal_t *a = &w->ani[k];
        if (!a->alive || a->held || a->kind == A_WOLF || sim_danger[IDX(a->tx, a->ty)]) continue;
        if (small_only && a->kind != A_RABBIT) continue;
        /* 只剩一两头时很难找到，除非快饿死了否则不去追，给种群留条活路 */
        if (sim_kind_count(w, a->kind) - hunters[a->kind] <= 2 && r->food >= 150) continue;
        bool chased = false;
        for (int j = 0; j < MAX_RES; j++)
            if (j != i && w->res[j].alive && w->res[j].goal == G_HUNT && w->res[j].tgt == k) chased = true;
        if (chased) continue;
        int d = iabs(a->tx - r->tx) + iabs(a->ty - r->ty);
        if (a->kind == A_RABBIT) d += 4; /* 同样远近先打野猪，肉多 */
        if (d < bestd) { bestd = d; best = k; }
    }
    if (best < 0 || !reachable(w, i, IDX(w->ani[best].tx, w->ani[best].ty))) return false;
    go(r, G_HUNT, s_goal_idx);
    r->tgt = (uint8_t)best;
    r->timer = 70; /* 最多追这么多格 */
    return true;
}

static bool try_fish(world_t *w, int i) { return w->fish > 0 && try_find(w, i, sim_pred_shore, G_FISH); }
static bool try_berry(world_t *w, int i) { return try_find(w, i, pred_berry, G_BERRY); }
static bool try_harvest(world_t *w, int i) { return try_find(w, i, pred_ripe, G_HARVEST); }

/* 按性格和年纪决定找食物的顺序；force 表示自己已经饿了 */
static bool food_job(world_t *w, int i, bool force)
{
    resident_t *r = &w->res[i];
    int age = sim_age(r);
    if (try_harvest(w, i)) return true;
    if (age < AGE_ADULT || age >= AGE_ELDER || r->sick)
        return try_berry(w, i) || try_fish(w, i) || (!r->sick && try_hunt(w, i, true));
    if (r->trait == TR_BRAVE) return try_hunt(w, i, false) || try_fish(w, i) || try_berry(w, i);
    if (r->trait == TR_TIMID)
        return try_berry(w, i) || try_hunt(w, i, true) || try_fish(w, i) ||
               (force && r->food < 150 && try_hunt(w, i, false));
    int k = rn(w, 100);
    if (k < 40) return try_berry(w, i) || try_fish(w, i) || try_hunt(w, i, false);
    if (k < 70) return try_fish(w, i) || try_berry(w, i) || try_hunt(w, i, false);
    return try_hunt(w, i, false) || try_fish(w, i) || try_berry(w, i);
}

static void wander(world_t *w, int i, int goal)
{
    resident_t *r = &w->res[i];
    int bx = r->tx, by = r->ty;
    int k = rn(w, 100);
    if (k < 40) {
        bx = w->camp_x;
        by = w->camp_y;
    } else if (k < 70 && home_ok(w, r)) {
        bx = r->home % MAP_W;
        by = r->home / MAP_W;
    }
    for (int tries = 0; tries < 6; tries++) {
        int x = bx + rn(w, 9) - 4, y = by + rn(w, 9) - 4;
        if (!inmap(x, y) || !sim_walkable(w, IDX(x, y)) || sim_danger[IDX(x, y)]) continue;
        if (!reachable(w, i, IDX(x, y))) continue;
        go(r, goal, s_goal_idx);
        return;
    }
    set_idle(r, 20 + rn(w, 30));
}

static bool try_chat(world_t *w, int i)
{
    resident_t *r = &w->res[i];
    int j = rn(w, MAX_RES);
    for (int k = 0; k < MAX_RES; k++, j = (j + 1) % MAX_RES) {
        const resident_t *o = &w->res[j];
        if (j == i || !o->alive || o->state == S_SLEEP || o->goal == G_FLEE || o->goal == G_HUNT) continue;
        if (sim_age(o) < AGE_CHILD || cheb(r->tx, r->ty, o->tx, o->ty) > 9) continue;
        if (sim_danger[IDX(o->tx, o->ty)] || !reachable(w, i, IDX(o->tx, o->ty))) continue;
        go(r, G_CHAT, s_goal_idx);
        r->tgt = (uint8_t)j;
        return true;
    }
    return false;
}

static bool is_parent(const resident_t *p, const resident_t *c) { return c->father == p->uid || c->mother == p->uid; }
static bool kin(const resident_t *a, const resident_t *b)
{
    if (is_parent(a, b) || is_parent(b, a)) return true;
    return (a->father != NO_ID && a->father == b->father) || (a->mother != NO_ID && a->mother == b->mother);
}

static bool someone_doing(const world_t *w, int self, int goal, int tgt)
{
    for (int j = 0; j < MAX_RES; j++)
        if (j != self && w->res[j].alive && w->res[j].goal == goal && w->res[j].tgt == tgt) return true;
    return false;
}

/* 有没有饿着的婴儿需要我去喂 */
static bool try_tend(world_t *w, int i)
{
    resident_t *r = &w->res[i];
    if (w->food == 0) return false;
    for (int j = 0; j < MAX_RES; j++) {
        const resident_t *b = &w->res[j];
        if (!b->alive || sim_age(b) >= AGE_CHILD || b->food >= 450) continue;
        bool mine = is_parent(r, b);
        if (!mine) { /* 孤儿，或者父母顾不上：善良的人会搭把手，快饿坏了谁都会管 */
            if (r->trait != TR_KIND && b->food >= 200) continue;
        }
        if (someone_doing(w, i, G_TEND, j) || sim_danger[IDX(b->tx, b->ty)]) continue;
        if (!reachable(w, i, IDX(b->tx, b->ty))) continue;
        go(r, G_TEND, s_goal_idx);
        r->tgt = (uint8_t)j;
        return true;
    }
    return false;
}

static bool try_help(world_t *w, int i)
{
    resident_t *r = &w->res[i];
    for (int j = 0; j < MAX_RES; j++) {
        const resident_t *o = &w->res[j];
        if (j == i || !o->alive || o->hp >= 400 || sim_age(o) < AGE_CHILD) continue;
        if (r->trait != TR_KIND && w->rel[i][j] < 40 && r->partner != j && !kin(r, o)) continue;
        if (someone_doing(w, i, G_HELP, j) || sim_danger[IDX(o->tx, o->ty)]) continue;
        if (!reachable(w, i, IDX(o->tx, o->ty))) continue;
        go(r, G_HELP, s_goal_idx);
        r->tgt = (uint8_t)j;
        return true;
    }
    return false;
}

/* 没有家：先投靠伴侣或父母，再找空房，最后自己盖 */
static void adopt_home(world_t *w, int i)
{
    resident_t *r = &w->res[i];
    if (home_ok(w, r)) return;
    r->home = NO_ID;
    if (r->partner != NO_SLOT && home_ok(w, &w->res[r->partner])) {
        r->home = w->res[r->partner].home;
        return;
    }
    if (sim_age(r) < AGE_ADULT)
        for (int j = 0; j < MAX_RES; j++)
            if (w->res[j].alive && is_parent(&w->res[j], r) && home_ok(w, &w->res[j])) {
                r->home = w->res[j].home;
                return;
            }
}

static bool try_claim(world_t *w, int i)
{
    resident_t *r = &w->res[i];
    if (!sim_houses) return false;
    for (int t = 0; t < MAP_N; t++) {
        if (!sim_is_house(OBJ(w->tiles[t])) || sim_danger[t]) continue;
        bool owned = false;
        for (int j = 0; j < MAX_RES; j++)
            if (w->res[j].alive && w->res[j].home == t) owned = true;
        if (owned) continue;
        r->home = (uint16_t)t;
        sim_logf("%s搬进了一间空屋", sim_name(r));
        return true;
    }
    return false;
}

static bool shrine_ok(const world_t *w) { return w->shrine != NO_ID && OBJ(w->tiles[w->shrine]) == O_SHRINE; }

/* 走到祭石旁边祈祷 */
static bool try_pray(world_t *w, int i)
{
    if (!shrine_ok(w) || sim_danger[w->shrine]) return false;
    int sx = w->shrine % MAP_W, sy = w->shrine / MAP_W, d = rn(w, 4);
    for (int k = 0; k < 4; k++, d = (d + 1) & 3) {
        int x = sx + DX[d], y = sy + DY[d];
        if (!inmap(x, y) || !sim_walkable(w, IDX(x, y)) || !reachable(w, i, IDX(x, y))) continue;
        go(&w->res[i], G_PRAY, IDX(x, y));
        return true;
    }
    return false;
}

static bool bad_weather(const world_t *w) { return w->weather == W_STORM || w->weather == W_SNOW || w->weather == W_THUNDER; }

static void decide_child(world_t *w, int i)
{
    resident_t *r = &w->res[i];
    if (r->food < 450) {
        if (w->food > 0 && camp_safe(w, i)) { go_camp(w, i, G_EAT_CAMP); return; }
        if (try_find(w, i, pred_berry, G_EAT_BERRY)) return;
    }
    if (sim_is_night(w) || r->energy < 150) { go_sleep(w, i); return; }
    if (bad_weather(w) && home_ok(w, r) && reachable(w, i, r->home)) { go(r, G_SHELTER, r->home); return; }
    if (rn(w, 100) < 45 && try_chat(w, i)) return;
    wander(w, i, G_WANDER);
}

static void decide(world_t *w, int i)
{
    resident_t *r = &w->res[i];
    int age = sim_age(r);
    bool night = sim_is_night(w);
    bool worker = age >= AGE_WORK && !r->sick;
    bool strong = age >= AGE_ADULT && age < AGE_ELDER && !r->sick;

    adopt_home(w, i);
    if (age < AGE_WORK) { decide_child(w, i); return; }

    if (r->carry_n) {
        if (camp_safe(w, i)) go_camp(w, i, G_HAUL);
        else wander(w, i, G_REST);
        return;
    }
    if (r->energy < 100 && r->food >= 200) { go_sleep(w, i); return; }

    /* 善良的人会多忍一会儿，把仓库里的食物留给别人 */
    int hungry = (r->trait == TR_KIND) ? 300 : 420;
    if (r->food < hungry) {
        if (w->food > 0 && camp_safe(w, i)) { go_camp(w, i, G_EAT_CAMP); return; }
        if (try_find(w, i, pred_berry, G_EAT_BERRY)) return;
        if (food_job(w, i, true)) return;
    }
    if (try_tend(w, i)) return;
    if (try_help(w, i)) return;
    if (night) { go_sleep(w, i); return; }
    if (r->sick && r->hp < 600) { /* 病重：在家或营地歇着 */
        if (home_ok(w, r) && reachable(w, i, r->home)) go(r, G_SHELTER, r->home);
        else wander(w, i, G_REST);
        return;
    }

    int pop = sim_pop(w);
    bool need_food = w->food < pop * 2;
    if (bad_weather(w) && !need_food) { /* 恶劣天气：有家回家，没家就守着篝火 */
        if (home_ok(w, r) && reachable(w, i, r->home)) { go(r, G_SHELTER, r->home); return; }
        if (w->weather == W_SNOW && camp_safe(w, i)) { go_camp(w, i, G_REST); return; }
    }

    /* 恐惧：神降灾太多，岛民惶惶不可终日，胆小的人尤其如此 */
    if (w->fear >= 40 && !need_food && rn(w, 100) < (w->fear - 30) / 2 + (r->trait == TR_TIMID ? 20 : 0)) {
        if (home_ok(w, r) && reachable(w, i, r->home)) go(r, G_COWER, r->home);
        else if (camp_safe(w, i)) go_camp(w, i, G_COWER);
        else set_idle(r, 30);
        return;
    }
    /* 信仰：立一块祭石，闲下来去拜一拜 */
    if (strong && w->faith >= 35 && w->faith > w->fear && !shrine_ok(w) && rn(w, 100) < 25 &&
        try_find(w, i, pred_shrine, G_SHRINE))
        return;
    if (w->faith >= 20 && rn(w, 100) < w->faith / 6 && try_pray(w, i)) return;

    int laze = r->trait == TR_LAZY ? 35 : r->trait == TR_DILIGENT ? 3 : 12;
    if (w->faith >= 60) laze -= laze / 3; /* 虔诚的岛民更勤快 */
    if (r->energy < 350) laze += 30;
    if (age >= AGE_ELDER) laze += 25;
    if (r->sick) laze += 40;
    int hour = sim_hour(w);
    if (hour >= 18) laze += 30; /* 傍晚大家聚在一起 */
    if (rn(w, 100) < laze) {
        if (rn(w, 100) < 60 && try_chat(w, i)) return;
        wander(w, i, rn(w, 2) ? G_REST : G_WANDER);
        return;
    }

    /* 自家房子坏了先修 */
    if (worker && home_ok(w, r) && VAL(w->tiles[r->home]) < 70 && w->wood >= 2 && reachable(w, i, r->home)) {
        go(r, G_REPAIR, r->home);
        return;
    }
    bool need_wood = w->wood < 6;
    if (strong && need_wood && (!need_food || r->trait == TR_KIND || rn(w, 100) < 30))
        if (try_find(w, i, pred_tree, G_CHOP)) return;
    if (worker && (need_food || (w->food < pop * 5 && rn(w, 100) < 50)))
        if (food_job(w, i, false)) return;
    if (worker && w->food < pop * 8 && rn(w, 100) < 60 && try_harvest(w, i)) return;

    /* 野猪多的时候，勇敢的人会主动去打猎；多到泛滥时谁都会去 */
    {
        int boars = sim_kind_count(w, A_BOAR), rabbits = sim_kind_count(w, A_RABBIT);
        if (strong && w->food < pop * 12) {
            if (((r->trait == TR_BRAVE && boars >= 6 && rn(w, 100) < 35) || (boars >= 9 && rn(w, 100) < 20)) &&
                try_hunt(w, i, false))
                return;
            if (rabbits >= 8 && rn(w, 100) < (r->trait == TR_BRAVE ? 25 : 10) && try_hunt(w, i, true)) return;
        }
    }
    /* 安家 */
    if (strong && !home_ok(w, r)) {
        if (try_claim(w, i)) { set_idle(r, 5); return; }
        if (w->wood >= 12 && try_find(w, i, pred_site, G_BUILD)) return;
        if (w->wood < 12 && rn(w, 100) < 60 && try_find(w, i, pred_tree, G_CHOP)) return;
    }
    if (strong && home_ok(w, r) && OBJ(w->tiles[r->home]) == O_HOUSE1 && w->wood >= 12 && w->stone >= 4 &&
        rn(w, 100) < (r->trait == TR_LAZY ? 10 : 40) && reachable(w, i, r->home)) {
        go(r, G_UPGRADE, r->home);
        return;
    }
    /* 开垦：每人一块半地为限 */
    int want = pop * 3 / 2 > 20 ? 20 : pop * 3 / 2;
    if (worker && sim_farms < want && rn(w, 100) < (r->trait == TR_DILIGENT ? 60 : 30)) {
        if (sim_farms > 0 && try_find(w, i, pred_till_adj, G_TILL)) return;
        if (try_find(w, i, pred_till_any, G_TILL)) return;
    }
    if (strong && sim_houses > 0 && w->stone < 8 && rn(w, 100) < 25 && try_find(w, i, pred_mine, G_MINE)) return;
    if (strong && w->wood < 16 && rn(w, 100) < 40)
        if (try_find(w, i, pred_tree, G_CHOP)) return;
    if (rn(w, 100) < 40 && try_chat(w, i)) return;
    wander(w, i, G_WANDER);
}

/* ---------- 居民：行动 ---------- */

static void start_act(resident_t *r, int ticks)
{
    r->state = S_ACT;
    r->timer = (uint16_t)ticks;
}

static bool near_tgt(const world_t *w, const resident_t *r)
{
    const resident_t *o = &w->res[r->tgt % MAX_RES];
    return o->alive && cheb(r->tx, r->ty, o->tx, o->ty) <= 2;
}

static void arrive(world_t *w, int i)
{
    resident_t *r = &w->res[i];
    int idx = IDX(r->tx, r->ty);
    int o = OBJ(w->tiles[idx]), v = VAL(w->tiles[idx]);
    switch (r->goal) {
    case G_EAT_CAMP: start_act(r, 15); break;
    case G_EAT_BERRY:
    case G_BERRY:
        if (o == O_BERRY) start_act(r, 20);
        else set_idle(r, 0);
        break;
    case G_CHOP:
        if (o == O_TREE) start_act(r, 50);
        else set_idle(r, 0);
        break;
    case G_FISH: start_act(r, 60); break;
    case G_HAUL: start_act(r, 6); break;
    case G_SLEEP: r->state = S_SLEEP; break;
    case G_REST: start_act(r, 60 + rn(w, 60)); break;
    case G_SHELTER: start_act(r, 80); break;
    case G_COWER: start_act(r, 100); break;
    case G_PRAY: start_act(r, 40); break;
    case G_SHRINE:
        if (empty_grass(w, idx) && !shrine_ok(w)) start_act(r, 100);
        else set_idle(r, 0);
        break;
    case G_FLEE: start_act(r, 25); break;
    case G_CHAT:
        if (near_tgt(w, r) && w->res[r->tgt].state != S_SLEEP) {
            r->face = w->res[r->tgt].tx < r->tx;
            start_act(r, 30);
        } else {
            set_idle(r, 0);
        }
        break;
    case G_HELP:
    case G_TEND:
        if (near_tgt(w, r)) start_act(r, r->goal == G_HELP ? 40 : 20);
        else set_idle(r, 0);
        break;
    case G_BUILD:
        if (o == O_SITE) {
            start_act(r, 120);
        } else if (empty_grass(w, idx) && w->wood >= 8) {
            w->wood -= 8;
            w->tiles[idx] = MKTILE(T_GRASS, O_SITE);
            start_act(r, 120);
        } else {
            set_idle(r, 0);
        }
        break;
    case G_UPGRADE:
        if (o == O_HOUSE1 && w->wood >= 6 && w->stone >= 4) {
            w->wood -= 6;
            w->stone -= 4;
            start_act(r, 150);
        } else {
            set_idle(r, 0);
        }
        break;
    case G_REPAIR:
        if (sim_is_house(o) && v < 100 && w->wood >= 2) {
            w->wood -= 2;
            start_act(r, 60);
        } else {
            set_idle(r, 0);
        }
        break;
    case G_TILL:
        if (empty_grass(w, idx)) start_act(r, 60);
        else set_idle(r, 0);
        break;
    case G_HARVEST:
        if (o == O_FARM && v >= 100) start_act(r, 30);
        else set_idle(r, 0);
        break;
    case G_MINE: start_act(r, 70); break;
    default: start_act(r, 15 + rn(w, 40)); break;
    }
}

static void add_food(resident_t *r, int v)
{
    int f = r->food + v;
    r->food = (uint16_t)(f > 1000 ? 1000 : f);
}
static void add_mood(resident_t *r, int v)
{
    int f = r->mood + v;
    r->mood = (uint16_t)(f > 1000 ? 1000 : f);
}
static void tire(resident_t *r, int v) { r->energy = (uint16_t)(r->energy > v ? r->energy - v : 0); }

static void marry(world_t *w, int a, int b)
{
    resident_t *ra = &w->res[a], *rb = &w->res[b];
    ra->partner = (uint8_t)b;
    rb->partner = (uint8_t)a;
    sim_logf("%s和%s结为伴侣", sim_name(ra), sim_name(rb));
    w->marriages++;
    sim_sfx(SFX_CHIME);
    /* 还跟父母挤在一起的，成家后要自立门户 */
    for (int j = 0; j < MAX_RES; j++) {
        const resident_t *p = &w->res[j];
        if (!p->alive) continue;
        if (is_parent(p, ra) && p->home == ra->home) ra->home = NO_ID;
        if (is_parent(p, rb) && p->home == rb->home) rb->home = NO_ID;
    }
    if (home_ok(w, ra)) rb->home = ra->home; /* 住到一起；空出来的房子别人可以搬进去 */
    else if (home_ok(w, rb)) ra->home = rb->home;
    add_mood(ra, 200);
    add_mood(rb, 200);
}

static void bond(world_t *w, int a, int b, int d)
{
    resident_t *ra = &w->res[a], *rb = &w->res[b];
    int old = w->rel[a][b], v = old + d;
    if (v > 100) v = 100;
    w->rel[a][b] = w->rel[b][a] = (uint8_t)v;
    add_mood(ra, 30);
    add_mood(rb, 30);
    if (old < 40 && v >= 40 && sim_age(ra) >= AGE_WORK && sim_age(rb) >= AGE_WORK)
        sim_logf("%s和%s成了朋友", sim_name(ra), sim_name(rb));
    if (v >= 70 && ra->sex != rb->sex && ra->partner == NO_SLOT && rb->partner == NO_SLOT &&
        sim_age(ra) >= AGE_ADULT && sim_age(rb) >= AGE_ADULT && !kin(ra, rb))
        marry(w, a, b);
    /* 面对面说话会传染 */
    if ((ra->sick || rb->sick) && rn(w, 100) < 35) {
        resident_t *t = ra->sick ? rb : ra;
        if (!t->sick && !t->immune) {
            sim_infect(w, ra->sick ? b : a);
            sim_logf("%s也染上了瘟疫", sim_name(t));
        }
    }
}

static void finish(world_t *w, int i)
{
    resident_t *r = &w->res[i];
    int idx = IDX(r->tx, r->ty);
    int te = TERR(w->tiles[idx]), o = OBJ(w->tiles[idx]);
    switch (r->goal) {
    case G_EAT_CAMP:
        if (w->food > 0) {
            w->food--;
            add_food(r, w->fire_lit ? 420 : 350); /* 有火就能烤熟了吃，更顶饿 */
        }
        break;
    case G_EAT_BERRY:
        if (o == O_BERRY) {
            w->tiles[idx] = MKTILE(te, O_BUSH);
            add_food(r, 400);
        }
        break;
    case G_BERRY:
        if (o == O_BERRY) {
            w->tiles[idx] = MKTILE(te, O_BUSH);
            r->carry = C_FOOD;
            r->carry_n = 2;
            tire(r, 15);
        }
        break;
    case G_CHOP:
        if (o == O_TREE) {
            w->tiles[idx] = MKTILE(te, O_STUMP);
            r->carry = C_WOOD;
            r->carry_n = 4;
            tire(r, 35);
        }
        break;
    case G_FISH: {
        tire(r, 20);
        int p = (w->weather == W_STORM || w->weather == W_WIND) ? 40 : w->weather == W_SNOW ? 50 : 70;
        if (w->fish > 0 && rn(w, 100) < p) {
            w->fish--;
            r->carry = C_FOOD;
            r->carry_n = 3;
            if (rn(w, 100) < 20) sim_logf("%s钓上来一条大鱼", sim_name(r));
        }
        break;
    }
    case G_HUNT: {
        animal_t *a = &w->ani[r->tgt % MAX_ANI];
        tire(r, 40);
        if (!a->alive || a->held != i + 1) break;
        a->held = 0;
        if (a->kind == A_RABBIT) { /* 兔子好抓，肉少，不伤人 */
            if (rn(w, 100) < 75) {
                a->alive = 0;
                r->carry = C_FOOD;
                r->carry_n = 3;
                if (rn(w, 100) < 30) sim_logf("%s抓到了一只兔子", sim_name(r));
            } else {
                a->flee = 12;
            }
            break;
        }
        int p = r->trait == TR_BRAVE ? 85 : r->trait == TR_TIMID ? 45 : 65;
        if (rn(w, 100) < p) {
            a->alive = 0;
            r->carry = C_FOOD;
            r->carry_n = 8;
            sim_logf("%s捕获了一头野猪", sim_name(r));
        } else {
            a->flee = 10;
            if (rn(w, 100) < 40) {
                r->hp = (uint16_t)(r->hp > 150 ? r->hp - 150 : 1);
                sim_logf("%s被野猪撞伤了", sim_name(r));
            }
        }
        break;
    }
    case G_HAUL:
        if (r->carry == C_FOOD) w->food = (uint16_t)(w->food + r->carry_n > 999 ? 999 : w->food + r->carry_n);
        if (r->carry == C_WOOD) w->wood = (uint16_t)(w->wood + r->carry_n > 999 ? 999 : w->wood + r->carry_n);
        if (r->carry == C_STONE) w->stone = (uint16_t)(w->stone + r->carry_n > 999 ? 999 : w->stone + r->carry_n);
        r->carry = C_NONE;
        r->carry_n = 0;
        break;
    case G_CHAT:
        if (near_tgt(w, r)) bond(w, i, r->tgt, 8 + (r->trait == TR_KIND ? 4 : 0) + rn(w, 5));
        break;
    case G_HELP:
        if (near_tgt(w, r)) {
            resident_t *t = &w->res[r->tgt];
            int heal = t->sick ? 120 : 300; /* 病人只能缓一缓，治不好 */
            t->hp = (uint16_t)(t->hp + heal > 1000 ? 1000 : t->hp + heal);
            sim_logf("%s照料了受伤的%s", sim_name(r), sim_name(t));
            bond(w, i, r->tgt, 10);
        }
        break;
    case G_TEND:
        if (near_tgt(w, r) && w->food > 0) {
            w->food--;
            add_food(&w->res[r->tgt], 420);
            add_mood(&w->res[r->tgt], 60);
        }
        break;
    case G_BUILD:
        if (o == O_SITE) {
            w->tiles[idx] = MKT(te, O_HOUSE1, 100);
            sim_houses++;
            w->built++;
            r->home = (uint16_t)idx;
            if (r->partner != NO_SLOT && !home_ok(w, &w->res[r->partner])) w->res[r->partner].home = (uint16_t)idx;
            sim_logf("%s盖起了一间小屋", sim_name(r));
            tire(r, 60);
            add_mood(r, 150);
        }
        break;
    case G_UPGRADE:
        if (o == O_HOUSE1) {
            w->tiles[idx] = MKT(te, O_HOUSE2, 100);
            sim_logf("%s把小屋翻修成了木屋", sim_name(r));
            tire(r, 60);
        }
        break;
    case G_REPAIR:
        if (sim_is_house(o)) w->tiles[idx] = MKT(te, o, 100);
        break;
    case G_TILL:
        if (empty_grass(w, idx)) {
            w->tiles[idx] = MKT(T_GRASS, O_FARM, 1);
            sim_farms++;
            tire(r, 30);
            if (sim_farms == 1) sim_logf("%s开垦了岛上第一块农田", sim_name(r));
        }
        break;
    case G_HARVEST:
        if (o == O_FARM && VAL(w->tiles[idx]) >= 100) {
            w->tiles[idx] = MKT(te, O_FARM, 1); /* 收完接着种 */
            r->carry = C_FOOD;
            r->carry_n = 5;
            tire(r, 15);
        }
        break;
    case G_SHRINE:
        if (empty_grass(w, idx) && !shrine_ok(w)) {
            w->tiles[idx] = MKTILE(te, O_SHRINE);
            w->shrine = (uint16_t)idx;
            sim_logf("%s垒起一块祭石，感谢神的眷顾", sim_name(r));
            sim_sfx(SFX_CHIME);
        }
        break;
    case G_PRAY:
        add_mood(r, 40);
        if (rn(w, 100) < 12) sim_devotion(w, 1, 0); /* 信仰靠祈祷代代相传 */
        break;
    case G_MINE:
        r->carry = C_STONE;
        r->carry_n = 2;
        tire(r, 40);
        break;
    default: break;
    }
    set_idle(r, 0);
}

static void move(world_t *w, int i)
{
    resident_t *r = &w->res[i];
    if (r->step) {
        int here = IDX(r->tx, r->ty);
        bool slow = TERR(w->tiles[here]) == T_RIVER || sim_flooded(w, here);
        r->step += slow ? 1 : 2;
        if (r->step < TILE) return;
        r->tx = r->nx;
        r->ty = r->ny;
        r->step = 0;
    }
    if (r->goal == G_HUNT) {
        animal_t *a = &w->ani[r->tgt % MAX_ANI];
        if (!a->alive || (a->held && a->held != i + 1) || r->timer == 0) { set_idle(r, 0); return; }
        r->timer--;
        if (cheb(r->tx, r->ty, a->tx, a->ty) <= 1) {
            a->held = (uint8_t)(i + 1);
            r->face = a->tx < r->tx;
            start_act(r, 30);
            return;
        }
        r->gx = a->tx;
        r->gy = a->ty;
    } else if (r->tx == r->gx && r->ty == r->gy) {
        arrive(w, i);
        return;
    }
    int dir = -1;
    s_self = i;
    s_goal_idx = IDX(r->gx, r->gy);
    if (bfs(w, r->tx, r->ty, pred_goal, &dir) < 0 || dir < 0) { set_idle(r, 10); return; }
    r->nx = (uint8_t)(r->tx + DX[dir]);
    r->ny = (uint8_t)(r->ty + DY[dir]);
    if (DX[dir]) r->face = DX[dir] < 0;
    r->step = 1;
}

/* 每 10 tick 结算一次需求和环境伤害 */
static void needs(world_t *w, int i)
{
    resident_t *r = &w->res[i];
    int idx = IDX(r->tx, r->ty);
    bool asleep = r->state == S_SLEEP;
    bool night = sim_is_night(w);
    bool indoors = sim_in_house(w, r);
    bool baby = sim_age(r) < AGE_CHILD;
    bool by_fire = w->fire_lit && cheb(r->tx, r->ty, w->camp_x, w->camp_y) <= 3;

    int v = r->food - ((asleep || baby) ? 6 : 10) - ((w->weather == W_SNOW && !indoors) ? 2 : 0);
    r->food = (uint16_t)(v < 0 ? 0 : v);
    if (asleep || baby) {
        v = r->energy + (indoors ? 30 : w->fire_lit ? 25 : 16);
        r->energy = (uint16_t)(v > 1000 ? 1000 : v);
    } else {
        tire(r, r->sick ? 12 : 9);
    }
    if (r->hurt_cd) r->hurt_cd--;

    /* 环境伤害 */
    if (TERR(w->tiles[idx]) == T_LAVA) { sim_die(w, i, D_LAVA); return; }
    if (OBJ(w->tiles[idx]) == O_FLAME || (w->fires && near4(w, idx, -1, O_FLAME))) {
        if (!sim_hurt(w, i, 60, D_FIRE)) return;
        if (!r->hurt_cd) {
            sim_logf("%s在火灾中受伤了", sim_name(r));
            r->hurt_cd = 40;
        }
    }
    if (w->flood >= 2 && sim_flooded(w, idx) && !sim_hurt(w, i, 25, D_FLOOD)) return;
    if (w->weather == W_SNOW && !indoors && !by_fire && !sim_hurt(w, i, 4, D_COLD)) return;
    if (r->sick && !sim_hurt(w, i, (sim_age(r) >= AGE_ELDER || sim_age(r) < AGE_WORK) ? 6 : 4, D_PLAGUE)) return;
    if (r->food == 0) {
        if (!sim_hurt(w, i, 9, D_STARVE)) return;
    } else if (r->food > 450 && r->hp < 1000 && !r->sick) {
        v = r->hp + (asleep ? 5 : 3);
        r->hp = (uint16_t)(v > 1000 ? 1000 : v);
    }

    int target = 500 + (r->food - 500) / 4 + (r->energy - 500) / 5;
    if (night) target += (indoors || w->fire_lit) ? 50 : -100;
    if (r->hp < 500) target -= 150;
    if (r->sick) target -= 150;
    target += w->faith / 5 - w->fear / 3;
    if (r->partner != NO_SLOT) target += 40;
    if (home_ok(w, r)) target += 40;
    if (!indoors && (w->weather == W_RAIN || w->weather == W_STORM)) target -= 40;
    if (!indoors && w->weather == W_SNOW) target -= 100;
    if (target < 0) target = 0;
    if (target > 1000) target = 1000;
    if (r->mood < target) r->mood += 4;
    else if (r->mood > target) r->mood = (uint16_t)(r->mood > 4 ? r->mood - 4 : 0);
}

static bool in_danger(const resident_t *r)
{
    int d = sim_danger[IDX(r->tx, r->ty)];
    return d == 1 || d == 2 || (d == 3 && r->trait == TR_TIMID); /* 胆小的人离得老远就跑 */
}

void sim_res_tick(world_t *w, int i)
{
    resident_t *r = &w->res[i];
    if ((w->tick + (uint32_t)i * 3) % 10 == 0) needs(w, i);
    if (!r->alive) return;

    if (sim_age(r) < AGE_CHILD) { /* 婴儿：待在家里，白天醒着晚上睡 */
        if (home_ok(w, r)) {
            r->tx = r->nx = (uint8_t)(r->home % MAP_W);
            r->ty = r->ny = (uint8_t)(r->home / MAP_W);
        }
        r->state = sim_is_night(w) ? S_SLEEP : S_IDLE;
        r->goal = G_NONE;
        r->step = 0;
        return;
    }

    /* 灾害当前：放下手里的事先逃命（走到格子中间才转向） */
    if (s_hazard && r->step == 0 && r->goal != G_FLEE && (w->tick + (uint32_t)i) % 5 == 0 && in_danger(r)) {
        drop_task(w, i);
        s_self = i;
        int idx = bfs(w, r->tx, r->ty, pred_safe, NULL);
        if (idx >= 0) go(r, G_FLEE, idx);
        else set_idle(r, 10);
        return;
    }

    switch (r->state) {
    case S_IDLE:
        if (r->timer) r->timer--;
        else decide(w, i);
        break;
    case S_MOVE: move(w, i); break;
    case S_ACT:
        if (r->timer) r->timer--;
        else finish(w, i);
        break;
    case S_SLEEP:
        if ((w->tick + (uint32_t)i * 7) % 25) break;
        if (sim_is_night(w) ? r->food < 120 : r->energy >= 600 || r->food < 250) set_idle(r, 0);
        break;
    }
}
