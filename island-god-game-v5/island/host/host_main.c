/*
 * 主机端测试台：不需要设备，直接用 gcc 编译运行。
 *   host_test soak [seed] [days]   无人干预长跑，逐日打印统计并检查不变量
 *   host_test shots <outdir>       用真实渲染器导出屏幕截图（PPM）
 *   host_test save                 存档 / 读档往返测试
 *   host_test away                 离线推进与归来纪事
 *   host_test tree                 家谱一致性
 *   host_test audio                音效合成与麦克风识别
 *   host_test powers               新互动：重启文明、重造新岛、赐福降罚、祈求、恶狼
 *   host_test fuzz                 随机按键 + 随机神力压力测试
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "game.h"
#include "sfx.h"
#include "sim_int.h"

/* ---------- 平台层的主机实现 ---------- */

static bool s_save_enabled = false;
static uint32_t s_fake_ms, s_wall = 1000, s_away;

static void save_path(const char *key, char *out, size_t n) { snprintf(out, n, "/tmp/island_host_%d_%s.bin", (int)getpid(), key); }

bool plat_load(const char *key, void *buf, uint32_t len)
{
    char path[80];
    if (!s_save_enabled) return false;
    save_path(key, path, sizeof path);
    FILE *f = fopen(path, "rb");
    if (!f) return false;
    size_t n = fread(buf, 1, len, f);
    fclose(f);
    return n == len;
}
bool plat_save(const char *key, const void *buf, uint32_t len)
{
    char path[80];
    if (!s_save_enabled) return true;
    save_path(key, path, sizeof path);
    FILE *f = fopen(path, "wb");
    if (!f) return false;
    size_t n = fwrite(buf, 1, len, f);
    fclose(f);
    return n == len;
}
uint32_t plat_ms(void) { return s_fake_ms += 2; } /* 每问一次过 2 ms：补算循环每帧约跑 1500 tick */
uint32_t plat_wall_s(void) { return s_wall; }
uint32_t plat_away_s(uint32_t saved)
{
    (void)saved;
    uint32_t a = s_away;
    s_away = 0;
    return a;
}
static void remove_saves(void)
{
    char path[80];
    save_path("world", path, sizeof path);
    remove(path);
    save_path("tree", path, sizeof path);
    remove(path);
}

/* ---------- 不变量 ---------- */

#define FAIL(...) do { printf("INVARIANT FAIL: " __VA_ARGS__); printf(" (tick %u)\n", (unsigned)w->tick); exit(1); } while (0)

static void check(const world_t *w)
{
    for (int i = 0; i < MAX_RES; i++) {
        const resident_t *r = &w->res[i];
        if (!r->alive) continue;
        if (r->tx >= MAP_W || r->ty >= MAP_H || r->nx >= MAP_W || r->ny >= MAP_H || r->hp > 1000 || r->food > 1000 ||
            r->energy > 1000 || r->mood > 1000 || r->step > TILE)
            FAIL("resident %d out of range", i);
        if (r->partner != NO_SLOT && (r->partner >= MAX_RES || (w->res[r->partner].alive && w->res[r->partner].partner != i)))
            FAIL("partner link of resident %d", i);
        if (r->home != NO_ID && r->home >= MAP_N) FAIL("home of resident %d", i);
        const person_t *p = sim_person(r->uid);
        if (!p || p->age != 0xFF) FAIL("resident %d missing from family tree", i);
    }
    for (int k = 0; k < MAX_ANI; k++) {
        const animal_t *a = &w->ani[k];
        if (a->alive && (a->tx >= MAP_W || a->ty >= MAP_H || a->nx >= MAP_W || a->ny >= MAP_H || a->kind >= A_KINDS))
            FAIL("animal %d", k);
    }
    if (w->faith > 100 || w->fear > 100) FAIL("faith/fear");
    if (w->shrine != NO_ID && w->shrine >= MAP_N) FAIL("shrine");
}

