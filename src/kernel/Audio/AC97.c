#include "AC97.h"
#include "IO.h"
#include "memory.h"
#include "print.h"
#include "string_helpers.h"

// PCI Configuration Space Registers
#define PCI_CONFIG_ADDRESS  0xCF8
#define PCI_CONFIG_DATA     0xCFC

#define PCI_VENDOR_ID       0x00
#define PCI_DEVICE_ID       0x02
#define PCI_COMMAND         0x04
#define PCI_STATUS          0x06
#define PCI_CLASS_CODE      0x0B
#define PCI_SUBCLASS        0x0A
#define PCI_PROG_IF         0x09
#define PCI_BAR0            0x10
#define PCI_BAR1            0x14
#define PCI_IRQ_LINE        0x3C

// PCI Command Register Bits
#define PCI_COMMAND_IO      0x0001
#define PCI_COMMAND_MEMORY  0x0002
#define PCI_COMMAND_MASTER  0x0004

// Global device instance
ac97_device_t *g_ac97_device = NULL;

// Forward declarations
static uint32_t pci_read_config(uint8_t bus, uint8_t device, uint8_t func, uint8_t offset);
static void pci_write_config(uint8_t bus, uint8_t device, uint8_t func, uint8_t offset, uint32_t value);
static int ac97_find_device(void);
static void* ac97_alloc_dma_buffer(uint32_t *phys_addr);
static void ac97_setup_bd_list(ac97_stream_t *stream);
static uint16_t volume_to_ac97(uint8_t volume);
static uint8_t ac97_to_volume(uint16_t ac97_val);

// ============================================================================
// PCI Configuration Functions
// ============================================================================

static uint32_t pci_read_config(uint8_t bus, uint8_t device, uint8_t func, uint8_t offset) {
    uint32_t address = 0x80000000 | ((uint32_t)bus << 16) | 
                       ((uint32_t)device << 11) | ((uint32_t)func << 8) | 
                       (offset & 0xFC);
    
    outl(PCI_CONFIG_ADDRESS, address);
    return inl(PCI_CONFIG_DATA);
}

static void pci_write_config(uint8_t bus, uint8_t device, uint8_t func, 
                             uint8_t offset, uint32_t value) {
    uint32_t address = 0x80000000 | ((uint32_t)bus << 16) | 
                       ((uint32_t)device << 11) | ((uint32_t)func << 8) | 
                       (offset & 0xFC);
    
    outl(PCI_CONFIG_ADDRESS, address);
    outl(PCI_CONFIG_DATA, value);
}

static int ac97_find_device(void) {
    // Scan PCI bus for AC'97 compatible device
    for (uint16_t bus = 0; bus < 256; bus++) {
        for (uint8_t device = 0; device < 32; device++) {
            for (uint8_t func = 0; func < 8; func++) {
                uint32_t vendor_device = pci_read_config(bus, device, func, PCI_VENDOR_ID);
                uint16_t vendor_id = vendor_device & 0xFFFF;
                uint16_t device_id = (vendor_device >> 16) & 0xFFFF;
                
                if (vendor_id == 0xFFFF) continue;
                
                // Check for Intel AC'97 devices
                if (vendor_id == AC97_VENDOR_INTEL) {
                    if (device_id == AC97_DEVICE_ICH || device_id == AC97_DEVICE_ICH0 ||
                        device_id == AC97_DEVICE_ICH2 || device_id == AC97_DEVICE_ICH3 ||
                        device_id == AC97_DEVICE_ICH4 || device_id == AC97_DEVICE_ICH5 ||
                        device_id == AC97_DEVICE_ICH6 || device_id == AC97_DEVICE_ICH7) {
                        
                        g_ac97_device->vendor_id = vendor_id;
                        g_ac97_device->device_id = device_id;
                        g_ac97_device->bus = bus;
                        g_ac97_device->device = device;
                        g_ac97_device->function = func;
                        
                        return 0;
                    }
                }
                
                // Check class code for generic AC'97 audio (0x0401)
                uint32_t class_info = pci_read_config(bus, device, func, PCI_SUBCLASS);
                uint8_t class_code = (class_info >> 24) & 0xFF;
                uint8_t subclass = (class_info >> 16) & 0xFF;
                
                if (class_code == 0x04 && subclass == 0x01) {
                    g_ac97_device->vendor_id = vendor_id;
                    g_ac97_device->device_id = device_id;
                    g_ac97_device->bus = bus;
                    g_ac97_device->device = device;
                    g_ac97_device->function = func;
                    
                    return 0;
                }
            }
        }
    }
    
    return -1;
}

