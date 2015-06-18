/*
* UAE - The Un*x Amiga Emulator
*
* PCI Bridge board emulation
*
* Copyright 2015 Toni Wilen
* Hardware information by Radoslaw Kujawa
*
*/
#include "sysconfig.h"
#include "sysdeps.h"

#include "options.h"
#include "custom.h"
#include "memory.h"
#include "debug.h"
#include "pci_hw.h"
#include "pci.h"
#include "ncr_scsi.h"
#include "newcpu.h"

#include "qemuvga/qemuuaeglue.h"
#include "qemuvga/queue.h"
#include "qemuvga/scsi/scsi.h"

#define PCI_BRIDGE_WILDFIRE 0
#define PCI_BRIDGE_GREX (PCI_BRIDGE_WILDFIRE + 1)
#define PCI_BRIDGE_XVISION (PCI_BRIDGE_GREX + 1)
#define PCI_BRIDGE_PROMETHEUS (PCI_BRIDGE_XVISION + 1)
#define PCI_BRIDGE_MEDIATOR (PCI_BRIDGE_PROMETHEUS * MAX_DUPLICATE_EXPANSION_BOARDS)
#define PCI_BRIDGE_MAX (PCI_BRIDGE_MEDIATOR * MAX_DUPLICATE_EXPANSION_BOARDS + 1)

static struct pci_bridge *bridges[PCI_BRIDGE_MAX + 1];
static int last_bridge_index;

extern addrbank pci_config_bank, pci_io_bank, pci_mem_bank, pci_bridge_bank;

static void pci_board_free(struct pci_board_state *pcibs)
{
	if (!pcibs || !pcibs->board)
		return;
	if (pcibs->board->free)
		pcibs->board->free(pcibs);
}

static struct pci_bridge *pci_bridge_alloc(void)
{
	struct pci_bridge *pcib = xcalloc(struct pci_bridge, 1);
	last_bridge_index = 0;
	return pcib;
};

static struct pci_bridge *pci_bridge_get_zorro(struct romconfig *rc)
{
	for (int i = 0; i < PCI_BRIDGE_MAX; i++) {
		if (bridges[i] && bridges[i]->rc == rc) {
			return bridges[i];
		}
	}
	return NULL;
}

static struct pci_bridge *pci_bridge_alloc_zorro(int offset, struct romconfig *rc)
{
	struct pci_bridge *pcib = pci_bridge_alloc();
	for (int i = 0; i < MAX_DUPLICATE_EXPANSION_BOARDS; i++) {
		if (bridges[i + offset] == NULL) {
			bridges[i + offset] = pcib;
			pcib->rc = rc;
			pcib->type = offset;
			return pcib;
		}
	}
	return NULL;
}


static void pci_bridge_free(struct pci_bridge *pcib)
{
	if (!pcib)
		return;
	for (int i = 0; i < MAX_PCI_BOARDS; i++) {
		pci_board_free(&pcib->boards[i]);
	}
	last_bridge_index = 0;
	xfree(pcib->data);
	xfree(pcib);
}

static struct pci_board *pci_board_alloc(struct pci_config *config)
{
	struct pci_board *pci = xcalloc(struct pci_board, 1);
	pci->config = config;
	return pci;
}

static void pci_board_add(struct pci_bridge *pcib, const struct pci_board *pci, int index)
{
	struct pci_board_state *pcibs = &pcib->boards[index];
	pcibs->board = pci;
	pcibs->index = index;
	pcibs->bridge = pcib;
	memset(pcibs->config_data, 0, sizeof pcibs->config_data);
	for (int i = 0; i < MAX_PCI_BARS; i++) {
		pcibs->bar_size[i] = pci->config->bars[i];
	}
	if (pci->init)
		pci->init(pcibs);
}

void pci_free(void)
{
	for (int i = 0; i < PCI_BRIDGE_MAX; i++) {
		pci_bridge_free(bridges[i]);
		bridges[i] = NULL;
	}
}

void pci_reset(void)
{
	pci_free();
}

void pci_rethink(void)
{
	for (int i = 0; i < PCI_BRIDGE_MAX; i++) {
		struct pci_bridge *pcib = bridges[i];
		if (!pcib)
			continue;
		pcib->irq = false;
		for (int j = 0; j < MAX_PCI_BOARDS; j++) {
			struct pci_board_state *pcibs = &pcib->boards[j];
			if (pcibs->board) {
				const struct pci_config *c = pcibs->board->config;
				if (c->interruptpin && (1 << (pcibs->board->config->interruptpin - 1)) & pcib->intena) {
					if ((pcibs->config_data[5] & (1 << 3)) && !(pcibs->config_data[6] & (1 << (10 - 8)))) {
						pcib->irq = true;
					}
				}
			}
		}
		if (pcib->irq)
			INTREQ_0(0x8000 | pcib->intreq_mask);
	}
}

static void set_pci_irq(struct pci_bridge *pcib, struct pci_board_state *pcibs, bool active)
{
	pcibs->config_data[5] &= ~(1 << 3);
	if (active)
		pcibs->config_data[5] |= (1 << 3);
	pci_rethink();
}

static void create_config_data(struct pci_board_state *s)
{
	uae_u8 *d = s->config_data;
	const struct pci_config *c = s->board->config;

	// big endian, get/put functions will swap if needed.
	d[0] = c->device >> 8;
	d[1] = c->device;
	d[2] = c->vendor >> 8;
	d[3] = c->vendor;

	d[8] = c->deviceclass >> 16;
	d[9] = c->deviceclass >> 8;
	d[10] = c->deviceclass;
	d[11] = c->revision;

	d[13] = c->header;

	for (int i = 0; i < MAX_PCI_BARS; i++) {
		int off = i == MAX_PCI_BARS - 1 ? 0x30 : 0x10 + i * 4;
		d[off + 0] = s->bar[i] >> 24;
		d[off + 1] = s->bar[i] >> 16;
		d[off + 2] = s->bar[i] >>  8;
		d[off + 3] = s->bar[i] >>  0;
	}

	d[0x2c] = c->subsystem >> 8;
	d[0x2d] = c->subsystem;
	d[0x2e] = c->subsystenvendor >> 8;
	d[0x2f] = c->subsystenvendor;

	d[0x3e] = c->interruptpin;
}

static struct pci_bridge *get_pci_bridge(uaecptr addr)
{
	if (addr < 0x10000 || (addr & 0xffff0000) == 0xe80000 || (addr & 0xff000000) == 0xff000000) {
		for (int i = 0; i < PCI_BRIDGE_MAX; i++) {
			struct pci_bridge *pcib = bridges[i];
			if (pcib && pcib->configured == 0) {
				return pcib;
			}
		}
	}
	struct pci_bridge *pcib = bridges[last_bridge_index];
	if (pcib) {
		if (addr >= pcib->baseaddress && addr < pcib->baseaddress_end)
			return pcib;
		if (pcib->configured_2 && addr >= pcib->baseaddress_2 && addr < pcib->baseaddress_end_2)
			return pcib;
	}
	for (int i = 0; i < PCI_BRIDGE_MAX; i++) {
		struct pci_bridge *pcib = bridges[i];
		if (pcib) {
			if ((addr >= pcib->baseaddress && addr < pcib->baseaddress_end) ||
				(pcib->configured_2 && addr >= pcib->baseaddress_2 && addr < pcib->baseaddress_end_2)) {
				last_bridge_index = i;
				return pcib;
			}
		}
	}
	return NULL;
}

static struct pci_bridge *get_pci_bridge_2(uaecptr addr)
{
	if (addr < 0x10000 || (addr & 0xffff0000) == 0xe80000 || (addr & 0xff000000) == 0xff000000) {
		for (int i = 0; i < PCI_BRIDGE_MAX; i++) {
			struct pci_bridge *pcib = bridges[i];
			if (pcib && pcib->configured_2 == 0) {
				return pcib;
			}
		}
	}
	for (int i = 0; i < PCI_BRIDGE_MAX; i++) {
		struct pci_bridge *pcib = bridges[i];
		if (pcib && pcib->configured_2) {
			if (addr >= pcib->baseaddress_2 && addr < pcib->baseaddress_end_2) {
				last_bridge_index = i;
				return pcib;
			}
		}
	}
	return NULL;
}

