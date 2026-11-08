#include "sysconfig.h"
#include "sysdeps.h"
#include "parser.h"
#include "midiemu.h"

const TCHAR *host_midiemu_output_name(int device)
{
	if (device < 0 || device >= MAX_MIDI_PORTS || !midioutportinfo[device]) {
		return _T("");
	}
	return midioutportinfo[device]->name;
}

bool host_midiemu_rom_path(TCHAR *path, int size)
{
	const TCHAR fallback[] = _T("C:\\mt32-rom-data\\");
	if (!path || size < (int)(sizeof fallback / sizeof(TCHAR))) {
		return false;
	}
	_tcscpy(path, fallback);
	return true;
}