// ============================================================================
// DMA Buffer Management
// ============================================================================

static void* ac97_alloc_dma_buffer(uint32_t *phys_addr) {
    // Allocate physically contiguous memory for DMA
    void *virt = kmalloc(AC97_BUFFER_SIZE);
    if (!virt) return NULL;
    
    // For simplicity, assume virtual = physical (adjust for your memory manager)
    // In a real implementation, you'd use your PMM to get physical address
    *phys_addr = (uint32_t)(uintptr_t)virt;
    
    return virt;
}

static void ac97_setup_bd_list(ac97_stream_t *stream) {
    for (int i = 0; i < AC97_BD_COUNT; i++) {
        stream->bd_list[i].buffer_addr = stream->buffer_phys[i];
        stream->bd_list[i].length = AC97_BUFFER_SAMPLES;
        stream->bd_list[i].flags = AC97_BD_FLAG_IOC;  // Interrupt on completion
    }
}

// ============================================================================
// Codec Communication
// ============================================================================

int ac97_wait_codec_ready(void) {
    uint32_t timeout = 1000000;
    
    while (timeout--) {
        uint32_t status = inl(g_ac97_device->nabm_bar + AC97_GLOB_STA);
        if (status & AC97_GLOB_STA_PRI_READY) {
            g_ac97_device->codec_ready = 1;
            return 0;
        }
        
        // Small delay
        for (volatile int i = 0; i < 100; i++);
    }
    
    PRINT(YELLOW, BLACK, "[AC97] Codec ready timeout\n");
    return -1;
}

int ac97_codec_write(uint8_t reg, uint16_t value) {
    if (!g_ac97_device->codec_ready) {
        if (ac97_wait_codec_ready() != 0) return -1;
    }
    
    outw(g_ac97_device->nam_bar + reg, value);
    
    // Wait for write to complete
    for (volatile int i = 0; i < 1000; i++);
    
    return 0;
}

uint16_t ac97_codec_read(uint8_t reg) {
    if (!g_ac97_device->codec_ready) {
        ac97_wait_codec_ready();
    }
    
    return inw(g_ac97_device->nam_bar + reg);
}

// ============================================================================
// Volume Control Functions
// ============================================================================

static uint16_t volume_to_ac97(uint8_t volume) {
    // Convert 0-100 to AC'97 attenuation (0 = 0dB, 31 = -46.5dB)
    // Volume 100 = attenuation 0, Volume 0 = mute (bit 15)
    if (volume == 0) return 0x8000;  // Mute
    if (volume > 100) volume = 100;
    
    uint8_t attenuation = 31 - ((volume * 31) / 100);
    return attenuation;
}

static uint8_t ac97_to_volume(uint16_t ac97_val) {
    if (ac97_val & 0x8000) return 0;  // Muted
    
    uint8_t attenuation = ac97_val & 0x1F;
    return ((31 - attenuation) * 100) / 31;
}

void ac97_set_master_volume(uint8_t left, uint8_t right) {
    uint16_t left_atten = volume_to_ac97(left);
    uint16_t right_atten = volume_to_ac97(right);
    uint16_t value = (left_atten << 8) | right_atten;
    
    ac97_codec_write(AC97_NAM_MASTER_VOLUME, value);
    
    g_ac97_device->master_volume_left = left;
    g_ac97_device->master_volume_right = right;
}

void ac97_set_pcm_volume(uint8_t left, uint8_t right) {
    uint16_t left_atten = volume_to_ac97(left);
    uint16_t right_atten = volume_to_ac97(right);
    uint16_t value = (left_atten << 8) | right_atten;
    
    ac97_codec_write(AC97_NAM_PCM_OUT_VOLUME, value);
    
    g_ac97_device->pcm_volume_left = left;
    g_ac97_device->pcm_volume_right = right;
}

void ac97_set_mic_volume(uint8_t level) {
    uint16_t atten = volume_to_ac97(level);
    ac97_codec_write(AC97_NAM_MIC_VOLUME, atten);
}

