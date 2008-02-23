
static void NOINLINE BLT_NAME (unsigned int w, unsigned int h, uae_u8 *src, uae_u8 *dst, int srcpitch, int dstpitch)
{
    uae_u8 *src2 = src;
    uae_u8 *dst2 = dst;
    uae_u8 *src2e = src + w;
    uae_u32 *src2_32 = (uae_u32*)src;
    uae_u32 *dst2_32 = (uae_u32*)dst;
    unsigned int y, x, ww, xxd;
    uae_u32 tmp;

    if (w < 8) {
    	for(y = 0; y < h; y++) {
	    uae_u32 *src_32 = (uae_u32*)src2;
	    uae_u32 *dst_32 = (uae_u32*)dst2;
	    for (x = 0; x < w; x++) {
		BLT_FUNC (src_32, dst_32);
		src_32++; dst_32++;
	    }
	    dst2 += dstpitch;
	    src2 += srcpitch;
	}
	return;
    }

    ww = w / 8;
    xxd = w - ww * 8;
    for(y = 0; y < h; y++) {
	uae_u32 *src_32 = (uae_u32*)src2;
	uae_u32 *dst_32 = (uae_u32*)dst2;
    	for (x = 0; x < ww; x++) {
	    BLT_FUNC (src_32, dst_32);
	    src_32++; dst_32++;
	    BLT_FUNC (src_32, dst_32);
	    src_32++; dst_32++;
	    BLT_FUNC (src_32, dst_32);
	    src_32++; dst_32++;
	    BLT_FUNC (src_32, dst_32);
	    src_32++; dst_32++;
	    BLT_FUNC (src_32, dst_32);
	    src_32++; dst_32++;
	    BLT_FUNC (src_32, dst_32);
	    src_32++; dst_32++;
	    BLT_FUNC (src_32, dst_32);
	    src_32++; dst_32++;
	    BLT_FUNC (src_32, dst_32);
	    src_32++; dst_32++;
	}
	for (x = 0; x < xxd; x++) {
	    BLT_FUNC (src_32, dst_32);
	    src_32++; dst_32++;
	}
	dst2 += dstpitch;
	src2 += srcpitch;
    }
}
#undef BLT_NAME
#undef BLT_FUNC
		