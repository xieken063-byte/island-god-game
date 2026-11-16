/* 模拟核心内部共用的声明：sim.c（居民）与 sim_world.c（世界）之间的接口。 */
#ifndef SIM_INT_H
#define SIM_INT_H

#include "sim.h"

#define IDX(x, y) ((y) * MAP_W + (x))

extern const int8_t SIM_DX[4], SIM_DY[4];
extern uint8_t sim_wdist[MAP_N];  /* 每格到最近水域的格数（封顶 15） */
extern uint8_t sim_danger[MAP_N]; /* 0 安全 1 灾害本体 2 紧邻火/岩浆 3 三格以内 */
extern uint8_t sim_par[MAP_N];    /* 最近一次 BFS 的到达标记 */
extern int sim_farms, sim_houses; /* 每小时重新统计 */

typedef bool (*sim_pred_t)(const world_t *, int);

uint32_t sim_rnd(world_t *w);
int sim_rn(world_t *w, int n);
int sim_cheb(int ax, int ay, int bx, int by);
bool sim_inmap(int x, int y);
#if defined(__GNUC__)
void sim_logf(const char *fmt, ...) __attribute__((format(printf, 1, 2)));
#else
void sim_logf(const char *fmt, ...);
#endif
void sim_fx_add(int type, int x, int y, int dur);

bool sim_walkable(const world_t *w, int idx);
bool sim_is_water(int terr);
bool sim_is_house(int obj);
bool sim_occupied(const world_t *w, int idx);
int sim_bfs(const world_t *w, int sx, int sy, sim_pred_t pred, int *dir_out);
bool sim_pred_none(const world_t *w, int idx);
bool sim_pred_shore(const world_t *w, int idx);
void sim_set_self(int i);

void sim_calc_wdist(const world_t *w);
void sim_calc_danger(const world_t *w);

bool sim_camp_spot(const world_t *w, int i, int *x, int *y);
int sim_rand_grass(world_t *w, int mind, bool empty_only);
void sim_spawn_animal(world_t *w, int x, int y, int kind);
void sim_sfx(int id);
void sim_devotion(world_t *w, int dfaith, int dfear);
int sim_spawn_resident(world_t *w, int sex, int trait, int age_years, int at_idx);

void sim_die(world_t *w, int i, int cause);
bool sim_hurt(world_t *w, int i, int amount, int cause); /* 返回 false 表示死了 */
void sim_house_lost(world_t *w, int idx);                /* 房子没了：住户变成无家可归 */
bool sim_ignite(world_t *w, int idx);
void sim_infect(world_t *w, int i);
void sim_res_tick(world_t *w, int i);

bool sim_gen_map(world_t *w, uint32_t seed);
void sim_eco_hour(world_t *w);
void sim_eco_day(world_t *w);
void sim_fire_step(world_t *w);
void sim_ani_tick(world_t *w, int k);

#endif