void ac97_get_master_volume(uint8_t *left, uint8_t *right) {
    uint16_t value = ac97_codec_read(AC97_NAM_MASTER_VOLUME);
    *left = ac97_to_volume((value >> 8) & 0xFF);
    *right = ac97_to_volume(value & 0xFF);
}

void ac97_get_pcm_volume(uint8_t *left, uint8_t *right) {
    uint16_t value = ac97_codec_read(AC97_NAM_PCM_OUT_VOLUME);
    *left = ac97_to_volume((value >> 8) & 0xFF);
    *right = ac97_to_volume(value & 0xFF);
}

void ac97_mute_master(int mute) {
    uint16_t current = ac97_codec_read(AC97_NAM_MASTER_VOLUME);
    if (mute) {
        ac97_codec_write(AC97_NAM_MASTER_VOLUME, current | 0x8000);
    } else {
        ac97_codec_write(AC97_NAM_MASTER_VOLUME, current & 0x7FFF);
    }
}

void ac97_mute_pcm(int mute) {
    uint16_t current = ac97_codec_read(AC97_NAM_PCM_OUT_VOLUME);
    if (mute) {
        ac97_codec_write(AC97_NAM_PCM_OUT_VOLUME, current | 0x8000);
    } else {
        ac97_codec_write(AC97_NAM_PCM_OUT_VOLUME, current & 0x7FFF);
    }
}

// ============================================================================
// Sample Rate Control
// ============================================================================

int ac97_set_sample_rate(ac97_sample_rate_t rate) {
    if (!g_ac97_device->has_variable_rate) {
        if (rate != AC97_RATE_48000) {
            PRINT(YELLOW, BLACK, "[AC97] Only 48kHz supported on this codec\n");
            return -1;
        }
        return 0;
    }
    
    // Set PCM Front DAC rate
    ac97_codec_write(AC97_NAM_PCM_FRONT_DAC_RATE, (uint16_t)rate);
    
    // Verify
    uint16_t actual = ac97_codec_read(AC97_NAM_PCM_FRONT_DAC_RATE);
    if (actual != (uint16_t)rate) {
        PRINT(YELLOW, BLACK, "[AC97] Sample rate set to %u instead of %u\n", 
              actual, (uint16_t)rate);
    }
    
    g_ac97_device->playback_stream.sample_rate = rate;
    return 0;
}

ac97_sample_rate_t ac97_get_sample_rate(void) {
    return g_ac97_device->playback_stream.sample_rate;
}

// ============================================================================
// Stream Control
// ============================================================================

int ac97_stream_init(ac97_stream_t *stream, int is_playback) {
    // Allocate buffer descriptor list
    stream->bd_list = (ac97_bd_entry_t*)kmalloc(sizeof(ac97_bd_entry_t) * AC97_BD_COUNT);
    if (!stream->bd_list) {
        PRINT(YELLOW, BLACK, "[AC97] Failed to allocate BD list\n");
        return -1;
    }
    
    stream->bd_list_phys = (uint32_t)(uintptr_t)stream->bd_list;
    
    // Allocate DMA buffers
    for (int i = 0; i < AC97_BD_COUNT; i++) {
        stream->buffers[i] = (uint8_t*)ac97_alloc_dma_buffer(&stream->buffer_phys[i]);
        if (!stream->buffers[i]) {
            PRINT(YELLOW, BLACK, "[AC97] Failed to allocate DMA buffer %d\n", i);
            return -1;
        }
        
        // Clear buffer
        for (int j = 0; j < AC97_BUFFER_SIZE; j++) {
            stream->buffers[i][j] = 0;
        }
    }
    
    // Setup buffer descriptors
    ac97_setup_bd_list(stream);
    
    // Initialize stream state
    stream->current_buffer = 0;
    stream->play_buffer = 0;
    stream->running = 0;
    stream->format = AC97_FORMAT_STEREO_16;
    stream->sample_rate = AC97_RATE_48000;
    stream->channels = 2;
    stream->bits_per_sample = 16;
    
    // Write BD list address to controller
    uint16_t bdbar_reg = is_playback ? AC97_PO_BDBAR : AC97_PI_BDBAR;
    outl(g_ac97_device->nabm_bar + bdbar_reg, stream->bd_list_phys);
    
    PRINT(MAGENTA, BLACK, "[AC97] Stream initialized: %d buffers of %d bytes\n",
          AC97_BD_COUNT, AC97_BUFFER_SIZE);
    
    return 0;
}

