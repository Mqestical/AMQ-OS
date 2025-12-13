#include "AC97.h"
#include "IO.h"
#include "memory.h"
#include "print.h"
#include "string_helpers.h"
#include "sleep.h"

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


#define PCI_COMMAND_IO      0x0001
#define PCI_COMMAND_MEMORY  0x0002
#define PCI_COMMAND_MASTER  0x0004


ac97_device_t *g_ac97_device = NULL;


static uint32_t pci_read_config(uint8_t bus, uint8_t device, uint8_t func, uint8_t offset);
static void pci_write_config(uint8_t bus, uint8_t device, uint8_t func, uint8_t offset, uint32_t value);
static int ac97_find_device(void);
static void* ac97_alloc_dma_buffer(uint32_t *phys_addr);
static void ac97_setup_bd_list(ac97_stream_t *stream);
static uint16_t volume_to_ac97(uint8_t volume);
static uint8_t ac97_to_volume(uint16_t ac97_val);

static uint8_t *dma_buffer_ptrs[AC97_BD_COUNT];
static uint32_t dma_buffer_phys[AC97_BD_COUNT];
static int buffers_allocated = 0;

void ac97_dump_registers(void) {
    if (!g_ac97_device) return;

    PRINT(CYAN, BLACK, "\n=== AC97 Register Dump ===\n");


    PRINT(WHITE, BLACK, "\nMixer Registers (NAM BAR: 0x");
    print_unsigned(g_ac97_device->nam_bar, 16);
    PRINT(WHITE, BLACK, "):\n");

    uint16_t reset = ac97_codec_read(0x00);
    uint16_t master = ac97_codec_read(0x02);
    uint16_t pcm = ac97_codec_read(0x18);
    uint16_t ext_id = ac97_codec_read(0x28);
    uint16_t ext_stat = ac97_codec_read(0x2A);
    uint16_t pcm_rate = ac97_codec_read(0x2C);

    PRINT(WHITE, BLACK, "  0x00 Reset:      0x");
    print_unsigned(reset, 16);
    PRINT(WHITE, BLACK, "\n");

    PRINT(WHITE, BLACK, "  0x02 Master Vol: 0x");
    print_unsigned(master, 16);
    PRINT(WHITE, BLACK, " (Mute: %s)\n", (master & 0x8000) ? "YES" : "NO");

    PRINT(WHITE, BLACK, "  0x18 PCM Vol:    0x");
    print_unsigned(pcm, 16);
    PRINT(WHITE, BLACK, " (Mute: %s)\n", (pcm & 0x8000) ? "YES" : "NO");

    PRINT(WHITE, BLACK, "  0x28 Ext Audio:  0x");
    print_unsigned(ext_id, 16);
    PRINT(WHITE, BLACK, " (VRA: %s)\n", (ext_id & 0x01) ? "YES" : "NO");

    PRINT(WHITE, BLACK, "  0x2A Ext Status: 0x");
    print_unsigned(ext_stat, 16);
    PRINT(WHITE, BLACK, " (VRA: %s)\n", (ext_stat & 0x01) ? "Enabled" : "Disabled");

    PRINT(WHITE, BLACK, "  0x2C PCM Rate:   ");
    print_unsigned(pcm_rate, 10);
    PRINT(WHITE, BLACK, " Hz\n");


    PRINT(WHITE, BLACK, "\nBus Master Registers (NABM BAR: 0x");
    print_unsigned(g_ac97_device->nabm_bar, 16);
    PRINT(WHITE, BLACK, "):\n");

    uint32_t glob_cnt = inl(g_ac97_device->nabm_bar + 0x2C);
    uint32_t glob_sta = inl(g_ac97_device->nabm_bar + 0x30);

    PRINT(WHITE, BLACK, "  Global Control:  0x");
    print_unsigned(glob_cnt, 16);
    PRINT(WHITE, BLACK, "\n");
    PRINT(WHITE, BLACK, "    GIE (bit 0):   %s\n", (glob_cnt & 0x01) ? "ON" : "OFF");
    PRINT(WHITE, BLACK, "    Cold Reset:    %s\n", (glob_cnt & 0x02) ? "ON" : "OFF");

    PRINT(WHITE, BLACK, "  Global Status:   0x");
    print_unsigned(glob_sta, 16);
    PRINT(WHITE, BLACK, "\n");
    PRINT(WHITE, BLACK, "    Pri Codec Rdy: %s\n", (glob_sta & 0x100) ? "YES" : "NO");


    PRINT(WHITE, BLACK, "\nPCM Output Channel:\n");

    uint32_t bdbar = inl(g_ac97_device->nabm_bar + AC97_PO_BDBAR);
    uint8_t civ = inb(g_ac97_device->nabm_bar + AC97_PO_CIV);
    uint8_t lvi = inb(g_ac97_device->nabm_bar + AC97_PO_LVI);
    uint16_t sr = inw(g_ac97_device->nabm_bar + AC97_PO_SR);
    uint16_t picb = inw(g_ac97_device->nabm_bar + AC97_PO_PICB);
    uint8_t cr = inb(g_ac97_device->nabm_bar + AC97_PO_CR);

    PRINT(WHITE, BLACK, "  BDBAR:    0x");
    print_unsigned(bdbar, 16);
    PRINT(WHITE, BLACK, "\n");

    PRINT(WHITE, BLACK, "  CIV:      ");
    print_unsigned(civ, 10);
    PRINT(WHITE, BLACK, " (current buffer)\n");

    PRINT(WHITE, BLACK, "  LVI:      ");
    print_unsigned(lvi, 10);
    PRINT(WHITE, BLACK, " (last valid)\n");

    PRINT(WHITE, BLACK, "  SR:       0x");
    print_unsigned(sr, 16);
    PRINT(WHITE, BLACK, "\n");
    PRINT(WHITE, BLACK, "    DCH:    %s (DMA halted)\n", (sr & 0x01) ? "YES" : "NO");
    PRINT(WHITE, BLACK, "    LVBCI:  %s (last valid buf)\n", (sr & 0x04) ? "YES" : "NO");
    PRINT(WHITE, BLACK, "    BCIS:   %s (buf complete)\n", (sr & 0x08) ? "YES" : "NO");
    PRINT(WHITE, BLACK, "    FIFOE:  %s (FIFO error)\n", (sr & 0x10) ? "YES" : "NO");

    PRINT(WHITE, BLACK, "  PICB:     ");
    print_unsigned(picb, 10);
    PRINT(WHITE, BLACK, " samples remaining\n");

    PRINT(WHITE, BLACK, "  CR:       0x");
    print_unsigned(cr, 16);
    PRINT(WHITE, BLACK, "\n");
    PRINT(WHITE, BLACK, "    RPBM:   %s (run/pause)\n", (cr & 0x01) ? "RUN" : "PAUSE");
    PRINT(WHITE, BLACK, "    RR:     %s (reset)\n", (cr & 0x02) ? "YES" : "NO");
    PRINT(WHITE, BLACK, "    LVBIE:  %s (LVB int)\n", (cr & 0x04) ? "ON" : "OFF");
    PRINT(WHITE, BLACK, "    IOCE:   %s (IOC int)\n", (cr & 0x08) ? "ON" : "OFF");

    PRINT(CYAN, BLACK, "=========================\n\n");
}

