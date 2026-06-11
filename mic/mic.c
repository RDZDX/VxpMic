#include "mic.h"
#include "string.h"

typedef enum {
    MEDIA_FORMAT_GSM_FR,
    MEDIA_FORMAT_GSM_HR,
    MEDIA_FORMAT_GSM_EFR,
    MEDIA_FORMAT_AMR,
    MEDIA_FORMAT_AMR_WB,
    MEDIA_FORMAT_DAF,
    MEDIA_FORMAT_AAC,
    MEDIA_FORMAT_PCM_8K,
    MEDIA_FORMAT_PCM_16K,
    MEDIA_FORMAT_G711_ALAW,
    MEDIA_FORMAT_G711_ULAW,
    MEDIA_FORMAT_DVI_ADPCM,
    MEDIA_FORMAT_VRD,
    MEDIA_FORMAT_WAV,
    MEDIA_FORMAT_WAV_ALAW,
    MEDIA_FORMAT_WAV_ULAW,
    MEDIA_FORMAT_WAV_DVI_ADPCM,
    MEDIA_FORMAT_SMF,
    MEDIA_FORMAT_IMELODY,
    MEDIA_FORMAT_SMF_SND,
    MEDIA_FORMAT_MMF,
    MEDIA_FORMAT_AU,
    MEDIA_FORMAT_AIFF,
    MEDIA_FORMAT_VRSI,
    MEDIA_FORMAT_WMA,
    MEDIA_FORMAT_M4A,
    MEDIA_FORMAT_WAV_DVI_ADPCM_16K,
    MEDIA_FORMAT_VOIPEVL,
    MEDIA_FORMAT_AAC_PLUS,
    MEDIA_FORMAT_AAC_PLUS_V2,
    MEDIA_FORMAT_BSAC,
    MEDIA_FORMAT_MUSICAM = 32,
    MEDIA_FORMAT_AWB_PLUS,
    MEDIA_FORMAT_AWB_PLUS_EXTEND,
    MEDIA_FORMAT_WAV_16K,
    MEDIA_FORMAT_MP4_AAC,
    MEDIA_FORMAT_MP4_AMR,
    MEDIA_FORMAT_MP4_AMR_WB,
    MEDIA_FORMAT_MP4_BSAC,
    MEDIA_FORMAT_DRA,
    MEDIA_FORMAT_COOK,
    MEDIA_FORMAT_APE,
    MEDIA_FORMAT_PCM,
    MEDIA_FORMAT_JTS,
    MEDIA_FORMAT_VORBIS,
    MEDIA_FORMAT_FLAC,
    MEDIA_FORMAT_MIXER = 100,
    MEDIA_FORMAT_UNKNOWN
} Media_Format;

typedef enum {
    MEDIA_NONE,
    MEDIA_DATA_REQUEST,
    MEDIA_DATA_NOTIFICATION,
    MEDIA_END,
    MEDIA_ERROR,
    MEDIA_DECODER_UNSUPPORT,
    MEDIA_REPEATED,
    MEDIA_TERMINATED,
    MEDIA_LED_ON,
    MEDIA_LED_OFF,
    MEDIA_VIBRATOR_ON,
    MEDIA_VIBRATOR_OFF,
    MEDIA_BACKLIGHT_ON,
    MEDIA_BACKLIGHT_OFF,
    MEDIA_EXTENDED_EVENT,
    MEDIA_READ_ERROR,
    MEDIA_UPDATE_DUR,
    MEDIA_STOP_TIME_UP,
    MEDIA_DEMO_TIME_UP,
    MEDIA_BUFFER_UNDERFLOW,
    MEDIA_READY_TO_PLAY,
    MEDIA_DATA_REFILL
} Media_Event;

typedef enum {
    MEDIA_SUCCESS = 200,
    MEDIA_FAIL,
    MEDIA_REENTRY,
    MEDIA_NOT_INITIALIZED,
    MEDIA_BAD_FORMAT,
    MEDIA_BAD_PARAMETER,
    MEDIA_BAD_COMMAND,
    MEDIA_NO_HANDLER,
    MEDIA_UNSUPPORTED_CHANNEL,
    MEDIA_UNSUPPORTED_FREQ,
    MEDIA_UNSUPPORTED_TYPE,
    MEDIA_UNSUPPORTED_OPERATION,
    MEDIA_SEEK_FAIL,
    MEDIA_SEEK_EOF,
    MEDIA_READ_FAIL,
    MEDIA_WRITE_FAIL,
    MEDIA_DISK_FULL,
    MEDIA_MERGE_TYPE_MISMATCH,
    MEDIA_FILE_INCOMPLETE
} Media_Status;

