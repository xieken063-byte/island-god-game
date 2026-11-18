/*
 * 《造物主：孤岛》—— FoloToy AI Passport 入口（ESP32-C3，ESP-IDF 5.5.x，官方 BSP + LVGL 9）。
 *
 * LVGL 在这里只当"贴图机"用：三块 RGB565 帧缓冲各包成一个 lv_image，
 * 游戏自己把画面画进缓冲，再让 LVGL 刷到屏上（圆角遮罩由 BSP 负责）。
 *
 * 三个任务：
 *   game_task   每 100 ms：取按键和麦克风事件 → 推进模拟 → 重绘 → 管理息屏和休眠
 *   audio_task  播放音效；开了"声控神力"时持续读麦克风，识别拍手和吹气
 *   on_key      按键回调（在 esp_timer 任务里），只往队列里塞一个字节
 */
#include <inttypes.h>
#include <string.h>
#include <time.h>

#include "esp_heap_caps.h"
#include "esp_log.h"
#include "esp_random.h"
#include "esp_sleep.h"
#include "esp_system.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/task.h"
#include "lvgl.h"
#include "nvs.h"
#include "nvs_flash.h"

#include "bsp_audio.h"
#include "bsp_battery.h"
#include "bsp_button.h"
#include "bsp_display.h"
#include "bsp_i2c.h"
#include "bsp_pins.h"
#include "game.h"
#include "sfx.h"

static const char *TAG = "island";

#define BACKLIGHT_PCT 80
#define VOLUME_PCT 60

/* ---------- 平台层：存档、时钟 ---------- */

static bool s_nvs_ok;
static bool s_woke_from_sleep;

bool plat_load(const char *key, void *buf, uint32_t len)
{
    if (!s_nvs_ok) return false;
    nvs_handle_t h;
    if (nvs_open("island", NVS_READONLY, &h) != ESP_OK) return false;
    size_t n = len;
    esp_err_t err = nvs_get_blob(h, key, buf, &n);
    nvs_close(h);
    return err == ESP_OK && n == len;
}

bool plat_save(const char *key, const void *buf, uint32_t len)
{
    if (!s_nvs_ok) return false;
    nvs_handle_t h;
    if (nvs_open("island", NVS_READWRITE, &h) != ESP_OK) return false;
    esp_err_t err = nvs_set_blob(h, key, buf, len);
    if (err == ESP_OK) err = nvs_commit(h);
    nvs_close(h);
    if (err != ESP_OK) ESP_LOGW(TAG, "save %s failed: %s", key, esp_err_to_name(err));
    return err == ESP_OK;
}

uint32_t plat_ms(void) { return (uint32_t)(esp_timer_get_time() / 1000); }

/* 系统时间由 RTC 维持，深度休眠期间照常走；彻底断电后从零开始 */
uint32_t plat_wall_s(void) { return (uint32_t)time(NULL); }

uint32_t plat_away_s(uint32_t saved)
{
    if (!s_woke_from_sleep) return 0; /* 冷启动：不知道关机了多久，不推进 */
    uint32_t now = plat_wall_s();
    return now > saved ? now - saved : 0;
}

/* ---------- 按键：回调在 esp_timer 任务里，只入队不干活 ---------- */

static QueueHandle_t s_keys;

static void on_key(bsp_btn_t btn, bsp_btn_ev_t ev, void *user)
{
    (void)user;
    uint8_t k[2];
    int n = 0;
    /* 唤醒设备的那一下按键不算数 */
    if (s_woke_from_sleep && esp_timer_get_time() < 1000 * 1000) return;
    if (ev == BSP_BTN_LONG) {
        k[n++] = btn == BSP_BTN_UP ? KEY_UP_LONG : btn == BSP_BTN_DOWN ? KEY_DOWN_LONG : KEY_OK_LONG;
    } else if (ev == BSP_BTN_CLICK || ev == BSP_BTN_DOUBLE) {
        uint8_t key = btn == BSP_BTN_UP ? KEY_UP : btn == BSP_BTN_DOWN ? KEY_DOWN : KEY_OK;
        k[n++] = key;
        /* 连按两下上/下会被识别成双击，这里还原成两次点击 */
        if (ev == BSP_BTN_DOUBLE && btn != BSP_BTN_OK) k[n++] = key;
    }
    for (int i = 0; i < n; i++) xQueueSend(s_keys, &k[i], 0);
}

/* ---------- 声音：播放音效 + 听拍手和吹气 ---------- */

static QueueHandle_t s_sfx_q, s_mic_q;
static volatile bool s_audio_ok, s_listen, s_audio_quit, s_audio_parked;

