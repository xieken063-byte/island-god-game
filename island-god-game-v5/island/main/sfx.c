#include "sfx.h"

#include "sim.h"

/* 一个音效 = 若干音符。hz2 不为 0 时在音符内从 hz 滑到 hz2；hz 为 0 是噪声（lp 越大越闷） */
typedef struct { uint16_t hz, hz2, ms; uint8_t vol, lp; } note_t;

static const note_t N_BIRTH[] = {{660, 0, 90, 70, 0}, {880, 0, 90, 70, 0}, {1320, 0, 200, 70, 0}, {0, 0, 0, 0, 0}};
static const note_t N_DEATH[] = {{440, 0, 220, 60, 0}, {330, 0, 220, 60, 0}, {220, 0, 420, 60, 0}, {0, 0, 0, 0, 0}};
static const note_t N_CHIME[] = {{880, 0, 130, 60, 0}, {1175, 0, 300, 60, 0}, {0, 0, 0, 0, 0}};
static const note_t N_BLESS[] = {{523, 0, 70, 55, 0}, {659, 0, 70, 55, 0}, {784, 0, 70, 55, 0}, {1047, 0, 220, 55, 0}, {0, 0, 0, 0, 0}};
static const note_t N_BLIP[] = {{1000, 0, 35, 40, 0}, {0, 0, 0, 0, 0}};
static const note_t N_THUNDER[] = {{0, 0, 90, 100, 2}, {0, 0, 800, 90, 5}, {0, 0, 0, 0, 0}};
static const note_t N_RUMBLE[] = {{0, 0, 1400, 100, 6}, {0, 0, 0, 0, 0}};
static const note_t N_ALARM[] = {{740, 0, 110, 65, 0}, {1, 0, 60, 0, 0}, {740, 0, 110, 65, 0}, {0, 0, 0, 0, 0}};
static const note_t N_WOLF[] = {{300, 520, 350, 60, 0}, {520, 380, 500, 60, 0}, {0, 0, 0, 0, 0}};
static const note_t *const TABLE[SFX_COUNT] = {0, N_BIRTH, N_DEATH, N_CHIME, N_BLESS, N_BLIP, N_THUNDER, N_RUMBLE, N_ALARM, N_WOLF};

void sfx_start(sfx_gen_t *g, int id)
{
    g->id = (uint8_t)(id > 0 && id < SFX_COUNT ? id : 0);
    g->seg = 0;
    g->pos = 0;
    g->phase = 0;
    g->lfsr = 0xACE1u;
    g->lp = 0;
}

int sfx_fill(sfx_gen_t *g, int16_t *buf, int n)
{
    int out = 0;
    const note_t *seq = TABLE[g->id];
    if (!seq) return 0;
    while (out < n) {
        const note_t *nt = &seq[g->seg];
        if (nt->ms == 0) { g->id = 0; break; }
        uint32_t len = (uint32_t)nt->ms * SFX_RATE / 1000;
        if (g->pos >= len) {
            g->seg++;
            g->pos = 0;
            continue;
        }
        /* 包络：2 ms 起音，其余线性衰减到零，避免爆音 */
        int32_t env = (int32_t)((len - g->pos) * 256 / len);
        if (g->pos < 32) env = env * (int32_t)g->pos / 32;
        int32_t v;
        if (nt->hz == 0) { /* 噪声：伽罗瓦 LFSR 过一阶低通 */
            g->lfsr = (g->lfsr >> 1) ^ (-(g->lfsr & 1u) & 0xB400u);
            int32_t x = (int32_t)(g->lfsr & 0xFFFF) - 32768;
            g->lp += (x - g->lp) / (1 << nt->lp);
            v = g->lp * (nt->lp > 3 ? 4 : 1); /* 低通后幅度变小，补回来 */
            if (v > 32767) v = 32767;
            if (v < -32768) v = -32768;
        } else { /* 三角波 */
            uint32_t hz = nt->hz;
            if (nt->hz2) hz = (uint32_t)((int32_t)nt->hz + ((int32_t)nt->hz2 - (int32_t)nt->hz) * (int32_t)g->pos / (int32_t)len);
            g->phase += hz * (0xFFFFFFFFu / SFX_RATE);
            uint32_t ph = g->phase >> 16; /* 0..65535 */
            v = ph < 32768 ? (int32_t)ph * 2 - 32768 : 98303 - (int32_t)ph * 2;
        }
        buf[out++] = (int16_t)(v * env / 256 * nt->vol / 280);
        g->pos++;
    }
    return out;
}

/* ---------- 麦克风 ---------- */

#define BLOCK 320 /* 20 ms */

void mic_init(mic_t *m)
{
    *m = (mic_t){0};
    m->floor_q8 = 200 << 8;
    m->cooldown = 50; /* 开机头 1 秒不触发 */
}

void mic_mute(mic_t *m, int ms)
{
    int blocks = ms / 20 + 1;
    if (m->cooldown < blocks) m->cooldown = (uint16_t)blocks;
    m->clap_wait = 0;
    m->blow_run = 0;
}

static int mic_block(mic_t *m, int32_t amp, int32_t low)
{
    int ev = 0;
    int32_t floor = m->floor_q8 >> 8;
    if (m->cooldown) {
        m->cooldown--;
        m->clap_wait = 0;
        m->blow_run = 0;
    } else {
        /* 拍手：比环境响得多、来得突然、几十毫秒内就消失 */
        if (m->clap_wait) {
            if (amp * 3 < m->peak) {
                ev = 1;
                m->clap_wait = 0;
            } else if (--m->clap_wait == 0) {
                m->peak = 0; /* 拖得太长，不是拍手 */
            }
        } else if (amp > floor * 6 && amp > 2500 && m->prev[0] * 3 < amp && m->prev[1] * 3 < amp) {
            m->peak = amp;
            m->clap_wait = 6;
        }
        /* 吹气：持续 0.7 秒以上、以低频为主的大声音，中间没有明显停顿 */
        if (amp > floor * 3 && amp > 1200 && low * 10 >= amp * 6) {
            if (m->blow_run < 250) m->blow_run++;
            if (m->blow_run == 35 && !ev) ev = 2;
        } else {
            m->blow_run = 0;
        }
        if (ev) m->cooldown = 250; /* 触发后 5 秒内不再响应 */
    }
    /* 噪声底：安静时跟得快，吵的时候只慢慢往上爬 */
    if (amp < floor * 2) m->floor_q8 += ((amp << 8) - m->floor_q8) / 32;
    else m->floor_q8 += m->floor_q8 / 512;
    if (m->floor_q8 < (40 << 8)) m->floor_q8 = 40 << 8;
    m->prev[1] = m->prev[0];
    m->prev[0] = amp;
    return ev;
}

int mic_feed(mic_t *m, const int16_t *pcm, int n)
{
    int ev = 0;
    for (int i = 0; i < n; i++) {
        m->dc += ((int32_t)pcm[i] * 256 - m->dc) / 128; /* 去直流 */
        int32_t x = pcm[i] - m->dc / 256;
        m->sum += x < 0 ? -x : x;
        m->box += x;
        if (++m->boxn == 16) {
            m->lfsum += m->box < 0 ? -m->box : m->box;
            m->box = 0;
            m->boxn = 0;
        }
        if (++m->fill == BLOCK) {
            int e = mic_block(m, m->sum / BLOCK, m->lfsum / BLOCK);
            if (e && !ev) ev = e;
            m->fill = 0;
            m->sum = 0;
            m->lfsum = 0;
        }
    }
    return ev;
}
