/* 游戏层：按键 → 界面状态 → 模拟推进 → 渲染 → 存档。与硬件无关。 */
#ifndef GAME_H
#define GAME_H

#include "render.h"
#include "sim.h"

enum { KEY_UP, KEY_DOWN, KEY_OK, KEY_OK_LONG, KEY_UP_LONG, KEY_DOWN_LONG };
enum { DIRTY_TOP = 1, DIRTY_MAP = 2, DIRTY_BOT = 4, GAME_SLEEP = 8 };
enum { MIC_NONE, MIC_CLAP, MIC_BLOW };

#define FRAME_MS 100
#define AUTOSAVE_MS (3 * 60 * 1000)   /* 每 3 分钟（现实时间）自动存档一次 */
#define SCREEN_OFF_MS (2 * 60 * 1000) /* 2 分钟没按键：息屏，岛屿照常运行 */
#define SLEEP_MS (10 * 60 * 1000)     /* 10 分钟没按键：存档并深度休眠 */
#define AWAY_MAX_S (24 * 60 * 60)     /* 离线推进最多补算 24 小时（1440 个游戏日） */

void game_init(uint32_t seed); /* 有存档则读档，否则用 seed 生成新岛 */
void game_key(int key);
void game_mic(int ev);          /* 麦克风识别到拍手 / 吹气 */
int game_frame(uint32_t dt_ms); /* 推进并重绘，返回需要刷新的区域；带 GAME_SLEEP 表示该休眠了 */
bool game_save(void);
int game_sfx_pop(void); /* 取一个该播放的音效（已考虑静音和快进） */
bool game_screen_off(void);
bool game_voice_on(void);
world_t *game_world(void);
const ui_t *game_ui(void);

/* 由平台层实现（ESP32: NVS + RTC；主机: 文件 + 假时钟） */
bool plat_load(const char *key, void *buf, uint32_t len);
bool plat_save(const char *key, const void *buf, uint32_t len);
uint32_t plat_ms(void);                       /* 单调毫秒 */
uint32_t plat_wall_s(void);                   /* 休眠期间也在走的秒表 */
uint32_t plat_away_s(uint32_t saved_wall_s);  /* 距离上次存档过去了多少秒；无从得知时返回 0 */

#endif
