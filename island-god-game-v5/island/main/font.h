/* 12px 点阵字：ASCII 占 6 像素宽，汉字占 12 像素宽。 */
#ifndef FONT_H_INCLUDED
#define FONT_H_INCLUDED

#include <stdint.h>

#define FONT_H 12

extern const uint16_t font_count;
extern const uint16_t font_cp[];
extern const uint16_t font_rows[][FONT_H];

/* 在 RGB565 帧缓冲上画一行 UTF-8 文字，返回结束处的 x */
int font_draw(uint16_t *fb, int fbw, int fbh, int x, int y, const char *utf8, uint16_t color);
int font_width(const char *utf8);
/* 放大 scale 倍画（用于屏幕中央的大字） */
int font_draw_scaled(uint16_t *fb, int fbw, int fbh, int x, int y, const char *utf8, uint16_t color, int scale);

#endif