void ac97_stream_reset(ac97_stream_t *stream, int is_playback) {
    uint16_t cr_reg = is_playback ? AC97_PO_CR : AC97_PI_CR;
    
    // Set reset bit
    outb(g_ac97_device->nabm_bar + cr_reg, AC97_CR_RR);
    
    // Wait for reset to complete
    for (volatile int i = 0; i < 10000; i++);
    
    // Clear reset bit
    outb(g_ac97_device->nabm_bar + cr_reg, 0);
    
    stream->current_buffer = 0;
    stream->play_buffer = 0;
}

void ac97_stream_start(ac97_stream_t *stream, int is_playback) {
    uint16_t cr_reg = is_playback ? AC97_PO_CR : AC97_PI_CR;
    uint16_t lvi_reg = is_playback ? AC97_PO_LVI : AC97_PI_LVI;
    
    // Set Last Valid Index (circular buffer)
    outb(g_ac97_device->nabm_bar + lvi_reg, (AC97_BD_COUNT - 1));
    
    // Enable interrupts and start DMA
    uint8_t control = AC97_CR_RPBM | AC97_CR_LVBIE | AC97_CR_IOCE;
    outb(g_ac97_device->nabm_bar + cr_reg, control);
    
    stream->running = 1;
    
    PRINT(MAGENTA, BLACK, "[AC97] Stream started\n");
}

void ac97_stream_stop(ac97_stream_t *stream, int is_playback) {
    uint16_t cr_reg = is_playback ? AC97_PO_CR : AC97_PI_CR;
    
    // Clear run bit
    outb(g_ac97_device->nabm_bar + cr_reg, 0);
    
    stream->running = 0;
    
    PRINT(MAGENTA, BLACK, "[AC97] Stream stopped\n");
}

// ============================================================================
// Playback Functions
// ============================================================================

int ac97_play_init(ac97_format_t format, ac97_sample_rate_t rate) {
    if (!g_ac97_device || !g_ac97_device->initialized) {
        PRINT(YELLOW, BLACK, "[AC97] Device not initialized\n");
        return -1;
    }
    
    // Stop if already playing
    if (g_ac97_device->playback_stream.running) {
        ac97_play_stop();
    }
    
    // Reset stream
    ac97_stream_reset(&g_ac97_device->playback_stream, 1);
    
    // Set format
    g_ac97_device->playback_stream.format = format;
    
    switch (format) {
        case AC97_FORMAT_STEREO_16:
            g_ac97_device->playback_stream.channels = 2;
            g_ac97_device->playback_stream.bits_per_sample = 16;
            break;
        case AC97_FORMAT_STEREO_8:
            g_ac97_device->playback_stream.channels = 2;
            g_ac97_device->playback_stream.bits_per_sample = 8;
            break;
        case AC97_FORMAT_MONO_16:
            g_ac97_device->playback_stream.channels = 1;
            g_ac97_device->playback_stream.bits_per_sample = 16;
            break;
        case AC97_FORMAT_MONO_8:
            g_ac97_device->playback_stream.channels = 1;
            g_ac97_device->playback_stream.bits_per_sample = 8;
            break;
    }
    
    // Set sample rate
    return ac97_set_sample_rate(rate);
}

int ac97_play_buffer(const void *data, uint32_t size) {
    if (!g_ac97_device || !g_ac97_device->playback_stream.running) {
        return -1;
    }
    
    ac97_stream_t *stream = &g_ac97_device->playback_stream;
    
    // Get current buffer index
    uint32_t buffer_idx = stream->current_buffer;
    
    // Check if buffer is available (not being played)
    uint8_t civ = inb(g_ac97_device->nabm_bar + AC97_PO_CIV);
    if (buffer_idx == civ) {
        return -2;  // Buffer not available yet
    }
    
    // Copy data to buffer
    uint32_t copy_size = (size > AC97_BUFFER_SIZE) ? AC97_BUFFER_SIZE : size;
    
    for (uint32_t i = 0; i < copy_size; i++) {
        stream->buffers[buffer_idx][i] = ((uint8_t*)data)[i];
    }
    
    // Clear rest of buffer if needed
    for (uint32_t i = copy_size; i < AC97_BUFFER_SIZE; i++) {
        stream->buffers[buffer_idx][i] = 0;
    }
    
    // Move to next buffer
    stream->current_buffer = (stream->current_buffer + 1) % AC97_BD_COUNT;
    
    return copy_size;
}