static struct pci_board_state *get_pci_board_state_config(struct pci_bridge *pcib, uaecptr addr)
{
	if (!pcib)
		return NULL;
	// get slot
	int idx = pcib->get_index(addr);
	if (idx < 0)
		return NULL;
	struct pci_board_state *pcibs = &pcib->boards[idx];
	if (!pcibs->board) {
		write_log(_T("- Empty slot %d\n"), idx);
		return NULL;
	}
	return pcibs;
}

static int stored_board, stored_bar;

static struct pci_board_state *get_pci_board_state(struct pci_bridge *pcib, uaecptr addr, int *bar)
{
	uaecptr addr2 = addr - pcib->io_offset;
	struct pci_board_state *pcibs2 = &pcib->boards[stored_board];
	if (pcibs2) {
		if (pcibs2->bar_enabled[stored_bar] && addr2 >= pcibs2->bar_start[stored_bar] && addr2 <= pcibs2->bar_end[stored_bar]) {
			*bar = stored_bar;
			return pcibs2;
		}
	}
	for (int i = 0; i < MAX_PCI_BOARDS; i++) {
		struct pci_board_state *pcibs = &pcib->boards[i];
		for (int j = 0; j < MAX_PCI_BARS; j++) {
			if (pcibs2->bar_enabled[j] && addr2 >= pcibs->bar_start[j] && addr2 <= pcibs->bar_end[j]) {
				*bar = j;
				stored_board = i;
				stored_bar = j;
				return pcibs;
			}
		}
	}
	return NULL;
}

static const pci_addrbank *get_pci_io(uaecptr *addrp, struct pci_board_state **pcibsp, int *endianswap)
{
	uaecptr addr = *addrp;
	int bar;
	write_log(_T("get_pci_io %08x %08x\n"), addr, M68K_GETPC);
	struct pci_bridge *pcib = get_pci_bridge(addr);
	if (!pcib)
		return NULL;
	struct pci_board_state *pcibs = get_pci_board_state(pcib, addr, &bar);
	if (!pcibs)
		return NULL;
	*pcibsp = pcibs;
	pcibs->selected_bar = bar;
	*endianswap = pcib->endian_swap_io;
	addr -= pcib->io_offset;
	addr &= (pcibs->bar_size[bar] & ~1) - 1;
	*addrp = addr;
	return &pcibs->board->bars[bar];
}

static const pci_addrbank *get_pci_mem(uaecptr *addrp, struct pci_board_state **pcibsp, int *endianswap)
{
	uaecptr addr = *addrp;
	int bar;
	write_log(_T("get_pci_mem %08x %08x\n"), addr, M68K_GETPC);
	struct pci_bridge *pcib = get_pci_bridge(addr);
	if (!pcib)
		return NULL;
	struct pci_board_state *pcibs = get_pci_board_state(pcib, addr, &bar);
	if (!pcibs)
		return NULL;
	*pcibsp = pcibs;
	pcibs->selected_bar = bar;
	*endianswap = pcib->endian_swap_memory;
	addr &= pcibs->bar_size[bar] - 1;
	addr -= pcib->memory_offset;
	*addrp = addr;
	return &pcibs->board->bars[bar];
}

static uae_u8 *get_pci_config(uaecptr addr, int size, uae_u32 v, int *endianswap)
{
	if (size < 0) {
		size = -size;
		write_log(_T("PCI Config Space %s READ %08x PC=%08x\n"),
			size == 4 ? _T("LONG") : (size == 2 ? _T("WORD") : _T("BYTE")), addr, M68K_GETPC);
	} else {
		write_log(_T("PCI Config Space %s WRITE %08x = %08x PC=%08x\n"),
					 size == 4 ? _T("LONG") : (size == 2 ? _T("WORD") : _T("BYTE")), addr, v, M68K_GETPC);
	}
	struct pci_bridge *pcib = get_pci_bridge(addr);
	if (!pcib)
		return NULL;
	struct pci_board_state *pcibs = get_pci_board_state_config(pcib, addr);
	if (!pcibs)
		return NULL;
	*endianswap = pcib->endian_swap_config;
	write_log(_T("- Board %d (%s)\n"), pcibs->index, pcibs->board->label);
	create_config_data(pcibs);
	return pcibs->config_data;
}

static void map_pci_banks(struct pci_board_state *pcibs, int type, bool enable)
{
	const struct pci_board *pci = pcibs->board;
	uae_u32 mask = type ? 3 : 15;
	for (int i = 0; i < MAX_PCI_BARS; i++) {
		if (pcibs->bar_size[i] == 0)
			continue;
		if ((pcibs->bar_size[i] & 1) != type)
			continue;
		pcibs->bar_start[i] = (pcibs->bar[i] & ~mask) + pcibs->bridge->baseaddress_offset;
		pcibs->bar_end[i] = pcibs->bar_start[i] + (pcibs->bar_size[i] & ~1) - 1;
		if (enable && pcibs->bar[i] < 0xffff0000) {
			pcibs->bar_enabled[i] = true;
			if (pcibs->bar_old[i] != pcibs->bar_start[i]) {
				write_log(_T("Board %d ('%s') BAR%d: %08x-%08x\n"), pcibs->index, pci->label, i, pcibs->bar_start[i], pcibs->bar_end[i]);
			}
		} else {
			pcibs->bar_enabled[i] = false;
			if (pcibs->bar_old[i] != pcibs->bar_start[i]) {
				write_log(_T("Board %d ('%s') BAR%d: %08x-%08x\n"), pcibs->index, pci->label, i, pcibs->bar_start[i], pcibs->bar_end[i]);
			}
		}
		pcibs->bar_old[i] = pcibs->bar_start[i];
	}
}

static void update_pci_config(uaecptr addr)
{
	struct pci_bridge *pcib = get_pci_bridge(addr);
	if (!pcib)
		return;
	struct pci_board_state *pcibs = get_pci_board_state_config(pcib, addr);
	if (!pcibs)
		return;
	uae_u8 *d = pcibs->config_data;
	const struct pci_config *c = pcibs->board->config;
	for (int i = 0; i < MAX_PCI_BARS; i++) {
		int off = i == MAX_PCI_BARS - 1 ? 0x30 : 0x10 + i * 4;
		if (pcibs->bar_size[i]) {
			pcibs->bar[i] = d[off + 0] << 24;
			pcibs->bar[i] |= d[off + 1] << 16;
			pcibs->bar[i] |= d[off + 2] <<  8;
			pcibs->bar[i] |= d[off + 3] <<  0;
			pcibs->bar[i] &= ~((pcibs->bar_size[i] & ~1) - 1);
			pcibs->bar[i] |= (pcibs->bar_size[i] & 1);
		} else {
			pcibs->bar[i] = 0;
		}
	}
	create_config_data(pcibs);
	pcibs->io_map_active = (d[7] & 1) != 0;
	pcibs->memory_map_active = (d[7] & 2) != 0;
	map_pci_banks(pcibs, 1, pcibs->io_map_active);
	map_pci_banks(pcibs, 0, pcibs->memory_map_active);
}


static uaecptr beswap(int endianswap, uaecptr addr)
{
	if (endianswap > 0)
		return (addr & ~3) | (3 - (addr & 3));
	return addr;
}