static void dump_ppm(const char *dir, const char *name)
{
    static uint16_t full[SCR_W * 320];
    char path[256];
    snprintf(path, sizeof path, "%s/%s.ppm", dir, name);
    memcpy(full, fb_top, sizeof fb_top);
    memcpy(full + SCR_W * TOP_H, fb_map, sizeof fb_map);
    memcpy(full + SCR_W * (TOP_H + MAPV_H), fb_bot, sizeof fb_bot);
    FILE *f = fopen(path, "wb");
    if (!f) { perror(path); exit(1); }
    fprintf(f, "P6\n%d %d\n255\n", SCR_W, 320);
    for (int i = 0; i < SCR_W * 320; i++) {
        uint16_t c = full[i];
        unsigned char rgb[3] = {(unsigned char)((c >> 11) * 255 / 31), (unsigned char)(((c >> 5) & 63) * 255 / 63),
                                (unsigned char)((c & 31) * 255 / 31)};
        fwrite(rgb, 1, 3, f);
    }
    fclose(f);
}

static void run_frames(int n)
{
    for (int i = 0; i < n; i++) {
        game_frame(FRAME_MS);
        check(game_world());
    }
}

/* 按键；屏幕灭着就先点亮（第一下按键只负责亮屏） */
static void key(int k)
{
    if (game_screen_off()) game_key(KEY_UP);
    game_key(k);
}
static void select_power(int p)
{
    if (game_ui()->inspect) key(game_ui()->inspect == 4 ? KEY_OK_LONG : KEY_OK);
    while (game_ui()->sel != p) key(KEY_DOWN);
}

static int soak(uint32_t seed, int days, bool verbose)
{
    world_t *w = game_world();
    sim_new(w, seed);
    uint32_t seen = 0;
    for (int d = 1; d <= days; d++) {
        for (int t = 0; t < TICKS_PER_DAY; t++) {
            sim_tick(w);
            check(w);
        }
        if (verbose) {
            for (; seen < g_log.seq; seen++)
                if (g_log.seq - seen <= LOG_N) printf("      · %s\n", g_log.msg[seen % LOG_N]);
            if (d <= 10 || d % 10 == 0)
                printf("day %3d pop %2d (+%d -%d gen %d) house %d farm %2d | food %3d wood %3d | boar %d rabbit %2d wolf %d | "
                       "faith %2d fear %2d | %s\n",
                       d, sim_pop(w), w->births, w->deaths, w->max_gen, sim_houses, sim_farms, w->food, w->wood,
                       sim_kind_count(w, A_BOAR), sim_kind_count(w, A_RABBIT), sim_kind_count(w, A_WOLF), w->faith, w->fear,
                       sim_weather_name(w->weather));
        }
    }
    return sim_pop(w);
}

/* ---------- 各项测试 ---------- */

static int test_soak(int argc, char **argv)
{
    if (argc > 2) {
        soak((uint32_t)strtoul(argv[2], NULL, 0), argc > 3 ? atoi(argv[3]) : 60, true);
        return 0;
    }
    int survived = 0, total = 0, n = 30, days = 400, wolf_deaths = 0, shrines = 0;
    for (uint32_t s = 1; s <= (uint32_t)n; s++) {
        int p = soak(s * 7919u, days, false);
        world_t *w = game_world();
        int wd = 0;
        for (int i = 0; i < TREE_N; i++) wd += g_tree[i].uid && g_tree[i].age != 0xFF && g_tree[i].cause == D_WOLF;
        printf("seed %6u: pop %2d (+%d -%d gen %d) house %d farm %2d food %3d boar %d rabbit %2d wolf %d wolf-deaths %d faith %d\n",
               (unsigned)(s * 7919u), p, w->births, w->deaths, w->max_gen, sim_houses, sim_farms, w->food,
               sim_kind_count(w, A_BOAR), sim_kind_count(w, A_RABBIT), sim_kind_count(w, A_WOLF), wd, w->faith);
        survived += p > 0;
        total += p;
        wolf_deaths += wd;
        shrines += w->shrine != NO_ID;
    }
    printf("islands with survivors after %d days: %d/%d, mean pop %.1f, wolf deaths %d, shrines %d\n", days, survived, n,
           (double)total / n, wolf_deaths, shrines);
    return 0;
}

