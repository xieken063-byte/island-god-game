/*
 * 《造物主：孤岛》模拟核心 —— 纯 C、无硬件依赖，可在主机上编译测试。
 */
#ifndef SIM_H
#define SIM_H

#include <stdbool.h>
#include <stdint.h>

#define MAP_W 30
#define MAP_H 26
#define MAP_N (MAP_W * MAP_H)
#define TILE 8

#define MAX_RES 16
#define MAX_ANI 20
#define TREE_N 200 /* 家谱登记的人数上限（环形，满了覆盖最早的人） */
#define MAX_DEAD 8

#define TICKS_PER_DAY 600 /* 1x 速度下 1 tick = 100 ms，1 天 = 现实 1 分钟 */
#define TICKS_PER_HOUR (TICKS_PER_DAY / 24)
#define DAYS_PER_YEAR 4 /* 4 个游戏日记为 1 岁 */

/* 年龄段（岁） */
#define AGE_CHILD 3   /* 3 岁前是婴儿，待在家里由大人照顾 */
#define AGE_WORK 12   /* 12 岁开始干活 */
#define AGE_ADULT 16  /* 16 岁可以结为伴侣 */
#define AGE_ELDER 55  /* 55 岁后不再干重活 */
#define AGE_OLD 60    /* 60 岁起每天有概率老死，年纪越大概率越高 */

/* 每格 16 位：低 4 位地形，中 5 位地物，高 7 位附加值（作物长势 / 房屋耐久 / 燃烧剩余时间） */
enum { T_SEA, T_SHOAL, T_SAND, T_GRASS, T_ROCK, T_RIVER, T_LAKE, T_ASH, T_LAVA };
enum {
    O_NONE, O_SAPLING, O_YOUNG, O_TREE, O_STUMP, O_BUSH, O_BERRY, O_FLOWER,
    O_FIRE, O_STORE, O_GRAVE, O_BURNT,
    O_FARM, O_SITE, O_HOUSE1, O_HOUSE2, O_RUIN, O_FLAME, O_SHRINE
};
enum { A_BOAR, A_RABBIT, A_WOLF, A_KINDS };
#define TERR(t) ((t) & 15)
#define OBJ(t) (((t) >> 4) & 31)
#define VAL(t) ((t) >> 9)
#define MKT(te, o, v) ((uint16_t)((te) | ((o) << 4) | ((v) << 9)))
#define MKTILE(te, o) MKT(te, o, 0)

enum { TR_DILIGENT, TR_LAZY, TR_BRAVE, TR_TIMID, TR_KIND, TR_COUNT };
enum { S_IDLE, S_MOVE, S_ACT, S_SLEEP };
enum {
    G_NONE, G_WANDER, G_REST, G_EAT_CAMP, G_EAT_BERRY, G_BERRY,
    G_CHOP, G_HUNT, G_FISH, G_HAUL, G_SLEEP,
    G_FLEE, G_CHAT, G_HELP, G_TEND, G_BUILD, G_UPGRADE, G_REPAIR,
    G_TILL, G_HARVEST, G_MINE, G_SHELTER, G_SHRINE, G_PRAY, G_COWER
};
enum { C_NONE, C_FOOD, C_WOOD, C_STONE };
enum { W_SUNNY, W_CLOUDY, W_RAIN, W_STORM, W_THUNDER, W_WIND, W_SNOW, W_COUNT };
enum { D_STARVE, D_AGE, D_BOLT, D_FIRE, D_FLOOD, D_LAVA, D_QUAKE, D_PLAGUE, D_COLD, D_WOLF, D_COUNT };
enum { SFX_NONE, SFX_BIRTH, SFX_DEATH, SFX_CHIME, SFX_BLESS, SFX_BLIP, SFX_THUNDER, SFX_RUMBLE, SFX_ALARM, SFX_WOLF, SFX_COUNT };

/* 神力，按类别排列（长按上/下可以整类跳过） */
enum {
    P_INSPECT, P_TREE, P_RECORDS,
    P_FOREST, P_BEAST, P_RABBIT, P_WOLF, P_FISH, P_FOOD, P_HUMAN, P_ORE, P_WATER, P_CROP,
    P_HEAL, P_GROW, P_CALM,
    P_W_SUNNY, P_W_CLOUDY, P_W_RAIN, P_W_STORM, P_W_THUNDER, P_W_WIND, P_W_SNOW,
    P_BOLT, P_WILDFIRE, P_FLOOD, P_QUAKE, P_VOLCANO, P_PLAGUE,
    P_SPEED, P_SOUND, P_VOICE, P_COUNT
};

#define NO_ID 0xFFFF
#define NO_SLOT 0xFF

typedef struct {
    uint8_t alive;
    uint8_t name; /* 名字池下标 */
    uint8_t sex;  /* 0 男 1 女 */
    uint8_t trait;
    uint16_t uid;
    uint16_t age_days;
    uint16_t hp, food, energy, mood; /* 0..1000 */
    uint8_t tx, ty, nx, ny;          /* 当前格 / 下一格 */
    uint8_t step;                    /* 0..TILE 的格间进度 */
    uint8_t face;                    /* 1 = 朝左 */
    uint8_t state, goal;
    uint8_t gx, gy;
    uint8_t tgt; /* 狩猎目标 / 聊天、照顾、救助对象的下标 */
    uint8_t carry, carry_n;
    uint8_t partner;       /* 伴侣的下标，NO_SLOT = 无 */
    uint16_t timer;
    uint16_t home;         /* 房屋所在格，NO_ID = 无家可归 */
    uint16_t father, mother; /* 父母的 uid，NO_ID = 初代 */
    uint8_t fname, mname;  /* 父母的名字下标（父母去世后仍可查看） */
    uint8_t gen;           /* 第几代，初代为 1 */
    uint8_t kids;
    uint8_t preg;          /* 距离分娩的天数，0 = 未怀孕 */
    uint8_t sick;          /* 染病剩余天数 */
    uint8_t immune;        /* 病愈后的免疫天数 */
    uint8_t hurt_cd;       /* 受伤消息冷却 */
    uint16_t last_birth;   /* 上次生育是第几天 */
} resident_t;

