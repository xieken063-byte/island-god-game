/*
 * 渲染器：把世界画进三块 RGB565 帧缓冲（顶栏 / 地图 / 底部面板）。
 * 不依赖 LVGL，主机上可以直接导出图片核对。
 */
#ifndef RENDER_H
#define RENDER_H

#include "sim.h"

#define SCR_W 240
#define TOP_H 24
#define MAPV_H (MAP_H * TILE) /* 208 */
#define BOT_H 88              /* 24 + 208 + 88 = 320 */

#define TREE_ROWS 10 /* 家谱页一屏最多列出的亲属数 */

/* 岛屿纪事：离开期间发生了什么 */
typedef struct {
    uint32_t away_s;
    uint16_t from_day, days, births, deaths, marriages, built;
} chron_t;

typedef struct {
    uint8_t sel;        /* 当前选中的神力 */
    uint8_t inspect;    /* 0 平时 1 观察居民 2 岛屿纪事 3 归来纪事 4 家谱 */
    uint8_t who;        /* 被观察的居民下标 */
    uint8_t screen_off; /* 息屏中 */
    uint8_t catching;   /* 正在补算离线期间的时间 */
    uint8_t tree_cur;   /* 家谱页的光标 */
    uint16_t tree_uid;  /* 家谱页正在看的人 */
    uint32_t catch_total, catch_left;
    chron_t chron;
    uint32_t frame;
} ui_t;

extern uint16_t fb_top[SCR_W * TOP_H];
extern uint16_t fb_map[SCR_W * MAPV_H];
extern uint16_t fb_bot[SCR_W * BOT_H];

void render_init(void);
void render_top(const world_t *w, const ui_t *ui);
void render_map(const world_t *w, const ui_t *ui);
void render_bot(const world_t *w, const ui_t *ui);

#endif
