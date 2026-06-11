#pragma once
#ifdef __cplusplus
extern "C" {
#endif

#include <vmsys.h>

typedef enum {
    MIC_FORMAT_PCM_8K,
    MIC_FORMAT_PCM_16K,
    MIC_FORMAT_AMR_NB
} mic_format_t;

typedef enum {
    MIC_EVENT_DATA_READY,
    MIC_EVENT_STOPPED,
    MIC_EVENT_ERROR
} mic_event_t;

typedef void (*mic_handler_t)(mic_event_t event);

int mic_init();
int mic_start(mic_format_t format, mic_handler_t handler);
VMUINT32 mic_read(VMUINT8* buf, VMUINT32 buf_size, VMUINT32* readed);
void mic_stop();
void mic_deinit();

#ifdef __cplusplus
}
#endif