
#include "chdtypes.h"

#include "chd.h"
#include "hashing.h"
#include "chdcdrom.h"
#include "coretmpl.h"
#include "bitstream.h"
#include "huffman.h"

// standard metadata formats
const char *HARD_DISK_METADATA_FORMAT = "CYLS:%d,HEADS:%d,SECS:%d,BPS:%d";
const char *CDROM_TRACK_METADATA_FORMAT = "TRACK:%d TYPE:%s SUBTYPE:%s FRAMES:%d";
const char *CDROM_TRACK_METADATA2_FORMAT = "TRACK:%d TYPE:%s SUBTYPE:%s FRAMES:%d PREGAP:%d PGTYPE:%s PGSUB:%s POSTGAP:%d";
const char *GDROM_TRACK_METADATA_FORMAT = "TRACK:%d TYPE:%s SUBTYPE:%s FRAMES:%d PAD:%d PREGAP:%d PGTYPE:%s PGSUB:%s POSTGAP:%d";
const char *AV_METADATA_FORMAT = "FPS:%d.%06d WIDTH:%d HEIGHT:%d INTERLACED:%d CHANNELS:%d SAMPLERATE:%d";

static const UINT32 METADATA_HEADER_SIZE = 16;			// metadata header size

static const UINT8 V34_MAP_ENTRY_FLAG_TYPE_MASK	= 0x0f;		// what type of hunk
static const UINT8 V34_MAP_ENTRY_FLAG_NO_CRC = 0x10;		// no CRC is present



// V3-V4 entry types
enum
{
	V34_MAP_ENTRY_TYPE_INVALID = 0,				// invalid type
	V34_MAP_ENTRY_TYPE_COMPRESSED = 1,			// standard compression
	V34_MAP_ENTRY_TYPE_UNCOMPRESSED = 2,		// uncompressed data
	V34_MAP_ENTRY_TYPE_MINI = 3,				// mini: use offset as raw data
	V34_MAP_ENTRY_TYPE_SELF_HUNK = 4,			// same as another hunk in this file
	V34_MAP_ENTRY_TYPE_PARENT_HUNK = 5,			// same as a hunk in the parent file
	V34_MAP_ENTRY_TYPE_2ND_COMPRESSED = 6		// compressed with secondary algorithm (usually FLAC CDDA)
};

// V5 compression types
enum
{
	// these types are live when running
	COMPRESSION_TYPE_0 = 0,						// codec #0
	COMPRESSION_TYPE_1 = 1,						// codec #1
	COMPRESSION_TYPE_2 = 2,						// codec #2
	COMPRESSION_TYPE_3 = 3,						// codec #3
	COMPRESSION_NONE = 4,						// no compression; implicit length = hunkbytes
	COMPRESSION_SELF = 5,						// same as another block in this chd
	COMPRESSION_PARENT = 6,						// same as a hunk's worth of units in the parent chd

	// these additional pseudo-types are used for compressed encodings:
	COMPRESSION_RLE_SMALL,						// start of small RLE run (4-bit length)
	COMPRESSION_RLE_LARGE,						// start of large RLE run (8-bit length)
	COMPRESSION_SELF_0,							// same as the last COMPRESSION_SELF block
	COMPRESSION_SELF_1,							// same as the last COMPRESSION_SELF block + 1
	COMPRESSION_PARENT_SELF,					// same block in the parent
	COMPRESSION_PARENT_0,						// same as the last COMPRESSION_PARENT block
	COMPRESSION_PARENT_1						// same as the last COMPRESSION_PARENT block + 1
};


//**************************************************************************
//  TYPE DEFINITIONS
//**************************************************************************

// ======================> metadata_entry

// description of where a metadata entry lives within the file
struct chd_file::metadata_entry
{
	UINT64					offset;			// offset within the file of the header
	UINT64					next;			// offset within the file of the next header
	UINT64					prev;			// offset within the file of the previous header
	UINT32					length;			// length of the metadata
	UINT32					metatag;		// metadata tag
	UINT8					flags;			// flag bits
};


// ======================> metadata_hash

struct chd_file::metadata_hash
{
	UINT8					tag[4];			// tag of the metadata in big-endian
	sha1_t					sha1;			// hash data
};


//-------------------------------------------------
//  be_read - extract a big-endian number from
//  a byte buffer
//-------------------------------------------------

inline UINT64 chd_file::be_read(const UINT8 *base, int numbytes)
{
	UINT64 result = 0;
	while (numbytes--)
		result = (result << 8) | *base++;
	return result;
}

//-------------------------------------------------
//  be_write - write a big-endian number to a byte
//  buffer
//-------------------------------------------------

inline void chd_file::be_write(UINT8 *base, UINT64 value, int numbytes)
{
	base += numbytes;
	while (numbytes--)
	{
		*--base = value;
		value >>= 8;
	}
}