static int test_shots(int argc, char **argv)
{
    const char *dir = argc > 2 ? argv[2] : ".";
    uint32_t seed = argc > 3 ? (uint32_t)strtoul(argv[3], NULL, 0) : 2026;
    world_t *w = game_world();
    s_save_enabled = true;
    remove_saves();
    game_init(seed);
    run_frames(60);
    dump_ppm(dir, "01_day1");
    select_power(P_FOREST), key(KEY_OK);
    run_frames(4);
    dump_ppm(dir, "02_cast_forest");
    select_power(P_INSPECT), key(KEY_OK);
    run_frames(30);
    dump_ppm(dir, "03_inspect_zoom");
    key(KEY_UP_LONG);
    run_frames(3);
    dump_ppm(dir, "04_bless_one");
    /* 快进到有了村庄的时候 */
    select_power(P_FOOD), key(KEY_OK), key(KEY_OK);
    select_power(P_SPEED), key(KEY_OK), key(KEY_OK), key(KEY_OK);
    while (sim_day(w) < 90 || sim_hour(w) != 10) run_frames(1);
    select_power(P_SPEED), key(KEY_OK);
    run_frames(20);
    dump_ppm(dir, "05_village");
    w->food = 0; /* 断粮，等岛民开口祈求 */
    while (w->wish != WISH_FOOD && sim_day(w) < 99) run_frames(1), w->food = 0;
    run_frames(3);
    dump_ppm(dir, "06_wish");
    select_power(P_FOOD), key(KEY_OK);
    run_frames(5);
    dump_ppm(dir, "07_wish_answered");
    run_frames(200);
    select_power(P_WOLF), key(KEY_OK);
    run_frames(5);
    dump_ppm(dir, "08_fierce_wolves");
    run_frames(700);
    dump_ppm(dir, "09_wolves_hunting");
    select_power(P_SLAY), key(KEY_OK);
    run_frames(4);
    dump_ppm(dir, "10_slay_wolves");
    run_frames(300);
    select_power(P_VOLCANO), key(KEY_OK);
    run_frames(4);
    dump_ppm(dir, "11_volcano_cast");
    run_frames(500);
    dump_ppm(dir, "12_volcano_later");
    select_power(P_FLOOD), key(KEY_OK);
    run_frames(60);
    dump_ppm(dir, "13_flood");
    /* 赶尽杀绝，再一键重来 */
    for (int n = 0; n < 60 && sim_pop(w); n++) {
        select_power(n % 2 ? P_BOLT : P_QUAKE), key(KEY_OK);
        run_frames(30);
    }
    while (sim_pop(w)) select_power(P_BOLT), key(KEY_OK), run_frames(20);
    run_frames(40); /* 最后一人死去后，菜单会自己跳到"降下先民" */
    dump_ppm(dir, "14_extinct");
    key(KEY_OK);
    run_frames(6);
    dump_ppm(dir, "15_genesis");
    printf("day %d pop %d\n", sim_day(w), sim_pop(w));
    remove_saves();
    return 0;
}

