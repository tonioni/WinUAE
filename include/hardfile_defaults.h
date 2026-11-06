#ifndef UAE_HARDFILE_DEFAULTS_H
#define UAE_HARDFILE_DEFAULTS_H

// Both the core mount configuration and frontend geometry use these fields.
template<typename Geometry>
static inline void hardfile_geometry_defaults(Geometry *geometry, bool rdb)
{
	geometry->sectors = rdb ? 0 : 32;
	geometry->surfaces = rdb ? 0 : 1;
	geometry->reserved = rdb ? 0 : 2;
	geometry->blocksize = 512;
}

#endif
