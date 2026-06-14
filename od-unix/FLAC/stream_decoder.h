#ifndef WINUAE_OD_UNIX_FLAC_STREAM_DECODER_H
#define WINUAE_OD_UNIX_FLAC_STREAM_DECODER_H

#include <stddef.h>
#include <stdint.h>

typedef int FLAC__bool;
typedef uint8_t FLAC__byte;
typedef int16_t FLAC__int16;
typedef int32_t FLAC__int32;
typedef uint64_t FLAC__uint64;

typedef struct FLAC__StreamDecoder FLAC__StreamDecoder;
struct FLAC__StreamDecoder {};

enum {
    FLAC__METADATA_TYPE_STREAMINFO = 0,
    FLAC__METADATA_TYPE_CUESHEET = 5
};

typedef enum {
    FLAC__STREAM_DECODER_WRITE_STATUS_CONTINUE = 0
} FLAC__StreamDecoderWriteStatus;

typedef enum {
    FLAC__STREAM_DECODER_READ_STATUS_CONTINUE = 0,
    FLAC__STREAM_DECODER_READ_STATUS_END_OF_STREAM,
    FLAC__STREAM_DECODER_READ_STATUS_ABORT
} FLAC__StreamDecoderReadStatus;

typedef enum {
    FLAC__STREAM_DECODER_SEEK_STATUS_OK = 0
} FLAC__StreamDecoderSeekStatus;

typedef enum {
    FLAC__STREAM_DECODER_TELL_STATUS_OK = 0
} FLAC__StreamDecoderTellStatus;

typedef enum {
    FLAC__STREAM_DECODER_LENGTH_STATUS_OK = 0
} FLAC__StreamDecoderLengthStatus;

typedef int FLAC__StreamDecoderErrorStatus;

typedef struct {
    uint32_t blocksize;
} FLAC__FrameHeader;

typedef struct {
    FLAC__FrameHeader header;
} FLAC__Frame;

typedef struct {
    uint64_t total_samples;
    uint32_t bits_per_sample;
    uint32_t channels;
} FLAC__StreamMetadata_StreamInfo;

typedef struct {
    int type;
    union {
        FLAC__StreamMetadata_StreamInfo stream_info;
    } data;
} FLAC__StreamMetadata;

typedef FLAC__StreamDecoderReadStatus (*FLAC__StreamDecoderReadCallback)(const FLAC__StreamDecoder*, FLAC__byte[], size_t*, void*);
typedef FLAC__StreamDecoderSeekStatus (*FLAC__StreamDecoderSeekCallback)(const FLAC__StreamDecoder*, FLAC__uint64, void*);
typedef FLAC__StreamDecoderTellStatus (*FLAC__StreamDecoderTellCallback)(const FLAC__StreamDecoder*, FLAC__uint64*, void*);
typedef FLAC__StreamDecoderLengthStatus (*FLAC__StreamDecoderLengthCallback)(const FLAC__StreamDecoder*, FLAC__uint64*, void*);
typedef FLAC__bool (*FLAC__StreamDecoderEofCallback)(const FLAC__StreamDecoder*, void*);
typedef FLAC__StreamDecoderWriteStatus (*FLAC__StreamDecoderWriteCallback)(const FLAC__StreamDecoder*, const FLAC__Frame*, const FLAC__int32* const[], void*);
typedef void (*FLAC__StreamDecoderMetadataCallback)(const FLAC__StreamDecoder*, const FLAC__StreamMetadata*, void*);
typedef void (*FLAC__StreamDecoderErrorCallback)(const FLAC__StreamDecoder*, FLAC__StreamDecoderErrorStatus, void*);

static inline FLAC__StreamDecoder *FLAC__stream_decoder_new(void)
{
    return 0;
}

static inline void FLAC__stream_decoder_delete(FLAC__StreamDecoder*) {}
static inline void FLAC__stream_decoder_set_md5_checking(FLAC__StreamDecoder*, FLAC__bool) {}
static inline void FLAC__stream_decoder_set_metadata_respond(FLAC__StreamDecoder*, int) {}
static inline int FLAC__stream_decoder_init_stream(
    FLAC__StreamDecoder*,
    FLAC__StreamDecoderReadCallback,
    FLAC__StreamDecoderSeekCallback,
    FLAC__StreamDecoderTellCallback,
    FLAC__StreamDecoderLengthCallback,
    FLAC__StreamDecoderEofCallback,
    FLAC__StreamDecoderWriteCallback,
    FLAC__StreamDecoderMetadataCallback,
    FLAC__StreamDecoderErrorCallback,
    void*)
{
    return 0;
}
static inline int FLAC__stream_decoder_process_until_end_of_metadata(FLAC__StreamDecoder*) { return 0; }
static inline int FLAC__stream_decoder_process_until_end_of_stream(FLAC__StreamDecoder*) { return 0; }

#endif /* WINUAE_OD_UNIX_FLAC_STREAM_DECODER_H */