void ac97_play_start(void) {
    if (!g_ac97_device) return;
    
    ac97_stream_start(&g_ac97_device->playback_stream, 1);
}

void ac97_play_stop(void) {
    if (!g_ac97_device) return;
    
    ac97_stream_stop(&g_ac97_device->playback_stream, 1);
}

void ac97_play_pause(void) {
    if (!g_ac97_device) return;
    
    uint8_t cr = inb(g_ac97_device->nabm_bar + AC97_PO_CR);
    cr &= ~AC97_CR_RPBM;
    outb(g_ac97_device->nabm_bar + AC97_PO_CR, cr);
}

void ac97_play_resume(void) {
    if (!g_ac97_device) return;
    
    uint8_t cr = inb(g_ac97_device->nabm_bar + AC97_PO_CR);
    cr |= AC97_CR_RPBM;
    outb(g_ac97_device->nabm_bar + AC97_PO_CR, cr);
}

// ============================================================================
// Interrupt Handler
// ============================================================================

void ac97_interrupt_handler(void) {
    if (!g_ac97_device) return;
    
    // Read status registers
    uint16_t po_sr = inw(g_ac97_device->nabm_bar + AC97_PO_SR);
    uint16_t pi_sr = inw(g_ac97_device->nabm_bar + AC97_PI_SR);
    
    // Handle playback interrupts
    if (po_sr & (AC97_SR_LVBCI | AC97_SR_BCIS)) {
        // Clear interrupt flags
        outw(g_ac97_device->nabm_bar + AC97_PO_SR, po_sr);
        
        // Update buffer pointers
        g_ac97_device->playback_stream.play_buffer = 
            inb(g_ac97_device->nabm_bar + AC97_PO_CIV);
    }
    
    // Handle FIFO errors
    if (po_sr & AC97_SR_FIFOE) {
        PRINT(YELLOW, BLACK, "[AC97] Playback FIFO error\n");
        outw(g_ac97_device->nabm_bar + AC97_PO_SR, AC97_SR_FIFOE);
    }
    
    // Handle recording interrupts
    if (pi_sr & (AC97_SR_LVBCI | AC97_SR_BCIS)) {
        outw(g_ac97_device->nabm_bar + AC97_PI_SR, pi_sr);
    }
    
    if (pi_sr & AC97_SR_FIFOE) {
        PRINT(YELLOW, BLACK, "[AC97] Record FIFO error\n");
        outw(g_ac97_device->nabm_bar + AC97_PI_SR, AC97_SR_FIFOE);
    }
}

// ============================================================================
// Utility Functions
// ============================================================================

int ac97_get_position(void) {
    if (!g_ac97_device) return 0;
    
    uint16_t picb = inw(g_ac97_device->nabm_bar + AC97_PO_PICB);
    uint8_t civ = inb(g_ac97_device->nabm_bar + AC97_PO_CIV);
    
    return (civ * AC97_BUFFER_SAMPLES) + (AC97_BUFFER_SAMPLES - picb);
}

int ac97_get_buffer_status(void) {
    if (!g_ac97_device) return 0;
    
    uint8_t civ = inb(g_ac97_device->nabm_bar + AC97_PO_CIV);
    ac97_stream_t *stream = &g_ac97_device->playback_stream;
    
    int filled = stream->current_buffer - civ;
    if (filled < 0) filled += AC97_BD_COUNT;
    
    return filled;
}

void ac97_wait_for_buffer(void) {
    if (!g_ac97_device) return;
    
    while (ac97_get_buffer_status() >= (AC97_BD_COUNT - 1)) {
        for (volatile int i = 0; i < 1000; i++);
    }
}

// ============================================================================
// Reset and Initialization
// ============================================================================

