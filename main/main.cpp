#include "vmsys.h"
#include "vmio.h"
#include "vmgraph.h"
#include "vmchset.h"
#include "vmstdlib.h"
#include "vmpromng.h"
#include "vmtimer.h"
#include "string.h"
#include <mic.h>

extern "C" {
#include "fix_fft.h"
}

#define SCALE_WIDTH 0 //30
#define LABEL_WIDTH 28
#define SCALE_BG VM_COLOR_BLUE

VMINT layer_hdl[2];
VMUINT8* layer_buf = 0;

VMINT screen_w = 0;
VMINT screen_h = 0;

static VMUINT32 total_bytes = 0;
//static VMUINT32 last_tick = 0;

enum ScaleMode
{
    SCALE_NONE = 0,
    SCALE_FREQ,
    SCALE_DB
};

static int scale_mode = SCALE_FREQ;

char msg[64];
static VMUINT32 last_time = 0;
int res;

vm_graphic_color color;

void handle_sysevt(VMINT message, VMINT param); // system events 
void handle_keyevt(VMINT event, VMINT keycode); // key events 
void tick(int tid);
void mic_handler(mic_event_t event);
static void draw_hello(void);
void draw_scale();
static void draw_frequency_scale(void);
void redraw_scale();

void tick(int tid) {
	VMINT16 buf[256] = {};
//        VMINT16 buf[1024];
	VMINT16 imag[256] = {};
//        VMINT16 imag[1024] = {};

//	int bytes_read = mic_read((VMUINT8*)buf, 256 * 2, 0);
        int bytes_read = mic_read((VMUINT8*)buf, sizeof(buf), 0);

if (bytes_read < sizeof(buf))
    return;

        total_bytes += bytes_read;

        VMUINT32 now = vm_get_tick_count();

        if (now - last_time >= 1000)
        {
//            char msg[64];

            sprintf(msg, "bt=%lu smpl=%lu st=%d", total_bytes, total_bytes / 2, res);

//            vm_log_debug(msg);     // or draw on screen
//            draw_hello();

            total_bytes = 0;
            last_time = now;
        }

//	if (bytes_read < 256 * 2) return;

	VMINT32 sum = 0;
	for (int i = 0; i < 256; i++) sum += buf[i];
	VMINT16 avg = sum / 256;
	for (int i = 0; i < 256; i++) buf[i] -= avg;

	fix_fft(buf, imag, 8, 0);

if (screen_w <= SCALE_WIDTH + 1)
    return;

//	memmove(layer_buf, layer_buf + 2, (screen_w * screen_h - 1) * 2);
        memmove(layer_buf + SCALE_WIDTH * 2, layer_buf + (SCALE_WIDTH + 1) * 2, (screen_w - SCALE_WIDTH - 1) * screen_h * 2);

	VMUINT16* screen_buf16 = (VMUINT16*)layer_buf;

if (screen_h <= 1)
    return;

	for (int y = 0; y < screen_h; y++) {

		int inverted_y = (screen_h - 1) - y;

//		int fft_bin = (inverted_y * 128) / screen_h;
                int fft_bin = (inverted_y * 127) / (screen_h - 1);

		int re = buf[fft_bin] < 0 ? -buf[fft_bin] : buf[fft_bin];
		int im = imag[fft_bin] < 0 ? -imag[fft_bin] : imag[fft_bin];

		int max = re > im ? re : im;
		int min = re < im ? re : im;
		int magnitude = max + (min >> 1);

//		int intensity = magnitude << 4;


int db;

if (magnitude < 1)
    db = 0;
else
{
    int tmp = magnitude;
    db = 0;

    while (tmp > 1)
    {
        tmp >>= 1;
        db++;
    }

    db *= 6;     // ~6 dB per bit
}

if (db > 60)
    db = 60;

int intensity = db * 255 / 60;




		if (intensity > 255) intensity = 255;

		VMUINT16 color;
		if (intensity < 30) {
			color = VM_COLOR_BLACK; 
		}
		else if (intensity < 128) {
			color = VM_COLOR_888_TO_565(0, intensity * 2, 0);
		}
		else {
			color = VM_COLOR_888_TO_565(intensity, 255 - intensity, 0);
		}

		screen_buf16[screen_w - 1 + y * screen_w] = color;
	}

//	vm_graphic_flush_layer(layer_hdl, 1);
        vm_graphic_flush_layer(layer_hdl, 2);
}