static uae_u32 REGPARAM2 pci_config_lget(uaecptr addr)
{
#ifdef JIT
	special_mem |= S_READ;
#endif
	uae_u32 v = 0xffffffff;
	int endianswap;
	uae_u8 *config = get_pci_config(addr, -4, 0, &endianswap);
	if (config) {
		uae_u32 offset = addr & 0xff;
		if (!endianswap) {
			v = config[offset + 0] << 24;
			v |= config[offset + 1] << 16;
			v |= config[offset + 2] << 8;
			v |= config[offset + 3] << 0;
		} else {
			v = config[offset + 3] << 24;
			v |= config[offset + 2] << 16;
			v |= config[offset + 1] << 8;
			v |= config[offset + 0] << 0;
		}
		write_log(_T("- %08x\n"), v);
	}
	return v;
}
static uae_u32 REGPARAM2 pci_config_wget(uaecptr addr)
{
#ifdef JIT
	special_mem |= S_READ;
#endif
	uae_u32 v = 0xffff;
	int endianswap;
	uae_u8 *config = get_pci_config(addr, -2, 0, &endianswap);
	if (config) {
		uae_u32 offset = addr & 0xff;
		if (!endianswap) {
			v = config[offset + 0] << 8;
			v |= config[offset + 1] << 0;
		} else {
			v = config[(offset ^ (endianswap > 0 ? 2 : 0)) + 1] << 8;
			v |= config[(offset ^ (endianswap > 0 ? 2 : 0)) + 0] << 0;
		}
		write_log(_T("- %04x\n"), v);
	}
	return v;
}
static uae_u32 REGPARAM2 pci_config_bget(uaecptr addr)
{
#ifdef JIT
	special_mem |= S_READ;
#endif
	uae_u8 v = 0xff;
	int endianswap;
	uae_u8 *config = get_pci_config(addr, -1, 0, &endianswap);
	if (config) {
		uae_u32 v;
		uae_u32 offset = addr & 0xff;
		if (!endianswap) {
			v = config[offset + 0];
		} else {
			v = config[beswap(endianswap, offset)];
		}
		write_log(_T("- %02x\n"), v);
	}
	return v;
}
static void REGPARAM2 pci_config_lput(uaecptr addr, uae_u32 b)
{
#ifdef JIT
	special_mem |= S_WRITE;
#endif
	int endianswap;
	uae_u8 *config = get_pci_config(addr, 4, b, &endianswap);
	if (config) {
		uae_u32 offset = addr & 0xff;
		if (!endianswap) {
			config[offset + 0] = b >> 24;
			config[offset + 1] = b >> 16;
			config[offset + 2] = b >> 8;
			config[offset + 3] = b >> 0;
		} else {
			config[offset + 3] = b >> 24;
			config[offset + 2] = b >> 16;
			config[offset + 1] = b >> 8;
			config[offset + 0] = b >> 0;
		}
		update_pci_config(addr);
	}
}
static void REGPARAM2 pci_config_wput(uaecptr addr, uae_u32 b)
{
#ifdef JIT
	special_mem |= S_WRITE;
#endif
	int endianswap;
	uae_u8 *config = get_pci_config(addr, 2, b, &endianswap);
	if (config) {
		uae_u32 offset = addr & 0xff;
		if (!endianswap) {
			config[offset + 0] = b >> 8;
			config[offset + 1] = b >> 0;
		} else {
			config[(offset ^ (endianswap > 0 ? 2 : 0)) + 1] = b >> 8;
			config[(offset ^ (endianswap > 0 ? 2 : 0)) + 0] = b >> 0;
		}
		update_pci_config(addr);
	}
}
static void REGPARAM2 pci_config_bput(uaecptr addr, uae_u32 b)
{
#ifdef JIT
	special_mem |= S_WRITE;
#endif
	int endianswap;
	uae_u8 *config = get_pci_config(addr, 1, b, &endianswap);
	if (config) {
		uae_u32 offset = addr & 0xff;
		if (!endianswap) {
			config[offset] = b;
		} else {
			config[beswap(endianswap, offset)] = b;
		}
		update_pci_config(addr);
	}
}

static uae_u32 endianswap_long(uae_u32 v)
{
	v = (v >> 24) | ((v >> 8) & 0x0000ff00) | ((v << 8) & 0x00ff0000) | (v << 24);
	return v;
}
static uae_u16 endianswap_word(uae_u16 v)
{
	v = (v >> 8) | (v << 8);
	return v;
}

static uae_u32 REGPARAM2 pci_io_lget(uaecptr addr)
{
#ifdef JIT
	special_mem |= S_READ;
#endif
	uae_u32 v = 0xffffffff;
	int endianswap;
	struct pci_board_state *pcibs;
	const pci_addrbank *a = get_pci_io(&addr, &pcibs, &endianswap);
	if (a && a->lget) {
		v = a->lget(pcibs, addr);
		if (endianswap)
			v = endianswap_long(v);
	}
	return v;
}
static uae_u32 REGPARAM2 pci_io_wget(uaecptr addr)
{
#ifdef JIT
	special_mem |= S_READ;
#endif
	uae_u32 v = 0xffff;
	int endianswap;
	struct pci_board_state *pcibs;
	const pci_addrbank *a = get_pci_io(&addr, &pcibs, &endianswap);
	if (a && a->wget) {
		if (endianswap) {
			v = a->wget(pcibs, addr ^ (endianswap > 0 ? 2 : 0));
			v = endianswap_word(v);
		} else {
			v = a->wget(pcibs, addr);
		}
	}
	return v;
}
static uae_u32 REGPARAM2 pci_io_bget(uaecptr addr)
{
#ifdef JIT
	special_mem |= S_READ;
#endif
	uae_u32 v = 0xff;
	int endianswap;
	struct pci_board_state *pcibs;
	const pci_addrbank *a = get_pci_io(&addr, &pcibs, &endianswap);
	if (a && a->bget) {
		if (endianswap) {
			v = a->bget(pcibs, beswap(endianswap, addr));
		} else {
			v = a->bget(pcibs, addr);
		}
	}
	return v;
}
static void REGPARAM2 pci_io_lput(uaecptr addr, uae_u32 b)
{
#ifdef JIT
	special_mem |= S_WRITE;
#endif
	int endianswap;
	struct pci_board_state *pcibs;
	const pci_addrbank *a = get_pci_io(&addr, &pcibs, &endianswap);
	if (a && a->lput) {
		if (endianswap)
			b = endianswap_long(b);
		a->lput(pcibs, addr, b);
	}
}
static void REGPARAM2 pci_io_wput(uaecptr addr, uae_u32 b)
{
#ifdef JIT
	special_mem |= S_WRITE;
#endif
	int endianswap;
	struct pci_board_state *pcibs;
	const pci_addrbank *a = get_pci_io(&addr, &pcibs, &endianswap);
	if (a && a->wput) {
		if (endianswap) {
			b = endianswap_word(b);
			a->wput(pcibs, addr ^ (endianswap > 0 ? 2 : 0), b);
		} else {
			a->wput(pcibs, addr, b);
		}
	}
}
static void REGPARAM2 pci_io_bput(uaecptr addr, uae_u32 b)
{
#ifdef JIT
	special_mem |= S_WRITE;
#endif
	int endianswap;
	struct pci_board_state *pcibs;
	const pci_addrbank *a = get_pci_io(&addr, &pcibs, &endianswap);
	if (a && a->bput) {
		if (endianswap) {
			a->bput(pcibs, beswap(endianswap, addr), b);
		} else {
			a->bput(pcibs, addr, b);
		}
	}
}

