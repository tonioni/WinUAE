
#ifdef _WIN32
#include <dsound.h>
#endif

extern volatile bool cd_audio_mode_changed;

class cda_audio
{
private:
	int bufsize;
#ifdef _WIN32
	HWAVEOUT wavehandle;
	WAVEHDR whdr[2];
	int num_sectors;
	int sectorsize;
	int volume[2];
	LPDIRECTSOUND8 ds;
	LPDIRECTSOUNDBUFFER8 dsbuf;
	LPDIRECTSOUNDNOTIFY dsnotify;
	HANDLE notifyevent[2];
#endif
	bool playing;
	bool active;

public:
	uae_u8 *buffers[2];

	cda_audio(int num_sectors, int sectorsize, int samplerate, bool internalmode);
	~cda_audio();
	void setvolume(int left, int right);
	bool play(int bufnum);
	void wait(void);
	void wait(int bufnum);
	bool isplaying(int bufnum);
};