//-------------------------------------------------
//  be_read_sha1 - fetch a sha1_t from a data
//  stream in bigendian order
//-------------------------------------------------

inline sha1_t chd_file::be_read_sha1(const UINT8 *base)
{
	sha1_t result;
	memcpy(&result.m_raw[0], base, sizeof(result.m_raw));
	return result;
}

//-------------------------------------------------
//  file_read - read from the file at the given
//  offset; on failure throw an error
//-------------------------------------------------

inline void chd_file::file_read(UINT64 offset, void *dest, UINT32 length)
{
	// no file = failure
	if (m_file == NULL)
		throw CHDERR_NOT_OPEN;

	// seek and read
	zfile_fseek(m_file, offset, SEEK_SET);
	UINT32 count = zfile_fread(dest, 1, length, m_file);
	if (count != length)
		throw CHDERR_READ_ERROR;
}

//-------------------------------------------------
//  hunk_info - return information about this
//  hunk
//-------------------------------------------------

chd_error chd_file::hunk_info(UINT32 hunknum, chd_codec_type &compressor, UINT32 &compbytes)
{
	// error if invalid
	if (hunknum >= m_hunkcount)
		return CHDERR_HUNK_OUT_OF_RANGE;

	// get the map pointer
	UINT8 *rawmap;
	switch (m_version)
	{
		// v3/v4 map entries
		case 3:
		case 4:
			rawmap = m_rawmap + 16 * hunknum;
			switch (rawmap[15] & V34_MAP_ENTRY_FLAG_TYPE_MASK)
			{
				case V34_MAP_ENTRY_TYPE_COMPRESSED:
					compressor = CHD_CODEC_ZLIB;
					compbytes = be_read(&rawmap[12], 2) + (rawmap[14] << 16);
					break;

				case V34_MAP_ENTRY_TYPE_UNCOMPRESSED:
					compressor = CHD_CODEC_NONE;
					compbytes = m_hunkbytes;
					break;

				case V34_MAP_ENTRY_TYPE_MINI:
					compressor = CHD_CODEC_MINI;
					compbytes = 0;
					break;

				case V34_MAP_ENTRY_TYPE_SELF_HUNK:
					compressor = CHD_CODEC_SELF;
					compbytes = 0;
					break;

				case V34_MAP_ENTRY_TYPE_PARENT_HUNK:
					compressor = CHD_CODEC_PARENT;
					compbytes = 0;
					break;
			}
			break;

		// v5 map entries
		case 5:
			rawmap = m_rawmap + m_mapentrybytes * hunknum;

			// uncompressed case
			if (!compressed())
			{
				if (be_read(&rawmap[0], 4) == 0)
				{
					compressor = CHD_CODEC_PARENT;
					compbytes = 0;
				}
				else
				{
					compressor = CHD_CODEC_NONE;
					compbytes = m_hunkbytes;
				}
				break;
			}

			// compressed case
			switch (rawmap[0])
			{
				case COMPRESSION_TYPE_0:
				case COMPRESSION_TYPE_1:
				case COMPRESSION_TYPE_2:
				case COMPRESSION_TYPE_3:
					compressor = m_compression[rawmap[0]];
					compbytes = be_read(&rawmap[1], 3);
					break;

				case COMPRESSION_NONE:
					compressor = CHD_CODEC_NONE;
					compbytes = m_hunkbytes;
					break;

				case COMPRESSION_SELF:
					compressor = CHD_CODEC_SELF;
					compbytes = 0;
					break;

				case COMPRESSION_PARENT:
					compressor = CHD_CODEC_PARENT;
					compbytes = 0;
					break;
			}
			break;
	}
	return CHDERR_NONE;
}

//-------------------------------------------------
//  open - open an existing file for read or
//  read/write
//-------------------------------------------------

chd_error chd_file::open(struct zfile *file, bool writeable, chd_file *parent)
{
	// make sure we don't already have a file open
	if (m_file != NULL)
		return CHDERR_ALREADY_OPEN;

	// open the file
	m_file = file;
	m_owns_file = false;
	m_parent = parent;
	return open_common(writeable);
}

//-------------------------------------------------
//  close - close a CHD file for access
//-------------------------------------------------