static uae_u32 REGPARAM2 pci_mem_lget(uaecptr addr)
{
#ifdef JIT
	special_mem |= S_READ;
#endif
	uae_u32 v = 0xffffffff;
	int endianswap;
	struct pci_board_state *pcibs;
	const pci_addrbank *a = get_pci_mem(&addr, &pcibs, &endianswap);
	if (a && a->lget) {
		v = a->lget(pcibs, addr);
		if (endianswap)
			v = endianswap_long(v);
	}
	return v;
}
static uae_u32 REGPARAM2 pci_mem_wget(uaecptr addr)
{
#ifdef JIT
	special_mem |= S_READ;
#endif
	uae_u32 v = 0xffff;
	int endianswap;
	struct pci_board_state *pcibs;
	const pci_addrbank *a = get_pci_mem(&addr, &pcibs, &endianswap);
	if (a && a->wget) {
		if (endianswap) {
			v = a->wget(pcibs, addr ^ (endianswap > 0 ? 2 : 0));
			v = endianswap_word(v);
		} else {
			v = a->wget(pcibs, addr);
		}
	}
	return v;
}
static uae_u32 REGPARAM2 pci_mem_bget(uaecptr addr)
{
#ifdef JIT
	special_mem |= S_READ;
#endif
	uae_u32 v = 0xff;
	int endianswap;
	struct pci_board_state *pcibs;
	const pci_addrbank *a = get_pci_mem(&addr, &pcibs, &endianswap);
	if (a && a->bget) {
		if (endianswap) {
			v = a->bget(pcibs, beswap(endianswap, addr));
		} else {
			v = a->bget(pcibs, addr);
		}
	}
	return v;
}
static void REGPARAM2 pci_mem_lput(uaecptr addr, uae_u32 b)
{
#ifdef JIT
	special_mem |= S_WRITE;
#endif
	int endianswap;
	struct pci_board_state *pcibs;
	const pci_addrbank *a = get_pci_mem(&addr, &pcibs, &endianswap);
	if (a && a->lput) {
		if (endianswap)
			b = endianswap_long(b);
		a->lput(pcibs, addr, b);
	}
}
static void REGPARAM2 pci_mem_wput(uaecptr addr, uae_u32 b)
{
#ifdef JIT
	special_mem |= S_WRITE;
#endif
	int endianswap;
	struct pci_board_state *pcibs;
	const pci_addrbank *a = get_pci_mem(&addr, &pcibs, &endianswap);
	if (a && a->wput) {
		if (endianswap) {
			b = endianswap_word(b);
			a->wput(pcibs, addr ^ (endianswap > 0 ? 2 : 0), b);
		} else {
			a->wput(pcibs, addr, b);
		}
	}
}
static void REGPARAM2 pci_mem_bput(uaecptr addr, uae_u32 b)
{
#ifdef JIT
	special_mem |= S_WRITE;
#endif
	int endianswap;
	struct pci_board_state *pcibs;
	const pci_addrbank *a = get_pci_mem(&addr, &pcibs, &endianswap);
	if (a && a->bput) {
		if (endianswap) {
			a->bput(pcibs, beswap(endianswap, addr), b);
		} else {
			a->bput(pcibs, addr, b);
		}
	}
}

static uae_u32 REGPARAM2 pci_bridge_lget(uaecptr addr)
{
#ifdef JIT
	special_mem |= S_READ;
#endif
	uae_u32 v = 0;
	write_log(_T("pci_bridge_lget %08x PC=%08x\n"), addr, M68K_GETPC);
	struct pci_bridge *pcib = get_pci_bridge(addr);
	if (!pcib)
		return v;
	if (pcib == bridges[PCI_BRIDGE_GREX] || pcib == bridges[PCI_BRIDGE_XVISION]) {
		int reg = (addr & 0xf0) >> 4;
		switch(reg)
		{
			case 0:
			v = pcib->endian_swap_io ? 2 : 0;
			v |= pcib->config[0];
			if (pcib == bridges[PCI_BRIDGE_GREX])
				v |= 0x02000000;
			break;
			case 1:
			v = pcib->intena ? 1 : 0;
			break;
			case 2:
			break;
			case 3:
			break;
			case 4:
			break;
		}
	}
	write_log(_T("=%08x\n"), v);
	return v;
}
static uae_u32 REGPARAM2 pci_bridge_wget(uaecptr addr)
{
#ifdef JIT
	special_mem |= S_READ;
#endif
	uae_u16 v = 0;
	write_log(_T("pci_bridge_wget %08x PC=%08x\n"), addr, M68K_GETPC);
	return v;
}
static uae_u32 REGPARAM2 pci_bridge_bget(uaecptr addr)
{
#ifdef JIT
	special_mem |= S_READ;
#endif
	uae_u8 v = 0;
	struct pci_bridge *pcib = get_pci_bridge(addr);
	if (!pcib)
		return v;
	if (!pcib->configured) {
		uaecptr offset = addr & 65535;
		if (offset >= sizeof pcib->acmemory)
			return 0;
		return pcib->acmemory[offset];

	} else if (pcib == bridges[PCI_BRIDGE_WILDFIRE]) {
		int offset = addr & 15;
		v = pcib->config[offset / 4];
	}
	write_log(_T("pci_bridge_bget %08x %02x PC=%08x\n"), addr, v, M68K_GETPC);
	return v;
}
static void REGPARAM2 pci_bridge_lput(uaecptr addr, uae_u32 b)
{
#ifdef JIT
	special_mem |= S_WRITE;
#endif
	write_log(_T("pci_bridge_lput %08x %08x PC=%08x\n"), addr, b, M68K_GETPC);
	struct pci_bridge *pcib = get_pci_bridge(addr);
	if (!pcib)
		return;
	if (pcib == bridges[PCI_BRIDGE_GREX] || pcib == bridges[PCI_BRIDGE_XVISION]) {
		int reg = (addr & 0xf0) >> 4;
		switch (reg)
		{
			case 0:
			pcib->endian_swap_memory = pcib->endian_swap_io = (b & 2) != 0;
			break;
			case 1:
			pcib->intena = (b & 1) ? 0xff : 0x00;
			break;
			case 3:
			pcib->config[0] = b & 1;
			pcib->endian_swap_memory = pcib->endian_swap_io = (b & 2) != 0;
			break;
		}
	}
}
static void REGPARAM2 pci_bridge_wput(uaecptr addr, uae_u32 b)
{
#ifdef JIT
	special_mem |= S_WRITE;
#endif
	struct pci_bridge *pcib = get_pci_bridge(addr);
	if (!pcib)
		return;
	if (!pcib->configured) {
		uaecptr offset = addr & 65535;
		if (pcib->bank_zorro == 3) {
			switch (offset)
			{
				case 0x44:
				if (pcib->type == PCI_BRIDGE_PROMETHEUS) {
					map_banks(&pci_io_bank, (expamem_z3_pointer) >> 16, 0xf0000 >> 16, 0);
					map_banks(&pci_mem_bank, (expamem_z3_pointer + 0x100000) >> 16, (511 * 1024 * 1024) >> 16, 0);
					map_banks(&pci_config_bank, (expamem_z3_pointer + 0xf0000) >> 16, 0x10000 >> 16, 0);
					pcib->baseaddress_offset = pcib->baseaddress;
				} else if (pcib->type == PCI_BRIDGE_MEDIATOR) {
					map_banks(&pci_mem_bank, expamem_z3_pointer >> 16, expamem_z3_size >> 16, 0);
					pcib->baseaddress_offset = 0;
				}
				pcib->baseaddress = expamem_z3_pointer;
				pcib->board_size = expamem_z3_size;
				pcib->baseaddress_end = pcib->baseaddress + pcib->board_size;
				pcib->configured = 1;
				expamem_next(pcib->bank, NULL);
				break;
			}
		}
	}
	write_log(_T("pci_bridge_wput %08x %04x PC=%08x\n"), addr, b & 0xffff, M68K_GETPC);
}
static void REGPARAM2 pci_bridge_bput(uaecptr addr, uae_u32 b)
{
#ifdef JIT
	special_mem |= S_WRITE;
#endif
	struct pci_bridge *pcib = get_pci_bridge(addr);
	if (!pcib)
		return;
	write_log(_T("pci_bridge_bput %08x %02x PC=%08x\n"), addr, b & 0xff, M68K_GETPC);
	if (!pcib->configured) {
		uaecptr offset = addr & 65535;
		if (pcib->bank_zorro == 2) {
			switch (offset)
			{
				case 0x48:
				// Mediator 1200
				map_banks_z2(&pci_mem_bank, b, expamem_z2_size >> 16);			
				pcib->baseaddress = b << 16;
				pcib->board_size = expamem_z2_size;
				pcib->baseaddress_end = pcib->baseaddress + pcib->board_size;
				pcib->configured = 1;
				expamem_next(pcib->bank, NULL);
				break;
				case 0x4c:
				pcib->configured = -1;
				expamem_shutup(pcib->bank);
				break;
			}
		}
	}
	if (pcib == bridges[PCI_BRIDGE_WILDFIRE]) {
		addr &= 15;
		if (addr == 8) {
			pcib->config[2] = b;
			if (b & 1) {
				write_log(_T("Wildfire 68000 mode!\n"));
				cpu_halt(CPU_HALT_ACCELERATOR_CPU_FALLBACK);
			}
		}
	}
}