typedef VMINT(*vm_get_sym_entry_t)(char* symbol);
extern vm_get_sym_entry_t vm_get_sym_entry;

typedef void (*mic_handler)(Media_Event event);

static VMUINT32 BT_PcmLoopbackTestStop = 0;

typedef void (*Media_SetBuffer_t)(VMUINT16* buffer, VMUINT32 buf_len);
static Media_SetBuffer_t Media_SetBuffer = 0x00000000;

typedef void (*Media_GetReadBuffer_t)(VMUINT16** buffer, VMUINT32* buf_len);
static Media_GetReadBuffer_t Media_GetReadBuffer = 0x00000000;

typedef Media_Status (*Media_Record_t)(Media_Format format, mic_handler handler, void* param);
static Media_Record_t Media_Record = 0x00000000;

typedef void (*Media_ReadDataDone_t)(VMUINT32 len);
static Media_ReadDataDone_t Media_ReadDataDone = 0x00000000;

typedef void (*Media_Stop_t)();
static Media_Stop_t Media_Stop = 0x00000000;

static VMUINT8* ring_buf = 0;
static const int RING_BUFFER_SIZE = 4 * 1024;

static char api_ready = 0;

static mic_handler_t g_user_handler = 0;

// Scaning phone firmare for BT_PcmLoopbackTestStop entry point by magic value. Warning: May cause fatal error (reboot) after activation
static void inject_test_func() {
    const int magic_len = 12;
    const unsigned char BT_PcmLoopbackTestStop_magic[] = { 0xF0, 0xB5, 0x00, 0x25, 0x87, 0xB0, 0x2C, 0x00, 0x02, 0x20, 0x05, 0x95 };

    for (int i = 0; i < 0x1000000; i += 4) {
        unsigned char* adr = (((unsigned int)vm_get_sym_entry) & (0xFF000000)) + i;

        if (!memcmp(adr, BT_PcmLoopbackTestStop_magic, magic_len)) {
            BT_PcmLoopbackTestStop = ((VMUINT32)adr);
            break;
        }
    }
}

static VMUINT32 resolve_bl_target(VMUINT32 bl_addr) {
    VMUINT16 hw1 = *(VMUINT16*)bl_addr;
    VMUINT16 hw2 = *(VMUINT16*)(bl_addr + 2);
    VMINT32 offset = (hw1 & 0x7FF) << 12;
    if (hw1 & 0x400) offset |= 0xFF800000;
    offset |= (hw2 & 0x7FF) << 1;
    VMUINT32 target_addr = (bl_addr + 4) + offset;

    if ((hw2 & 0xF800) == 0xE800) {
        target_addr &= ~3; // BLX
    }
    else {
        target_addr |= 1;  // BL
    }

    if ((target_addr & 1) == 0) {
        VMUINT32 arm_instr = *(VMUINT32*)target_addr;
        if (arm_instr == 0xE51FF004) return *(VMUINT32*)(target_addr + 4);
        if (arm_instr == 0xE59FF000) return *(VMUINT32*)(target_addr + 8);
    }
    return target_addr;
}

static void resolve_audio_api() {
    if (BT_PcmLoopbackTestStop == 0) return;

    VMUINT32 adr = BT_PcmLoopbackTestStop;

    for (int i = 0; i < 256; i += 2) {
        VMUINT16 instr = *(VMUINT16*)(adr + i);
        VMUINT16 instr2 = *(VMUINT16*)(adr + i + 2);

        if (instr == 0x0039 && instr2 == 0x9001 && Media_SetBuffer == 0) {
            VMUINT16 call_instr = *(VMUINT16*)(adr + i + 4);
            if ((call_instr & 0xF800) == 0xF000) {
                Media_SetBuffer = (Media_SetBuffer_t)resolve_bl_target(adr + i + 4);
            }
        }

        if (instr == 0x2200 && instr2 == 0x2007 && Media_Record == 0) {
            VMUINT16 call_instr = *(VMUINT16*)(adr + i + 4);
            if ((call_instr & 0xF800) == 0xF000) {
                Media_Record = (Media_Record_t)resolve_bl_target(adr + i + 4);
            }
        }

        if (instr == 0xA905 && instr2 == 0xA806 && Media_GetReadBuffer == 0) {
            VMUINT16 call_instr = *(VMUINT16*)(adr + i + 4);
            if ((call_instr & 0xF800) == 0xF000) {
                Media_GetReadBuffer = (Media_GetReadBuffer_t)resolve_bl_target(adr + i + 4);
            }
        }

        if (instr == 0x9805 && instr2 == 0x1824 && Media_ReadDataDone == 0) {
            VMUINT16 call_instr = *(VMUINT16*)(adr + i - 4);
            if ((call_instr & 0xF800) == 0xF000) {
                Media_ReadDataDone = (Media_ReadDataDone_t)resolve_bl_target(adr + i - 4);
            }
        }

        if (instr == 0x2200 && instr2 == 0x9801 && Media_Stop == 0) {
            VMUINT16 call_instr = *(VMUINT16*)(adr + i - 8);
            if ((call_instr & 0xF800) == 0xF000) {
                Media_Stop = (Media_Stop_t)resolve_bl_target(adr + i - 8);
            }
        }
    }
}

