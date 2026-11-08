
extern int midi_emu;
extern void midi_emu_close(void);
extern int midi_emu_open(const TCHAR *);
extern void midi_emu_parse(uae_u8 *midi, int len);
extern bool midi_emu_available(const TCHAR *);
extern void midi_emu_reopen(void);
extern void midi_update_sound(float);

// Native MIDI output selection and optional legacy ROM directory.
const TCHAR *host_midiemu_output_name(int device);
bool host_midiemu_rom_path(TCHAR *path, int size);