typedef struct {
    uint8_t alive, kind;
    uint8_t tx, ty, nx, ny;
    uint8_t step, face;
    uint8_t held; /* 被哪个居民缠住（下标+1） */
    uint8_t flee;
    uint8_t hunger; /* 狼：多少小时没吃东西 */
    uint8_t pad;
} animal_t;

/* 家谱里的一条记录。在世者的年龄从居民数据里取，这里只记去世时的 */
typedef struct {
    uint16_t uid; /* 0 = 空 */
    uint16_t father, mother;
    uint16_t born; /* 出生在第几天 */
    uint8_t name, sex;
    uint8_t age;   /* 享年；0xFF = 在世 */
    uint8_t cause;
    uint8_t gen, pad;
} person_t;

typedef struct {
    uint8_t name, sex, age, gen, cause, used;
    uint16_t day;
} dead_t;

/* 整个世界就是一个无指针的 POD，直接整块存档 */
typedef struct {
    uint32_t rng;
    uint32_t tick;
    uint16_t tiles[MAP_N];
    resident_t res[MAX_RES];
    animal_t ani[MAX_ANI];
    uint8_t rel[MAX_RES][MAX_RES]; /* 好感度 0..100 */
    dead_t dead[MAX_DEAD];         /* 逝者名录（环形） */
    uint8_t camp_x, camp_y;
    uint8_t fire_lit;
    uint8_t speed_idx;
    uint8_t paused;
    uint8_t weather, weather_h; /* 当前天气与剩余小时数 */
    uint8_t flood;              /* 洪水等级 0..3：离水 N 格以内的陆地被淹 */
    uint8_t storm_h;            /* 暴雨已持续的小时数 */
    uint8_t volcano_h;          /* 火山还要喷发几小时 */
    uint8_t dead_n;             /* 写入逝者名录的总数（低 8 位） */
    uint8_t max_gen;
    uint16_t crater;            /* 火山口所在格 */
    uint16_t fires, lava;       /* 正在燃烧 / 岩浆格数 */
    uint16_t food, wood, stone, fish;
    uint16_t next_uid;
    uint16_t deaths, births;
    uint16_t marriages, built;
    uint16_t shrine;      /* 祭石所在格，NO_ID = 没有 */
    uint8_t faith, fear;  /* 岛民对神的信仰与恐惧，0..100 */
    uint8_t sound_on, voice_on;
    uint16_t power_used[P_COUNT];
} world_t;

/* 事件日志与特效：不进存档 */
#define LOG_N 4
#define LOG_LEN 72
typedef struct {
    char msg[LOG_N][LOG_LEN];
    uint32_t seq; /* 写入总数；最新一条是 msg[(seq-1)%LOG_N] */
} sim_log_t;

enum { FX_NONE, FX_SPARK, FX_BOLT, FX_HEAL, FX_QUAKE };
#define FX_N 8
typedef struct { uint8_t type, x, y, t; } sim_fx_t;

extern sim_log_t g_log;
extern sim_fx_t g_fx[FX_N];
extern person_t g_tree[TREE_N]; /* 家谱：单独存档 */
extern bool g_tree_dirty;

const person_t *sim_person(uint16_t uid);
void sim_tree_put(const world_t *w, const resident_t *r); /* 登记或更新一个人 */
/* 列出某人的亲属：role 0 父 1 母 2 配偶 3 子女；返回个数 */
int sim_relatives(const world_t *w, uint16_t uid, uint16_t *out, uint8_t *role, int max);
int sim_descendants(uint16_t uid, int *living); /* 后代总数；*living = 其中在世的 */
int sim_tree_count(void);
const resident_t *sim_resident_by_uid(const world_t *w, uint16_t uid);
int sim_kind_count(const world_t *w, int kind);
int sim_sfx_pop(void); /* 取出一个待播放的音效，没有则返回 SFX_NONE */

void sim_new(world_t *w, uint32_t seed);
void sim_refresh(world_t *w); /* 读档后调用：重建不进存档的派生数据 */
void sim_tick(world_t *w);
void sim_cast(world_t *w, int power);
void sim_fx_step(void); /* 每帧调用一次，推进特效 */

int sim_pop(const world_t *w);
int sim_animals(const world_t *w);
int sim_day(const world_t *w);  /* 从 1 开始 */
int sim_hour(const world_t *w); /* 0..23 */
int sim_speed(const world_t *w);
bool sim_is_night(const world_t *w);
bool sim_flooded(const world_t *w, int idx);
int sim_age(const resident_t *r); /* 岁 */
bool sim_in_house(const world_t *w, const resident_t *r);

const char *sim_name(const resident_t *r);
const char *sim_name_of(int sex, int name);
const char *sim_trait(const resident_t *r);
const char *sim_status(const world_t *w, const resident_t *r);
const char *sim_power_name(int power);
const char *sim_power_cat(int power);
int sim_power_cat_id(int power);
const char *sim_weather_name(int weather);
const char *sim_cause_name(int cause);

#endif
