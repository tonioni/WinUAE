
#include "uae.h"
#include "ibm.h"
#include "pit.h"
#include "pic.h"
#include "cpu.h"
#include "model.h"
#include "x86.h"
#include "x86_ops.h"
#include "codegen.h"
#include "timer.h"
#include "sound.h"
#include "sound_mpu401_uart.h"

#ifdef _WIN32
#include <windows.h>
#include "midi.h"
#endif

#include "pcemglue.h"

#include "sysconfig.h"
#include "sysdeps.h"

PIT pit, pit2;
PIC pic, pic2;
dma_t dma[8];
int AT;

int ppispeakon;
float CGACONST;
float MDACONST;
float VGACONST1, VGACONST2;
float RTCCONST;
int gated, speakval, speakon;

PPI ppi;

cpu_state_s cpu_state;


int codegen_flags_changed;
uint32_t codegen_endpc;
int codegen_in_recompile;
int codegen_flat_ds, codegen_flat_ss;
uint32_t recomp_page;
codeblock_t **codeblock_hash;

void codegen_reset(void)
{
}
void codegen_block_init(unsigned int)
{
}
void codegen_block_remove()
{
}
void codegen_block_start_recompile(struct codeblock_t *)
{
}
void codegen_block_end_recompile(struct codeblock_t *)
{
}
void codegen_block_end()
{
}
void codegen_generate_call(uint8_t opcode, OpFn op, uint32_t fetchdat, uint32_t new_pc, uint32_t old_pc)
{
}
void codegen_check_flush(struct page_t *page, uint64_t mask, uint32_t phys_addr)
{
}
void codegen_flush(void)
{
}
void codegen_init(void)
{
}

void pclog(char const *format, ...)
{
	va_list parms;
	va_start(parms, format);
	char buf[256];
	vsprintf(buf, format, parms);
	write_log("PCEMLOG: %s", buf);
	va_end(parms);
}

extern void activate_debugger(void);
void fatal(char const *format, ...)
{
	va_list parms;
	va_start(parms, format);
	char buf[256];
	vsprintf(buf, format, parms);
	write_log("PCEMFATAL: %s", buf);
	va_end(parms);
	activate_debugger();
}

void video_updatetiming(void)
{
	write_log(_T("video_updatetiming\n"));
}

void device_speed_changed(void)
{
	write_log(_T("device_speed_changed\n"));
}

union CR0_s CR0;

int model;
int cpuspeed;
int CPUID;
int insc;
int hasfpu;
int romset;
int nmi_mask;
int use32;
int stack32;
uint32_t cr2, cr3, cr4;
uint32_t rammask;
uintptr_t *readlookup2;
uintptr_t *writelookup2;
uint16_t flags, eflags;
x86seg gdt, ldt, idt, tr;
x86seg _cs, _ds, _es, _ss, _fs, _gs;
x86seg _oldds;
int writelookup[256], writelookupp[256];
int readlookup[256], readlookupp[256];
int readlnext;
int writelnext;
int pccache;
unsigned char *pccache2;
int cpl_override;
uint32_t oldds, oldss, olddslimit, oldsslimit, olddslimitw, oldsslimitw;
int pci_burst_time, pci_nonburst_time;
int optype;
uint32_t oxpc;
char *logs_path;
uint32_t ealimit, ealimitw;

uint32_t x87_pc_off, x87_op_off;
uint16_t x87_pc_seg, x87_op_seg;

uint32_t dr[8];

int xi8088_bios_128kb(void)
{
	return 0;
}

uint8_t portin(uint16_t portnum);
uint8_t inb(uint16_t port)
{
	return portin(port);
}

void portout(uint16_t, uint8_t);
void outb(uint16_t port, uint8_t v)
{
	portout(port, v);
}
uint16_t portin16(uint16_t portnum);
uint16_t inw(uint16_t port)
{
	return portin16(port);
}
void portout16(uint16_t portnum, uint16_t value);
void outw(uint16_t port, uint16_t val)
{
	portout16(port, val);
}
uint32_t portin32(uint16_t portnum);
uint32_t inl(uint16_t port)
{
	return portin32(port);
}
void portout32(uint16_t portnum, uint32_t value);
void outl(uint16_t port, uint32_t val)
{
	portout32(port, val);
}


static void model_init(void)
{

}

int AMSTRAD;
int TANDY;
int keybsenddelay;
int mouse_buttons;
int mouse_type;
int pcem_key[272];

int mouse_get_type(int mouse_type)
{
	return 0;
}

void t3100e_notify_set(unsigned char v)
{

}
void t3100e_display_set(unsigned char v)
{

}
void t3100e_turbo_set(unsigned char v)
{

}
void t3100e_mono_set(unsigned char v)
{

}
uint8_t t3100e_display_get()
{
	return 0;
}
uint8_t t3100e_config_get()
{
	return 0;
}
uint8_t t3100e_mono_get()
{
	return 0;
}
void xi8088_turbo_set(unsigned char v)
{

}
uint8_t xi8088_turbo_get()
{
	return 0;
}

void ps2_cache_clean(void)
{

}

int video_is_mda()
{
	return 0;
}

MODEL models[] =
{
	{ "[8088] Generic XT clone", ROM_GENXT, "genxt", { { "", cpus_8088 }, { "", NULL }, { "", NULL } }, MODEL_GFX_NONE, 32, 704, 16, model_init, NULL },
	{ "[286] AMI 286 clone", ROM_AMI286, "ami286", { { "", cpus_286 }, { "", NULL }, { "", NULL } }, MODEL_GFX_NONE | MODEL_AT | MODEL_HAS_IDE, 512, 16384, 128, model_init, NULL },
	{ "[386SX] AMI 386SX clone", ROM_AMI386SX, "ami386", { { "386SX", cpus_i386SX }, { "386DX", cpus_i386DX }, { "486", cpus_i486 } }, MODEL_GFX_NONE | MODEL_AT | MODEL_HAS_IDE, 512, 16384, 128, model_init, NULL },
};

int rom_present(char *s)
{
	return 0;
}
static int midi_open;

void midi_write(uint8_t v)
{
	if (!midi_open) {
		midi_open = Midi_Open();
	}
	Midi_Parse(midi_output, &v);
}

void pcem_close(void)
{
	if (midi_open)
		Midi_Close();
	midi_open = 0;
}

// void (*code)() = (void(*)(void))&block->data[BLOCK_START];