/* 新加的互动：重启文明、重造新岛、个人赐福与降罚、祈求、恶狼与灭狼、时间流速 */
static int test_powers(void)
{
    world_t *w = game_world();
    int fails = 0;
#define EXPECT(cond, msg) do { if (!(cond)) { printf("FAIL: %s\n", msg); fails++; } } while (0)
    game_init(31337);

    /* 1 倍速：现实 2 分钟 = 1 天 */
    int d0 = sim_day(w), h0 = sim_hour(w);
    run_frames(1200);
    printf("120 s at 1x: day %d %02d:00 -> day %d %02d:00\n", d0, h0, sim_day(w), sim_hour(w));
    EXPECT(sim_day(w) == d0 + 1 && sim_hour(w) == h0, "1x speed should be 2 minutes per day");

    /* 观察某人：长按上赐福，长按下降罚 */
    select_power(P_INSPECT), key(KEY_OK);
    int who = game_ui()->who;
    w->res[who].hp = 300, w->res[who].food = 100;
    key(KEY_UP_LONG);
    EXPECT(w->res[who].hp == 1000 && w->res[who].food == 1000, "bless should restore the resident");
    EXPECT(game_ui()->banner == 200, "bless banner");
    key(KEY_DOWN_LONG);
    EXPECT(!w->res[who].alive, "smite should kill the resident it lands on");
    printf("bless and smite: OK, pop now %d\n", sim_pop(w));
    key(KEY_OK);

    /* 祈求：断粮 → 祈求食物 → 赐食物 → 信仰上涨 */
    for (int i = 0; i < 20000 && w->wish != WISH_FOOD; i++) {
        w->food = 0;
        run_frames(1);
    }
    EXPECT(w->wish == WISH_FOOD, "starving islanders should pray for food");
    int f0 = w->faith;
    select_power(P_FOOD), key(KEY_OK);
    printf("wish for food answered: faith %d -> %d\n", f0, w->faith);
    EXPECT(w->wish == WISH_NONE && w->faith >= f0 + 18, "answering the wish should raise faith");

    /* 恶狼与灭狼 */
    select_power(P_WOLF), key(KEY_OK);
    int fierce = 0;
    for (int k = 0; k < MAX_ANI; k++) fierce += w->ani[k].alive && w->ani[k].kind == A_WOLF && w->ani[k].fierce;
    EXPECT(fierce == 5, "five fierce wolves");
    run_frames(50);
    select_power(P_SLAY), key(KEY_OK);
    EXPECT(sim_kind_count(w, A_WOLF) == 0, "slay removes every wolf");
    printf("fierce wolves released: %d, after slaying: %d\n", fierce, sim_kind_count(w, A_WOLF));

    /* 灭绝 → 菜单自动跳到"降下先民" → 一键重来 */
    for (int i = 0; i < MAX_RES; i++)
        if (w->res[i].alive) sim_die(w, i, D_BOLT);
    select_power(P_FOREST);
    run_frames(2);
    EXPECT(game_ui()->sel == P_GENESIS, "menu should jump to genesis when the last islander dies");
    key(KEY_OK);
    printf("after extinction, one press: pop %d\n", sim_pop(w));
    EXPECT(sim_pop(w) == 5, "genesis brings five founders");
    int sexes = 0;
    for (int i = 0; i < MAX_RES; i++) sexes |= w->res[i].alive ? 1 << w->res[i].sex : 0;
    EXPECT(sexes == 3, "founders include both sexes");

    /* 重造新岛：要按两次；中途按别的键就取消 */
    run_frames(600);
    uint16_t tile0 = w->tiles[IDX(15, 13)];
    uint32_t tick0 = w->tick;
    (void)tile0;
    select_power(P_NEWWORLD), key(KEY_OK);
    EXPECT(w->tick == tick0 && game_ui()->confirm_t, "first press only asks for confirmation");
    key(KEY_UP);
    EXPECT(!game_ui()->confirm_t, "another key cancels");
    select_power(P_NEWWORLD), key(KEY_OK);
    run_frames(50); /* 5 秒后确认过期 */
    EXPECT(!game_ui()->confirm_t && w->tick > tick0, "confirmation expires");
    key(KEY_OK), key(KEY_OK);
    printf("new island: day %d pop %d\n", sim_day(w), sim_pop(w));
    EXPECT(sim_day(w) == 1 && sim_pop(w) == 5 && w->births == 0, "second press rebuilds the island");
#undef EXPECT
    printf("power tests: %s\n", fails ? "FAILED" : "OK");
    return fails;
}

static int test_save(void)
{
    static world_t before, a;
    static person_t tree_before[TREE_N];
    s_save_enabled = true;
    remove_saves();
    game_init(4242);
    select_power(P_FOREST), key(KEY_OK);
    run_frames(3000);
    if (!game_save()) { printf("save failed\n"); return 1; }
    memcpy(&before, game_world(), sizeof before);
    memcpy(tree_before, g_tree, sizeof g_tree);
    memset(game_world(), 0xAA, sizeof(world_t));
    memset(g_tree, 0x55, sizeof g_tree);
    game_init(999); /* 应当读回存档而不是生成新岛 */
    bool same = !memcmp(&before, game_world(), sizeof before) && !memcmp(tree_before, g_tree, sizeof g_tree);
    if (!same) {
        const uint8_t *x = (const uint8_t *)&before, *y = (const uint8_t *)game_world();
        for (size_t i = 0; i < sizeof before; i++)
            if (x[i] != y[i]) { printf("world differs at byte %u (%02x vs %02x)\n", (unsigned)i, x[i], y[i]); break; }
        x = (const uint8_t *)tree_before, y = (const uint8_t *)g_tree;
        for (size_t i = 0; i < sizeof g_tree; i++)
            if (x[i] != y[i]) { printf("tree differs at byte %u (%02x vs %02x)\n", (unsigned)i, x[i], y[i]); break; }
    }
    printf("save blobs: world %u bytes + family tree %u bytes, roundtrip %s\n", (unsigned)(sizeof(world_t) + 12),
           (unsigned)sizeof g_tree, same ? "OK" : "MISMATCH");
    /* 读档后继续跑，结果要和不存档直接跑完全一致 */
    for (int i = 0; i < 2000; i++) sim_tick(game_world());
    memcpy(&a, game_world(), sizeof a);
    for (int i = 0; i < 2000; i++) sim_tick(&before);
    bool det = !memcmp(&a, &before, sizeof a);
    printf("resume determinism %s\n", det ? "OK" : "MISMATCH");
    remove_saves();
    return same && det ? 0 : 1;
}