void chd_file::close()
{
	// reset file characteristics
	if (m_owns_file && m_file != NULL)
		zfile_fclose(m_file);
	m_file = NULL;
	m_owns_file = false;
	m_allow_reads = false;
	m_allow_writes = false;

	// reset core parameters from the header
	m_version = HEADER_VERSION;
	m_logicalbytes = 0;
	m_mapoffset = 0;
	m_metaoffset = 0;
	m_hunkbytes = 0;
	m_hunkcount = 0;
	m_unitbytes = 0;
	m_unitcount = 0;
	memset(m_compression, 0, sizeof(m_compression));
	m_parent = NULL;
	m_parent_missing = false;

	// reset key offsets within the header
	m_mapoffset_offset = 0;
	m_metaoffset_offset = 0;
	m_sha1_offset = 0;
	m_rawsha1_offset = 0;
	m_parentsha1_offset = 0;

	// reset map information
	m_mapentrybytes = 0;
	m_rawmap.reset();

	// reset compression management
	for (int decompnum = 0; decompnum < ARRAY_LENGTH(m_decompressor); decompnum++)
	{
		delete m_decompressor[decompnum];
		m_decompressor[decompnum] = NULL;
	}
	m_compressed.reset();

	// reset caching
	m_cache.reset();
	m_cachehunk = ~0;
}


//-------------------------------------------------
//  read - read a single hunk from the CHD file
//-------------------------------------------------

chd_error chd_file::read_hunk(UINT32 hunknum, void *buffer)
{
	// wrap this for clean reporting
	try
	{
		// punt if no file
		if (m_file == NULL)
			throw CHDERR_NOT_OPEN;

		// return an error if out of range
		if (hunknum >= m_hunkcount)
			throw CHDERR_HUNK_OUT_OF_RANGE;

		// get a pointer to the map entry
		UINT64 blockoffs;
		UINT32 blocklen;
		UINT32 blockcrc;
		UINT8 *rawmap;
		UINT8 *dest = reinterpret_cast<UINT8 *>(buffer);
		switch (m_version)
		{
			// v3/v4 map entries
			case 3:
			case 4:
				rawmap = m_rawmap + 16 * hunknum;
				blockoffs = be_read(&rawmap[0], 8);
				blockcrc = be_read(&rawmap[8], 4);
				switch (rawmap[15] & V34_MAP_ENTRY_FLAG_TYPE_MASK)
				{
					case V34_MAP_ENTRY_TYPE_COMPRESSED:
						blocklen = be_read(&rawmap[12], 2) + (rawmap[14] << 16);
						file_read(blockoffs, m_compressed, blocklen);
						m_decompressor[0]->decompress(m_compressed, blocklen, dest, m_hunkbytes);
						if (!(rawmap[15] & V34_MAP_ENTRY_FLAG_NO_CRC) && dest != NULL && crc32_creator::simple(dest, m_hunkbytes) != blockcrc)
							throw CHDERR_DECOMPRESSION_ERROR;
						return CHDERR_NONE;

					case V34_MAP_ENTRY_TYPE_UNCOMPRESSED:
						file_read(blockoffs, dest, m_hunkbytes);
						if (!(rawmap[15] & V34_MAP_ENTRY_FLAG_NO_CRC) && crc32_creator::simple(dest, m_hunkbytes) != blockcrc)
							throw CHDERR_DECOMPRESSION_ERROR;
						return CHDERR_NONE;

					case V34_MAP_ENTRY_TYPE_MINI:
						be_write(dest, blockoffs, 8);
						for (UINT32 bytes = 8; bytes < m_hunkbytes; bytes++)
							dest[bytes] = dest[bytes - 8];
						if (!(rawmap[15] & V34_MAP_ENTRY_FLAG_NO_CRC) && crc32_creator::simple(dest, m_hunkbytes) != blockcrc)
							throw CHDERR_DECOMPRESSION_ERROR;
						return CHDERR_NONE;

					case V34_MAP_ENTRY_TYPE_SELF_HUNK:
						return read_hunk(blockoffs, dest);

					case V34_MAP_ENTRY_TYPE_PARENT_HUNK:
						if (m_parent_missing)
							throw CHDERR_REQUIRES_PARENT;
						return m_parent->read_hunk(blockoffs, dest);
				}
				break;

			// v5 map entries
			case 5:
				rawmap = m_rawmap + m_mapentrybytes * hunknum;

				// uncompressed case
				if (!compressed())
				{
					blockoffs = UINT64(be_read(rawmap, 4)) * UINT64(m_hunkbytes);
					if (blockoffs != 0)
						file_read(blockoffs, dest, m_hunkbytes);
					else if (m_parent_missing)
						throw CHDERR_REQUIRES_PARENT;
					else if (m_parent != NULL)
						m_parent->read_hunk(hunknum, dest);
					else
						memset(dest, 0, m_hunkbytes);
					return CHDERR_NONE;
				}

				// compressed case
				blocklen = be_read(&rawmap[1], 3);
				blockoffs = be_read(&rawmap[4], 6);
				blockcrc = be_read(&rawmap[10], 2);
				switch (rawmap[0])
				{
					case COMPRESSION_TYPE_0:
					case COMPRESSION_TYPE_1:
					case COMPRESSION_TYPE_2:
					case COMPRESSION_TYPE_3:
						file_read(blockoffs, m_compressed, blocklen);
						m_decompressor[rawmap[0]]->decompress(m_compressed, blocklen, dest, m_hunkbytes);
						if (!m_decompressor[rawmap[0]]->lossy() && dest != NULL && crc16_creator::simple(dest, m_hunkbytes) != blockcrc)
							throw CHDERR_DECOMPRESSION_ERROR;
						if (m_decompressor[rawmap[0]]->lossy() && crc16_creator::simple(m_compressed, blocklen) != blockcrc)
							throw CHDERR_DECOMPRESSION_ERROR;
						return CHDERR_NONE;

					case COMPRESSION_NONE:
						file_read(blockoffs, dest, m_hunkbytes);
						if (crc16_creator::simple(dest, m_hunkbytes) != blockcrc)
							throw CHDERR_DECOMPRESSION_ERROR;
						return CHDERR_NONE;

					case COMPRESSION_SELF:
						return read_hunk(blockoffs, dest);

					case COMPRESSION_PARENT:
						if (m_parent_missing)
							throw CHDERR_REQUIRES_PARENT;
						return m_parent->read_bytes(UINT64(blockoffs) * UINT64(m_parent->unit_bytes()), dest, m_hunkbytes);
				}
				break;
		}

		// if we get here, something was wrong
		throw CHDERR_READ_ERROR;
	}

	// just return errors
	catch (chd_error &err)
	{
		return err;
	}
}