void mic_handler(mic_event_t event) {}

void vm_main(void) {
	layer_hdl[0] = -1;
	screen_w = vm_graphic_get_screen_width();
	screen_h = vm_graphic_get_screen_height();

	layer_hdl[0] = vm_graphic_create_layer(0, 0, screen_w, screen_h, -1);
//	layer_hdl[1] = vm_graphic_create_layer(0, 0, screen_w, screen_h, -1);
        layer_hdl[1] = vm_graphic_create_layer(0, 0, screen_w, screen_h, SCALE_BG);
//vm_graphic_set_alpha_blending_layer(layer_hdl[1]);
//vm_graphic_set_layer_opacity(layer_hdl[1], 100);
	layer_buf = vm_graphic_get_layer_buffer(layer_hdl[0]);
	vm_graphic_set_clip(0, 0, screen_w, screen_h);
        vm_graphic_fill_rect(layer_buf, 0, 0, screen_w, screen_h, VM_COLOR_BLACK, VM_COLOR_BLACK);
	vm_graphic_set_font(VM_SMALL_FONT);


        redraw_scale();
        vm_graphic_flush_layer(layer_hdl, 2);

	vm_reg_sysevt_callback(handle_sysevt);
	vm_reg_keyboard_callback(handle_keyevt);
//	vm_reg_pen_callback(handle_penevt);
        last_time = vm_get_tick_count(); //---------
	mic_init();
//	mic_start(MIC_FORMAT_PCM_8K, mic_handler);
        res = mic_start(MIC_FORMAT_PCM_16K, mic_handler);

	vm_create_timer_ex(1000/30, tick);
}

