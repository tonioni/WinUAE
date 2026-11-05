#include "sysconfig.h"
#include "sysdeps.h"
#include "clipboard_common.h"
#include "traps.h"
#include "native2amiga_api.h"

static int failures;
static uae_u8 guest[4096];
static int signals;
static bool signal_free = true;

static void check(bool ok, const char *name)
{
	if (!ok) {
		fprintf(stderr, "FAIL: %s\n", name);
		failures++;
	}
}

void write_log(const TCHAR *, ...) {}
int native2amiga_isfree(void) { return signal_free; }
void uae_Signal(uaecptr task, uae_u32 mask)
{
	check(task == 0x1234 && mask == (1 << 13), "clipboard task signal");
	signals++;
}
void trap_get_bytes(TrapContext *, void *p, uaecptr addr, int size)
{
	if (addr > sizeof guest || size < 0 || (size_t)size > sizeof guest - addr) {
		check(false, "guest read bounds");
		return;
	}
	memcpy(p, guest + addr, size);
}
void trap_put_bytes(TrapContext *, const void *p, uaecptr addr, int size)
{
	if (addr > sizeof guest || size < 0 || (size_t)size > sizeof guest - addr) {
		check(false, "guest write bounds");
		return;
	}
	memcpy(guest + addr, p, size);
}
uae_u32 trap_get_long(TrapContext *ctx, uaecptr addr)
{
	uae_u8 b[4];
	trap_get_bytes(ctx, b, addr, 4);
	return ((uae_u32)b[0] << 24) | ((uae_u32)b[1] << 16) | (b[2] << 8) | b[3];
}
void trap_put_long(TrapContext *ctx, uaecptr addr, uae_u32 n)
{
	uae_u8 b[] = { (uae_u8)(n >> 24), (uae_u8)(n >> 16), (uae_u8)(n >> 8), (uae_u8)n };
	trap_put_bytes(ctx, b, addr, 4);
}
void trap_callback(TRAP_CALLBACK callback, void *p) { callback(NULL, p); }

static void be32(std::vector<uae_u8> &v, uae_u32 n)
{
	v.push_back(n >> 24); v.push_back(n >> 16);
	v.push_back(n >> 8); v.push_back(n);
}
static void chunk(std::vector<uae_u8> &v, const char *id, const std::vector<uae_u8> &data)
{
	v.insert(v.end(), id, id + 4);
	be32(v, (uae_u32)data.size());
	v.insert(v.end(), data.begin(), data.end());
	if (data.size() & 1) v.push_back(0);
}
static void form_size(std::vector<uae_u8> &v)
{
	uae_u32 n = (uae_u32)v.size() - 8;
	v[4] = n >> 24; v[5] = n >> 16; v[6] = n >> 8; v[7] = n;
}
static void text_tests()
{
	std::vector<uae_u8> v = clipboard_encode_text("a\r\nb");
	const uae_u8 expected[] = { 'F','O','R','M',0,0,0,16,'F','T','X','T',
		'C','H','R','S',0,0,0,3,'a','\n','b',0 };
	check(v.size() == sizeof expected && !memcmp(v.data(), expected, sizeof expected), "FTXT encoding and odd padding");
	std::string text;
	check(clipboard_decode_text(v.data(), (uae_u32)v.size(), &text, false) && text == "a\nb", "LF text");
	check(clipboard_decode_text(v.data(), (uae_u32)v.size(), &text, true) && text == "a\r\nb", "CRLF text");
	v.resize(12);
	chunk(v, "JUNK", { 1 });
	chunk(v, "CHRS", { 'a', 0x1b, '[', '3', '1', 'm' });
	chunk(v, "CHRS", { 'b', '\r', '\n', 'c' });
	form_size(v);
	check(clipboard_decode_text(v.data(), (uae_u32)v.size(), &text, false) && text == "ab\nc", "multiple chunks and CSI");
	check(clipboard_decode_text(v.data(), (uae_u32)v.size(), &text, true) && text == "a\033[31mb\r\nc", "existing CR preserves Windows text");
	v.resize(12);
	chunk(v, "CHRS", { 'a', 0, 'b' });
	form_size(v);
	check(clipboard_decode_text(v.data(), (uae_u32)v.size(), &text, false) && text == "ab", "Unix embedded NUL");
	check(clipboard_decode_text(v.data(), (uae_u32)v.size(), &text, true) && text == "a", "Windows NUL terminates text");
	for (uae_u32 i = 0; i < 12; i++) {
		check(!clipboard_decode_text(v.data(), i, &text, false), "short text header");
	}
	v = clipboard_encode_text("");
	check(clipboard_decode_text(v.data(), (uae_u32)v.size(), &text, false) && text.empty(), "empty text");
}

