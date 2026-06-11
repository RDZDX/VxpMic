#include "vmsys.h"
#include "vmio.h"
#include "vmgraph.h"
#include "vmchset.h"
#include "vmstdlib.h"
#include "vm4res.h"
#include "vmres.h"
#include "vmpromng.h"
#include "vmtimer.h"
#include <mic.h>

VMINT		layer_hdl[1];	// layer handle array. 
VMUINT8* layer_buf = 0;

VMINT screen_w = 0;
VMINT screen_h = 0;

void handle_sysevt(VMINT message, VMINT param); // system events 
void handle_keyevt(VMINT event, VMINT keycode); // key events 
void handle_penevt(VMINT event, VMINT x, VMINT y); // pen events

void tick(int tid) {
	vm_graphic_color color;

	VMINT16 buf[1024] = {};
	int samples =  mic_read((VMUINT8*)buf, 1024 * 2, 0) / 2;

	static int smooth_volume = 0;

	int max_amp = 0;
	for (int i = 0; i < samples; i++) {
		int val = buf[i];
		if (val < 0) {
			val = -val;
		}
		if (val > max_amp) {
			max_amp = val;
		}
	}

	int current_volume = (max_amp * 100) / 32768;

	if (current_volume > smooth_volume) {
		smooth_volume = current_volume;
	}
	else {
		smooth_volume -= 4;
		if (smooth_volume < 0) {
			smooth_volume = 0;
		}
	}

	vm_graphic_fill_rect(layer_buf, 0, 0, screen_w, screen_h, VM_COLOR_WHITE, VM_COLOR_WHITE);

	int bar_max_height = screen_h - 60;
	int bar_width = 40;
	int bar_x = (screen_w - bar_width) / 2;
	int bar_bottom_y = screen_h - 30;

	int fill_height = (smooth_volume * bar_max_height) / 100;

	VMUINT16 frame_color = VM_COLOR_888_TO_565(200, 200, 200);

	vm_graphic_rect(layer_buf,
		bar_x - 1,
		bar_bottom_y - bar_max_height - 1,
		bar_width + 2,
		bar_max_height + 2,
		frame_color);

	vm_graphic_fill_rect(layer_buf, bar_x, bar_bottom_y - fill_height, bar_width, fill_height, VM_COLOR_BLUE, VM_COLOR_BLUE);

	vm_graphic_flush_layer(layer_hdl, 1);
}

void mic_handler(mic_event_t event) {}

void vm_main(void) {
	layer_hdl[0] = -1;
	screen_w = vm_graphic_get_screen_width();
	screen_h = vm_graphic_get_screen_height();

	layer_hdl[0] = vm_graphic_create_layer(0, 0, screen_w, screen_h, -1);
	layer_buf = vm_graphic_get_layer_buffer(layer_hdl[0]);
	vm_graphic_set_clip(0, 0, screen_w, screen_h);

	vm_graphic_set_font(VM_SMALL_FONT);
	
	vm_reg_sysevt_callback(handle_sysevt);
	vm_reg_keyboard_callback(handle_keyevt);
	vm_reg_pen_callback(handle_penevt);

	mic_init();
	mic_start(MIC_FORMAT_PCM_8K, mic_handler);

	vm_create_timer_ex(1000/30, tick);
}

void handle_sysevt(VMINT message, VMINT param) {
#ifdef		SUPPORT_BG
	switch (message) {
	case VM_MSG_CREATE:
		break;
	case VM_MSG_PAINT:
		layer_hdl[0] = vm_graphic_create_layer(0, 0, screen_w, screen_h, -1);

		layer_buf = vm_graphic_get_layer_buffer(layer_hdl[0]);
		
		vm_graphic_set_clip(0, 0, screen_w, screen_h);
		
		draw_hello();
		break;
	case VM_MSG_HIDE:	
		if( layer_hdl[0] != -1 )
		{
			vm_graphic_delete_layer(layer_hdl[0]);
			layer_hdl[0] = -1;
		}
		break;
	case VM_MSG_QUIT:
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
}

void handle_penevt(VMINT event, VMINT x, VMINT y) {
}