static void mediator_set_window_offset(struct pci_bridge *pcib, uae_u16 v)
{
	uae_u32 offset = pcib->memory_offset;
	if (pcib->bank_2_zorro == 3) {
		// 4000
		uae_u8 mask = pcib->board_size == 256 * 1024 * 1024 ? 0xf0 : 0xe0;
		pcib->window = v & mask;
		pcib->memory_offset = pcib->window << 18;
	} else {
		// 1200
		uae_u16 mask = pcib->board_size == 4 * 1024 * 1024 ? 0xffc0 : 0xff80;
		pcib->window = v & mask;
		pcib->memory_offset = pcib->window << 16;
	}
	pcib->memory_offset -= pcib->baseaddress;
	if (pcib->memory_offset != offset) {
		write_log(_T("Mediator window: %08x offset: %08x\n"),
			pcib->memory_offset + pcib->baseaddress, pcib->memory_offset);
	}
}

static uae_u32 REGPARAM2 pci_bridge_bget_2(uaecptr addr)
{
#ifdef JIT
	special_mem |= S_READ;
#endif
	uae_u8 v = 0;
	struct pci_bridge *pcib = get_pci_bridge_2(addr);
	if (!pcib)
		return v;
	if (!pcib->configured_2) {
		uaecptr offset = addr & 65535;
		if (offset >= sizeof pcib->acmemory_2)
			return 0;
		return pcib->acmemory_2[offset];
	} else {
		if (pcib->bank_2_zorro == 3) {
			int offset = addr & 0x7fffff;
			if (offset == 0) {
				v = pcib->window;
			}
			if (offset == 4) {
				v = pcib->irq ? 0xff : 0x00;
			}
		}
	}
	write_log(_T("pci_bridge_bget_2 %08x %02x PC=%08x\n"), addr, v, M68K_GETPC);
	return v;
}
static uae_u32 REGPARAM2 pci_bridge_wget_2(uaecptr addr)
{
#ifdef JIT
	special_mem |= S_READ;
#endif
	uae_u16 v = 0;
	write_log(_T("pci_bridge_wget_2 %08x PC=%08x\n"), addr, M68K_GETPC);
	struct pci_bridge *pcib = get_pci_bridge_2(addr);
	if (!pcib)
		return v;
	if (pcib->configured_2) {
		if (pcib->bank_2_zorro == 2) {
			int offset = addr & 0xffff;
			if (offset == 2) {
				v = pcib->window;
			}
		}
	}
	return v;
}
static uae_u32 REGPARAM2 pci_bridge_lget_2(uaecptr addr)
{
#ifdef JIT
	special_mem |= S_READ;
#endif
	uae_u32 v = 0;
	write_log(_T("pci_bridge_lget_2 %08x PC=%08x\n"), addr, M68K_GETPC);
	struct pci_bridge *pcib = get_pci_bridge_2(addr);
	if (!pcib)
		return v;
	v = pci_bridge_wget_2(addr + 0) << 16;
	v |= pci_bridge_wget_2(addr + 2);
	return v;
}

static void REGPARAM2 pci_bridge_bput_2(uaecptr addr, uae_u32 b)
{
#ifdef JIT
	special_mem |= S_WRITE;
#endif
	struct pci_bridge *pcib = get_pci_bridge_2(addr);
	if (!pcib)
		return;
	write_log(_T("pci_bridge_bput_2 %08x %02x PC=%08x\n"), addr, b & 0xff, M68K_GETPC);
	if (!pcib->configured_2) {
		uaecptr offset = addr & 65535;
		if (pcib->bank_2_zorro == 2) {
			switch (offset)
			{
				case 0x48:
				// Mediator 1200 IO
				pcib->baseaddress_2 = b << 16;
				pcib->baseaddress_end_2 = (b << 16) + expamem_z2_size;
				map_banks_z2(pcib->bank_2, pcib->baseaddress_2 >> 16, 0x10000 >> 16);
				map_banks_z2(&dummy_bank, (pcib->baseaddress_2 + 0x10000) >> 16, (expamem_z2_size - 0x10000) >> 16);
				pcib->configured_2 = 1;
				expamem_next(pcib->bank_2, NULL);
				break;
				case 0x4c:
				pcib->configured_2 = -1;
				expamem_shutup(pcib->bank_2);
				break;
			}
		}
	} else {
		if (pcib->bank_2_zorro == 2) {
			// Mediator 1200
			int offset = addr & 0xffff;
			if (offset == 7) {
				// config/io mapping
				if (b & 0x20) {
					if (b & 0x80) {
						map_banks_z2(&pci_config_bank, (pcib->baseaddress_2 + 0x10000) >> 16, 0x10000 >> 16);
					} else {
						map_banks_z2(&pci_io_bank, (pcib->baseaddress_2 + 0x10000) >> 16, 0x10000 >> 16);
					}
				} else {
					map_banks_z2(&dummy_bank, (pcib->baseaddress_2 + 0x10000) >> 16, 0x10000 >> 16);
				}
			} else if (offset == 11) {
				pcib->intena = b >> 4;
			}
		}
		if (pcib->bank_2_zorro == 3) {
			// Mediator 4000 window
			int offset = addr & 0x7fffff;
			if (offset == 0) {
				mediator_set_window_offset(pcib, b);
			} else if (offset == 4) {
				pcib->intena = b >> 4;
			}
		}
	}
}
static void REGPARAM2 pci_bridge_wput_2(uaecptr addr, uae_u32 b)
{
#ifdef JIT
	special_mem |= S_WRITE;
#endif
	struct pci_bridge *pcib = get_pci_bridge_2(addr);
	if (!pcib)
		return;
	if (!pcib->configured_2) {
		uaecptr offset = addr & 65535;
		if (pcib->bank_2_zorro == 3) {
			switch (offset)
			{
				case 0x44:
				// Mediator 4000 IO
				map_banks(pcib->bank_2, expamem_z3_pointer >> 16, 0x800000 >> 16, 0);
				map_banks(&pci_config_bank, (expamem_z3_pointer + 0x800000) >> 16, 0x400000 >> 16, 0);
				map_banks(&pci_io_bank, (expamem_z3_pointer + 0xc00000) >> 16, 0x400000 >> 16, 0);
				pcib->baseaddress_2 = expamem_z3_pointer;
				pcib->baseaddress_end_2 = expamem_z3_pointer + expamem_z3_size;
				pcib->board_size_2 = expamem_z3_size;
				pcib->configured_2 = 1;
				pcib->io_offset = (expamem_z3_pointer + 0xc00000);
				expamem_next(pcib->bank, NULL);
				break;
			}
		}
	} else {
		if (pcib->bank_2_zorro == 2) {
			// Mediator 1200 window
			int offset = addr & 0xffff;
			if (offset == 2) {
				mediator_set_window_offset(pcib, b);
			}
		}
	}
	write_log(_T("pci_bridge_wput_2 %08x %04x PC=%08x\n"), addr, b & 0xffff, M68K_GETPC);
}
static void REGPARAM2 pci_bridge_lput_2(uaecptr addr, uae_u32 b)
{
#ifdef JIT
	special_mem |= S_WRITE;
#endif
	write_log(_T("pci_bridge_lput_2 %08x %08x PC=%08x\n"), addr, b, M68K_GETPC);
	struct pci_bridge *pcib = get_pci_bridge_2(addr);
	if (!pcib)
		return;
	pci_bridge_wput_2(addr + 0, b >> 16);
	pci_bridge_wput_2(addr + 2, b >>  0);
}