//-------------------------------------------------
//  read_bytes - read from the CHD at a byte level,
//  using the cache to handle partial hunks
//-------------------------------------------------

chd_error chd_file::read_bytes(UINT64 offset, void *buffer, UINT32 bytes)
{
	// iterate over hunks
	UINT32 first_hunk = offset / m_hunkbytes;
	UINT32 last_hunk = (offset + bytes - 1) / m_hunkbytes;
	UINT8 *dest = reinterpret_cast<UINT8 *>(buffer);
	for (UINT32 curhunk = first_hunk; curhunk <= last_hunk; curhunk++)
	{
		// determine start/end boundaries
		UINT32 startoffs = (curhunk == first_hunk) ? (offset % m_hunkbytes) : 0;
		UINT32 endoffs = (curhunk == last_hunk) ? ((offset + bytes - 1) % m_hunkbytes) : (m_hunkbytes - 1);

		// if it's a full block, just read directly from disk unless it's the cached hunk
		chd_error err = CHDERR_NONE;
		if (startoffs == 0 && endoffs == m_hunkbytes - 1 && curhunk != m_cachehunk)
			err = read_hunk(curhunk, dest);

		// otherwise, read from the cache
		else
		{
			if (curhunk != m_cachehunk)
			{
				err = read_hunk(curhunk, m_cache);
				if (err != CHDERR_NONE)
					return err;
				m_cachehunk = curhunk;
			}
			memcpy(dest, &m_cache[startoffs], endoffs + 1 - startoffs);
		}

		// handle errors and advance
		if (err != CHDERR_NONE)
			return err;
		dest += endoffs + 1 - startoffs;
	}
	return CHDERR_NONE;
}


//-------------------------------------------------
//  read_metadata - read the indexed metadata
//  of the given type
//-------------------------------------------------

chd_error chd_file::read_metadata(chd_metadata_tag searchtag, UINT32 searchindex, astring &output)
{
	// wrap this for clean reporting
	try
	{
		// if we didn't find it, just return
		metadata_entry metaentry;
		if (!metadata_find(searchtag, searchindex, metaentry))
			throw CHDERR_METADATA_NOT_FOUND;

		// read the metadata
		// TODO: how to properly allocate a dynamic char buffer?
		char* metabuf = new char[metaentry.length+1];
		memset(metabuf, 0x00, metaentry.length+1);
		file_read(metaentry.offset + METADATA_HEADER_SIZE, metabuf, metaentry.length);
		output.cpy(metabuf);
		delete[] metabuf;
		return CHDERR_NONE;
	}

	// just return errors
	catch (chd_error &err)
	{
		return err;
	}
}

chd_error chd_file::read_metadata(chd_metadata_tag searchtag, UINT32 searchindex, dynamic_buffer &output)
{
	// wrap this for clean reporting
	try
	{
		// if we didn't find it, just return
		metadata_entry metaentry;
		if (!metadata_find(searchtag, searchindex, metaentry))
			throw CHDERR_METADATA_NOT_FOUND;

		// read the metadata
		output.resize(metaentry.length);
		file_read(metaentry.offset + METADATA_HEADER_SIZE, output, metaentry.length);
		return CHDERR_NONE;
	}

	// just return errors
	catch (chd_error &err)
	{
		return err;
	}
}