int ac97_reset(void) {
    PRINT(WHITE, BLACK, "[AC97] Resetting device...\n");
    
    // Cold reset
    uint32_t glob_cnt = inl(g_ac97_device->nabm_bar + AC97_GLOB_CNT);
    glob_cnt &= ~AC97_GLOB_CNT_COLD_RESET;
    outl(g_ac97_device->nabm_bar + AC97_GLOB_CNT, glob_cnt);
    
    // Wait
    for (volatile int i = 0; i < 100000; i++);
    
    // Release reset
    glob_cnt |= AC97_GLOB_CNT_COLD_RESET;
    outl(g_ac97_device->nabm_bar + AC97_GLOB_CNT, glob_cnt);
    
    // Wait for codec ready
    if (ac97_wait_codec_ready() != 0) {
        return -1;
    }
    
    PRINT(MAGENTA, BLACK, "[AC97] Codec ready\n");
    
    // Reset mixer
    ac97_codec_write(AC97_NAM_RESET, 0);
    
    for (volatile int i = 0; i < 100000; i++);
    
    // Check for variable rate support
    uint16_t ext_id = ac97_codec_read(AC97_NAM_EXT_AUDIO_ID);
    g_ac97_device->has_variable_rate = (ext_id & 0x0001) ? 1 : 0;
    g_ac97_device->has_surround = (ext_id & 0x0040) ? 1 : 0;
    
    if (g_ac97_device->has_variable_rate) {
        PRINT(MAGENTA, BLACK, "[AC97] Variable rate audio supported\n");
        
        // Enable variable rate
        uint16_t ext_status = ac97_codec_read(AC97_NAM_EXT_AUDIO_STATUS);
        ext_status |= 0x0001;
        ac97_codec_write(AC97_NAM_EXT_AUDIO_STATUS, ext_status);
        
        g_ac97_device->max_sample_rate = 48000;
    } else {
        PRINT(WHITE, BLACK, "[AC97] Fixed 48kHz only\n");
        g_ac97_device->max_sample_rate = 48000;
    }
    
    // Set default volumes (50%)
    ac97_set_master_volume(50, 50);
    ac97_set_pcm_volume(100, 100);
    
    // Unmute
    ac97_mute_master(0);
    ac97_mute_pcm(0);
    
    return 0;
}

int ac97_detect(void) {
    PRINT(WHITE, BLACK, "[AC97] Detecting audio device...\n");
    
    if (ac97_find_device() != 0) {
        PRINT(YELLOW, BLACK, "[AC97] No AC'97 device found\n");
        return -1;
    }
    
    PRINT(MAGENTA, BLACK, "[AC97] Found device: %04X:%04X at %02X:%02X.%X\n",
          g_ac97_device->vendor_id, g_ac97_device->device_id,
          g_ac97_device->bus, g_ac97_device->device, g_ac97_device->function);
    
    // Enable PCI bus mastering and I/O space
    uint32_t command = pci_read_config(g_ac97_device->bus, g_ac97_device->device,
                                       g_ac97_device->function, PCI_COMMAND);
    command |= PCI_COMMAND_IO | PCI_COMMAND_MASTER;
    pci_write_config(g_ac97_device->bus, g_ac97_device->device,
                    g_ac97_device->function, PCI_COMMAND, command);
    
    // Read BARs
    uint32_t bar0 = pci_read_config(g_ac97_device->bus, g_ac97_device->device,
                                     g_ac97_device->function, PCI_BAR0);
    uint32_t bar1 = pci_read_config(g_ac97_device->bus, g_ac97_device->device,
                                     g_ac97_device->function, PCI_BAR1);
    
    g_ac97_device->nam_bar = bar0 & 0xFFFE;   // I/O space, mask low bit
    g_ac97_device->nabm_bar = bar1 & 0xFFFE;
    
    PRINT(MAGENTA, BLACK, "[AC97] NAM BAR:  0x%04X\n", g_ac97_device->nam_bar);
    PRINT(MAGENTA, BLACK, "[AC97] NABM BAR: 0x%04X\n", g_ac97_device->nabm_bar);
    
    // Read IRQ
    uint32_t irq_reg = pci_read_config(g_ac97_device->bus, g_ac97_device->device,
                                        g_ac97_device->function, PCI_IRQ_LINE);
    g_ac97_device->irq = irq_reg & 0xFF;
    PRINT(MAGENTA, BLACK, "[AC97] IRQ: %u\n", g_ac97_device->irq);
    
    return 0;
}

