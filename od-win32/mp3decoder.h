
class mp3decoder
{
	void *g_mp3stream;
public:
	mp3decoder::mp3decoder(struct zfile *zf);
	mp3decoder();
	~mp3decoder();
	uae_u8 *get(struct zfile *zf, uae_u8 *, int maxsize);
	uae_u32 mp3decoder::getsize(struct zfile *zf);
};