chd_error chd_file::read_metadata(chd_metadata_tag searchtag, UINT32 searchindex, void *output, UINT32 outputlen, UINT32 &resultlen)
{
	// wrap this for clean reporting
	try
	{
		// if we didn't find it, just return
		metadata_entry metaentry;
		if (!metadata_find(searchtag, searchindex, metaentry))
			throw CHDERR_METADATA_NOT_FOUND;

		// read the metadata
		resultlen = metaentry.length;
		file_read(metaentry.offset + METADATA_HEADER_SIZE, output, MIN(outputlen, resultlen));
		return CHDERR_NONE;
	}

	// just return errors
	catch (chd_error &err)
	{
		return err;
	}
}

chd_error chd_file::read_metadata(chd_metadata_tag searchtag, UINT32 searchindex, dynamic_buffer &output, chd_metadata_tag &resulttag, UINT8 &resultflags)
{
	// wrap this for clean reporting
	try
	{
		// if we didn't find it, just return
		metadata_entry metaentry;
		if (!metadata_find(searchtag, searchindex, metaentry))
			throw CHDERR_METADATA_NOT_FOUND;

		// read the metadata
		output.resize(metaentry.length);
		file_read(metaentry.offset + METADATA_HEADER_SIZE, output, metaentry.length);
		resulttag = metaentry.metatag;
		resultflags = metaentry.flags;
		return CHDERR_NONE;
	}

	// just return errors
	catch (chd_error &err)
	{
		return err;
	}
}


//-------------------------------------------------
//  guess_unitbytes - for older CHD formats, take
//  a guess at the bytes/unit based on metadata
//-------------------------------------------------

UINT32 chd_file::guess_unitbytes()
{
	// look for hard disk metadata; if found, then the unit size == sector size
	astring metadata;
	int i0, i1, i2, i3;
	if (read_metadata(HARD_DISK_METADATA_TAG, 0, metadata) == CHDERR_NONE && sscanf(metadata, HARD_DISK_METADATA_FORMAT, &i0, &i1, &i2, &i3) == 4)
		return i3;

	// look for CD-ROM metadata; if found, then the unit size == CD frame size
	if (read_metadata(CDROM_OLD_METADATA_TAG, 0, metadata) == CHDERR_NONE ||
		read_metadata(CDROM_TRACK_METADATA_TAG, 0, metadata) == CHDERR_NONE ||
		read_metadata(CDROM_TRACK_METADATA2_TAG, 0, metadata) == CHDERR_NONE ||
		read_metadata(GDROM_TRACK_METADATA_TAG, 0, metadata) == CHDERR_NONE)
		return CD_FRAME_SIZE;

	// otherwise, just map 1:1 with the hunk size
	return m_hunkbytes;
}


//-------------------------------------------------
//  parse_v3_header - parse the header from a v3
//  file and configure core parameters
//-------------------------------------------------

void chd_file::parse_v3_header(UINT8 *rawheader, sha1_t &parentsha1)
{
	// verify header length
	if (be_read(&rawheader[8], 4) != V3_HEADER_SIZE)
		throw CHDERR_INVALID_FILE;

	// extract core info
	m_logicalbytes = be_read(&rawheader[28], 8);
	m_mapoffset = 120;
	m_metaoffset = be_read(&rawheader[36], 8);
	m_hunkbytes = be_read(&rawheader[76], 4);
	m_hunkcount = be_read(&rawheader[24], 4);

	// extract parent SHA-1
	UINT32 flags = be_read(&rawheader[16], 4);
	m_allow_writes = (flags & 2) == 0;

	// determine compression
	switch (be_read(&rawheader[20], 4))
	{
		case 0:	m_compression[0] = CHD_CODEC_NONE;		break;
		case 1:	m_compression[0] = CHD_CODEC_ZLIB;		break;
		case 2:	m_compression[0] = CHD_CODEC_ZLIB;		break;
		case 3:	m_compression[0] = CHD_CODEC_AVHUFF;	break;
		default: throw CHDERR_UNKNOWN_COMPRESSION;
	}
	m_compression[1] = m_compression[2] = m_compression[3] = CHD_CODEC_NONE;

	// describe the format
	m_mapoffset_offset = 0;
	m_metaoffset_offset = 36;
	m_sha1_offset = 80;
	m_rawsha1_offset = 0;
	m_parentsha1_offset = 100;

	// determine properties of map entries
	m_mapentrybytes = 16;

	// extract parent SHA-1
	if (flags & 1)
		parentsha1 = be_read_sha1(&rawheader[m_parentsha1_offset]);

	// guess at the units based on snooping the metadata
	m_unitbytes = guess_unitbytes();
	m_unitcount = (m_logicalbytes + m_unitbytes - 1) / m_unitbytes;
}


//-------------------------------------------------
//  parse_v4_header - parse the header from a v4
//  file and configure core parameters
//-------------------------------------------------