int ac97_init(void) {
    PRINT(CYAN, BLACK, "\n=== AC'97 Audio Driver Initialization ===\n");
    
    // Allocate device structure
    g_ac97_device = (ac97_device_t*)kmalloc(sizeof(ac97_device_t));
    if (!g_ac97_device) {
        PRINT(YELLOW, BLACK, "[AC97] Failed to allocate device structure\n");
        return -1;
    }
    
    // Clear structure
    for (int i = 0; i < sizeof(ac97_device_t); i++) {
        ((uint8_t*)g_ac97_device)[i] = 0;
    }
    
    // Detect device
    if (ac97_detect() != 0) {
        kfree(g_ac97_device);
        g_ac97_device = NULL;
        return -1;
    }
    
    // Reset and initialize codec
    if (ac97_reset() != 0) {
        PRINT(YELLOW, BLACK, "[AC97] Reset failed\n");
        kfree(g_ac97_device);
        g_ac97_device = NULL;
        return -1;
    }
    
    // Initialize playback stream
    if (ac97_stream_init(&g_ac97_device->playback_stream, 1) != 0) {
        PRINT(YELLOW, BLACK, "[AC97] Failed to initialize playback stream\n");
        kfree(g_ac97_device);
        g_ac97_device = NULL;
        return -1;
    }
    
    // Enable global interrupts
    uint32_t glob_cnt = inl(g_ac97_device->nabm_bar + AC97_GLOB_CNT);
    glob_cnt |= AC97_GLOB_CNT_GIE;
    outl(g_ac97_device->nabm_bar + AC97_GLOB_CNT, glob_cnt);
    
    g_ac97_device->initialized = 1;
    
    PRINT(MAGENTA, BLACK, "[AC97] Initialization complete!\n");
    PRINT(CYAN, BLACK, "=========================================\n\n");
    
    return 0;
}

// ============================================================================
// High-Level Audio API
// ============================================================================

int audio_play_pcm(const int16_t *samples, uint32_t sample_count, 
                   int channels, int sample_rate) {
    if (!g_ac97_device || !g_ac97_device->initialized) {
        return -1;
    }
    
    // Determine format
    ac97_format_t format;
    if (channels == 2) {
        format = AC97_FORMAT_STEREO_16;
    } else {
        format = AC97_FORMAT_MONO_16;
    }
    
    // Initialize playback
    ac97_sample_rate_t ac97_rate = (ac97_sample_rate_t)sample_rate;
    if (ac97_play_init(format, ac97_rate) != 0) {
        return -1;
    }
    
    // Start playback
    ac97_play_start();
    
    // Feed data
    uint32_t bytes_to_write = sample_count * sizeof(int16_t) * channels;
    uint32_t offset = 0;
    
    while (offset < bytes_to_write) {
        ac97_wait_for_buffer();
        
        uint32_t chunk_size = bytes_to_write - offset;
        if (chunk_size > AC97_BUFFER_SIZE) {
            chunk_size = AC97_BUFFER_SIZE;
        }
        
        int written = ac97_play_buffer(((uint8_t*)samples) + offset, chunk_size);
        if (written < 0) {
            break;
        }
        
        offset += written;
    }
    
    return 0;
}

int audio_beep(uint32_t frequency, uint32_t duration_ms) {
    if (!g_ac97_device || !g_ac97_device->initialized) {
        return -1;
    }
    
    // Generate sine wave
    const int sample_rate = 48000;
    const int samples_needed = (sample_rate * duration_ms) / 1000;
    
    int16_t *buffer = (int16_t*)kmalloc(samples_needed * 2 * sizeof(int16_t));
    if (!buffer) return -1;
    
    // Simple square wave for beep
    int samples_per_cycle = sample_rate / frequency;
    int16_t amplitude = 8000;
    
    for (int i = 0; i < samples_needed; i++) {
        int16_t value = ((i % samples_per_cycle) < (samples_per_cycle / 2)) ? 
                        amplitude : -amplitude;
        buffer[i * 2] = value;      // Left
        buffer[i * 2 + 1] = value;  // Right
    }
    
    int result = audio_play_pcm(buffer, samples_needed, 2, sample_rate);
    
    // Wait for playback to finish
    for (volatile int i = 0; i < duration_ms * 10000; i++);
    
    ac97_play_stop();
    
    kfree(buffer);
    
    return result;
}

