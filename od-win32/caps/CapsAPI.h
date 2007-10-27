#ifndef CAPSAPI_H
#define CAPSAPI_H

#define CAPS_FILEEXT "ipf"
#define CAPS_FILEPFX ".ipf"

// Flags provided for locking, in order:
// 0: re-align data as index synced recording
// 1: decode track to word aligned size
// 2: generate cell density for variable density tracks
// 3: generate density for automatically sized cells
// 4: generate density for unformatted cells
// 5: generate unformatted data
// 6: generate unformatted data, that changes each revolution
// 7: directly use source memory buffer supplied with LockImageMemory
// 8: flakey data is created on one revolution, updated with each lock
// 9: ...Info.type holds the expected structure type
#define DI_LOCK_INDEX    DF_0
#define DI_LOCK_ALIGN    DF_1
#define DI_LOCK_DENVAR   DF_2
#define DI_LOCK_DENAUTO  DF_3
#define DI_LOCK_DENNOISE DF_4
#define DI_LOCK_NOISE    DF_5
#define DI_LOCK_NOISEREV DF_6
#define DI_LOCK_MEMREF   DF_7
#define DI_LOCK_UPDATEFD DF_8
#define DI_LOCK_TYPE     DF_9

#define CAPS_MAXPLATFORM 4
#define CAPS_MTRS 5

#define CTIT_FLAG_FLAKEY DF_31
#define CTIT_MASK_TYPE 0xff

#pragma pack(push, 1)

// decoded caps date.time
struct CapsDateTimeExt {
	UDWORD year;
	UDWORD month;
	UDWORD day;
	UDWORD hour;
	UDWORD min;
	UDWORD sec;
	UDWORD tick;
};

typedef struct CapsDateTimeExt *PCAPSDATETIMEEXT;

// library version information block
struct CapsVersionInfo {
	UDWORD type;     // library type
	UDWORD release;  // release ID
	UDWORD revision; // revision ID
	UDWORD flag;     // supported flags
};

typedef struct CapsVersionInfo *PCAPSVERSIONINFO;

// disk image information block
struct CapsImageInfo {
	UDWORD type;        // image type
	UDWORD release;     // release ID
	UDWORD revision;    // release revision ID
	UDWORD mincylinder; // lowest cylinder number
	UDWORD maxcylinder; // highest cylinder number
	UDWORD minhead;     // lowest head number
	UDWORD maxhead;     // highest head number
	struct CapsDateTimeExt crdt; // image creation date.time
	UDWORD platform[CAPS_MAXPLATFORM]; // intended platform(s)
};

typedef struct CapsImageInfo *PCAPSIMAGEINFO;

// disk track information block
struct CapsTrackInfo {
	UDWORD type;       // track type
	UDWORD cylinder;   // cylinder#
	UDWORD head;       // head#
	UDWORD sectorcnt;  // available sectors
	UDWORD sectorsize; // sector size
	UDWORD trackcnt;   // track variant count
	PUBYTE trackbuf;   // track buffer memory
	UDWORD tracklen;   // track buffer memory length
	PUBYTE trackdata[CAPS_MTRS]; // track data pointer if available
	UDWORD tracksize[CAPS_MTRS]; // track data size
	UDWORD timelen;  // timing buffer length
	PUDWORD timebuf; // timing buffer
};

typedef struct CapsTrackInfo *PCAPSTRACKINFO;

// disk track information block
struct CapsTrackInfoT1 {
	UDWORD type;       // track type
	UDWORD cylinder;   // cylinder#
	UDWORD head;       // head#
	UDWORD sectorcnt;  // available sectors
	UDWORD sectorsize; // sector size
	PUBYTE trackbuf;   // track buffer memory
	UDWORD tracklen;   // track buffer memory length
	UDWORD timelen;    // timing buffer length
	PUDWORD timebuf;   // timing buffer
	UDWORD overlap;    // overlap position
};

typedef struct CapsTrackInfoT1 *PCAPSTRACKINFOT1;

#pragma pack(pop)

// image type
enum {
	ciitNA=0, // invalid image type
	ciitFDD   // floppy disk
};

// platform IDs, not about configuration, but intended use
enum {
	ciipNA=0,    // invalid platform (dummy entry)
	ciipAmiga,   // Amiga
	ciipAtariST, // Atari ST
	ciipPC       // PC
};

// track type
enum {
	ctitNA=0,  // invalid type
	ctitNoise, // cells are unformatted (random size)
	ctitAuto,  // automatic cell size, according to track size
	ctitVar    // variable density
};

// image error status
enum {
	imgeOk,
	imgeUnsupported,
	imgeGeneric,
	imgeOutOfRange,
	imgeReadOnly,
	imgeOpen,
	imgeType,
	imgeShort,
	imgeTrackHeader,
	imgeTrackStream,
	imgeTrackData,
	imgeDensityHeader,
	imgeDensityStream,
	imgeDensityData,
	imgeIncompatible,
	imgeUnsupportedType
};

#endif