void chd_file::parse_v4_header(UINT8 *rawheader, sha1_t &parentsha1)
{
	// verify header length
	if (be_read(&rawheader[8], 4) != V4_HEADER_SIZE)
		throw CHDERR_INVALID_FILE;

	// extract core info
	m_logicalbytes = be_read(&rawheader[28], 8);
	m_mapoffset = 108;
	m_metaoffset = be_read(&rawheader[36], 8);
	m_hunkbytes = be_read(&rawheader[44], 4);
	m_hunkcount = be_read(&rawheader[24], 4);

	// extract parent SHA-1
	UINT32 flags = be_read(&rawheader[16], 4);
	m_allow_writes = (flags & 2) == 0;

	// determine compression
	switch (be_read(&rawheader[20], 4))
	{
		case 0:	m_compression[0] = CHD_CODEC_NONE;		break;
		case 1:	m_compression[0] = CHD_CODEC_ZLIB;		break;
		case 2:	m_compression[0] = CHD_CODEC_ZLIB;		break;
		case 3:	m_compression[0] = CHD_CODEC_AVHUFF;	break;
		default: throw CHDERR_UNKNOWN_COMPRESSION;
	}
	m_compression[1] = m_compression[2] = m_compression[3] = CHD_CODEC_NONE;

	// describe the format
	m_mapoffset_offset = 0;
	m_metaoffset_offset = 36;
	m_sha1_offset = 48;
	m_rawsha1_offset = 88;
	m_parentsha1_offset = 68;

	// determine properties of map entries
	m_mapentrybytes = 16;

	// extract parent SHA-1
	if (flags & 1)
		parentsha1 = be_read_sha1(&rawheader[m_parentsha1_offset]);

	// guess at the units based on snooping the metadata
	m_unitbytes = guess_unitbytes();
	m_unitcount = (m_logicalbytes + m_unitbytes - 1) / m_unitbytes;
}


//-------------------------------------------------
//  parse_v5_header - read the header from a v5
//  file and configure core parameters
//-------------------------------------------------

void chd_file::parse_v5_header(UINT8 *rawheader, sha1_t &parentsha1)
{
	// verify header length
	if (be_read(&rawheader[8], 4) != V5_HEADER_SIZE)
		throw CHDERR_INVALID_FILE;

	// extract core info
	m_logicalbytes = be_read(&rawheader[32], 8);
	m_mapoffset = be_read(&rawheader[40], 8);
	m_metaoffset = be_read(&rawheader[48], 8);
	m_hunkbytes = be_read(&rawheader[56], 4);
	m_hunkcount = (m_logicalbytes + m_hunkbytes - 1) / m_hunkbytes;
	m_unitbytes = be_read(&rawheader[60], 4);
	m_unitcount = (m_logicalbytes + m_unitbytes - 1) / m_unitbytes;

	// determine compression
	m_compression[0] = be_read(&rawheader[16], 4);
	m_compression[1] = be_read(&rawheader[20], 4);
	m_compression[2] = be_read(&rawheader[24], 4);
	m_compression[3] = be_read(&rawheader[28], 4);

	m_allow_writes = !compressed();

	// describe the format
	m_mapoffset_offset = 40;
	m_metaoffset_offset = 48;
	m_sha1_offset = 84;
	m_rawsha1_offset = 64;
	m_parentsha1_offset = 104;

	// determine properties of map entries
	m_mapentrybytes = compressed() ? 12 : 4;

	// extract parent SHA-1
	parentsha1 = be_read_sha1(&rawheader[m_parentsha1_offset]);
}


//-------------------------------------------------
//  open_common - common path when opening an
//  existing CHD file for input
//-------------------------------------------------

chd_error chd_file::open_common(bool writeable)
{
	// wrap in try for proper error handling
	try
	{
		// reads are always permitted
		m_allow_reads = true;

		// read the raw header
		UINT8 rawheader[MAX_HEADER_SIZE];
		file_read(0, rawheader, sizeof(rawheader));

		// verify the signature
		if (memcmp(rawheader, "MComprHD", 8) != 0)
			throw CHDERR_INVALID_FILE;

		// only allow writes to the most recent version
		m_version = be_read(&rawheader[12], 4);
		if (writeable && m_version < HEADER_VERSION)
			throw CHDERR_UNSUPPORTED_VERSION;

		// read the header if we support it
		sha1_t parentsha1 = sha1_t::null;
		switch (m_version)
		{
			case 3:		parse_v3_header(rawheader, parentsha1);	break;
			case 4:		parse_v4_header(rawheader, parentsha1);	break;
			case 5:		parse_v5_header(rawheader, parentsha1);	break;
			default:	throw CHDERR_UNSUPPORTED_VERSION;
		}

		if (writeable && !m_allow_writes)
			throw CHDERR_FILE_NOT_WRITEABLE;

		// make sure we have a parent if we need one (and don't if we don't)
		if (parentsha1 != sha1_t::null)
		{
			if (m_parent == NULL)
				m_parent_missing = true;
			else if (m_parent->sha1() != parentsha1)
				throw CHDERR_INVALID_PARENT;
		}
		else if (m_parent != NULL)
			throw CHDERR_INVALID_PARAMETER;

		// finish opening the file
		create_open_common();
		return CHDERR_NONE;
	}

	// handle errors by closing ourself
	catch (chd_error &err)
	{
		close();
		return err;
	}
}