addrbank pci_config_bank = {
	pci_config_lget, pci_config_wget, pci_config_bget,
	pci_config_lput, pci_config_wput, pci_config_bput,
	default_xlate, default_check, NULL, NULL, _T("PCI CONFIG"),
	pci_config_lget, pci_config_wget, ABFLAG_IO | ABFLAG_SAFE
};
addrbank pci_io_bank = {
	pci_io_lget, pci_io_wget, pci_io_bget,
	pci_io_lput, pci_io_wput, pci_io_bput,
	default_xlate, default_check, NULL, NULL, _T("PCI IO"),
	pci_io_lget, pci_io_wget, ABFLAG_IO | ABFLAG_SAFE
};
addrbank pci_mem_bank = {
	pci_mem_lget, pci_mem_wget, pci_mem_bget,
	pci_mem_lput, pci_mem_wput, pci_mem_bput,
	default_xlate, default_check, NULL, NULL, _T("PCI MEMORY"),
	pci_mem_lget, pci_mem_wget, ABFLAG_IO | ABFLAG_SAFE
};
addrbank pci_bridge_bank = {
	pci_bridge_lget, pci_bridge_wget, pci_bridge_bget,
	pci_bridge_lput, pci_bridge_wput, pci_bridge_bput,
	default_xlate, default_check, NULL, NULL, _T("PCI BRIDGE"),
	pci_bridge_lget, pci_bridge_wget, ABFLAG_IO | ABFLAG_SAFE
};
addrbank pci_bridge_bank_2 = {
	pci_bridge_lget_2, pci_bridge_wget_2, pci_bridge_bget_2,
	pci_bridge_lput_2, pci_bridge_wput_2, pci_bridge_bput_2,
	default_xlate, default_check, NULL, NULL, _T("PCI BRIDGE #2"),
	pci_bridge_lget_2, pci_bridge_wget_2, ABFLAG_IO | ABFLAG_SAFE
};

static void pci_dump_out(const TCHAR *txt, int log)
{
	if (log > 0)
		write_log(txt);
	else if (log == 0)
		console_out(txt);
}

static void pci_dump_memio_region(struct pci_bridge *pcib, uaecptr start, uaecptr end, int type, int log)
{
	for (int i = 0; i < MAX_PCI_BOARDS; i++) {
		struct pci_board_state *pcibs = &pcib->boards[i];
		for (int j = 0; j < MAX_PCI_BARS; j++) {
			if (pcibs->bar_size[i] && (pcibs->bar_start[j] || pcibs->bar_end[j]) && (pcibs->bar_size[j] & 1) == type) {
				TCHAR txt[256];
				_stprintf(txt, _T("        - %08X - %08X: BAR%d %s\n"), pcibs->bar_start[j], pcibs->bar_end[j], j, pcibs->board->label);
				pci_dump_out(txt, log);
			}
		}
	}
}

static void pci_dump_region(addrbank *bank, uaecptr *start, uaecptr *end)
{
	*start = 0;
	*end = 0;
	for (int i = 0; i < 65536 + 1; i++) {
		addrbank *a = mem_banks[i];
		if (*start == 0 && a == bank)
			*start = i << 16;
		if (*start && a != bank) {
			*end = i << 16;
			return;
		}
	}
}

void pci_dump(int log)
{
	for (int i = 0; i < PCI_BRIDGE_MAX; i++) {
		TCHAR txt[256];
		uae_u8 slots[MAX_PCI_BOARDS] = { 0 };
		uaecptr start, end;
		struct pci_bridge *pcib = bridges[i];
		if (!pcib)
			continue;
		_stprintf(txt, _T("PCI bridge '%s'\n"), pcib->label);
		pci_dump_out(txt, log);
		pci_dump_region(&pci_config_bank, &start, &end);
		if (start) {
			int previdx = -1;
			_stprintf(txt, _T("%08X - %08X: Configuration space\n"), start, end - 1);
			pci_dump_out(txt, log);
			while (start < end) {
				int idx = pcib->get_index(start);
				if (idx >= 0 && idx != previdx && slots[idx] == 0) {
					struct pci_board_state *pcibs = &pcib->boards[idx];
					const struct pci_board *pci = pcibs->board;
					if (pcibs->board) {
						_stprintf(txt, _T("        - Slot %d: [%04X/%04X] %s IO=%d MEM=%d\n"),
							idx, pci->config->vendor, pci->config->device, pci->label,
							pcibs->io_map_active, pcibs->memory_map_active);
					} else {
						_stprintf(txt, _T("        - Slot %d: <none>\n"), idx);
					}
					pci_dump_out(txt, log);
					previdx = idx;
					slots[idx] = 1;
				}
				start += 256;
			}
		}
		pci_dump_region(&pci_io_bank, &start, &end);
		if (start) {
			_stprintf(txt, _T("%08X - %08X: IO space\n"), start, end - 1);
			pci_dump_out(txt, log);
			pci_dump_memio_region(pcib, start, end, 1, log);
		}
		pci_dump_region(&pci_mem_bank, &start, &end);
		if (start) {
			_stprintf(txt, _T("%08X - %08X: Memory space\n"), start, end - 1);
			pci_dump_out(txt, log);
			pci_dump_memio_region(pcib, start, end, 0, log);
		}
	}
}

static int countbit(int mask)
{
	int found = -1;
	for (int i = 0; i < 15; i++) {
		if (mask & (1 << i)) {
			if (found >= 0)
				return -1;
			found = i;
		}
	}
	return found;
}

/* DKB Wildfire */

#define WILDFIRE_CONFIG_MASK 32767

static void REGPARAM2 wildfire_bput(struct pci_board_state *pcibs, uaecptr addr, uae_u32 b)
{
	// BAR6 = "ROM"
	if (pcibs->selected_bar == 6) {
		bridges[PCI_BRIDGE_WILDFIRE]->data[addr & WILDFIRE_CONFIG_MASK] = b;
	} else {
		ncr815_io_bput_wildfire(addr, b);
	}
}
static void REGPARAM2 wildfire_wput(struct pci_board_state *pcibs, uaecptr addr, uae_u32 b)
{
	if (pcibs->selected_bar == 6) {
		bridges[PCI_BRIDGE_WILDFIRE]->data[(addr + 0) & WILDFIRE_CONFIG_MASK] = b >> 8;
		bridges[PCI_BRIDGE_WILDFIRE]->data[(addr + 1) & WILDFIRE_CONFIG_MASK] = b;
	} else {
		ncr815_io_bput_wildfire(addr + 1, b >> 0);
		ncr815_io_bput_wildfire(addr + 0, b >> 8);
	}
}
static void REGPARAM2 wildfire_lput(struct pci_board_state *pcibs, uaecptr addr, uae_u32 b)
{
	if (pcibs->selected_bar == 6) {
		bridges[PCI_BRIDGE_WILDFIRE]->data[(addr + 0) & WILDFIRE_CONFIG_MASK] = b >> 24;
		bridges[PCI_BRIDGE_WILDFIRE]->data[(addr + 1) & WILDFIRE_CONFIG_MASK] = b >> 16;
		bridges[PCI_BRIDGE_WILDFIRE]->data[(addr + 2) & WILDFIRE_CONFIG_MASK] = b >> 8;
		bridges[PCI_BRIDGE_WILDFIRE]->data[(addr + 3) & WILDFIRE_CONFIG_MASK] = b >> 0;
	} else {
		ncr815_io_bput_wildfire(addr + 3, b >> 0);
		ncr815_io_bput_wildfire(addr + 2, b >> 8);
		ncr815_io_bput_wildfire(addr + 1, b >> 16);
		ncr815_io_bput_wildfire(addr + 0, b >> 24);
	}
}
static uae_u32 REGPARAM2 wildfire_bget(struct pci_board_state *pcibs, uaecptr addr)
{
	uae_u32 v = 0;
	if (pcibs->selected_bar == 6) {
		v = bridges[PCI_BRIDGE_WILDFIRE]->data[addr & WILDFIRE_CONFIG_MASK];
	} else {
		v = ncr815_io_bget_wildfire(addr);
	}
	return v;
}
static uae_u32 REGPARAM2 wildfire_wget(struct pci_board_state *pcibs, uaecptr addr)
{
	uae_u32 v = 0;
	if (pcibs->selected_bar == 6) {
		v = bridges[PCI_BRIDGE_WILDFIRE]->data[(addr + 0) & WILDFIRE_CONFIG_MASK] << 8;
		v |= bridges[PCI_BRIDGE_WILDFIRE]->data[(addr + 1) & WILDFIRE_CONFIG_MASK];
	} else {
		v = ncr815_io_bget_wildfire(addr + 1) << 0;
		v |= ncr815_io_bget_wildfire(addr + 0) << 8;
	}
	return v;
}
static uae_u32 REGPARAM2 wildfire_lget(struct pci_board_state *pcibs, uaecptr addr)
{
	uae_u32 v = 0;
	if (pcibs->selected_bar == 6) {
		v = bridges[PCI_BRIDGE_WILDFIRE]->data[(addr + 0) & WILDFIRE_CONFIG_MASK] << 24;
		v |= bridges[PCI_BRIDGE_WILDFIRE]->data[(addr + 1) & WILDFIRE_CONFIG_MASK] << 16;
		v |= bridges[PCI_BRIDGE_WILDFIRE]->data[(addr + 2) & WILDFIRE_CONFIG_MASK] << 8;
		v |= bridges[PCI_BRIDGE_WILDFIRE]->data[(addr + 3) & WILDFIRE_CONFIG_MASK];
	} else {
		v = ncr815_io_bget_wildfire(addr + 3) << 0;
		v |= ncr815_io_bget_wildfire(addr + 2) <<  8;
		v |= ncr815_io_bget_wildfire(addr + 1) << 16;
		v |= ncr815_io_bget_wildfire(addr + 0) << 24;
	}
	return v;
}

