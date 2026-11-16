#include "game.h"

#include <string.h>

#define SAVE_MAGIC 0x314C5349u /* "ISL1" */
#define SAVE_VERSION 3

typedef struct {
    uint32_t magic;
    uint16_t version;
    uint16_t size;
    uint32_t wall_s; /* 存档时刻，用来算离开了多久 */
    world_t world;
} save_t;

static save_t s_save; /* 世界本体就放在存档结构里，存档时不用再拷贝 */
static ui_t s_ui;
static uint32_t s_acc_ms, s_save_ms, s_idle_ms;
static uint32_t s_sum_top, s_sum_bot;
static uint32_t s_catch_left;
static struct { uint16_t day, births, deaths, marriages, built; } s_before;

world_t *game_world(void) { return &s_save.world; }
const ui_t *game_ui(void) { return &s_ui; }
bool game_screen_off(void) { return s_ui.screen_off; }
bool game_voice_on(void) { return s_save.world.voice_on && !s_ui.catching; }

bool game_save(void)
{
    s_save.magic = SAVE_MAGIC;
    s_save.version = SAVE_VERSION;
    s_save.size = (uint16_t)sizeof(world_t);
    s_save.wall_s = plat_wall_s();
    s_save_ms = 0;
    bool ok = plat_save("world", &s_save, sizeof s_save);
    if (ok && g_tree_dirty && plat_save("tree", g_tree, sizeof g_tree)) g_tree_dirty = false; /* 家谱没变就不重写 */
    return ok;
}

static void start_catchup(uint32_t away_s)
{
    const world_t *w = &s_save.world;
    uint32_t sim_s = away_s > AWAY_MAX_S ? AWAY_MAX_S : away_s;
    s_catch_left = sim_s * 10; /* 离线按 1 倍速推进：1 秒 = 10 tick */
    s_ui.catching = 1;
    s_ui.catch_total = s_catch_left;
    s_ui.catch_left = s_catch_left;
    s_ui.chron.away_s = away_s;
    s_before.day = (uint16_t)sim_day(w);
    s_before.births = w->births;
    s_before.deaths = w->deaths;
    s_before.marriages = w->marriages;
    s_before.built = w->built;
}

static void finish_catchup(void)
{
    const world_t *w = &s_save.world;
    chron_t *c = &s_ui.chron;
    c->from_day = s_before.day;
    c->days = (uint16_t)(sim_day(w) - s_before.day);
    c->births = (uint16_t)(w->births - s_before.births);
    c->deaths = (uint16_t)(w->deaths - s_before.deaths);
    c->marriages = (uint16_t)(w->marriages - s_before.marriages);
    c->built = (uint16_t)(w->built - s_before.built);
    s_ui.catching = 0;
    s_ui.catch_left = 0;
    s_catch_left = 0;
    s_ui.inspect = 3; /* 岛屿纪事：你不在的时候发生了什么 */
    while (sim_sfx_pop() != SFX_NONE) {}
    game_save();
}

void game_init(uint32_t seed)
{
    world_t *w = &s_save.world;
    render_init();
    memset(&s_ui, 0, sizeof s_ui);
    s_catch_left = 0;
    bool ok = plat_load("world", &s_save, sizeof s_save) && s_save.magic == SAVE_MAGIC &&
              s_save.version == SAVE_VERSION && s_save.size == sizeof(world_t);
    if (ok) {
        sim_refresh(w); /* 重建不进存档的派生数据 */
        if (!plat_load("tree", g_tree, sizeof g_tree)) { /* 家谱丢了：至少把在世的人登记回去 */
            memset(g_tree, 0, sizeof g_tree);
            for (int i = 0; i < MAX_RES; i++)
                if (w->res[i].alive) sim_tree_put(w, &w->res[i]);
        } else {
            g_tree_dirty = false;
        }
        uint32_t away = plat_away_s(s_save.wall_s);
        if (away >= 60 && !w->paused) start_catchup(away);
    } else {
        sim_new(w, seed);
        game_save();
    }
    s_acc_ms = s_save_ms = s_idle_ms = 0;
    s_sum_top = s_sum_bot = 0;
}

