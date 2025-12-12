#ifndef AC97_H
#define AC97_H

#include <stdint.h>
#include <stddef.h>

// AC'97 PCI Configuration
#define AC97_VENDOR_INTEL       0x8086
#define AC97_DEVICE_ICH         0x2415
#define AC97_DEVICE_ICH0        0x2425
#define AC97_DEVICE_ICH2        0x2445
#define AC97_DEVICE_ICH3        0x2485
#define AC97_DEVICE_ICH4        0x24C5
#define AC97_DEVICE_ICH5        0x24D5
#define AC97_DEVICE_ICH6        0x266E
#define AC97_DEVICE_ICH7        0x27DE

// Native Audio Mixer (NAM) Registers - BAR0
#define AC97_NAM_RESET              0x00
#define AC97_NAM_MASTER_VOLUME      0x02
#define AC97_NAM_AUX_OUT_VOLUME     0x04
#define AC97_NAM_MONO_VOLUME        0x06
#define AC97_NAM_MASTER_TONE        0x08
#define AC97_NAM_PC_BEEP_VOLUME     0x0A
#define AC97_NAM_PHONE_VOLUME       0x0C
#define AC97_NAM_MIC_VOLUME         0x0E
#define AC97_NAM_LINE_IN_VOLUME     0x10
#define AC97_NAM_CD_VOLUME          0x12
#define AC97_NAM_VIDEO_VOLUME       0x14
#define AC97_NAM_AUX_IN_VOLUME      0x16
#define AC97_NAM_PCM_OUT_VOLUME     0x18
#define AC97_NAM_RECORD_SELECT      0x1A
#define AC97_NAM_RECORD_GAIN        0x1C
#define AC97_NAM_RECORD_GAIN_MIC    0x1E
#define AC97_NAM_GENERAL_PURPOSE    0x20
#define AC97_NAM_3D_CONTROL         0x22
#define AC97_NAM_AUDIO_INT_PAGING   0x24
#define AC97_NAM_POWERDOWN_CTRL     0x26
#define AC97_NAM_EXT_AUDIO_ID       0x28
#define AC97_NAM_EXT_AUDIO_STATUS   0x2A
#define AC97_NAM_PCM_FRONT_DAC_RATE 0x2C
#define AC97_NAM_PCM_SURR_DAC_RATE  0x2E
#define AC97_NAM_PCM_LFE_DAC_RATE   0x30
#define AC97_NAM_PCM_LR_ADC_RATE    0x32
#define AC97_NAM_PCM_MIC_ADC_RATE   0x34
#define AC97_NAM_VENDOR_ID1         0x7C
#define AC97_NAM_VENDOR_ID2         0x7E

// Native Audio Bus Master (NABM) Registers - BAR1
// PCM Out (Playback)
#define AC97_PO_BDBAR               0x10  // Buffer Descriptor Base Address
#define AC97_PO_CIV                 0x14  // Current Index Value
#define AC97_PO_LVI                 0x15  // Last Valid Index
#define AC97_PO_SR                  0x16  // Status Register
#define AC97_PO_PICB                0x18  // Position In Current Buffer
#define AC97_PO_PIV                 0x1A  // Prefetched Index Value
#define AC97_PO_CR                  0x1B  // Control Register

// PCM In (Recording)
#define AC97_PI_BDBAR               0x00
#define AC97_PI_CIV                 0x04
#define AC97_PI_LVI                 0x05
#define AC97_PI_SR                  0x06
#define AC97_PI_PICB                0x08
#define AC97_PI_PIV                 0x0A
#define AC97_PI_CR                  0x0B

// Microphone In
#define AC97_MC_BDBAR               0x20
#define AC97_MC_CIV                 0x24
#define AC97_MC_LVI                 0x25
#define AC97_MC_SR                  0x26
#define AC97_MC_PICB                0x28
#define AC97_MC_PIV                 0x2A
#define AC97_MC_CR                  0x2B

// Global Control
#define AC97_GLOB_CNT               0x2C
#define AC97_GLOB_STA               0x30