//-------------------------------------------------
//  create_open_common - common code for handling
//  creation and opening of a file
//-------------------------------------------------

void chd_file::create_open_common()
{
	// verify the compression types and initialize the codecs
	for (int decompnum = 0; decompnum < ARRAY_LENGTH(m_compression); decompnum++)
	{
		m_decompressor[decompnum] = chd_codec_list::new_decompressor(m_compression[decompnum], *this);
		if (m_decompressor[decompnum] == NULL && m_compression[decompnum] != 0)
			throw CHDERR_UNKNOWN_COMPRESSION;
	}

	// read the map; v5+ compressed drives need to read and decompress their map
	m_rawmap.resize(m_hunkcount * m_mapentrybytes);
	if (m_version >= 5 && compressed())
		decompress_v5_map();
	else
		file_read(m_mapoffset, m_rawmap, m_rawmap.count());

	// allocate the temporary compressed buffer and a buffer for caching
	m_compressed.resize(m_hunkbytes);
	m_cache.resize(m_hunkbytes);
}


//-------------------------------------------------
//  metadata_find - find a metadata entry
//-------------------------------------------------

bool chd_file::metadata_find(chd_metadata_tag metatag, INT32 metaindex, metadata_entry &metaentry, bool resume)
{
	// start at the beginning unless we're resuming a previous search
	if (!resume)
	{
		metaentry.offset = m_metaoffset;
		metaentry.prev = 0;
	}
	else
	{
		metaentry.prev = metaentry.offset;
		metaentry.offset = metaentry.next;
	}

	// loop until we run out of options
	while (metaentry.offset != 0)
	{
		// read the raw header
		UINT8 raw_meta_header[METADATA_HEADER_SIZE];
		file_read(metaentry.offset, raw_meta_header, sizeof(raw_meta_header));

		// extract the data
		metaentry.metatag = be_read(&raw_meta_header[0], 4);
		metaentry.flags = raw_meta_header[4];
		metaentry.length = be_read(&raw_meta_header[5], 3);
		metaentry.next = be_read(&raw_meta_header[8], 8);

		// if we got a match, proceed
		if (metatag == CHDMETATAG_WILDCARD || metaentry.metatag == metatag)
			if (metaindex-- == 0)
				return true;

		// no match, fetch the next link
		metaentry.prev = metaentry.offset;
		metaentry.offset = metaentry.next;
	}

	// if we get here, we didn't find it
	return false;
}


//-------------------------------------------------
//  decompress_v5_map - decompress the v5 map
//-------------------------------------------------