static void audio_task(void *arg)
{
    (void)arg;
    static int16_t buf[320]; /* 20 ms */
    sfx_gen_t gen;
    mic_t mic;
    bool awake = true;
    int idle_ms = 0;
    mic_init(&mic);
    for (;;) {
        if (s_audio_quit) { /* 要休眠了：停掉一切 PCM 读写 */
            s_audio_parked = true;
            vTaskSuspend(NULL);
        }
        uint8_t id;
        if (xQueueReceive(s_sfx_q, &id, s_listen ? 0 : pdMS_TO_TICKS(100)) == pdTRUE) {
            if (!awake) awake = bsp_audio_wake() == ESP_OK;
            if (!awake) continue;
            int n;
            sfx_start(&gen, id);
            while (!s_audio_quit && (n = sfx_fill(&gen, buf, 320)) > 0) bsp_audio_write(buf, (size_t)n * 2);
            memset(buf, 0, sizeof buf); /* 垫一段静音，把尾音推出去 */
            bsp_audio_write(buf, sizeof buf);
            mic_mute(&mic, 500); /* 喇叭的余音别当成拍手 */
            idle_ms = 0;
            continue;
        }
        if (s_listen) {
            if (!awake) awake = bsp_audio_wake() == ESP_OK;
            if (awake && bsp_audio_read(buf, sizeof buf) == ESP_OK) {
                uint8_t ev = (uint8_t)mic_feed(&mic, buf, 320);
                if (ev) xQueueSend(s_mic_q, &ev, 0);
            } else {
                vTaskDelay(pdMS_TO_TICKS(50));
            }
            idle_ms = 0;
        } else if (awake && (idle_ms += 100) >= 3000) { /* 3 秒没声音：让编解码器睡下省电 */
            bsp_audio_sleep();
            awake = false;
        }
    }
}

static void audio_start(void)
{
    if (bsp_audio_init() != ESP_OK || bsp_audio_set_format(SFX_RATE, 16, 1) != ESP_OK) {
        ESP_LOGW(TAG, "audio unavailable, running silent");
        return;
    }
    bsp_audio_set_volume(VOLUME_PCT);
    s_sfx_q = xQueueCreate(2, sizeof(uint8_t));
    s_mic_q = xQueueCreate(4, sizeof(uint8_t));
    if (!s_sfx_q || !s_mic_q) return;
    s_audio_ok = xTaskCreate(audio_task, "audio", 4096, NULL, 5, NULL) == pdPASS;
}

/* ---------- 显示 ---------- */

static lv_image_dsc_t s_dsc_top, s_dsc_map, s_dsc_bot;
static lv_obj_t *s_img_top, *s_img_map, *s_img_bot;

static lv_obj_t *make_image(lv_image_dsc_t *d, const uint16_t *fb, int h, int y)
{
    memset(d, 0, sizeof *d);
    d->header.magic = LV_IMAGE_HEADER_MAGIC;
    d->header.cf = LV_COLOR_FORMAT_RGB565;
    d->header.w = SCR_W;
    d->header.h = h;
    d->header.stride = SCR_W * 2;
    d->data = (const uint8_t *)fb;
    d->data_size = (uint32_t)(SCR_W * h * 2);
    lv_obj_t *img = lv_image_create(lv_screen_active());
    lv_image_set_src(img, d);
    lv_obj_set_pos(img, 0, y);
    return img;
}

/* ---------- 深度休眠：顺序照官方 demo_low_power.c ---------- */

static void enter_deep_sleep(void)
{
    /* 键还按着就睡会立刻被唤醒：这一帧先不睡，下一帧再试 */
    int mv = bsp_button_read_mv();
    if (mv >= 0 && mv < BSP_BTN_MV_RELEASED_MIN) return;

    game_save(); /* 存档里带着此刻的时间，醒来时据此补算 */
    ESP_LOGI(TAG, "idle, entering deep sleep");

    if (s_audio_ok) { /* 等音频任务停下所有 PCM 读写 */
        s_audio_quit = true;
        for (int i = 0; i < 40 && !s_audio_parked; i++) vTaskDelay(pdMS_TO_TICKS(50));
    }
    int level = 1;
    if (bsp_button_prepare_deep_sleep(&level) != ESP_OK) esp_restart(); /* 按键已不可恢复，只能重启 */
    if (level != 1) ESP_LOGW(TAG, "key pressed while arming wake, may wake at once");
    esp_err_t err = esp_deep_sleep_enable_gpio_wakeup(1ULL << BSP_BTN_GPIO, ESP_GPIO_WAKEUP_GPIO_LOW);
    if (err != ESP_OK) ESP_LOGE(TAG, "GPIO wake arm: %s", esp_err_to_name(err));
    bsp_battery_sleep();
    bsp_audio_sleep();
    bsp_audio_prepare_deep_sleep();
    bsp_i2c_prepare_deep_sleep();
    if (!bsp_lvgl_lock(1000)) esp_restart();
    bsp_display_prepare_deep_sleep();
    esp_deep_sleep_start();
    esp_restart(); /* 不该走到这里 */
}