static int next_alive(int from, int dir)
{
    const world_t *w = &s_save.world;
    for (int k = 1; k <= MAX_RES; k++) {
        int i = ((from + dir * k) % MAX_RES + MAX_RES) % MAX_RES;
        if (w->res[i].alive) return i;
    }
    return -1;
}

static bool wake(void)
{
    bool was_off = s_ui.screen_off;
    s_idle_ms = 0;
    s_ui.screen_off = 0;
    if (was_off) s_sum_top = s_sum_bot = 0; /* 亮屏后整屏重画 */
    return was_off;
}

static void open_tree(void)
{
    const world_t *w = &s_save.world;
    uint16_t uid = 0;
    if (w->res[s_ui.who].alive) {
        uid = w->res[s_ui.who].uid;
    } else {
        int n = next_alive(s_ui.who, 1);
        if (n >= 0) uid = w->res[n].uid;
    }
    if (!sim_person(uid)) { /* 岛上没人了：从最晚出生的那位看起 */
        uid = 0;
        for (int i = 0; i < TREE_N; i++)
            if (g_tree[i].uid && (!uid || g_tree[i].born >= sim_person(uid)->born)) uid = g_tree[i].uid;
    }
    if (!uid) return;
    s_ui.tree_uid = uid;
    s_ui.tree_cur = 0;
    s_ui.inspect = 4;
}

static void key_tree(int key)
{
    uint16_t rel[TREE_ROWS];
    uint8_t role[TREE_ROWS];
    int n = sim_relatives(&s_save.world, s_ui.tree_uid, rel, role, TREE_ROWS);
    switch (key) {
    case KEY_UP: case KEY_UP_LONG:
        if (n) s_ui.tree_cur = (uint8_t)((s_ui.tree_cur + n - 1) % n);
        break;
    case KEY_DOWN: case KEY_DOWN_LONG:
        if (n) s_ui.tree_cur = (uint8_t)((s_ui.tree_cur + 1) % n);
        break;
    case KEY_OK:
        if (n && s_ui.tree_cur < n) {
            s_ui.tree_uid = rel[s_ui.tree_cur];
            s_ui.tree_cur = 0;
        } else {
            s_ui.inspect = 0;
        }
        break;
    default: s_ui.inspect = 0; break;
    }
}

void game_key(int key)
{
    world_t *w = &s_save.world;
    if (wake()) return; /* 息屏时第一下按键只负责点亮屏幕 */
    if (s_ui.catching) {
        if (key == KEY_OK || key == KEY_OK_LONG) s_catch_left = 0; /* 跳过剩下的补算 */
        return;
    }
    if (s_ui.inspect == 4) { key_tree(key); return; }
    if (s_ui.inspect) {
        bool up = key == KEY_UP || key == KEY_UP_LONG, down = key == KEY_DOWN || key == KEY_DOWN_LONG;
        if (s_ui.inspect == 1 && (up || down)) {
            int n = next_alive(s_ui.who, up ? -1 : 1);
            if (n >= 0) s_ui.who = (uint8_t)n;
        } else if (!up && !down) {
            s_ui.inspect = 0;
        }
        return;
    }
    switch (key) {
    case KEY_UP: s_ui.sel = (uint8_t)((s_ui.sel + P_COUNT - 1) % P_COUNT); break;
    case KEY_DOWN: s_ui.sel = (uint8_t)((s_ui.sel + 1) % P_COUNT); break;
    case KEY_DOWN_LONG: { /* 跳到下一类的第一项 */
        int c = sim_power_cat_id(s_ui.sel), p = s_ui.sel;
        while (sim_power_cat_id(p) == c) p = (p + 1) % P_COUNT;
        s_ui.sel = (uint8_t)p;
        break;
    }
    case KEY_UP_LONG: { /* 跳到上一类的第一项 */
        int p = s_ui.sel;
        while (p > 0 && sim_power_cat_id(p - 1) == sim_power_cat_id(s_ui.sel)) p--;
        p = (p + P_COUNT - 1) % P_COUNT;
        while (p > 0 && sim_power_cat_id(p - 1) == sim_power_cat_id(p)) p--;
        s_ui.sel = (uint8_t)p;
        break;
    }
    case KEY_OK:
        if (s_ui.sel == P_RECORDS) {
            s_ui.inspect = 2;
        } else if (s_ui.sel == P_TREE) {
            open_tree();
        } else if (s_ui.sel == P_INSPECT) {
            int n = w->res[s_ui.who].alive ? s_ui.who : next_alive(s_ui.who, 1);
            if (n >= 0) {
                s_ui.who = (uint8_t)n;
                s_ui.inspect = 1;
            }
        } else {
            sim_cast(w, s_ui.sel);
        }
        break;
    case KEY_OK_LONG:
        w->paused = !w->paused;
        if (w->paused) game_save(); /* 暂停是个存档的好时机 */
        break;
    default: break;
    }
}