static int dkb_wildfire_get_index(uaecptr addr)
{
	int idx = 0;
	int slot = -1;
	for (int i = 0x0800; i <= 0x10000000; i <<= 1, idx++) {
		if (addr & i) {
			if (slot >= 0)
				return -1;
			slot = idx;
		}
	}
	if (slot > 5)
		slot = -1;
	return slot;
}

void pci_irq_callback(struct pci_board_state *pcibs, bool irq)
{
	set_pci_irq(pcibs->bridge, pcibs, irq);
}

static const struct pci_config ncr_53c815_pci_config =
{
	0x1000, 0x0004, 0, 0, 0, 0x010000, 0, 0, 0, 1, { 256 | 1, 1024 | 0, 0, 0, 0, 0, 32768 | 0 }
};
static const struct pci_board ncr_53c815_pci_board =
{
	_T("NCR53C815"),
	&ncr_53c815_pci_config, NULL, NULL, NULL, pci_irq_callback,
	{
		{ wildfire_lget, wildfire_wget, wildfire_bget, wildfire_lput, wildfire_wput, wildfire_bput },
		{ wildfire_lget, wildfire_wget, wildfire_bget, wildfire_lput, wildfire_wput, wildfire_bput },
		{ NULL },
		{ NULL },
		{ NULL },
		{ NULL },
		{ wildfire_lget, wildfire_wget, wildfire_bget, wildfire_lput, wildfire_wput, wildfire_bput }
	}
};

static void add_pci_devices(struct pci_bridge *pcib)
{
	if (currprefs.ne2000pciname[0])
		pci_board_add(pcib, &ne2000_pci_board, 0);
	
	//pci_board_add(pcib, &ncr_53c815_pci_board, 1);
}

// Wildfire

void wildfire_ncr815_irq(int v)
{
	struct pci_board_state *pcibs = &bridges[PCI_BRIDGE_WILDFIRE]->boards[0];
	set_pci_irq(bridges[PCI_BRIDGE_WILDFIRE], pcibs, v != 0);
}

addrbank *dkb_wildfire_pci_init(struct romconfig *rc)
{
	struct pci_bridge *pcib = pci_bridge_alloc();

	bridges[PCI_BRIDGE_WILDFIRE] = pcib;
	pcib->label = _T("Wildfire");
	pcib->endian_swap_config = 0;
	pcib->endian_swap_io = 0;
	pcib->endian_swap_memory = 0;
	pcib->intena = 0xff; // controlled by bridge config bits, bit unknown.
	pcib->intreq_mask = 0x2000;
	pcib->get_index = dkb_wildfire_get_index;
	pcib->baseaddress = 0x80000000;
	pcib->baseaddress_end = 0xffffffff;
	pcib->configured = -1;
	pci_board_add(pcib, &ncr_53c815_pci_board, 0);
	map_banks(&pci_config_bank, 0x80000000 >> 16, 0x10000000 >> 16, 0);
	map_banks(&pci_mem_bank, 0x90000000 >> 16, 0x30000000 >> 16, 0);
	map_banks(&pci_io_bank, 0xc0000000 >> 16, 0x30000000 >> 16, 0);
	map_banks(&pci_bridge_bank, 0xffff0000 >> 16, 0x10000 >> 16, 0);
	pcib->data = xcalloc(uae_u8, 32768);
	return &expamem_null;
}

// Prometheus: 44359/1

static const uae_u8 prometheus_autoconfig[16] = { 0x85, 0x01, 0x30, 0x00, 0xad, 0x47, 0x00, 0x00, 0x00, 0x7f, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00 };

static void ew(uae_u8 *acmemory, int addr, uae_u8 value)
{
	if (addr == 00 || addr == 02 || addr == 0x40 || addr == 0x42) {
		acmemory[addr] = (value & 0xf0);
		acmemory[addr + 2] = (value & 0x0f) << 4;
	} else {
		acmemory[addr] = ~(value & 0xf0);
		acmemory[addr + 2] = ~((value & 0x0f) << 4);
	}
}

static int prometheus_get_index(uaecptr addr)
{
	struct pci_bridge *pcib = get_pci_bridge(addr);

	addr -= pcib->baseaddress;
	if ((addr & 0xffff0000) != 0x000f0000)
		return -1;
	int slot = (addr & 0xf000) >> 13;
	if (slot > 3)
		slot = -1;
	return slot;
}

static addrbank *prometheus_pci_init(struct romconfig *rc)
{
	struct pci_bridge *pcib = pci_bridge_alloc_zorro(PCI_BRIDGE_PROMETHEUS, rc);
	if (!pcib)
		return &expamem_null;
	pcib->label = _T("Prometheus");
	pcib->endian_swap_config = 1;
	pcib->endian_swap_io = 1;
	pcib->endian_swap_memory = 1;
	pcib->intena = 0xff;
	pcib->intreq_mask = 0x0008;
	pcib->get_index = prometheus_get_index;
	pcib->bank = &pci_bridge_bank;
	pcib->bank_zorro = 3;

	add_pci_devices(pcib);

	memset(pcib->acmemory, 0xff, sizeof pcib->acmemory);
	for (int i = 0; i < sizeof prometheus_autoconfig; i++) {
		ew(pcib->acmemory, i * 4, prometheus_autoconfig[i]);
	}
	return pcib->bank;
}

// G-REX

static int grex_get_index(uaecptr addr)
{
	int slot = -1;
	struct pci_bridge *pcib = get_pci_bridge(addr);

	if ((addr & 0xfffc0700) == 0xfffc0000) {
		int v = (addr & 0x3f000) >> 13;
		slot = countbit(v);
	}
	return slot;
}

static addrbank *grex_pci_init(struct romconfig *rc)
{
	struct pci_bridge *pcib = pci_bridge_alloc();

	bridges[PCI_BRIDGE_GREX] = pcib;
	pcib->label = _T("G-REX");
	pcib->intena = 0;
	pcib->intreq_mask = 0x0008;
	pcib->get_index = grex_get_index;
	pcib->baseaddress = 0x80000000;
	pcib->baseaddress_end = 0xffffffff;
	pcib->configured = -1;

	add_pci_devices(pcib);

	map_banks(&pci_config_bank, 0xfffc0000 >> 16, 0x20000 >> 16, 0);
	map_banks(&pci_mem_bank, 0x80000000 >> 16, 0x78000000 >> 16, 0);
	map_banks(&pci_io_bank, 0xfffa0000 >> 16, 0x20000 >> 16, 0);
	map_banks(&pci_bridge_bank, 0xfffe0000 >> 16, 0x10000 >> 16, 0);
	pcib->io_offset = 0xfffa0000;
	return &expamem_null;
}