static int test_away(void)
{
    static world_t direct;
    world_t *w = game_world();
    int fails = 0;
    s_save_enabled = true;
    remove_saves();
    game_init(77);
    run_frames(600);
    game_save();
    memcpy(&direct, w, sizeof direct);

    /* 离开 9 小时：应当补算 540 天，结果与一直开着机逐 tick 跑出来的完全一样 */
    s_away = 9 * 3600;
    game_init(1);
    if (!game_ui()->catching) { printf("FAIL: catch-up did not start\n"); return 1; }
    int frames = 0;
    while (game_ui()->catching) {
        run_frames(1);
        frames++;
    }
    const chron_t *c = &game_ui()->chron;
    printf("away 9h: %d frames, +%d days, births %d deaths %d marriages %d houses %d, pop %d gen %d\n", frames, c->days,
           c->births, c->deaths, c->marriages, c->built, sim_pop(w), w->max_gen);
    if (c->days != 540 || game_ui()->inspect != 3) { printf("FAIL: expected 540 days and the chronicle page\n"); fails++; }
    for (int i = 0; i < 9 * 3600 * 10; i++) sim_tick(&direct);
    bool same = !memcmp(&direct, w, sizeof direct);
    printf("catch-up equals live simulation: %s\n", same ? "OK" : "MISMATCH");
    fails += !same;
    key(KEY_OK);
    if (game_ui()->inspect != 0) { printf("FAIL: chronicle did not close\n"); fails++; }

    /* 离开 100 小时：封顶 1440 天 */
    game_save();
    s_away = 100 * 3600;
    game_init(1);
    int day0 = sim_day(w);
    while (game_ui()->catching) run_frames(1);
    printf("away 100h: +%d days (cap 1440), pop %d gen %d\n", sim_day(w) - day0, sim_pop(w), w->max_gen);
    if (sim_day(w) - day0 != 1440) fails++;
    key(KEY_OK);

    /* 中途按确认跳过 */
    game_save();
    s_away = 9 * 3600;
    game_init(1);
    day0 = sim_day(w);
    run_frames(10);
    key(KEY_OK);
    run_frames(2);
    printf("skip: stopped after +%d days, catching=%d inspect=%d\n", sim_day(w) - day0, game_ui()->catching, game_ui()->inspect);
    if (game_ui()->catching || game_ui()->inspect != 3) fails++;
    key(KEY_OK);

    /* 暂停着离开：不推进。不到一分钟：不推进 */
    key(KEY_OK_LONG);
    s_away = 9 * 3600;
    game_init(1);
    if (game_ui()->catching) { printf("FAIL: paused island advanced\n"); fails++; }
    key(KEY_OK_LONG);
    game_save();
    s_away = 30;
    game_init(1);
    if (game_ui()->catching) { printf("FAIL: 30 s away triggered catch-up\n"); fails++; }

    /* 息屏与休眠请求 */
    int flags = 0, off_at = -1, sleep_at = -1;
    for (int i = 0; i < 7000 && sleep_at < 0; i++) {
        flags = game_frame(FRAME_MS);
        if (off_at < 0 && game_screen_off()) off_at = i;
        if (flags & GAME_SLEEP) sleep_at = i;
    }
    printf("idle: screen off after %d s, sleep requested after %d s\n", off_at / 10, sleep_at / 10);
    if (off_at != SCREEN_OFF_MS / FRAME_MS - 1 || sleep_at != SLEEP_MS / FRAME_MS - 1) fails++;
    int sel = game_ui()->sel;
    game_key(KEY_DOWN); /* 第一下只亮屏 */
    if (game_screen_off() || game_ui()->sel != sel) { printf("FAIL: wake key was not swallowed\n"); fails++; }
    game_key(KEY_DOWN);
    if (game_ui()->sel == sel) fails++;
    remove_saves();
    printf("away tests: %s\n", fails ? "FAILED" : "OK");
    return fails;
}