// ============================================================================
// Information Functions
// ============================================================================

const char* ac97_get_device_name(void) {
    if (!g_ac97_device) return "None";
    
    switch (g_ac97_device->device_id) {
        case AC97_DEVICE_ICH:   return "Intel ICH";
        case AC97_DEVICE_ICH0:  return "Intel ICH0";
        case AC97_DEVICE_ICH2:  return "Intel ICH2";
        case AC97_DEVICE_ICH3:  return "Intel ICH3";
        case AC97_DEVICE_ICH4:  return "Intel ICH4";
        case AC97_DEVICE_ICH5:  return "Intel ICH5";
        case AC97_DEVICE_ICH6:  return "Intel ICH6";
        case AC97_DEVICE_ICH7:  return "Intel ICH7";
        default: return "Generic AC'97";
    }
}

void ac97_print_info(void) {
    if (!g_ac97_device || !g_ac97_device->initialized) {
        PRINT(YELLOW, BLACK, "AC'97: Not initialized\n");
        return;
    }
    
    PRINT(CYAN, BLACK, "\n=== AC'97 Audio Device Information ===\n");
    PRINT(WHITE, BLACK, "Device: %s\n", ac97_get_device_name());
    PRINT(WHITE, BLACK, "Vendor ID: 0x%04X\n", g_ac97_device->vendor_id);
    PRINT(WHITE, BLACK, "Device ID: 0x%04X\n", g_ac97_device->device_id);
    PRINT(WHITE, BLACK, "PCI Location: %02X:%02X.%X\n",
          g_ac97_device->bus, g_ac97_device->device, g_ac97_device->function);
    PRINT(WHITE, BLACK, "IRQ: %u\n", g_ac97_device->irq);
    PRINT(WHITE, BLACK, "NAM BAR: 0x%04X\n", g_ac97_device->nam_bar);
    PRINT(WHITE, BLACK, "NABM BAR: 0x%04X\n", g_ac97_device->nabm_bar);
    
    PRINT(WHITE, BLACK, "\nFeatures:\n");
    PRINT(WHITE, BLACK, "  Variable Rate: %s\n", 
          g_ac97_device->has_variable_rate ? "Yes" : "No");
    PRINT(WHITE, BLACK, "  Surround: %s\n", 
          g_ac97_device->has_surround ? "Yes" : "No");
    PRINT(WHITE, BLACK, "  Max Sample Rate: %u Hz\n", 
          g_ac97_device->max_sample_rate);
    
    uint8_t left, right;
    ac97_get_master_volume(&left, &right);
    PRINT(WHITE, BLACK, "\nVolume:\n");
    PRINT(WHITE, BLACK, "  Master: L=%u%% R=%u%%\n", left, right);
    
    ac97_get_pcm_volume(&left, &right);
    PRINT(WHITE, BLACK, "  PCM: L=%u%% R=%u%%\n", left, right);
    
    PRINT(WHITE, BLACK, "\nPlayback Stream:\n");
    PRINT(WHITE, BLACK, "  Status: %s\n", 
          g_ac97_device->playback_stream.running ? "Running" : "Stopped");
    PRINT(WHITE, BLACK, "  Format: %d-bit %s\n",
          g_ac97_device->playback_stream.bits_per_sample,
          g_ac97_device->playback_stream.channels == 2 ? "Stereo" : "Mono");
    PRINT(WHITE, BLACK, "  Sample Rate: %u Hz\n", 
          g_ac97_device->playback_stream.sample_rate);
    PRINT(WHITE, BLACK, "  Buffers: %d x %d bytes\n", 
          AC97_BD_COUNT, AC97_BUFFER_SIZE);
    
    if (g_ac97_device->playback_stream.running) {
        PRINT(WHITE, BLACK, "  Position: %d samples\n", ac97_get_position());
        PRINT(WHITE, BLACK, "  Buffers Filled: %d\n", ac97_get_buffer_status());
    }
    
    PRINT(CYAN, BLACK, "======================================\n\n");
}