int ac97_test_buffers(void) {
    PRINT(CYAN, BLACK, "\n=== Testing Buffer Access ===\n");

    if (!g_ac97_device) {
        PRINT(YELLOW, BLACK, "Device not initialized\n");
        return -1;
    }

    ac97_stream_t *stream = &g_ac97_device->playback_stream;

    for (int i = 0; i < 2; i++) {
        PRINT(WHITE, BLACK, "Buffer %d: ", i);

        uint8_t *buf = stream->buffers[i];
        if (!buf) {
            PRINT(YELLOW, BLACK, "NULL!\n");
            return -1;
        }


        buf[0] = 0xAA;
        buf[1] = 0xBB;

        if (buf[0] == 0xAA && buf[1] == 0xBB) {
            PRINT(MAGENTA, BLACK, "OK (virt=0x");
            print_unsigned((uint64_t)buf, 16);
            PRINT(MAGENTA, BLACK, ", phys=0x");
            print_unsigned(stream->buffer_phys[i], 16);
            PRINT(MAGENTA, BLACK, ")\n");
        } else {
            PRINT(YELLOW, BLACK, "FAILED - can't write!\n");
            return -1;
        }


        buf[0] = 0;
        buf[1] = 0;
    }

    PRINT(MAGENTA, BLACK, "All buffers accessible!\n\n");
    return 0;
}

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

    for (uint16_t bus = 0; bus < 256; bus++) {
        for (uint8_t device = 0; device < 32; device++) {
            for (uint8_t func = 0; func < 8; func++) {
                uint32_t vendor_device = pci_read_config(bus, device, func, PCI_VENDOR_ID);
                uint16_t vendor_id = vendor_device & 0xFFFF;
                uint16_t device_id = (vendor_device >> 16) & 0xFFFF;

                if (vendor_id == 0xFFFF) continue;


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





static void* ac97_alloc_dma_buffer(uint32_t *phys_addr) {
    if (buffers_allocated >= AC97_BD_COUNT) {
        return NULL;
    }


    int pages_needed = (AC97_BUFFER_SIZE + 4095) / 4096;
    void *buffer = pmm_alloc_pages(pages_needed);

    if (!buffer) {
        PRINT(YELLOW, BLACK, "[AC97] Failed to allocate physical pages\n");
        return NULL;
    }



    *phys_addr = (uint32_t)(uint64_t)buffer;


    dma_buffer_ptrs[buffers_allocated] = (uint8_t*)buffer;
    dma_buffer_phys[buffers_allocated] = *phys_addr;


    uint8_t *ptr = (uint8_t*)buffer;
    for (int i = 0; i < AC97_BUFFER_SIZE; i++) {
        ptr[i] = 0;
    }

    buffers_allocated++;
    PRINT(WHITE, BLACK, "[AC97] Allocated DMA buffer %d: virt=0x", buffers_allocated - 1);
    print_unsigned((uint64_t)buffer, 16);
    PRINT(WHITE, BLACK, ", phys=0x");
    print_unsigned(*phys_addr, 16);
    PRINT(WHITE, BLACK, "\n");

    return buffer;
}

static void ac97_setup_bd_list(ac97_stream_t *stream) {
    uint16_t samples = AC97_BUFFER_SIZE / 4;

    for (int i = 0; i < AC97_BD_COUNT; i++) {
        stream->bd_list[i].buffer_addr = stream->buffer_phys[i];
        stream->bd_list[i].length = samples;



        if (i == AC97_BD_COUNT - 1) {
            stream->bd_list[i].flags = AC97_BD_FLAG_IOC | AC97_BD_FLAG_BUP;
        } else {
            stream->bd_list[i].flags = AC97_BD_FLAG_IOC;
        }
    }
}






void ac97_ensure_gie(void) {
    uint32_t glob_cnt = inl(g_ac97_device->nabm_bar + AC97_GLOB_CNT);

    PRINT(WHITE, BLACK, "[AC97] Current Global Control: 0x");
    print_unsigned(glob_cnt, 16);
    PRINT(WHITE, BLACK, "\n");

    if (!(glob_cnt & AC97_GLOB_CNT_GIE)) {
        PRINT(YELLOW, BLACK, "[AC97] GIE was OFF, enabling now...\n");
        glob_cnt |= AC97_GLOB_CNT_GIE;
        outl(g_ac97_device->nabm_bar + AC97_GLOB_CNT, glob_cnt);


        uint32_t verify = inl(g_ac97_device->nabm_bar + AC97_GLOB_CNT);
        PRINT(WHITE, BLACK, "[AC97] New Global Control: 0x");
        print_unsigned(verify, 16);
        PRINT(WHITE, BLACK, "\n");
        PRINT(WHITE, BLACK, "[AC97] GIE is now: %s\n",
              (verify & AC97_GLOB_CNT_GIE) ? "ON" : "OFF");
    } else {
        PRINT(MAGENTA, BLACK, "[AC97] GIE already ON\n");
    }
}

int ac97_wait_codec_ready(void) {
    uint32_t timeout = 1000000;

    while (timeout--) {
        uint32_t status = inl(g_ac97_device->nabm_bar + AC97_GLOB_STA);
        if (status & AC97_GLOB_STA_PRI_READY) {
            g_ac97_device->codec_ready = 1;
            return 0;
        }


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




    return 0;
}

uint16_t ac97_codec_read(uint8_t reg) {
    if (!g_ac97_device->codec_ready) {
        ac97_wait_codec_ready();
    }

    return inw(g_ac97_device->nam_bar + reg);
}





static uint16_t volume_to_ac97(uint8_t volume) {


    if (volume == 0) return 0x8000;
    if (volume > 100) volume = 100;

    uint8_t attenuation = 31 - ((volume * 31) / 100);
    return attenuation;
}

static uint8_t ac97_to_volume(uint16_t ac97_val) {
    if (ac97_val & 0x8000) return 0;

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





int ac97_set_sample_rate(ac97_sample_rate_t rate) {
    if (!g_ac97_device->has_variable_rate) {
        if (rate != AC97_RATE_48000) {
            PRINT(YELLOW, BLACK, "[AC97] Only 48kHz supported on this codec\n");
            return -1;
        }
        return 0;
    }


    ac97_codec_write(AC97_NAM_PCM_FRONT_DAC_RATE, (uint16_t)rate);


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





int ac97_stream_init(ac97_stream_t *stream, int is_playback) {

    stream->bd_list = (ac97_bd_entry_t*)kmalloc(sizeof(ac97_bd_entry_t) * AC97_BD_COUNT);
    if (!stream->bd_list) {
        PRINT(YELLOW, BLACK, "[AC97] Failed to allocate BD list\n");
        return -1;
    }

    stream->bd_list_phys = (uint32_t)(uintptr_t)stream->bd_list;


    for (int i = 0; i < AC97_BD_COUNT; i++) {
        stream->buffers[i] = (uint8_t*)ac97_alloc_dma_buffer(&stream->buffer_phys[i]);
        if (!stream->buffers[i]) {
            PRINT(YELLOW, BLACK, "[AC97] Failed to allocate DMA buffer %d\n", i);
            return -1;
        }


        for (int j = 0; j < AC97_BUFFER_SIZE; j++) {
            stream->buffers[i][j] = 0;
        }
    }


    ac97_setup_bd_list(stream);


    stream->current_buffer = 0;
    stream->play_buffer = 0;
    stream->running = 0;
    stream->format = AC97_FORMAT_STEREO_16;
    stream->sample_rate = AC97_RATE_48000;
    stream->channels = 2;
    stream->bits_per_sample = 16;


    uint16_t bdbar_reg = is_playback ? AC97_PO_BDBAR : AC97_PI_BDBAR;
    outl(g_ac97_device->nabm_bar + bdbar_reg, stream->bd_list_phys);

    PRINT(MAGENTA, BLACK, "[AC97] Stream initialized: %d buffers of %d bytes\n",
          AC97_BD_COUNT, AC97_BUFFER_SIZE);

    return 0;
}

void ac97_stream_reset(ac97_stream_t *stream, int is_playback) {
    uint16_t cr_reg = is_playback ? AC97_PO_CR : AC97_PI_CR;


    outb(g_ac97_device->nabm_bar + cr_reg, AC97_CR_RR);





    outb(g_ac97_device->nabm_bar + cr_reg, 0);

    stream->current_buffer = 0;
    stream->play_buffer = 0;
}

void ac97_stream_start(ac97_stream_t *stream, int is_playback, uint8_t buffers_to_play) {
    uint16_t cr_reg = is_playback ? AC97_PO_CR : AC97_PI_CR;
    uint16_t lvi_reg = is_playback ? AC97_PO_LVI : AC97_PI_LVI;
    uint16_t sr_reg = is_playback ? AC97_PO_SR : AC97_PI_SR;

    PRINT(WHITE, BLACK, "[AC97] Starting stream with ");
    print_unsigned(buffers_to_play, 10);
    PRINT(WHITE, BLACK, " buffers...\n");


    ac97_ensure_gie();


    outw(g_ac97_device->nabm_bar + sr_reg, 0x1E);


    uint8_t lvi_value = (buffers_to_play > 0) ? (buffers_to_play - 1) : 0;
    outb(g_ac97_device->nabm_bar + lvi_reg, lvi_value);

    PRINT(WHITE, BLACK, "[AC97] Set LVI to ");
    print_unsigned(lvi_value, 10);
    PRINT(WHITE, BLACK, "\n");





    uint8_t control = AC97_CR_RPBM | AC97_CR_IOCE;
    outb(g_ac97_device->nabm_bar + cr_reg, control);

    PRINT(WHITE, BLACK, "[AC97] Control register set to 0x");
    print_unsigned(control, 16);
    PRINT(WHITE, BLACK, "\n");




    uint8_t cr_check = inb(g_ac97_device->nabm_bar + cr_reg);
    uint16_t sr_check = inw(g_ac97_device->nabm_bar + sr_reg);
    uint8_t civ = inb(g_ac97_device->nabm_bar + (is_playback ? AC97_PO_CIV : AC97_PI_CIV));

    PRINT(WHITE, BLACK, "[AC97] Verification:\n");
    PRINT(WHITE, BLACK, "  CR=0x");
    print_unsigned(cr_check, 16);
    PRINT(WHITE, BLACK, "\n  SR=0x");
    print_unsigned(sr_check, 16);
    PRINT(WHITE, BLACK, "\n  CIV=");
    print_unsigned(civ, 10);
    PRINT(WHITE, BLACK, "\n");
    PRINT(WHITE, BLACK, "  DMA Running: %s\n", (cr_check & 0x01) ? "YES" : "NO");
    PRINT(WHITE, BLACK, "  DMA Halted: %s\n", (sr_check & 0x01) ? "YES" : "NO");

    if (sr_check & 0x01) {
        PRINT(YELLOW, BLACK, "[AC97] WARNING: DMA is still halted!\n");
    } else {
        PRINT(MAGENTA, BLACK, "[AC97] DMA is running!\n");
    }

    stream->running = 1;
}

void ac97_stream_stop(ac97_stream_t *stream, int is_playback) {
    uint16_t cr_reg = is_playback ? AC97_PO_CR : AC97_PI_CR;


    outb(g_ac97_device->nabm_bar + cr_reg, 0);

    stream->running = 0;

    PRINT(MAGENTA, BLACK, "[AC97] Stream stopped\n");
}





int ac97_play_init(ac97_format_t format, ac97_sample_rate_t rate) {
    if (!g_ac97_device || !g_ac97_device->initialized) {
        PRINT(YELLOW, BLACK, "[AC97] Device not initialized\n");
        return -1;
    }


    if (g_ac97_device->playback_stream.running) {
        ac97_play_stop();
    }


    ac97_stream_reset(&g_ac97_device->playback_stream, 1);


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


    return ac97_set_sample_rate(rate);
}

int ac97_play_buffer(const void *data, uint32_t size) {
    if (!g_ac97_device || !g_ac97_device->playback_stream.running) {
        return -1;
    }

    ac97_stream_t *stream = &g_ac97_device->playback_stream;


    uint32_t buffer_idx = stream->current_buffer;


    uint8_t civ = inb(g_ac97_device->nabm_bar + AC97_PO_CIV);
    if (buffer_idx == civ) {
        return -2;
    }


    uint32_t copy_size = (size > AC97_BUFFER_SIZE) ? AC97_BUFFER_SIZE : size;

    for (uint32_t i = 0; i < copy_size; i++) {
        stream->buffers[buffer_idx][i] = ((uint8_t*)data)[i];
    }


    for (uint32_t i = copy_size; i < AC97_BUFFER_SIZE; i++) {
        stream->buffers[buffer_idx][i] = 0;
    }


    stream->current_buffer = (stream->current_buffer + 1) % AC97_BD_COUNT;

    return copy_size;
}

void ac97_play_start(void) {
    if (!g_ac97_device) return;


    ac97_stream_start(&g_ac97_device->playback_stream, 1, AC97_BD_COUNT);
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





void ac97_interrupt_handler(void) {
    if (!g_ac97_device) return;


    uint16_t po_sr = inw(g_ac97_device->nabm_bar + AC97_PO_SR);
    uint16_t pi_sr = inw(g_ac97_device->nabm_bar + AC97_PI_SR);


    if (po_sr & (AC97_SR_LVBCI | AC97_SR_BCIS)) {

        outw(g_ac97_device->nabm_bar + AC97_PO_SR, po_sr);


        g_ac97_device->playback_stream.play_buffer =
            inb(g_ac97_device->nabm_bar + AC97_PO_CIV);
    }


    if (po_sr & AC97_SR_FIFOE) {
        PRINT(YELLOW, BLACK, "[AC97] Playback FIFO error\n");
        outw(g_ac97_device->nabm_bar + AC97_PO_SR, AC97_SR_FIFOE);
    }


    if (pi_sr & (AC97_SR_LVBCI | AC97_SR_BCIS)) {
        outw(g_ac97_device->nabm_bar + AC97_PI_SR, pi_sr);
    }

    if (pi_sr & AC97_SR_FIFOE) {
        PRINT(YELLOW, BLACK, "[AC97] Record FIFO error\n");
        outw(g_ac97_device->nabm_bar + AC97_PI_SR, AC97_SR_FIFOE);
    }
}





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

    }
}





int ac97_reset(void) {
    PRINT(WHITE, BLACK, "[AC97] Resetting device...\n");


    uint32_t glob_cnt = inl(g_ac97_device->nabm_bar + AC97_GLOB_CNT);
    PRINT(WHITE, BLACK, "[AC97] Initial Global Control: 0x");
    print_unsigned(glob_cnt, 16);
    PRINT(WHITE, BLACK, "\n");

    glob_cnt &= ~AC97_GLOB_CNT_COLD_RESET;
    outl(g_ac97_device->nabm_bar + AC97_GLOB_CNT, glob_cnt);

    PRINT(WHITE, BLACK, "[AC97] Cold reset asserted\n");





    glob_cnt = inl(g_ac97_device->nabm_bar + AC97_GLOB_CNT);
    glob_cnt |= AC97_GLOB_CNT_COLD_RESET;
    outl(g_ac97_device->nabm_bar + AC97_GLOB_CNT, glob_cnt);

    PRINT(WHITE, BLACK, "[AC97] Cold reset released\n");





    PRINT(WHITE, BLACK, "[AC97] Waiting for codec ready...\n");

    if (ac97_wait_codec_ready() != 0) {
        PRINT(YELLOW, BLACK, "[AC97] Codec ready timeout!\n");
        return -1;
    }

    PRINT(MAGENTA, BLACK, "[AC97] Codec ready signal received\n");


    glob_cnt = inl(g_ac97_device->nabm_bar + AC97_GLOB_CNT);
    glob_cnt |= AC97_GLOB_CNT_GIE;
    outl(g_ac97_device->nabm_bar + AC97_GLOB_CNT, glob_cnt);

    PRINT(WHITE, BLACK, "[AC97] GIE enable requested\n");




    uint32_t verify = inl(g_ac97_device->nabm_bar + AC97_GLOB_CNT);
    PRINT(WHITE, BLACK, "[AC97] Global Control after GIE: 0x");
    print_unsigned(verify, 16);
    PRINT(WHITE, BLACK, "\n");
    PRINT(WHITE, BLACK, "[AC97] GIE Status: %s\n",
          (verify & AC97_GLOB_CNT_GIE) ? "ENABLED" : "DISABLED");

    if (!(verify & AC97_GLOB_CNT_GIE)) {
        PRINT(YELLOW, BLACK, "[AC97] WARNING: GIE not enabled\n");
        PRINT(WHITE, BLACK, "[AC97] This is normal for some devices - will use polling mode\n");
    } else {
        PRINT(MAGENTA, BLACK, "[AC97] GIE successfully enabled\n");
    }


    PRINT(WHITE, BLACK, "[AC97] Resetting codec mixer...\n");
    ac97_codec_write(AC97_NAM_RESET, 0);





    PRINT(WHITE, BLACK, "[AC97] Powering up codec sections...\n");


    uint16_t power = ac97_codec_read(AC97_NAM_POWERDOWN_CTRL);
    PRINT(WHITE, BLACK, "[AC97] Initial power register: 0x");
    print_unsigned(power, 16);
    PRINT(WHITE, BLACK, "\n");



    for (int attempt = 0; attempt < 3; attempt++) {
        PRINT(WHITE, BLACK, "[AC97] Power-up attempt %d/3...\n", attempt + 1);
        ac97_codec_write(AC97_NAM_POWERDOWN_CTRL, 0x0000);

    }


    PRINT(WHITE, BLACK, "[AC97] Waiting for codec stabilization...\n");


    power = ac97_codec_read(AC97_NAM_POWERDOWN_CTRL);
    PRINT(WHITE, BLACK, "[AC97] Power register after powerup: 0x");
    print_unsigned(power, 16);
    PRINT(WHITE, BLACK, "\n");


    if (power & AC97_PWR_DAC) {
        PRINT(YELLOW, BLACK, "[AC97] WARNING: DAC bit still set (0x");
        print_unsigned(power, 16);
        PRINT(YELLOW, BLACK, ")\n");
        PRINT(WHITE, BLACK, "[AC97] This is common with some codecs - continuing anyway\n");
        PRINT(WHITE, BLACK, "[AC97] Audio playback may still work correctly\n");
    } else {
        PRINT(MAGENTA, BLACK, "[AC97] DAC powered up successfully!\n");
    }


    power = ac97_codec_read(AC97_NAM_POWERDOWN_CTRL);
    if (power & AC97_PWR_EAPD) {
        PRINT(WHITE, BLACK, "[AC97] Enabling external amplifier (clearing EAPD)...\n");
        power &= ~AC97_PWR_EAPD;
        ac97_codec_write(AC97_NAM_POWERDOWN_CTRL, power);

    }


    power = ac97_codec_read(AC97_NAM_POWERDOWN_CTRL);
    PRINT(WHITE, BLACK, "[AC97] Final power register: 0x");
    print_unsigned(power, 16);
    PRINT(WHITE, BLACK, "\n");

    if (power == 0) {
        PRINT(MAGENTA, BLACK, "[AC97] All sections report powered up!\n");
    } else {
        PRINT(WHITE, BLACK, "[AC97] Some sections may not report correctly, but continuing...\n");
    }

uint16_t codec_id = ac97_codec_read(AC97_NAM_RESET);
PRINT(WHITE, BLACK, "[AC97] Codec ID: 0x");
print_unsigned(codec_id, 16);
PRINT(WHITE, BLACK, "\n");


if (codec_id == 0x0000) {
    PRINT(YELLOW, BLACK, "[AC97] WARNING: Codec ID is 0x0000\n");
    PRINT(WHITE, BLACK, "[AC97] Performing diagnostic checks...\n");


    PRINT(WHITE, BLACK, "[AC97] Test 1: Register write/read test\n");


    uint16_t orig_vol = ac97_codec_read(AC97_NAM_MASTER_VOLUME);
    PRINT(WHITE, BLACK, "  Original master volume: 0x");
    print_unsigned(orig_vol, 16);
    PRINT(WHITE, BLACK, "\n");


    ac97_codec_write(AC97_NAM_MASTER_VOLUME, 0x0808);


    uint16_t test_vol = ac97_codec_read(AC97_NAM_MASTER_VOLUME);
    PRINT(WHITE, BLACK, "  After writing 0x0808, read: 0x");
    print_unsigned(test_vol, 16);
    PRINT(WHITE, BLACK, "\n");


    ac97_codec_write(AC97_NAM_MASTER_VOLUME, orig_vol);

    if (test_vol == 0x0808 || (test_vol & 0x1F1F) == 0x0808) {
        PRINT(MAGENTA, BLACK, "   Codec IS responding to writes!\n");
        PRINT(WHITE, BLACK, "  This is likely an emulated codec (QEMU/VirtualBox)\n");
        PRINT(WHITE, BLACK, "  Codec ID of 0x0 is acceptable - continuing...\n");
    } else if (test_vol == 0x0000 || test_vol == 0xFFFF) {
        PRINT(YELLOW, BLACK, "   Codec NOT responding properly!\n");
        PRINT(WHITE, BLACK, "  Read back: 0x");
        print_unsigned(test_vol, 16);
        PRINT(WHITE, BLACK, "\n");


        PRINT(WHITE, BLACK, "[AC97] Test 2: Checking BARs...\n");
        PRINT(WHITE, BLACK, "  NAM BAR: 0x");
        print_unsigned(g_ac97_device->nam_bar, 16);
        PRINT(WHITE, BLACK, "\n");
        PRINT(WHITE, BLACK, "  NABM BAR: 0x");
        print_unsigned(g_ac97_device->nabm_bar, 16);
        PRINT(WHITE, BLACK, "\n");

        if (g_ac97_device->nam_bar == 0 || g_ac97_device->nabm_bar == 0) {
            PRINT(YELLOW, BLACK, "   BARs are invalid! Cannot continue.\n");
            return -1;
        }


        uint32_t glob_sta = inl(g_ac97_device->nabm_bar + AC97_GLOB_STA);
        PRINT(WHITE, BLACK, "  Global Status: 0x");
        print_unsigned(glob_sta, 16);
        PRINT(WHITE, BLACK, "\n");

        if (!(glob_sta & AC97_GLOB_STA_PRI_READY)) {
            PRINT(YELLOW, BLACK, "   Codec ready bit NOT set!\n");
            PRINT(YELLOW, BLACK, "  Codec may not be present or initialized.\n");
            return -1;
        }

        PRINT(YELLOW, BLACK, "  Codec ready bit IS set but not responding.\n");
        PRINT(YELLOW, BLACK, "  This may be a hardware/emulation issue.\n");
        PRINT(WHITE, BLACK, "  Attempting to continue anyway...\n");
    } else {
        PRINT(MAGENTA, BLACK, "  ~ Codec responding but with unexpected value\n");
        PRINT(WHITE, BLACK, "  Continuing initialization...\n");
    }
} else if (codec_id == 0xFFFF) {
    PRINT(YELLOW, BLACK, "[AC97] ERROR: Codec ID is 0xFFFF (not present)\n");
    return -1;
} else {
    PRINT(MAGENTA, BLACK, "[AC97] Valid Codec ID detected\n");
}


    uint16_t ext_id = ac97_codec_read(AC97_NAM_EXT_AUDIO_ID);
    PRINT(WHITE, BLACK, "[AC97] Extended Audio ID: 0x");
    print_unsigned(ext_id, 16);
    PRINT(WHITE, BLACK, "\n");

    g_ac97_device->has_variable_rate = (ext_id & 0x0001) ? 1 : 0;
    g_ac97_device->has_surround = (ext_id & 0x0040) ? 1 : 0;

    if (g_ac97_device->has_variable_rate) {
        PRINT(MAGENTA, BLACK, "[AC97] Variable Rate Audio (VRA) supported\n");


        uint16_t ext_status = ac97_codec_read(AC97_NAM_EXT_AUDIO_STATUS);
        ext_status |= 0x0001;
        ac97_codec_write(AC97_NAM_EXT_AUDIO_STATUS, ext_status);

        PRINT(WHITE, BLACK, "[AC97] VRA enabled\n");


        ext_status = ac97_codec_read(AC97_NAM_EXT_AUDIO_STATUS);
        if (ext_status & 0x0001) {
            PRINT(MAGENTA, BLACK, "[AC97] VRA confirmed active\n");
            g_ac97_device->max_sample_rate = 48000;
        } else {
            PRINT(YELLOW, BLACK, "[AC97] VRA enable failed, using 48kHz only\n");
            g_ac97_device->has_variable_rate = 0;
            g_ac97_device->max_sample_rate = 48000;
        }
    } else {
        PRINT(WHITE, BLACK, "[AC97] Fixed 48kHz sample rate only\n");
        g_ac97_device->max_sample_rate = 48000;
    }

    if (g_ac97_device->has_surround) {
        PRINT(MAGENTA, BLACK, "[AC97] Surround sound supported\n");
    }


    if (g_ac97_device->has_variable_rate) {
        ac97_codec_write(AC97_NAM_PCM_FRONT_DAC_RATE, 48000);
        uint16_t actual_rate = ac97_codec_read(AC97_NAM_PCM_FRONT_DAC_RATE);
        PRINT(WHITE, BLACK, "[AC97] PCM Front DAC rate set to: ");
        print_unsigned(actual_rate, 10);
        PRINT(WHITE, BLACK, " Hz\n");
    }


    PRINT(WHITE, BLACK, "[AC97] Setting default volumes...\n");


    ac97_set_master_volume(75, 75);


    ac97_set_pcm_volume(100, 100);


    uint8_t left, right;
    ac97_get_master_volume(&left, &right);
    PRINT(WHITE, BLACK, "[AC97] Master volume: L=%u%% R=%u%%\n", left, right);

    ac97_get_pcm_volume(&left, &right);
    PRINT(WHITE, BLACK, "[AC97] PCM volume: L=%u%% R=%u%%\n", left, right);


    PRINT(WHITE, BLACK, "[AC97] Unmuting outputs...\n");

    ac97_mute_master(0);
    ac97_mute_pcm(0);


    uint16_t master_vol = ac97_codec_read(AC97_NAM_MASTER_VOLUME);
    uint16_t pcm_vol = ac97_codec_read(AC97_NAM_PCM_OUT_VOLUME);

    PRINT(WHITE, BLACK, "[AC97] Master mute: %s\n",
          (master_vol & 0x8000) ? "YES" : "NO");
    PRINT(WHITE, BLACK, "[AC97] PCM mute: %s\n",
          (pcm_vol & 0x8000) ? "YES" : "NO");


    PRINT(WHITE, BLACK, "[AC97] Resetting DMA channels...\n");


    outb(g_ac97_device->nabm_bar + AC97_PO_CR, AC97_CR_RR);

    outb(g_ac97_device->nabm_bar + AC97_PO_CR, 0);


    outb(g_ac97_device->nabm_bar + AC97_PI_CR, AC97_CR_RR);

    outb(g_ac97_device->nabm_bar + AC97_PI_CR, 0);


    outb(g_ac97_device->nabm_bar + AC97_MC_CR, AC97_CR_RR);

    outb(g_ac97_device->nabm_bar + AC97_MC_CR, 0);


    outw(g_ac97_device->nabm_bar + AC97_PO_SR, 0x1E);
    outw(g_ac97_device->nabm_bar + AC97_PI_SR, 0x1E);
    outw(g_ac97_device->nabm_bar + AC97_MC_SR, 0x1E);

    PRINT(MAGENTA, BLACK, "[AC97] DMA channels reset\n");


    uint32_t glob_sta = inl(g_ac97_device->nabm_bar + AC97_GLOB_STA);
    PRINT(WHITE, BLACK, "[AC97] Global Status: 0x");
    print_unsigned(glob_sta, 16);
    PRINT(WHITE, BLACK, "\n");
    PRINT(WHITE, BLACK, "[AC97] Primary Codec Ready: %s\n",
          (glob_sta & AC97_GLOB_STA_PRI_READY) ? "YES" : "NO");

    if (glob_sta & 0x200) {
        PRINT(WHITE, BLACK, "[AC97] Secondary Codec Ready: YES\n");
    }

    PRINT(MAGENTA, BLACK, "[AC97] Reset complete!\n\n");
    ClearScreen(BLACK);
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


    uint32_t command = pci_read_config(g_ac97_device->bus, g_ac97_device->device,
                                       g_ac97_device->function, PCI_COMMAND);
    command |= PCI_COMMAND_IO | PCI_COMMAND_MASTER;
    pci_write_config(g_ac97_device->bus, g_ac97_device->device,
                    g_ac97_device->function, PCI_COMMAND, command);


    uint32_t bar0 = pci_read_config(g_ac97_device->bus, g_ac97_device->device,
                                     g_ac97_device->function, PCI_BAR0);
    uint32_t bar1 = pci_read_config(g_ac97_device->bus, g_ac97_device->device,
                                     g_ac97_device->function, PCI_BAR1);

    g_ac97_device->nam_bar = bar0 & 0xFFFE;
    g_ac97_device->nabm_bar = bar1 & 0xFFFE;

    PRINT(MAGENTA, BLACK, "[AC97] NAM BAR:  0x%04X\n", g_ac97_device->nam_bar);
    PRINT(MAGENTA, BLACK, "[AC97] NABM BAR: 0x%04X\n", g_ac97_device->nabm_bar);


    uint32_t irq_reg = pci_read_config(g_ac97_device->bus, g_ac97_device->device,
                                        g_ac97_device->function, PCI_IRQ_LINE);
    g_ac97_device->irq = irq_reg & 0xFF;
    PRINT(MAGENTA, BLACK, "[AC97] IRQ: %u\n", g_ac97_device->irq);

    return 0;
}

int ac97_init(void) {
    PRINT(CYAN, BLACK, "\n=== AC'97 Audio Driver Initialization ===\n");


    g_ac97_device = (ac97_device_t*)kmalloc(sizeof(ac97_device_t));
    if (!g_ac97_device) {
        PRINT(YELLOW, BLACK, "[AC97] Failed to allocate device structure\n");
        return -1;
    }


    for (int i = 0; i < sizeof(ac97_device_t); i++) {
        ((uint8_t*)g_ac97_device)[i] = 0;
    }


    if (ac97_detect() != 0) {
        kfree(g_ac97_device);
        g_ac97_device = NULL;
        return -1;
    }

    PRINT(CYAN, BLACK, "\n=== BAR Validation ===\n");
    PRINT(WHITE, BLACK, "\nNAM BAR: 0x");
      print_unsigned(g_ac97_device->nam_bar, 16);
    PRINT(WHITE, BLACK, "\nNABM BAR:0x ");
    print_unsigned(g_ac97_device->nabm_bar, 16);
    PRINT(WHITE, BLACK, "\n");
    if (g_ac97_device->nam_bar == 0 || g_ac97_device->nabm_bar == 0) {
        PRINT(YELLOW, BLACK, "ERROR: Invalid BARs!\n");
        return -1;
    }


    if (ac97_reset() != 0) {
        PRINT(YELLOW, BLACK, "[AC97] Reset failed\n");
        kfree(g_ac97_device);
        g_ac97_device = NULL;
        return -1;
    }


    if (ac97_stream_init(&g_ac97_device->playback_stream, 1) != 0) {
        PRINT(YELLOW, BLACK, "[AC97] Failed to initialize playback stream\n");
        kfree(g_ac97_device);
        g_ac97_device = NULL;
        return -1;
    }



    uint32_t glob_cnt = inl(g_ac97_device->nabm_bar + AC97_GLOB_CNT);
    glob_cnt |= AC97_GLOB_CNT_GIE;
    outl(g_ac97_device->nabm_bar + AC97_GLOB_CNT, glob_cnt);

    g_ac97_device->initialized = 1;

     if (g_ac97_device->irq > 0) {
        extern void irq_install_handler(uint8_t irq, void (*handler)(void));
        irq_install_handler(g_ac97_device->irq, ac97_interrupt_handler);
        PRINT(MAGENTA, BLACK, "[AC97] IRQ %u handler registered\n", g_ac97_device->irq);
    }

    PRINT(MAGENTA, BLACK, "[AC97] Initialization complete!\n");
    PRINT(CYAN, BLACK, "=========================================\n\n");

    return 0;
}





int audio_play_pcm(const int16_t *samples, uint32_t sample_count,
                   int channels, int sample_rate) {
    if (!g_ac97_device || !g_ac97_device->initialized) {
        return -1;
    }


    ac97_format_t format;
    if (channels == 2) {
        format = AC97_FORMAT_STEREO_16;
    } else {
        format = AC97_FORMAT_MONO_16;
    }


    ac97_sample_rate_t ac97_rate = (ac97_sample_rate_t)sample_rate;
    if (ac97_play_init(format, ac97_rate) != 0) {
        return -1;
    }


    ac97_play_start();


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

void ac97_debug_playback(void) {
    PRINT(CYAN, BLACK, "\n=== AC97 Playback Debug ===\n");


    if (!g_ac97_device) {
        PRINT(YELLOW, BLACK, "Device pointer is NULL!\n");
        return;
    }

    if (!g_ac97_device->initialized) {
        PRINT(YELLOW, BLACK, "Device not initialized!\n");
        return;
    }

    PRINT(MAGENTA, BLACK, "Device initialized: YES\n");


    PRINT(WHITE, BLACK, "NAM BAR: 0x");
    print_unsigned(g_ac97_device->nam_bar, 16);
    PRINT(WHITE, BLACK, "\n");

    PRINT(WHITE, BLACK, "NABM BAR: 0x");
    print_unsigned(g_ac97_device->nabm_bar, 16);
    PRINT(WHITE, BLACK, "\n");

    if (g_ac97_device->nam_bar == 0 || g_ac97_device->nabm_bar == 0) {
        PRINT(YELLOW, BLACK, "BARs not set correctly!\n");
        return;
    }


    uint32_t glob_sta = inl(g_ac97_device->nabm_bar + AC97_GLOB_STA);
    PRINT(WHITE, BLACK, "Global Status: 0x");
    print_unsigned(glob_sta, 16);
    PRINT(WHITE, BLACK, "\n");
    PRINT(WHITE, BLACK, "Codec Ready: %s\n",
          (glob_sta & AC97_GLOB_STA_PRI_READY) ? "YES" : "NO");

    if (!(glob_sta & AC97_GLOB_STA_PRI_READY)) {
        PRINT(YELLOW, BLACK, "Codec not ready!\n");
        return;
    }


    uint16_t master = ac97_codec_read(AC97_NAM_MASTER_VOLUME);
    uint16_t pcm = ac97_codec_read(AC97_NAM_PCM_OUT_VOLUME);

    PRINT(WHITE, BLACK, "Master Volume: 0x");
    print_unsigned(master, 16);
    PRINT(WHITE, BLACK, " (Muted: %s)\n", (master & 0x8000) ? "YES" : "NO");

    PRINT(WHITE, BLACK, "PCM Volume: 0x");
    print_unsigned(pcm, 16);
    PRINT(WHITE, BLACK, " (Muted: %s)\n", (pcm & 0x8000) ? "YES" : "NO");

    if ((master & 0x8000) || (pcm & 0x8000)) {
        PRINT(YELLOW, BLACK, "Audio is MUTED!\n");
    }


    uint32_t bdbar = inl(g_ac97_device->nabm_bar + AC97_PO_BDBAR);
    PRINT(WHITE, BLACK, "BDBAR: 0x");
    print_unsigned(bdbar, 16);
    PRINT(WHITE, BLACK, "\n");

    if (bdbar == 0) {
        PRINT(YELLOW, BLACK, "BDBAR not set!\n");
        return;
    }


    ac97_stream_t *stream = &g_ac97_device->playback_stream;
    int has_data = 0;
    for (int i = 0; i < 100; i++) {
        if (stream->buffers[0][i] != 0) {
            has_data = 1;
            break;
        }
    }
    PRINT(WHITE, BLACK, "Buffer has data: %s\n", has_data ? "YES" : "NO");

    PRINT(CYAN, BLACK, "===========================\n\n");
}int audio_beep(uint32_t frequency, uint32_t duration_ms) {
    if (!g_ac97_device || !g_ac97_device->initialized) {
        PRINT(YELLOW, BLACK, "[BEEP] Device not initialized\n");
        return -1;
    }

    PRINT(CYAN, BLACK, "\n=== BEEP ");
    print_unsigned(frequency, 10);
    PRINT(CYAN, BLACK, " Hz ===\n");


    if (g_ac97_device->playback_stream.running) {
        ac97_play_stop();

    }


    PRINT(WHITE, BLACK, "[BEEP] Resetting DMA...\n");
    outb(g_ac97_device->nabm_bar + AC97_PO_CR, AC97_CR_RR);
    outb(g_ac97_device->nabm_bar + AC97_PO_CR, 0);
    outw(g_ac97_device->nabm_bar + AC97_PO_SR, 0x1E);


    if (ac97_play_init(AC97_FORMAT_STEREO_16, AC97_RATE_48000) != 0) {
        PRINT(YELLOW, BLACK, "[BEEP] Init failed\n");
        return -1;
    }

    ac97_stream_t *stream = &g_ac97_device->playback_stream;


    outl(g_ac97_device->nabm_bar + AC97_PO_BDBAR, stream->bd_list_phys);


    const int sample_rate = 48000;
    const int samples_per_cycle = sample_rate / frequency;
    const int16_t amplitude = 20000;


    int total_samples = (sample_rate * duration_ms) / 1000;
    int samples_written = 0;

    for (int buf = 0; buf < 2 && samples_written < total_samples; buf++) {
        int16_t *buffer = (int16_t*)stream->buffers[buf];
        int samples_in_buffer = AC97_BUFFER_SIZE / 4;

        for (int i = 0; i < samples_in_buffer && samples_written < total_samples; i++) {
            int16_t value = ((samples_written % samples_per_cycle) < (samples_per_cycle / 2))
                            ? amplitude : -amplitude;
            buffer[i * 2] = value;
            buffer[i * 2 + 1] = value;
            samples_written++;
        }

        PRINT(WHITE, BLACK, "[BEEP] Filled buffer ");
        print_unsigned(buf, 10);
        PRINT(WHITE, BLACK, "\n");
    }


    int16_t *check = (int16_t*)stream->buffers[0];
    PRINT(WHITE, BLACK, "[BEEP] First 4 samples: ");
    for (int i = 0; i < 4; i++) {
        if (check[i] >= 0) {
            print_unsigned(check[i], 10);
        } else {
            PRINT(WHITE, BLACK, "-");
            print_unsigned(-check[i], 10);
        }
        PRINT(WHITE, BLACK, " ");
    }
    PRINT(WHITE, BLACK, "\n");


    outb(g_ac97_device->nabm_bar + AC97_PO_LVI, 1);


    outw(g_ac97_device->nabm_bar + AC97_PO_SR, 0x1E);



    outb(g_ac97_device->nabm_bar + AC97_PO_CR, AC97_CR_RPBM);
    stream->running = 1;

    PRINT(MAGENTA, BLACK, "[BEEP] Playing...\n");


    extern volatile uint64_t timer_ticks;
    uint64_t timeout = timer_ticks + duration_ms + 20;

    while (timer_ticks < timeout) {
        uint16_t sr = inw(g_ac97_device->nabm_bar + AC97_PO_SR);
        if (sr & 0x01) {
            PRINT(WHITE, BLACK, "[BEEP] Completed\n");
            break;
        }
        __asm__ volatile("hlt");
    }


    outb(g_ac97_device->nabm_bar + AC97_PO_CR, 0);
    stream->running = 0;

    PRINT(MAGENTA, BLACK, "[BEEP] Done\n\n");
    return 0;
}



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