


#include "piano_synth.h"
#include "AC97.h"
#include "memory.h"
#include "print.h"
#include "string_helpers.h"
#include "sleep.h"
#include "IO.h"

typedef struct {
    uint8_t midi_note;
    uint8_t velocity;
    uint32_t duration_ms;
} piano_config_t;


static inline uint32_t get_freq(uint8_t note) {
    if (note >= 128) note = 60;


    switch(note) {
        case 21: return 28;
        case 24: return 33;
        case 36: return 65;
        case 48: return 131;
        case 60: return 262;
        case 61: return 277;
        case 62: return 294;
        case 63: return 311;
        case 64: return 330;
        case 65: return 349;
        case 66: return 370;
        case 67: return 392;
        case 68: return 415;
        case 69: return 440;
        case 70: return 466;
        case 71: return 494;
        case 72: return 523;
        case 73: return 554;
        case 74: return 587;
        case 75: return 622;
        case 76: return 659;
        case 77: return 698;
        case 78: return 740;
        case 79: return 784;
        case 80: return 831;
        case 81: return 880;
        case 82: return 932;
        case 83: return 988;
        case 84: return 1047;
        default: {

            int steps = (int)note - 69;
            uint32_t freq = 440;
            if (steps > 0) {
                for (int i = 0; i < steps; i++) freq = (freq * 106) / 100;
            } else {
                for (int i = 0; i < -steps; i++) freq = (freq * 100) / 106;
            }
            return freq < 20 ? 262 : freq;
        }
    }
}


static int16_t piano_sample_simple(piano_config_t *cfg, uint32_t sample_index) {
    uint32_t freq = get_freq(cfg->midi_note);



    uint32_t total_phase = sample_index * freq;
    uint32_t cycles = total_phase / 48000;
    uint32_t phase_in_cycle = total_phase % 48000;
    uint32_t angle = (phase_in_cycle * 360) / 48000;


    uint32_t time_ms = (sample_index * 1000) / 48000;
    int32_t envelope = 100;

    if (time_ms < 10) {

        envelope = 100;
    } else if (time_ms < 300) {

        envelope = 100 - ((time_ms - 10) * 20) / 290;
    } else if (time_ms < 1500) {

        envelope = 80 - ((time_ms - 300) * 40) / 1200;
    } else if (time_ms < 3000) {

        envelope = 40 - ((time_ms - 1500) * 40) / 1500;
        if (envelope < 0) envelope = 0;
    } else {
        envelope = 0;
    }


    int32_t fundamental;
    if (angle < 90) {
        fundamental = (angle * 10000) / 90;
    } else if (angle < 180) {
        fundamental = 10000 - ((angle - 90) * 10000) / 90;
    } else if (angle < 270) {
        fundamental = -((angle - 180) * 10000) / 90;
    } else {
        fundamental = -10000 + ((angle - 270) * 10000) / 90;
    }


    uint32_t angle2 = (angle * 2) % 360;
    int32_t harmonic2;
    if (angle2 < 90) {
        harmonic2 = (angle2 * 4000) / 90;
    } else if (angle2 < 180) {
        harmonic2 = 4000 - ((angle2 - 90) * 4000) / 90;
    } else if (angle2 < 270) {
        harmonic2 = -((angle2 - 180) * 4000) / 90;
    } else {
        harmonic2 = -4000 + ((angle2 - 270) * 4000) / 90;
    }


    uint32_t angle3 = (angle * 3) % 360;
    int32_t harmonic3;
    if (angle3 < 90) {
        harmonic3 = (angle3 * 2000) / 90;
    } else if (angle3 < 180) {
        harmonic3 = 2000 - ((angle3 - 90) * 2000) / 90;
    } else if (angle3 < 270) {
        harmonic3 = -((angle3 - 180) * 2000) / 90;
    } else {
        harmonic3 = -2000 + ((angle3 - 270) * 2000) / 90;
    }


    int32_t sample = fundamental + harmonic2 + harmonic3;


    sample = (sample * envelope) / 100;


    sample = (sample * cfg->velocity) / 127;


    if (sample > 32767) sample = 32767;
    if (sample < -32768) sample = -32768;

    return (int16_t)sample;
}

int audio_play_piano_note(uint8_t midi_note, uint8_t velocity, uint32_t duration_ms) {
    if (!g_ac97_device || !g_ac97_device->initialized) {
        return -1;
    }

    PRINT(CYAN, BLACK, "\n=== Piano Note: MIDI %u, Vel %u, Dur %ums ===\n",
          midi_note, velocity, duration_ms);


    if (g_ac97_device->playback_stream.running) {
        ac97_play_stop();
    }


    outb(g_ac97_device->nabm_bar + AC97_PO_CR, AC97_CR_RR);
    outb(g_ac97_device->nabm_bar + AC97_PO_CR, 0);
    outw(g_ac97_device->nabm_bar + AC97_PO_SR, 0x1E);

    if (ac97_play_init(AC97_FORMAT_STEREO_16, AC97_RATE_48000) != 0) {
        return -1;
    }

    piano_config_t cfg = {
        .midi_note = midi_note,
        .velocity = velocity,
        .duration_ms = duration_ms
    };

    uint32_t test_freq = get_freq(midi_note);
    PRINT(WHITE, BLACK, "[PIANO] Freq=%u Hz\n", test_freq);

    ac97_stream_t *stream = &g_ac97_device->playback_stream;
    outl(g_ac97_device->nabm_bar + AC97_PO_BDBAR, stream->bd_list_phys);

    uint32_t sample_count = 0;
    int samples_in_buffer = AC97_BUFFER_SIZE / 4;


    for (int buf = 0; buf < 2; buf++) {
        int16_t *buffer = (int16_t*)stream->buffers[buf];

        for (int i = 0; i < samples_in_buffer; i++) {
            int16_t sample = piano_sample_simple(&cfg, sample_count);
            buffer[i * 2] = sample;
            buffer[i * 2 + 1] = sample;
            sample_count++;
        }
    }


    int16_t *check = (int16_t*)stream->buffers[0];
    PRINT(WHITE, BLACK, "[PIANO] Samples: %d %d %d %d\n",
          check[0], check[1], check[2], check[3]);


    outb(g_ac97_device->nabm_bar + AC97_PO_LVI, 1);
    outw(g_ac97_device->nabm_bar + AC97_PO_SR, 0x1E);
    outb(g_ac97_device->nabm_bar + AC97_PO_CR, AC97_CR_RPBM);
    stream->running = 1;

    PRINT(MAGENTA, BLACK, "[PIANO] Playing...\n");


    extern volatile uint64_t timer_ticks;
    uint64_t timeout = timer_ticks + duration_ms + 500;

    while (timer_ticks < timeout) {
        uint16_t sr = inw(g_ac97_device->nabm_bar + AC97_PO_SR);
        if (sr & 0x01) break;
        __asm__ volatile("hlt");
    }


    outb(g_ac97_device->nabm_bar + AC97_PO_CR, 0);
    stream->running = 0;

    PRINT(GREEN, BLACK, "[PIANO] Done!\n\n");
    return 0;
}

int audio_play_piano_phrase(piano_phrase_note_t *notes, int note_count) {
    for (int i = 0; i < note_count; i++) {
        if (notes[i].delay_before_ms > 0) {
            sleep_ms(notes[i].delay_before_ms);
        }
        audio_play_piano_note(notes[i].note, notes[i].velocity, notes[i].duration_ms);
    }
    return 0;
}