// Control Register Bits
#define AC97_CR_RPBM                0x01  // Run/Pause Bus Master
#define AC97_CR_RR                  0x02  // Reset Registers
#define AC97_CR_LVBIE               0x04  // Last Valid Buffer Interrupt Enable
#define AC97_CR_FEIE                0x08  // FIFO Error Interrupt Enable
#define AC97_CR_IOCE                0x10  // Interrupt On Completion Enable

// Status Register Bits
#define AC97_SR_DCH                 0x01  // DMA Controller Halted
#define AC97_SR_CELV                0x02  // Current Equals Last Valid
#define AC97_SR_LVBCI               0x04  // Last Valid Buffer Completion Interrupt
#define AC97_SR_BCIS                0x08  // Buffer Completion Interrupt Status
#define AC97_SR_FIFOE               0x10  // FIFO Error

// Global Control Bits
#define AC97_GLOB_CNT_GIE           0x00000001  // Global Interrupt Enable
#define AC97_GLOB_CNT_COLD_RESET    0x00000002  // Cold Reset
#define AC97_GLOB_CNT_WARM_RESET    0x00000004  // Warm Reset
#define AC97_GLOB_CNT_SHUT_DOWN     0x00000008  // Shut Down
#define AC97_GLOB_CNT_PCM_246       0x00000000  // 2/4/6 PCM Out channels
#define AC97_GLOB_CNT_PCM_4         0x00100000  // 4 channel mode
#define AC97_GLOB_CNT_PCM_6         0x00200000  // 6 channel mode

// Global Status Bits
#define AC97_GLOB_STA_CODEC_READY   0x00000100
#define AC97_GLOB_STA_PRI_READY     0x00000100  // Primary Codec Ready
#define AC97_GLOB_STA_SEC_READY     0x00000200  // Secondary Codec Ready

// POWER REGISTER(S)('(S)) BITS

#define AC97_PWR_ADC            0x0001  // ADC sections
#define AC97_PWR_DAC            0x0002  // DAC sections  
#define AC97_PWR_ANL            0x0004  // Analog mixer
#define AC97_PWR_VREF           0x0008  // VREF
#define AC97_PWR_EAPD           0x8000  // External amplifier

// Buffer Descriptor Entry
typedef struct {
    uint32_t buffer_addr;     // Physical address of audio buffer
    uint16_t length;          // Length in samples (not bytes!)
    uint16_t flags;           // Control flags
} __attribute__((packed)) ac97_bd_entry_t;

// Buffer Descriptor Flags
#define AC97_BD_FLAG_IOC            0x8000  // Interrupt on Completion
#define AC97_BD_FLAG_BUP            0x4000  // Buffer Underrun Policy

// Audio Formats
typedef enum {
    AC97_FORMAT_STEREO_16 = 0,
    AC97_FORMAT_STEREO_8,
    AC97_FORMAT_MONO_16,
    AC97_FORMAT_MONO_8
} ac97_format_t;

// Sample Rates
typedef enum {
    AC97_RATE_8000 = 8000,
    AC97_RATE_11025 = 11025,
    AC97_RATE_16000 = 16000,
    AC97_RATE_22050 = 22050,
    AC97_RATE_32000 = 32000,
    AC97_RATE_44100 = 44100,
    AC97_RATE_48000 = 48000
} ac97_sample_rate_t;

// DMA Buffer Constants
#define AC97_BD_COUNT               32    // Number of buffer descriptors
#define AC97_BUFFER_SIZE            8192  // Size of each DMA buffer in bytes
#define AC97_BUFFER_SAMPLES         (AC97_BUFFER_SIZE / 4)  // For 16-bit stereo

