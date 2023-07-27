/*
 * MODULE: midi.h
 * AUTHOR: Brian King
 * COPYRIGHT: Copyright 1999 under GNU Public License
 * VERSION: 1.0
 */
#ifndef __MIDI_H__
#define __MIDI_H__

#define MIDI_BUFFERS 2
#define BUFFLEN 32766

#define MAXTEMPO        350
#define MINTEMPO        10

#define MIDI_NOTEOFF    0x80
#define MIDI_NOTEON     0x90
#define MIDI_PTOUCH     0xA0
#define MIDI_CCHANGE    0xB0
#define MIDI_PCHANGE    0xC0
#define MIDI_MTOUCH     0xD0
#define MIDI_PBEND      0xE0
#define MIDI_SYSX       0xF0
#define MIDI_MTC        0xF1
#define MIDI_SONGPP     0xF2
#define MIDI_SONGS      0xF3
#define MIDI_EOX        0xF7
#define MIDI_CLOCK      0xF8
#define MIDI_START      0xFA
#define MIDI_CONTINUE   0xFB
#define MIDI_STOP       0xFC
#define MIDI_SENSE      0xFE

typedef struct mos
{
    BYTE status;
    BYTE byte1;
    BYTE byte2;
    int length;
    int posn;
    int sysex;
    int timecode;
    int unknown;
} MidiOutStatus;

typedef enum
{
    midi_input,
    midi_output
} midi_direction_e;

extern BOOL midi_ready;

extern int Midi_Parse(midi_direction_e direction, BYTE *c);
extern int Midi_Open(void);
extern void Midi_Close(void);
extern void Midi_Reopen(void);
extern LONG getmidibyte(void);
extern int ismidibyte(void);

#endif