void game_mic(int ev)
{
    world_t *w = &s_save.world;
    if (!w->voice_on || s_ui.catching || ev == MIC_NONE) return;
    wake();
    sim_cast(w, ev == MIC_CLAP ? P_BOLT : P_W_WIND);
}

int game_sfx_pop(void)
{
    int id = sim_sfx_pop();
    if (s_ui.catching || !s_save.world.sound_on) return SFX_NONE;
    return id;
}

static uint32_t checksum(const uint16_t *p, int n)
{
    uint32_t h = 2166136261u;
    for (int i = 0; i < n; i++) h = (h ^ p[i]) * 16777619u;
    return h;
}

int game_frame(uint32_t dt_ms)
{
    world_t *w = &s_save.world;
    int flags = 0;
    if (dt_ms > 1000) dt_ms = 1000;

    if (s_ui.catching) {
        /* 离线推进：每帧拿出约 60 ms 全速补算，画面照常刷新，可以看着岁月流过 */
        uint32_t t0 = plat_ms();
        while (s_catch_left && plat_ms() - t0 < 60) {
            uint32_t n = s_catch_left < 50 ? s_catch_left : 50;
            for (uint32_t k = 0; k < n; k++) sim_tick(w);
            s_catch_left -= n;
        }
        s_ui.catch_left = s_catch_left;
        while (sim_sfx_pop() != SFX_NONE) {}
        if (!s_catch_left) finish_catchup();
    } else {
        if (!w->paused) {
            s_acc_ms += dt_ms * (uint32_t)sim_speed(w);
            int budget = 48; /* 单帧最多补这么多 tick，防止卡顿后雪崩 */
            while (s_acc_ms >= 100 && budget--) {
                s_acc_ms -= 100;
                sim_tick(w);
            }
            if (s_acc_ms >= 100) s_acc_ms = 0;
        }
        s_idle_ms += dt_ms;
        if (s_idle_ms >= SCREEN_OFF_MS) s_ui.screen_off = 1;
        if (s_idle_ms >= SLEEP_MS) flags |= GAME_SLEEP;
        s_save_ms += dt_ms;
        if (s_save_ms >= AUTOSAVE_MS) game_save();
    }
    sim_fx_step();
    s_ui.frame++;
    if (s_ui.inspect == 1 && !w->res[s_ui.who].alive) { /* 被观察的人死了：换下一个 */
        int n = next_alive(s_ui.who, 1);
        if (n >= 0) s_ui.who = (uint8_t)n;
        else s_ui.inspect = 0;
    }
    if (s_ui.screen_off) return flags; /* 息屏：只跑模拟，不画 */

    render_map(w, &s_ui);
    render_top(w, &s_ui);
    render_bot(w, &s_ui);
    flags |= DIRTY_MAP;
    uint32_t a = checksum(fb_top, SCR_W * TOP_H), b = checksum(fb_bot, SCR_W * BOT_H);
    if (a != s_sum_top) flags |= DIRTY_TOP;
    if (b != s_sum_bot) flags |= DIRTY_BOT;
    s_sum_top = a;
    s_sum_bot = b;
    return flags;
}
