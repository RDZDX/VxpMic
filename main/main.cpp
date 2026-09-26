#include <mic.h>
#include "string.h"
#include "vmchset.h"
#include "vmgraph.h"
#include "vmio.h"
#include "vmpromng.h"
#include "vmstdlib.h"
#include "vmsys.h"
#include "vmtimer.h"

extern "C" {
#include "fix_fft.h"
}

#define SCALE_WIDTH 0  // 30
#define LABEL_WIDTH 28
#define SCALE_BG VM_COLOR_BLUE

VMINT layer_hdl[2];
VMUINT8* layer_buf = 0;

VMINT screen_w = 0;
VMINT screen_h = 0;

static VMUINT32 total_bytes = 0;

enum ScaleMode { SCALE_NONE = 0, SCALE_FREQ, SCALE_DB };

static int scale_mode = SCALE_FREQ;

char msg[64];
static VMUINT32 last_time = 0;
int res;

vm_graphic_color color;

void handle_sysevt(VMINT message, VMINT param);  // system events
void handle_keyevt(VMINT event, VMINT keycode);  // key events
void tick(int tid);
void mic_handler(mic_event_t event);
void draw_scale();
static void draw_frequency_scale(void);
void redraw_scale();

void tick(int tid) {
    VMINT16 buf[256] = {};
    VMINT16 imag[256] = {};
    int bytes_read = mic_read((VMUINT8*)buf, sizeof(buf), 0);

    if (bytes_read < sizeof(buf)) return;

    total_bytes += bytes_read;

    VMUINT32 now = vm_get_tick_count();

    if (now - last_time >= 1000) {
        sprintf(msg, "bt=%lu smpl=%lu st=%d", total_bytes, total_bytes / 2, res);
        total_bytes = 0;
        last_time = now;
    }

    VMINT32 sum = 0;
    for (int i = 0; i < 256; i++) sum += buf[i];
    VMINT16 avg = sum / 256;
    for (int i = 0; i < 256; i++) buf[i] -= avg;

    fix_fft(buf, imag, 8, 0);

    if (screen_w <= SCALE_WIDTH + 1) return;

    memmove(layer_buf + SCALE_WIDTH * 2, layer_buf + (SCALE_WIDTH + 1) * 2, (screen_w - SCALE_WIDTH - 1) * screen_h * 2);

    VMUINT16* screen_buf16 = (VMUINT16*)layer_buf;

    if (screen_h <= 1) return;

    for (int y = 0; y < screen_h; y++) {
        int inverted_y = (screen_h - 1) - y;

        int fft_bin = (inverted_y * 127) / (screen_h - 1);

        int re = buf[fft_bin] < 0 ? -buf[fft_bin] : buf[fft_bin];
        int im = imag[fft_bin] < 0 ? -imag[fft_bin] : imag[fft_bin];

        int max = re > im ? re : im;
        int min = re < im ? re : im;
        int magnitude = max + (min >> 1);

        int db;

        if (magnitude < 1)
            db = 0;
        else {
            int tmp = magnitude;
            db = 0;

            while (tmp > 1) {
                tmp >>= 1;
                db++;
            }

            db *= 6;  // ~6 dB per bit
        }

        if (db > 60) db = 60;

        int intensity = db * 255 / 60;

        if (intensity > 255) intensity = 255;

        VMUINT16 color;
        if (intensity < 30) {
            color = VM_COLOR_BLACK;
        } else if (intensity < 128) {
            color = VM_COLOR_888_TO_565(0, intensity * 2, 0);
        } else {
            color = VM_COLOR_888_TO_565(intensity, 255 - intensity, 0);
        }

        screen_buf16[screen_w - 1 + y * screen_w] = color;
    }

//    if (scale_mode == SCALE_NONE)
//        vm_graphic_flush_layer(layer_hdl, 1); // only waterfall layer
//    else
//        vm_graphic_flush_layer(layer_hdl, 2); // waterfall + scale layer

if (scale_mode == SCALE_NONE)
    vm_graphic_flush_layer_ex(layer_hdl, 1, 0, 0, screen_w - 1, screen_h - 1);
else
    vm_graphic_flush_layer_ex(layer_hdl, 2, 0, 0, screen_w - 1, screen_h - 1);

}

void mic_handler(mic_event_t event) {}

void vm_main(void) {
    layer_hdl[0] = -1;
    screen_w = vm_graphic_get_screen_width();
    screen_h = vm_graphic_get_screen_height();

    layer_hdl[0] = vm_graphic_create_layer(0, 0, screen_w, screen_h, -1);
    layer_hdl[1] = vm_graphic_create_layer(0, 0, screen_w, screen_h, SCALE_BG);
    layer_buf = vm_graphic_get_layer_buffer(layer_hdl[0]);
    vm_graphic_set_clip(0, 0, screen_w, screen_h);
    vm_graphic_fill_rect(layer_buf, 0, 0, screen_w, screen_h, VM_COLOR_BLACK, VM_COLOR_BLACK);
    vm_graphic_set_font(VM_SMALL_FONT);

    redraw_scale();
    vm_graphic_flush_layer(layer_hdl, 2);

    vm_reg_sysevt_callback(handle_sysevt);
    vm_reg_keyboard_callback(handle_keyevt);
    last_time = vm_get_tick_count();  //---------
    mic_init();
    res = mic_start(MIC_FORMAT_PCM_16K, mic_handler);

//    vm_create_timer_ex(1000 / 30, tick); //30 FPS
    vm_create_timer_ex(40, tick); // 25 FPS
}