void chd_file::decompress_v5_map()
{
	// if no offset, we haven't written it yet
	if (m_mapoffset == 0)
	{
		memset(m_rawmap, 0xff, m_rawmap.count());
		return;
	}

	// read the reader
	UINT8 rawbuf[16];
	file_read(m_mapoffset, rawbuf, sizeof(rawbuf));
	UINT32 mapbytes = be_read(&rawbuf[0], 4);
	UINT64 firstoffs = be_read(&rawbuf[4], 6);
	UINT16 mapcrc = be_read(&rawbuf[10], 2);
	UINT8 lengthbits = rawbuf[12];
	UINT8 selfbits = rawbuf[13];
	UINT8 parentbits = rawbuf[14];

	// now read the map
	dynamic_buffer compressed(mapbytes);
	file_read(m_mapoffset + 16, compressed, mapbytes);
	bitstream_in bitbuf(compressed, compressed.count());

	// first decode the compression types
	huffman_decoder<16, 8> decoder;
	huffman_error err = decoder.import_tree_rle(bitbuf);
	if (err != HUFFERR_NONE)
		throw CHDERR_DECOMPRESSION_ERROR;
	UINT8 lastcomp = 0;
	int repcount = 0;
	for (int hunknum = 0; hunknum < m_hunkcount; hunknum++)
	{
		UINT8 *rawmap = &m_rawmap[hunknum * 12];
		if (repcount > 0)
			rawmap[0] = lastcomp, repcount--;
		else
		{
			UINT8 val = decoder.decode_one(bitbuf);
			if (val == COMPRESSION_RLE_SMALL)
				rawmap[0] = lastcomp, repcount = 2 + decoder.decode_one(bitbuf);
			else if (val == COMPRESSION_RLE_LARGE)
				rawmap[0] = lastcomp, repcount = 2 + 16 + (decoder.decode_one(bitbuf) << 4), repcount += decoder.decode_one(bitbuf);
			else
				rawmap[0] = lastcomp = val;
		}
	}

	// then iterate through the hunks and extract the needed data
	UINT64 curoffset = firstoffs;
	UINT32 last_self = 0;
	UINT64 last_parent = 0;
	for (int hunknum = 0; hunknum < m_hunkcount; hunknum++)
	{
		UINT8 *rawmap = &m_rawmap[hunknum * 12];
		UINT64 offset = curoffset;
		UINT32 length = 0;
		UINT16 crc = 0;
		switch (rawmap[0])
		{
			// base types
			case COMPRESSION_TYPE_0:
			case COMPRESSION_TYPE_1:
			case COMPRESSION_TYPE_2:
			case COMPRESSION_TYPE_3:
				curoffset += length = bitbuf.read(lengthbits);
				crc = bitbuf.read(16);
				break;

			case COMPRESSION_NONE:
				curoffset += length = m_hunkbytes;
				crc = bitbuf.read(16);
				break;

			case COMPRESSION_SELF:
				last_self = offset = bitbuf.read(selfbits);
				break;

			case COMPRESSION_PARENT:
				offset = bitbuf.read(parentbits);
				last_parent = offset;
				break;

			// pseudo-types; convert into base types
			case COMPRESSION_SELF_1:
				last_self++;
			case COMPRESSION_SELF_0:
				rawmap[0] = COMPRESSION_SELF;
				offset = last_self;
				break;

			case COMPRESSION_PARENT_SELF:
				rawmap[0] = COMPRESSION_PARENT;
				last_parent = offset = (UINT64(hunknum) * UINT64(m_hunkbytes)) / m_unitbytes;
				break;

			case COMPRESSION_PARENT_1:
				last_parent += m_hunkbytes / m_unitbytes;
			case COMPRESSION_PARENT_0:
				rawmap[0] = COMPRESSION_PARENT;
				offset = last_parent;
				break;
		}
		be_write(&rawmap[1], length, 3);
		be_write(&rawmap[4], offset, 6);
		be_write(&rawmap[10], crc, 2);
	}

	// verify the final CRC
	if (crc16_creator::simple(m_rawmap, m_hunkcount * 12) != mapcrc)
		throw CHDERR_DECOMPRESSION_ERROR;
}

//-------------------------------------------------
//  sha1 - return our SHA1 value
//-------------------------------------------------

sha1_t chd_file::sha1()
{
	try
	{
		// read the big-endian version
		UINT8 rawbuf[sizeof(sha1_t)];
		file_read(m_sha1_offset, rawbuf, sizeof(rawbuf));
		return be_read_sha1(rawbuf);
	}
	catch (chd_error &)
	{
		// on failure, return NULL
		return sha1_t::null;
	}
}


//-------------------------------------------------
//  raw_sha1 - return our raw SHA1 value
//-------------------------------------------------

sha1_t chd_file::raw_sha1()
{
	try
	{
		// determine offset within the file for data-only
		if (m_rawsha1_offset == 0)
			throw CHDERR_UNSUPPORTED_VERSION;

		// read the big-endian version
		UINT8 rawbuf[sizeof(sha1_t)];
		file_read(m_rawsha1_offset, rawbuf, sizeof(rawbuf));
		return be_read_sha1(rawbuf);
	}
	catch (chd_error &)
	{
		// on failure, return NULL
		return sha1_t::null;
	}
}


//-------------------------------------------------
//  parent_sha1 - return our parent's SHA1 value
//-------------------------------------------------

sha1_t chd_file::parent_sha1()
{
	try
	{
		// determine offset within the file
		if (m_parentsha1_offset == 0)
			throw CHDERR_UNSUPPORTED_VERSION;

		// read the big-endian version
		UINT8 rawbuf[sizeof(sha1_t)];
		file_read(m_parentsha1_offset, rawbuf, sizeof(rawbuf));
		return be_read_sha1(rawbuf);
	}
	catch (chd_error &)
	{
		// on failure, return NULL
		return sha1_t::null;
	}
}

//**************************************************************************
//  CHD FILE MANAGEMENT
//**************************************************************************

//-------------------------------------------------
//  chd_file - constructor
//-------------------------------------------------

chd_file::chd_file()
	: m_file(NULL),
      m_owns_file(false)
{
	// reset state
	memset(m_decompressor, 0, sizeof(m_decompressor));
	close();
}


//-------------------------------------------------
//  ~chd_file - destructor
//-------------------------------------------------

chd_file::~chd_file()
{
	// close any open files
	close();
}