/* ---------- 主循环 ---------- */

static void game_task(void *arg)
{
    (void)arg;
    TickType_t wake = xTaskGetTickCount();
    int64_t last = esp_timer_get_time();
    bool screen_off = false;
    for (;;) {
        uint8_t ev;
        while (xQueueReceive(s_keys, &ev, 0) == pdTRUE) game_key(ev);
        while (s_mic_q && xQueueReceive(s_mic_q, &ev, 0) == pdTRUE) game_mic(ev);

        int64_t now = esp_timer_get_time();
        uint32_t dt = (uint32_t)((now - last) / 1000);
        last = now;

        /* 渲染也放在锁里：避免 LVGL 正在刷屏时帧缓冲被改到一半 */
        int flags = 0;
        if (bsp_lvgl_lock(200)) {
            flags = game_frame(dt);
            if (flags & DIRTY_TOP) lv_obj_invalidate(s_img_top);
            if (flags & DIRTY_MAP) lv_obj_invalidate(s_img_map);
            if (flags & DIRTY_BOT) lv_obj_invalidate(s_img_bot);
            bsp_lvgl_unlock();
        }

        bool off = game_screen_off();
        if (off != screen_off) {
            bsp_display_backlight(off ? 0 : BACKLIGHT_PCT);
            screen_off = off;
        }
        if (s_audio_ok) {
            uint8_t id = (uint8_t)game_sfx_pop();
            if (id != SFX_NONE) xQueueSend(s_sfx_q, &id, 0); /* 放不下就丢：快进时事件太密 */
            s_listen = game_voice_on() && !off;                /* 息屏时不听，省电 */
        } else {
            (void)game_sfx_pop();
        }
        if (flags & GAME_SLEEP) enter_deep_sleep();

        /* 补算离线时间时一帧可能超过 100 ms：至少让出一点时间给 LVGL 和空闲任务 */
        if (xTaskDelayUntil(&wake, pdMS_TO_TICKS(FRAME_MS)) == pdFALSE) {
            vTaskDelay(pdMS_TO_TICKS(10));
            wake = xTaskGetTickCount();
        }
    }
}

void app_main(void)
{
    s_woke_from_sleep = esp_reset_reason() == ESP_RST_DEEPSLEEP;

    if (bsp_display_init() != ESP_OK) {
        ESP_LOGE(TAG, "display init failed");
        return;
    }
    if (bsp_lvgl_init() == NULL) {
        ESP_LOGE(TAG, "lvgl init failed");
        return;
    }

    /* NVS 初始化失败时不擦除（分区里可能有别的数据），只是这次运行不存档 */
    esp_err_t err = nvs_flash_init();
    s_nvs_ok = err == ESP_OK;
    if (!s_nvs_ok) ESP_LOGW(TAG, "nvs unavailable (%s), running without save", esp_err_to_name(err));

    game_init(esp_random() | 1u);

    if (!bsp_lvgl_lock(1000)) {
        ESP_LOGE(TAG, "lvgl lock failed");
        return;
    }
    lv_obj_t *scr = lv_screen_active();
    lv_obj_set_style_bg_color(scr, lv_color_black(), 0);
    lv_obj_set_style_bg_opa(scr, LV_OPA_COVER, 0);
    lv_obj_remove_flag(scr, LV_OBJ_FLAG_SCROLLABLE);
    s_img_top = make_image(&s_dsc_top, fb_top, TOP_H, 0);
    s_img_map = make_image(&s_dsc_map, fb_map, MAPV_H, TOP_H);
    s_img_bot = make_image(&s_dsc_bot, fb_bot, BOT_H, TOP_H + MAPV_H);
    game_frame(0);
    bsp_lvgl_unlock();
    bsp_display_backlight(BACKLIGHT_PCT);

    s_keys = xQueueCreate(8, sizeof(uint8_t));
    if (s_keys == NULL || bsp_button_init(on_key, NULL) != ESP_OK)
        ESP_LOGW(TAG, "buttons unavailable, the island still runs on its own");
    if (s_keys == NULL) s_keys = xQueueCreate(1, sizeof(uint8_t));

    audio_start();
    if (bsp_battery_init() != ESP_OK) ESP_LOGW(TAG, "fuel gauge not found");

    ESP_LOGI(TAG, "free heap %" PRIu32 " B, largest block %" PRIu32 " B, woke from sleep: %d",
             (uint32_t)heap_caps_get_free_size(MALLOC_CAP_DEFAULT),
             (uint32_t)heap_caps_get_largest_free_block(MALLOC_CAP_DEFAULT), (int)s_woke_from_sleep);

    xTaskCreate(game_task, "island", 8192, NULL, 4, NULL);
}