// Audio Stream Structure
typedef struct {
    uint8_t *buffers[AC97_BD_COUNT];      // Virtual addresses of buffers
    uint32_t buffer_phys[AC97_BD_COUNT];  // Physical addresses
    ac97_bd_entry_t *bd_list;             // Buffer descriptor list
    uint32_t bd_list_phys;                // Physical address of BD list
    
    uint32_t current_buffer;              // Current buffer being filled
    uint32_t play_buffer;                 // Buffer being played
    
    ac97_format_t format;
    ac97_sample_rate_t sample_rate;
    
    uint8_t running;
    uint8_t channels;
    uint8_t bits_per_sample;
} ac97_stream_t;

// Main AC'97 Device Structure
typedef struct {
    // PCI Information
    uint16_t vendor_id;
    uint16_t device_id;
    uint8_t bus;
    uint8_t device;
    uint8_t function;
    
    // I/O Ports
    uint16_t nam_bar;     // Native Audio Mixer BAR (BAR0)
    uint16_t nabm_bar;    // Native Audio Bus Master BAR (BAR1)
    
    // IRQ
    uint8_t irq;
    
    // Streams
    ac97_stream_t playback_stream;
    ac97_stream_t record_stream;
    
    // Device State
    uint8_t initialized;
    uint8_t codec_ready;
    
    // Volume (0-31, 31 = mute)
    uint8_t master_volume_left;
    uint8_t master_volume_right;
    uint8_t pcm_volume_left;
    uint8_t pcm_volume_right;
    
    // Capabilities
    uint8_t has_variable_rate;
    uint8_t has_surround;
    uint16_t max_sample_rate;
    
} ac97_device_t;

// Function Prototypes

// Initialization
int ac97_init(void);
int ac97_detect(void);
int ac97_reset(void);

// Codec Control
int ac97_codec_write(uint8_t reg, uint16_t value);
uint16_t ac97_codec_read(uint8_t reg);
int ac97_wait_codec_ready(void);

// Stream control
int ac97_stream_init(ac97_stream_t *stream, int is_playback);
void ac97_stream_reset(ac97_stream_t *stream, int is_playback);
void ac97_stream_start(ac97_stream_t *stream, int is_playback, uint8_t buffers_to_play);  // ← Updated
void ac97_stream_stop(ac97_stream_t *stream, int is_playback);

// Playback functions
int ac97_play_init(ac97_format_t format, ac97_sample_rate_t rate);
int ac97_play_buffer(const void *data, uint32_t size);
void ac97_play_start(void);
void ac97_play_stop(void);
void ac97_play_pause(void);
void ac97_play_resume(void);

// Recording Functions
int ac97_record_init(ac97_format_t format, ac97_sample_rate_t rate);
int ac97_record_buffer(void *data, uint32_t size);
void ac97_record_start(void);
void ac97_record_stop(void);

// Volume Control (0-100)
void ac97_set_master_volume(uint8_t left, uint8_t right);
void ac97_set_pcm_volume(uint8_t left, uint8_t right);
void ac97_set_mic_volume(uint8_t level);
void ac97_get_master_volume(uint8_t *left, uint8_t *right);
void ac97_get_pcm_volume(uint8_t *left, uint8_t *right);

// Mute Control
void ac97_mute_master(int mute);
void ac97_mute_pcm(int mute);

// Sample Rate Control
int ac97_set_sample_rate(ac97_sample_rate_t rate);
ac97_sample_rate_t ac97_get_sample_rate(void);

// Interrupt Handler
void ac97_interrupt_handler(void);

// Utility Functions
int ac97_get_position(void);  // Get current playback position in samples
int ac97_get_buffer_status(void);  // Get number of filled buffers
void ac97_wait_for_buffer(void);  // Wait for buffer to be available

// High-Level Audio API
int audio_play_pcm(const int16_t *samples, uint32_t sample_count, 
                   int channels, int sample_rate);
int audio_play_wav(const void *wav_data, uint32_t size);
int audio_beep(uint32_t frequency, uint32_t duration_ms);

// Device Information
const char* ac97_get_device_name(void);
void ac97_print_info(void);

// Global Device Instance
extern ac97_device_t *g_ac97_device;
/*GLOBAL INTERRUPT ENABLER*/
void ac97_ensure_gie(void);
#endif // AC97_H