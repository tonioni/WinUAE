#ifndef WINUAE_OD_UNIX_MP3DECODER_H
#define WINUAE_OD_UNIX_MP3DECODER_H

#include "uae/types.h"

struct zfile;

class mp3decoder
{
public:
    mp3decoder(struct zfile*) {}
    mp3decoder() {}
    ~mp3decoder() {}
    uae_u8 *get(struct zfile*, uae_u8*, int) { return nullptr; }
    uae_u32 getsize(struct zfile*) { return 0; }
};

#endif /* WINUAE_OD_UNIX_MP3DECODER_H */
