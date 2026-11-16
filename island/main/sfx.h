/* 音效合成与麦克风识别：纯计算，不碰硬件。16 kHz、16 位、单声道。 */
#ifndef SFX_H
#define SFX_H

#include <stdbool.h>
#include <stdint.h>

#define SFX_RATE 16000

typedef struct {
    uint8_t id, seg;
    uint32_t pos;   /* 当前音符内已输出的采样数 */
    uint32_t phase; /* 32 位相位累加器 */
    uint32_t lfsr;
    int32_t lp;     /* 噪声低通状态 */
} sfx_gen_t;

void sfx_start(sfx_gen_t *g, int id);
int sfx_fill(sfx_gen_t *g, int16_t *buf, int n); /* 返回写入的采样数；0 表示播完了 */

/* 麦克风：每喂一段 PCM，返回 MIC_NONE / MIC_CLAP / MIC_BLOW（数值与 game.h 一致） */
typedef struct {
    int32_t floor_q8;   /* 环境噪声底（放大 256 倍） */
    int32_t prev[2];    /* 前两块的平均幅度 */
    int32_t peak;       /* 疑似拍手的峰值 */
    uint8_t clap_wait;  /* 拍手确认倒计时（块） */
    uint8_t blow_run;   /* 连续符合吹气特征的块数 */
    uint16_t cooldown;  /* 触发后的冷却（块） */
    uint16_t fill;      /* 当前块已收的采样数 */
    int32_t sum, dc;
    int32_t box, lfsum; /* 16 点滑动平均：只留下约 500 Hz 以下的成分 */
    uint8_t boxn;
} mic_t;

void mic_init(mic_t *m);
int mic_feed(mic_t *m, const int16_t *pcm, int n);
void mic_mute(mic_t *m, int ms); /* 自己放音效时调用，避免把喇叭声当成拍手 */

#endif