static std::vector<uae_u8> planar(int planes, uae_u32 mode, int value, bool compressed, bool mask)
{
	std::vector<uae_u8> v = { 'F','O','R','M',0,0,0,0,'I','L','B','M' };
	std::vector<uae_u8> bmhd(20);
	bmhd[1] = 1; bmhd[3] = 1; bmhd[8] = planes;
	bmhd[9] = mask ? 1 : 0; bmhd[10] = compressed ? 1 : 0;
	chunk(v, "BMHD", bmhd);
	std::vector<uae_u8> camg; be32(camg, mode); chunk(v, "CAMG", camg);
	std::vector<uae_u8> cmap(256 * 3);
	cmap[0] = 0x12; cmap[1] = 0x34; cmap[2] = 0x56;
	cmap[3] = 0xab; cmap[4] = 0xcd; cmap[5] = 0xef;
	chunk(v, "CMAP", cmap);
	std::vector<uae_u8> body;
	for (int p = 0; p < planes + (mask ? 1 : 0); p++) {
		if (compressed) { body.push_back(128); body.push_back(1); }
		body.push_back((value & (1 << p)) ? 128 : 0);
		body.push_back(0);
	}
	chunk(v, "BODY", body); form_size(v);
	return v;
}
static void bitmap_tests()
{
	clipboard_bitmap src, dst;
	std::vector<uae_u8> v;
	for (int colors : { 1, 2, 255, 256, 257 }) {
		src.width = 257; src.height = 2;
		src.pixels.clear();
		for (int i = 0; i < src.width * src.height; i++) src.pixels.push_back(0x123400 + i % colors);
		check(clipboard_encode_bitmap(src, false, &v), "encode indexed/truecolor bitmap");
		check(clipboard_decode_bitmap(v.data(), (uae_u32)v.size(), &dst) &&
			dst.width == src.width && dst.height == src.height && dst.pixels == src.pixels,
			"bitmap roundtrip across palette cutoff");
	}
	for (bool compressed : { false, true }) {
		for (bool mask : { false, true }) {
			v = planar(2, 0, 1, compressed, mask);
			check(clipboard_decode_bitmap(v.data(), (uae_u32)v.size(), &dst) && dst.pixels[0] == 0xabcdef,
				"indexed bitmap, mask and ByteRun1");
		}
	}
	v = planar(6, 0x80, 33, true, false);
	check(clipboard_decode_bitmap(v.data(), (uae_u32)v.size(), &dst) && dst.pixels[0] == 0x556677, "EHB color");
	v = planar(6, 0x800, 0x1f, false, false);
	check(clipboard_decode_bitmap(v.data(), (uae_u32)v.size(), &dst) && dst.pixels[0] == 0xf0, "HAM6 blue");
	v = planar(8, 0x800, 0x7f, true, false);
	check(clipboard_decode_bitmap(v.data(), (uae_u32)v.size(), &dst) && dst.pixels[0] == 0xfc, "HAM8 blue");
	v = planar(24, 0, 0x563412, false, false);
	check(clipboard_decode_bitmap(v.data(), (uae_u32)v.size(), &dst) && dst.pixels[0] == 0x123456, "24-plane component order");
	v = planar(12, 0, 0x321, false, false);
	check(clipboard_decode_bitmap(v.data(), (uae_u32)v.size(), &dst, true, true) && dst.pixels[0] == 0x010203, "Windows reduced RGB depth");
	check(!clipboard_decode_bitmap(v.data(), (uae_u32)v.size(), &dst), "Unix RGB depth policy");
	v = planar(2, 0, 1, true, false);
	for (uae_u32 i = 0; i < v.size(); i++) {
		check(!clipboard_decode_bitmap(v.data(), i, &dst), "truncated image");
	}
	src.width = 100; src.height = 100; src.pixels.assign(10000, 0);
	check(!clipboard_encode_bitmap(src, true, &v), "initial bitmap size limit");
	check(clipboard_encode_bitmap(src, false, &v), "later bitmap size limit");
}
static void transfer_tests()
{
	for (bool deferred : { false, true }) {
		clipboard_transfer t;
		t.pending = clipboard_encode_text("transfer");
		t.start(); check(t.phase == 0, "wait for guest initialization");
		t.guest_data = 16;
		trap_put_long(NULL, 24, 0x1234);
		t.signaling = true;
		int initial_signals = signals;
		t.vsync(deferred);
		check(signals == initial_signals + 1, "signal guest before clipboard initialization");
		t.signaling = false;
		t.initialized = true;
		trap_put_long(NULL, 16, 0); trap_put_long(NULL, 20, 128); trap_put_long(NULL, 24, 0x1234);
		t.start(); check(t.phase == 1, "pending transfer");
		t.unsafe_period();
		for (int i = 0; i < 99; i++) t.vsync(deferred);
		check(t.phase == 1 && trap_get_long(NULL, 16) == 0, "unsafe period");
		t.vsync(deferred);
		check(t.phase == 2 && trap_get_long(NULL, 16) == t.pending.size(), "announce transfer");
		std::vector<uae_u8> expected = t.pending;
		check(t.want_data(NULL) == 1 && !memcmp(guest + 128, expected.data(), expected.size()), "guest receives IFF bytes");
		check(t.pending.empty() && t.phase == 0, "transfer released");
		t.pending = expected; t.start(); trap_put_long(NULL, 16, 999);
		check(t.want_data(NULL) == 0 && t.pending.empty() && t.phase == 0, "mismatched request released");
		t.signaling = true; t.delay = 2; t.unsafe_delay = 10;
		t.vsync(deferred);
		check(t.unsafe_delay == (deferred ? 10 : 9), "host signaling cadence");
		signal_free = false; int before = signals;
		t.vsync(deferred); check(signals == before, "busy native queue"); signal_free = true;
		t.pending = expected; t.start(); t.reset();
		check(!t.initialized && !t.signaling && !t.guest_data && t.pending.empty() && !t.phase, "reset clears transfer");
	}
}
int main()
{
	text_tests(); bitmap_tests(); transfer_tests();
	if (!failures) puts("Clipboard codec and transfer tests passed");
	return failures ? 1 : 0;
}