void handle_sysevt(VMINT message, VMINT param) {
#ifdef		SUPPORT_BG
	switch (message) {
	case VM_MSG_CREATE:
		break;
	case VM_MSG_PAINT:
                vm_switch_power_saving_mode(turn_off_mode);
		layer_hdl[0] = vm_graphic_create_layer(0, 0, screen_w, screen_h, -1);
//                layer_hdl[1] = vm_graphic_create_layer(0, 0, screen_w, screen_h, -1);
                layer_hdl[1] = vm_graphic_create_layer(0, 0, screen_w, screen_h, SCALE_BG);
//                vm_graphic_set_alpha_blending_layer(layer_hdl[1]);
//                vm_graphic_set_layer_opacity(layer_hdl[1], 100);
		layer_buf = vm_graphic_get_layer_buffer(layer_hdl[0]);
                vm_graphic_set_clip(0, 0, screen_w, screen_h);
//                vm_graphic_fill_rect(layer_buf, 0, 0, screen_w, screen_h, VM_COLOR_BLACK, VM_COLOR_BLACK);

                redraw_scale();
                vm_graphic_flush_layer(layer_hdl, 2);
		break;
	case VM_MSG_HIDE:

                if (layer_hdl[1] != -1)
                {
                        vm_graphic_delete_layer(layer_hdl[1]);
                        layer_hdl[1] = -1;
                }

                if( layer_hdl[0] != -1 )
		{
			vm_graphic_delete_layer(layer_hdl[0]);
			layer_hdl[0] = -1;
		}
		break;
	case VM_MSG_QUIT:

                if (layer_hdl[1] != -1)
                {
                        vm_graphic_delete_layer(layer_hdl[1]);
                        layer_hdl[1] = -1;
                }

		if( layer_hdl[0] != -1 )
		{
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

//   if (event == VM_KEY_EVENT_UP && keycode == VM_KEY_RIGHT_SOFTKEY) {

//	if (layer_hdl[1] != -1) {
//	    vm_graphic_delete_layer(layer_hdl[1]);
//            layer_hdl[1] = -1;
//        }
//	if (layer_hdl[0] != -1) {
//	    vm_graphic_delete_layer(layer_hdl[0]);
//            layer_hdl[0] = -1;
//        }
//        mic_deinit();
//        vm_exit_app();
//    }

    if (event == VM_KEY_EVENT_UP && keycode == VM_KEY_OK) {

//    scale_mode++;

//    if(scale_mode > SCALE_DB)
//        scale_mode = SCALE_NONE;

//    redraw_scale();

    if (scale_mode == SCALE_FREQ)
        scale_mode = SCALE_NONE;
    else
        scale_mode = SCALE_FREQ;

    redraw_scale();

    }

}

static void draw_hello(void) {

//        VMCHAR myText[100] = "Hello World !";
	VMWSTR s;
        VMWCHAR display_string[129];
	int x;
	int y;
	int wstr_len;
//	vm_graphic_color color;

//        vm_ascii_to_ucs2(display_string, 100, myText);
        vm_ascii_to_ucs2(display_string, 129, msg);
	s = (VMWSTR)display_string;
 	wstr_len = vm_graphic_get_string_width(s);
	x = (vm_graphic_get_screen_width() - wstr_len) / 2;
	y = (vm_graphic_get_screen_height() - vm_graphic_get_character_height()) / 2;
	color.vm_color_565 = VM_COLOR_WHITE;
	vm_graphic_setcolor(&color);
	vm_graphic_textout_to_layer(layer_hdl[0],x, y, s, wstr_len);
	vm_graphic_flush_layer(layer_hdl, 2);
}

void draw_scale()
{

//    vm_graphic_color color;

    color.vm_color_565 = VM_COLOR_WHITE;
    color.vm_color_888 = VM_COLOR_565_TO_888(VM_COLOR_WHITE);

    vm_graphic_setcolor(&color);

    VMUINT8 *scale_buf = vm_graphic_get_layer_buffer(layer_hdl[1]);

//    vm_graphic_fill_rect(scale_buf, 0, 0, screen_w, screen_h, VM_COLOR_TRANSPARENT, VM_COLOR_TRANSPARENT);
//    vm_graphic_fill_rect(scale_buf, 0, 0, screen_w, screen_h, VM_COLOR_BLACK, VM_COLOR_BLACK);

//vm_graphic_clear_layer_bg(layer_hdl[1]);

    VMUINT16 *buf16 = (VMUINT16*)scale_buf;

    for (int db = 0; db <= 50; db += 10)
    {
//        int y = (screen_h - 1) - (db * screen_h / 50);
        int y = (screen_h - 1) - (db * (screen_h - 1) / 50);

//        for (int x = 0; x < screen_w; x++)
//        {
//            buf16[y * screen_w + x] = VM_COLOR_888_TO_565(40, 40, 40);
//        }

//for (int x = SCALE_WIDTH; x < screen_w; x++)
//{
//    buf16[y * screen_w + x] = VM_COLOR_888_TO_565(40, 40, 40);
//}

//vm_graphic_fill_rect_ex(layer_hdl[1], SCALE_WIDTH, y - 1, screen_w - SCALE_WIDTH, 2);
//vm_graphic_line_ex(layer_hdl[1], SCALE_WIDTH, y, screen_w - 1, y);
vm_graphic_line_ex(layer_hdl[1], LABEL_WIDTH, y, screen_w - 1, y);

//for (int dy = -1; dy <= 1; dy++)
//{
//    int yy = y + dy;

//    if (yy < 0 || yy >= screen_h)
//        continue;

//    for (int x = SCALE_WIDTH; x < screen_w; x++)
//    {
//        buf16[yy * screen_w + x] = VM_COLOR_888_TO_565(80, 80, 80);
//    }
//}

        char txt[16];
        sprintf(txt, "%d", db);

        VMWCHAR ucs2[32];
        vm_ascii_to_ucs2(ucs2, 32, txt);

        vm_graphic_textout_to_layer(layer_hdl[1], 2, y - 6, (VMWSTR)ucs2, vm_graphic_get_string_width((VMWSTR)ucs2));
    }

    vm_graphic_flush_layer(layer_hdl, 2);
}

static void draw_frequency_scale(void)
{

    vm_graphic_color color;

    color.vm_color_565 = VM_COLOR_WHITE;
    color.vm_color_888 = VM_COLOR_565_TO_888(VM_COLOR_WHITE);

    vm_graphic_setcolor(&color);

//    vm_graphic_fill_rect(scale_buf, 0, 0, screen_w, screen_h, VM_COLOR_TRANSPARENT, VM_COLOR_TRANSPARENT);

//    VMUINT8 *scale_buf = vm_graphic_get_layer_buffer(layer_hdl[1]);

    VMUINT8 *scale_buf = vm_graphic_get_layer_buffer(layer_hdl[1]);

//    vm_graphic_fill_rect(scale_buf, 0, 0, screen_w, screen_h, VM_COLOR_TRANSPARENT, VM_COLOR_TRANSPARENT);
//    vm_graphic_fill_rect(scale_buf, 0, 0, screen_w, screen_h, VM_COLOR_BLACK, VM_COLOR_BLACK);

//vm_graphic_clear_layer_bg(layer_hdl[1]);

    VMUINT16 *buf16 = (VMUINT16*)scale_buf;


    for (int f = 0; f <= 8000; f += 2000)
    {
//        int y = screen_h - 1 - (f * screen_h / 8000);
        int y = (screen_h - 1) - (f * (screen_h - 1) / 8000);

////    for (int x = 0; x < screen_w; x++)
//    for (int x = SCALE_WIDTH; x < screen_w; x++)
//    {
//        buf16[y * screen_w + x] =
//            VM_COLOR_888_TO_565(40, 40, 40);
//    }

//vm_graphic_fill_rect_ex(layer_hdl[1], SCALE_WIDTH, y - 1, screen_w - SCALE_WIDTH, 2);
//vm_graphic_line_ex(layer_hdl[1], SCALE_WIDTH, y , screen_w - 1, y);
//vm_graphic_line_ex(layer_hdl[1], SCALE_WIDTH + 8, y, screen_w - SCALE_WIDTH - 8, y);
vm_graphic_line_ex(layer_hdl[1], LABEL_WIDTH + 16, y, screen_w - 1, y);

//for (int dy = -1; dy <= 1; dy++)
//{
//    int yy = y + dy;

//    if (yy < 0 || yy >= screen_h)
//        continue;

//    for (int x = SCALE_WIDTH; x < screen_w; x++)
//    {
//        buf16[yy * screen_w + x] = VM_COLOR_888_TO_565(80, 80, 80);
//    }
//}

        char txt[16];
//        sprintf(txt, "%dk", f / 1000);
//        sprintf(txt, "%d kHz", f / 1000);

if (f == 0)
    sprintf(txt, "0Hz");
else
    sprintf(txt, "%dkHz", f / 1000);

        VMWCHAR ucs2[32];
        vm_ascii_to_ucs2(ucs2, 32, txt);

//        vm_graphic_textout_to_layer(layer_hdl[1], 2, y - 8, (VMWSTR)ucs2, vm_graphic_get_string_width((VMWSTR)ucs2));

int text_y = y - 8;
if (text_y < 0)
    text_y = 0;

vm_graphic_textout_to_layer(layer_hdl[1], 2, text_y, (VMWSTR)ucs2, vm_graphic_get_string_width((VMWSTR)ucs2));


    }
}

//void redraw_scale()
//{
//    VMUINT8 *buf = vm_graphic_get_layer_buffer(layer_hdl[1]);

////    vm_graphic_fill_rect(buf, 0, 0, screen_w, screen_h, VM_COLOR_BLACK, VM_COLOR_BLACK);

//    switch(scale_mode)
//    {
//        case SCALE_FREQ:
//            draw_frequency_scale();
//            break;

//        case SCALE_DB:
//            draw_scale();
//            break;

//        case SCALE_NONE:
//            break;
//    }

//    vm_graphic_flush_layer(layer_hdl, 2);
//}

void redraw_scale()
{
    vm_graphic_clear_layer_bg(layer_hdl[1]);

    if (scale_mode == SCALE_FREQ)
        draw_frequency_scale();
//    else if (scale_mode == SCALE_DB)
//        draw_scale();

    vm_graphic_flush_layer(layer_hdl, 2);
}