static int test_tree(void)
{
    world_t *w = game_world();
    int fails = 0;
    for (uint32_t s = 1; s <= 10; s++) {
        soak(s * 7919u, 400, false);
        int n = sim_tree_count(), orphans = 0, living = 0, asym = 0;
        for (int i = 0; i < TREE_N; i++) {
            const person_t *p = &g_tree[i];
            if (!p->uid) continue;
            living += p->age == 0xFF;
            if ((p->father != NO_ID && !sim_person(p->father)) || (p->mother != NO_ID && !sim_person(p->mother))) orphans++;
            if (p->gen > 1 && p->mother == NO_ID) orphans++;
            /* 我是父母的孩子 ⇔ 父母的亲属列表里有我 */
            uint16_t rel[64];
            uint8_t role[64];
            for (int side = 0; side < 2; side++) {
                uint16_t par = side ? p->mother : p->father;
                if (!sim_person(par)) continue;
                int m = sim_relatives(w, par, rel, role, 64), found = 0;
                for (int k = 0; k < m; k++) found |= rel[k] == p->uid && role[k] == 3;
                asym += !found;
            }
        }
        /* 初代五人的后代加起来应当覆盖所有非初代的人 */
        int gen1 = 0, later = 0, maxd = 0;
        for (int i = 0; i < TREE_N; i++)
            if (g_tree[i].uid) {
                if (g_tree[i].gen == 1) {
                    int l, d = sim_descendants(g_tree[i].uid, &l);
                    if (d > maxd) maxd = d;
                    gen1++;
                } else {
                    later++;
                }
            }
        printf("seed %u: %d people on record (%d living, pop %d), founders %d, largest lineage %d of %d, broken links %d, asym %d\n",
               (unsigned)s, n, living, sim_pop(w), gen1, maxd, later, orphans, asym);
        if (living != sim_pop(w) || orphans || asym || n < w->births + 5) fails++; /* 漂流者也在册，所以可以更多 */
    }
    printf("family tree: %s\n", fails ? "FAILED" : "OK");
    return fails;
}

/* 测试信号用的随机数 */
static uint32_t s_r = 12345;
static int rnd16(void)
{
    s_r = s_r * 1103515245u + 12345u;
    return (int)((s_r >> 16) & 0xFFFF) - 32768;
}

static int feed(mic_t *m, const int16_t *pcm, int n, int *claps, int *blows)
{
    for (int i = 0; i < n; i += 160) { /* 每次 10 ms，和设备上一样分块喂 */
        int e = mic_feed(m, pcm + i, n - i < 160 ? n - i : 160);
        if (e == MIC_CLAP) (*claps)++;
        if (e == MIC_BLOW) (*blows)++;
    }
    return 0;
}