static void injector() {
#ifndef WIN32
    inject_test_func();
    resolve_audio_api();
#endif // !WIN32
}

static void find_api() {
    Media_SetBuffer = (Media_SetBuffer_t)vm_get_sym_entry("mremu_media_setbufer");
    Media_Record = (Media_Record_t)vm_get_sym_entry("mremu_media_record");
    Media_GetReadBuffer = (Media_GetReadBuffer_t)vm_get_sym_entry("mremu_media_getreadbuffer");
    Media_ReadDataDone = (Media_ReadDataDone_t)vm_get_sym_entry("mremu_media_readdatadone");
    Media_Stop = (Media_Stop_t)vm_get_sym_entry("mremu_media_stop");
}

static int test_api() {
    return Media_SetBuffer && Media_Record && Media_GetReadBuffer && Media_ReadDataDone && Media_Stop;
}

int mic_init() {
    find_api();
    if(!test_api())
        injector();
    if (!(api_ready = test_api()))
        return 0;

    if(!ring_buf)
        ring_buf = (VMUINT8*)vm_malloc(RING_BUFFER_SIZE / 2);

    if (!ring_buf) {
        return 1;
    }

    Media_SetBuffer(ring_buf, RING_BUFFER_SIZE);

    return 0;
}

static void internal_media_handler(Media_Event event) {
    if (!g_user_handler) return;

    switch (event) {
    case MEDIA_DATA_NOTIFICATION:
        g_user_handler(MIC_EVENT_DATA_READY);
        break;
    case MEDIA_STOP_TIME_UP:
    case MEDIA_TERMINATED:
        g_user_handler(MIC_EVENT_STOPPED);
        break;
    case MEDIA_ERROR:
    case MEDIA_READ_ERROR:
        g_user_handler(MIC_EVENT_ERROR);
        break;
    default:
        break;
    }
}

int mic_start(mic_format_t format, mic_handler_t handler) {
    if (!api_ready)
        return 1;

    g_user_handler = handler;
    Media_Format hw_format;

    switch (format) {
    case MIC_FORMAT_PCM_8K:  hw_format = MEDIA_FORMAT_PCM_8K; break;
    case MIC_FORMAT_PCM_16K: hw_format = MEDIA_FORMAT_PCM_16K; break;
    case MIC_FORMAT_AMR_NB:  hw_format = MEDIA_FORMAT_AMR; break;
    default: return -1;
    }

    Media_Status res = Media_Record(hw_format, internal_media_handler, (void*)0);

    return MEDIA_SUCCESS != res;
}

VMUINT32 mic_read(VMUINT8* buf, VMUINT32 buf_size, VMUINT32* readed) {
    if (!api_ready)
        return;

    VMUINT32 total_bytes_read = 0;
    VMUINT32 bytes_needed = buf_size;

    for (int i = 0; i < 2; ++i) {
        VMUINT16* mic_buf = 0;
        VMUINT32 mic_buf_len_words = 0;

        Media_GetReadBuffer(&mic_buf, &mic_buf_len_words);

        if (!mic_buf || mic_buf_len_words == 0) {
            break;
        }

        VMUINT32 available_bytes = mic_buf_len_words * 2;
        VMUINT32 bytes_to_copy = bytes_needed;

        if (bytes_to_copy > available_bytes)
            bytes_to_copy = available_bytes;

        memcpy(buf + total_bytes_read, mic_buf, bytes_to_copy);

        Media_ReadDataDone(bytes_to_copy / 2);

        total_bytes_read += bytes_to_copy;
        bytes_needed -= bytes_to_copy;
    }

    if (readed)
        *readed = total_bytes_read;

    return total_bytes_read;
}

void mic_stop() {
    if (!api_ready)
        return;

    Media_Stop();
}

void mic_deinit() {
    mic_stop();

    if (ring_buf) {
        vm_free(ring_buf);
        ring_buf = 0;
    }
}