// CyberVision/BlizzardVision without VGA chip...

static int xvision_get_index(uaecptr addr)
{
	struct pci_bridge *pcib = get_pci_bridge(addr);
	if ((addr & 0xfffcf700) == 0xfffc0000)
		return 0;
	return -1;
}

static addrbank *cbvision(struct romconfig *rc)
{
	struct pci_bridge *pcib = pci_bridge_alloc();

	bridges[PCI_BRIDGE_XVISION] = pcib;
	pcib->label = _T("CBVision");
	pcib->intena = 0;
	pcib->intreq_mask = 0x0008;
	pcib->get_index = xvision_get_index;
	pcib->baseaddress = 0xe0000000;
	pcib->baseaddress_end = 0xffffffff;
	pcib->configured = -1;

	map_banks(&pci_config_bank, 0xfffc0000 >> 16, 0x20000 >> 16, 0);
	map_banks(&pci_mem_bank, 0xe0000000 >> 16, 0x10000000 >> 16, 0);
	map_banks(&pci_io_bank, 0xfffa0000 >> 16, 0x20000 >> 16, 0);
	map_banks(&pci_bridge_bank, 0xfffe0000 >> 16, 0x10000 >> 16, 0);
	pcib->io_offset = 0xfffa0000;
	return &expamem_null;
}

// Mediator

static const uae_u8 autoconfig_mediator_4000mk2_256m[16] = { 0x84,0xa1,0x20,0x00,0x08,0x9e,0x00,0x00,0x00,0x00,0x00,0x00 };
static const uae_u8 autoconfig_mediator_4000mk2_512m[16] = { 0x85,0xa1,0x20,0x00,0x08,0x9e,0x00,0x00,0x00,0x00,0x00,0x00 };
static const uae_u8 autoconfig_mediator_4000mk2_2[16] = { 0x88,0x21,0x20,0x00,0x08,0x9e,0x00,0x00,0x00,0x00,0x00,0x00 };

static const uae_u8 autoconfig_mediator_1200tx_1[16] = { 0xca,0x3c,0x00,0x00,0x08,0x9e,0x00,0x00,0x00,0x00,0x00,0x00 };
static const uae_u8 autoconfig_mediator_1200tx_2_4m[16] = { 0xc7,0xbc,0x00,0x00,0x08,0x9e,0x00,0x00,0x00,0x00,0x00,0x00 };
static const uae_u8 autoconfig_mediator_1200tx_2_8m[16] = { 0xc0,0xbc,0x00,0x00,0x08,0x9e,0x00,0x00,0x00,0x00,0x00,0x00 };

static int mediator_get_index_1200(uaecptr addr)
{
	struct pci_bridge *pcib = get_pci_bridge(addr);
	if (!pcib)
		return -1;
	uae_u32 offset = addr - pcib->baseaddress_2;
	if (offset < 0x10000)
		return -1;
	offset -= 0x10000;
	int slot = offset / 0x800;
	if (slot >= 6)
		slot = -1;
	return slot;
}

static int mediator_get_index_4000(uaecptr addr)
{
	struct pci_bridge *pcib = get_pci_bridge(addr);
	if (!pcib)
		return -1;
	uae_u32 offset = addr - pcib->baseaddress_2;
	if (offset < 0x800000 || offset >= 0xc00000)
		return -1;
	offset -= 0x800000;
	int slot = offset / 0x800;
	if (slot >= 6)
		slot = -1;
	return slot;
}

static addrbank *mediator_pci_init_1200(struct romconfig *rc)
{
	struct pci_bridge *pcib = pci_bridge_alloc_zorro(PCI_BRIDGE_MEDIATOR, rc);
	if (!pcib)
		return &expamem_null;
	pcib->label = _T("Mediator 1200");
	pcib->endian_swap_config = 1;
	pcib->endian_swap_io = 1;
	pcib->endian_swap_memory = 1;
	pcib->intena = 0;
	pcib->intreq_mask = 0x0008;
	pcib->get_index = mediator_get_index_1200;
	pcib->bank = &pci_bridge_bank;
	pcib->bank_2 = &pci_bridge_bank_2;
	pcib->bank_zorro = 2;
	pcib->bank_2_zorro = 2;
	mediator_set_window_offset(pcib, 0);

	add_pci_devices(pcib);

	memset(pcib->acmemory_2, 0xff, sizeof pcib->acmemory_2);
	for (int i = 0; i < sizeof autoconfig_mediator_1200tx_1; i++) {
		ew(pcib->acmemory_2, i * 4, autoconfig_mediator_1200tx_1[i]);
	}
	return &pci_bridge_bank_2;
}
static addrbank *mediator_pci_init_1200_2(struct romconfig *rc, int size)
{
	struct pci_bridge *pcib = pci_bridge_get_zorro(rc);
	if (!pcib)
		return &expamem_null;

	memset(pcib->acmemory, 0xff, sizeof pcib->acmemory);
	const uae_u8 *ac = size ? autoconfig_mediator_1200tx_2_8m : autoconfig_mediator_1200tx_2_4m;
	for (int i = 0; i < 16; i++) {
		ew(pcib->acmemory, i * 4, ac[i]);
	}
	return &pci_bridge_bank;
}

static addrbank *mediator_pci_init_4000(struct romconfig *rc, int size)
{
	struct pci_bridge *pcib = pci_bridge_alloc_zorro(PCI_BRIDGE_MEDIATOR, rc);
	if (!pcib)
		return &expamem_null;
	pcib->label = _T("Mediator 4000");
	pcib->endian_swap_config = -1;
	pcib->endian_swap_io = -1;
	pcib->endian_swap_memory = -1;
	pcib->intena = 0;
	pcib->intreq_mask = 0x0008;
	pcib->get_index = mediator_get_index_4000;
	pcib->bank = &pci_bridge_bank;
	pcib->bank_2 = &pci_bridge_bank_2;
	pcib->bank_zorro = 3;
	pcib->bank_2_zorro = 3;
	mediator_set_window_offset(pcib, 0);

	add_pci_devices(pcib);

	memset(pcib->acmemory, 0xff, sizeof pcib->acmemory);
	const uae_u8 *ac = size ? autoconfig_mediator_4000mk2_512m : autoconfig_mediator_4000mk2_256m;
	for (int i = 0; i < 16; i++) {
		ew(pcib->acmemory, i * 4, ac[i]);
	}
	return pcib->bank;
}
static addrbank *mediator_pci_init_4000_2(struct romconfig *rc)
{
	struct pci_bridge *pcib = pci_bridge_get_zorro(rc);
	if (!pcib)
		return &expamem_null;

	memset(pcib->acmemory_2, 0xff, sizeof pcib->acmemory_2);
	for (int i = 0; i < sizeof autoconfig_mediator_4000mk2_2; i++) {
		ew(pcib->acmemory_2, i * 4, autoconfig_mediator_4000mk2_2[i]);
	}
	return pcib->bank_2;
}

addrbank *pcibridge_init(struct romconfig *rc)
{
	switch (rc->subtype)
	{
		case 0:
		return prometheus_pci_init(rc);
		case 1:
		return grex_pci_init(rc);
		case 2:
		return mediator_pci_init_1200(rc);
		case 3:
		return mediator_pci_init_1200(rc);
		case 4:
		return mediator_pci_init_4000(rc, 0);
		case 5:
		return mediator_pci_init_4000(rc, 1);
		case 6:
		return cbvision(rc);
	}
	return &expamem_null;
}
addrbank *pcibridge_init2(struct romconfig *rc)
{
	switch (rc->subtype)
	{
		case 2:
		return mediator_pci_init_1200_2(rc, 0);
		case 3:
		return mediator_pci_init_1200_2(rc, 1);
		case 4:
		return mediator_pci_init_4000_2(rc);
		case 5:
		return mediator_pci_init_4000_2(rc);
	}
	return &expamem_null;
}
