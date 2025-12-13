// ============================================================================
// piano_synth.h - Piano synthesis header (INTEGER-ONLY VERSION)
// ============================================================================
#ifndef PIANO_SYNTH_H
#define PIANO_SYNTH_H

#include <stdint.h>

// Piano note names to MIDI
typedef enum {
    C0=12, CS0, D0, DS0, E0, F0, FS0, G0, GS0, A0, AS0, B0,
    C1, CS1, D1, DS1, E1, F1, FS1, G1, GS1, A1, AS1, B1,
    C2, CS2, D2, DS2, E2, F2, FS2, G2, GS2, A2, AS2, B2,
    C3, CS3, D3, DS3, E3, F3, FS3, G3, GS3, A3, AS3, B3,
    C4, CS4, D4, DS4, E4, F4, FS4, G4, GS4, A4, AS4, B4, // Middle C = C4
    C5, CS5, D5, DS5, E5, F5, FS5, G5, GS5, A5, AS5, B5,
    C6, CS6, D6, DS6, E6, F6, FS6, G6, GS6, A6, AS6, B6,
    C7, CS7, D7, DS7, E7, F7, FS7, G7, GS7, A7, AS7, B7,
    C8
} piano_note_t;

// Musical phrase note
typedef struct {
    uint8_t note;
    uint8_t velocity;
    uint32_t duration_ms;
    uint32_t delay_before_ms;
} piano_phrase_note_t;

// Public API
int audio_play_piano_note(uint8_t midi_note, uint8_t velocity, uint32_t duration_ms);
int audio_play_piano_phrase(piano_phrase_note_t *notes, int note_count);

#endif // PIANO_SYNTH_H