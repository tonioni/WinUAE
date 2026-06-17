/*
 * Unix native MIDI backend.
 *
 * WinUAE routes MIDI through serial emulation when the guest selects the MIDI
 * baud rate. This header mirrors the small Win32 MIDI API used by that path.
 */
#ifndef UAE_UNIX_MIDI_H
#define UAE_UNIX_MIDI_H

#define MIDI_BUFFERS 2
#define BUFFLEN 32766

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

#ifndef _WIN32
typedef int LONG;
#endif

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

extern int unix_midi_output_device_count(void);
extern int unix_midi_output_device_id(int index);
extern const TCHAR *unix_midi_output_device_display_name(int index);
extern const TCHAR *unix_midi_output_device_config_name_for_id(int devid);
extern int unix_midi_output_device_id_from_config_name(const TCHAR *name);
extern int unix_midi_input_device_count(void);
extern int unix_midi_input_device_id(int index);
extern const TCHAR *unix_midi_input_device_display_name(int index);
extern const TCHAR *unix_midi_input_device_config_name_for_id(int devid);
extern int unix_midi_input_device_id_from_config_name(const TCHAR *name);

#endif