static int test_audio(void)
{
    static int16_t buf[SFX_RATE * 4];
    int fails = 0;
    static const char *const NAMES[SFX_COUNT] = {"", "birth", "death", "chime", "bless", "blip", "thunder", "rumble", "alarm", "wolf"};
    for (int id = 1; id < SFX_COUNT; id++) {
        sfx_gen_t g;
        sfx_start(&g, id);
        int total = 0, peak = 0, n;
        while ((n = sfx_fill(&g, buf + total, 256)) > 0) total += n; /* 和设备上一样小块生成 */
        for (int i = 0; i < total; i++) {
            int a = buf[i] < 0 ? -buf[i] : buf[i];
            if (a > peak) peak = a;
        }
        printf("sfx %-8s %4d ms, peak %5d\n", NAMES[id], total * 1000 / SFX_RATE, peak);
        if (total < 300 || total > SFX_RATE * 2 || peak < 1500 || peak > 13000) fails++;
    }

    /* 麦克风识别：合成信号 */
    static const struct { const char *name; int want_clap, want_blow; } CASES[] = {
        {"quiet room", 0, 0}, {"one clap", 1, 0}, {"blowing 1.2 s", 0, 1}, {"speech-like 3 s", 0, 0},
        {"steady 1 kHz tone", 0, 0}, {"loud hiss 1.5 s", 0, 0}, {"two claps 1 s apart", 1, 0}, {"own thunder sfx (muted)", 0, 0},
    };
    for (unsigned c = 0; c < sizeof CASES / sizeof CASES[0]; c++) {
        mic_t m;
        mic_init(&m);
        int claps = 0, blows = 0, n = SFX_RATE * 4;
        int32_t lp = 0;
        for (int i = 0; i < n; i++) { /* 底噪 */
            buf[i] = (int16_t)(rnd16() / 300);
        }
        feed(&m, buf, n, &claps, &blows); /* 先听 4 秒安静，让噪声底稳定 */
        claps = blows = 0;
        for (int i = 0; i < n; i++) {
            int t = i - SFX_RATE / 2; /* 事件从 0.5 秒处开始 */
            int v = rnd16() / 300;
            switch (c) {
            case 1: case 6: {
                int t2 = c == 6 ? t - SFX_RATE : -1;
                if (t >= 0 && t < 500) v += rnd16() * (500 - t) / 500 * 2 / 5;
                if (t2 >= 0 && t2 < 500) v += rnd16() * (500 - t2) / 500 * 2 / 5;
                break;
            }
            case 2:
                lp += (rnd16() - lp) / 24;
                if (t >= 0 && t < SFX_RATE * 12 / 10) v += lp * 5;
                break;
            case 3: { /* 150 Hz 基频加谐波，每秒 4 个音节 */
                int syl = (t / (SFX_RATE / 4)) & 1;
                int ph = t % 107;
                if (t >= 0 && t < SFX_RATE * 3 && syl) v += (ph < 53 ? 5000 : -5000) + ((t % 27) < 13 ? 1500 : -1500);
                break;
            }
            case 4:
                if (t >= 0 && t < SFX_RATE * 2) v += (t % 16) < 8 ? 6000 : -6000;
                break;
            case 5:
                if (t >= 0 && t < SFX_RATE * 3 / 2) v += rnd16() / 4;
                break;
            default: break;
            }
            buf[i] = (int16_t)(v > 32767 ? 32767 : v < -32768 ? -32768 : v);
        }
        if (c == 7) { /* 自己放雷声：设备会先 mic_mute，再把喇叭声录进来 */
            sfx_gen_t g;
            sfx_start(&g, SFX_THUNDER);
            int len = sfx_fill(&g, buf + SFX_RATE / 2, SFX_RATE * 2);
            mic_mute(&m, len * 1000 / SFX_RATE + 300 + 500);
        }
        feed(&m, buf, n, &claps, &blows);
        bool ok = claps == CASES[c].want_clap && blows == CASES[c].want_blow;
        printf("mic %-24s claps %d blows %d  %s\n", CASES[c].name, claps, blows, ok ? "OK" : "UNEXPECTED");
        fails += !ok;
    }
    printf("audio tests: %s\n", fails ? "FAILED" : "OK");
    return fails;
}

static int test_fuzz(void)
{
    srand(7);
    for (int round = 0; round < 30; round++) {
        game_init((uint32_t)rand() + 1);
        for (int i = 0; i < 20000; i++) {
            if (rand() % 6 == 0) game_key(rand() % 6);
            if (rand() % 400 == 0) game_mic(1 + rand() % 2);
            game_frame((uint32_t)(rand() % 300));
            while (game_sfx_pop() != SFX_NONE) {}
            check(game_world());
        }
    }
    printf("fuzz OK\n");
    return 0;
}

int main(int argc, char **argv)
{
    const char *mode = argc > 1 ? argv[1] : "soak";
    if (!strcmp(mode, "soak")) return test_soak(argc, argv);
    if (!strcmp(mode, "shots")) return test_shots(argc, argv);
    if (!strcmp(mode, "save")) return test_save();
    if (!strcmp(mode, "away")) return test_away();
    if (!strcmp(mode, "tree")) return test_tree();
    if (!strcmp(mode, "audio")) return test_audio();
    if (!strcmp(mode, "powers")) return test_powers();
    if (!strcmp(mode, "fuzz")) return test_fuzz();
    fprintf(stderr, "unknown mode %s\n", mode);
    return 1;
}