void handle_sysevt(VMINT message, VMINT param) {
#ifdef SUPPORT_BG
    switch (message) {
        case VM_MSG_CREATE:
            break;
        case VM_MSG_PAINT:
            vm_switch_power_saving_mode(turn_off_mode);
            layer_hdl[0] =
                vm_graphic_create_layer(0, 0, screen_w, screen_h, -1);
            layer_hdl[1] =
                vm_graphic_create_layer(0, 0, screen_w, screen_h, SCALE_BG);
            layer_buf = vm_graphic_get_layer_buffer(layer_hdl[0]);
            vm_graphic_set_clip(0, 0, screen_w, screen_h);
            redraw_scale();
            vm_graphic_flush_layer(layer_hdl, 2);
            break;
        case VM_MSG_HIDE:

            if (layer_hdl[1] != -1) {
                vm_graphic_delete_layer(layer_hdl[1]);
                layer_hdl[1] = -1;
            }

            if (layer_hdl[0] != -1) {
                vm_graphic_delete_layer(layer_hdl[0]);
                layer_hdl[0] = -1;
            }
            break;
        case VM_MSG_QUIT:

            if (layer_hdl[1] != -1) {
                vm_graphic_delete_layer(layer_hdl[1]);
                layer_hdl[1] = -1;
            }

            if (layer_hdl[0] != -1) {
                vm_graphic_delete_layer(layer_hdl[0]);
                layer_hdl[0] = -1;
            }
            mic_deinit();
            break;
    }
#else
    switch (message) {
        case VM_MSG_CREATE:
        case VM_MSG_ACTIVE:
            break;

        case VM_MSG_PAINT:
            vm_switch_power_saving_mode(turn_off_mode);
            break;

        case VM_MSG_INACTIVE:
            break;
        case VM_MSG_QUIT:
            mic_deinit();
            break;
    }
#endif
}

void handle_keyevt(VMINT event, VMINT keycode) {
    if (event == VM_KEY_EVENT_UP && keycode == VM_KEY_OK) {
        if (scale_mode == SCALE_FREQ)
            scale_mode = SCALE_NONE;
        else
            scale_mode = SCALE_FREQ;

        redraw_scale();
    }
}

void draw_scale() {
    color.vm_color_565 = VM_COLOR_WHITE;
    color.vm_color_888 = VM_COLOR_565_TO_888(VM_COLOR_WHITE);

    vm_graphic_setcolor(&color);

    VMUINT8* scale_buf = vm_graphic_get_layer_buffer(layer_hdl[1]);

    VMUINT16* buf16 = (VMUINT16*)scale_buf;

    for (int db = 0; db <= 50; db += 10) {
        int y = (screen_h - 1) - (db * (screen_h - 1) / 50);

        vm_graphic_line_ex(layer_hdl[1], LABEL_WIDTH, y, screen_w - 1, y);

        char txt[16];
        sprintf(txt, "%d", db);

        VMWCHAR ucs2[32];
        vm_ascii_to_ucs2(ucs2, 32, txt);

        vm_graphic_textout_to_layer(layer_hdl[1], 2, y - 6, (VMWSTR)ucs2, vm_graphic_get_string_width((VMWSTR)ucs2));
    }

    vm_graphic_flush_layer(layer_hdl, 2);
}

static void draw_frequency_scale(void) {
    vm_graphic_color color;

    color.vm_color_565 = VM_COLOR_WHITE;
    color.vm_color_888 = VM_COLOR_565_TO_888(VM_COLOR_WHITE);

    vm_graphic_setcolor(&color);

    VMUINT8* scale_buf = vm_graphic_get_layer_buffer(layer_hdl[1]);

    VMUINT16* buf16 = (VMUINT16*)scale_buf;

    for (int f = 0; f <= 8000; f += 2000) {
        int y = (screen_h - 1) - (f * (screen_h - 1) / 8000);

        vm_graphic_line_ex(layer_hdl[1], LABEL_WIDTH + 16, y, screen_w - 1, y);

        char txt[16];

        if (f == 0)
            sprintf(txt, "0Hz");
        else
            sprintf(txt, "%dkHz", f / 1000);

        VMWCHAR ucs2[32];
        vm_ascii_to_ucs2(ucs2, 32, txt);

        int text_y = y - 8;
        if (text_y < 0) text_y = 0;

        vm_graphic_textout_to_layer(layer_hdl[1], 2, text_y, (VMWSTR)ucs2, vm_graphic_get_string_width((VMWSTR)ucs2));
    }
}

void redraw_scale()
{
    vm_graphic_clear_layer_bg(layer_hdl[1]);

    if (scale_mode == SCALE_FREQ)
        draw_frequency_scale();

    if (scale_mode == SCALE_NONE)
        vm_graphic_flush_layer(layer_hdl, 1);
    else
        vm_graphic_flush_layer(layer_hdl, 2);
}
