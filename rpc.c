 /*
  * UAE - The Un*x Amiga Emulator
  *
  * RiscPC Interface.
  *
  * (c) 1995 Bernd Schmidt
  * (c) 1996 Gustavo Goedert
  */

#include "sysconfig.h"
#include "sysdeps.h"

#include "config.h"
#include "options.h"
#include "threaddep/thread.h"
#include "memory.h"
#include "custom.h"
#include "keyboard.h"
#include "xwin.h"
#include "keybuf.h"
#include "gui.h"

typedef char flagtype;

extern struct regstruct
{
    ULONG regs[16];
    CPTR  usp,isp,msp;
    UWORD sr;
    flagtype t1;
    flagtype t0;
    flagtype s;
    flagtype m;
    flagtype x;
    flagtype stopped;
    int intmask;
    ULONG pc;
#ifdef USE_POINTER
    UBYTE *pc_p;
    UBYTE *pc_oldp;
#endif

    ULONG vbr,sfc,dfc;

    double fp[8];
    ULONG fpcr,fpsr,fpiar;
    ULONG spcflags;
    ULONG kick_mask;
} regs;


#include "OS:wimp.h"
#include "OS:font.h"
#include "OS:os.h"
#include "OS:osbyte.h"
#include "OS:osword.h"
#include "OS:sound.h"


void gui_process(void);


#define A_base 0x300000
#define A_switch (A_base+0)
#define A_disk (A_base+1)
#define A_sonst (A_base+5)
#define A_hf (A_base+6)
#define A_mount (A_base+7)
#define A_rom (A_base+10)
#define A_uaeask (A_base+11)

int quit_program;
int uae_running;
int uae_startedup;

wimp_MESSAGE_LIST(13) messages=
  {A_switch, A_disk, A_disk+1, A_disk+2, A_disk+3, A_sonst, A_rom, A_mount,
   A_mount+1, A_mount+2, A_uaeask, message_PREQUIT, 0};

wimp_t taskhandle;
wimp_t frontendhandle;
wimp_block block;
wimp_message mblock;
int pollword;

os_MODE_SELECTOR(3) uaemode={1,0,0,0,-1,0,128,3,255,-1,0};
os_MODE_SELECTOR(3) wimpmode;

extern void datavox_type(int channel, int type);
extern void datavox_timed(int channel, int timed);
extern void datavox_pitch(int channel, int pitch);
extern void datavox_set_memory(int channel, char *start, char *end);
extern void datavox_set_repeat(int channel, char *start, char *end);
extern int datavox_allocate_channel(int key);
extern void datavox_de_allocate_channel(int channel, int key);

int sratecode;
char sbuffer[44100];
int deskvoice, uaechannel;

extern int uaedevfd, numtracks;

char mountpoint[64];
char mountdir[256];

char *scr;
int screenrowbytes,screenrows;

char pressed[128];
char translate[128]=
{        255,    255,     255,  AK_LSH, AK_CTRL,     AK_LALT,  AK_RSH,     AK_CTRL, //0
     AK_RALT,    255,     255,     255,     255,         255,     255,         255, //8
	AK_Q,   AK_3,    AK_4,    AK_5,   AK_F4,        AK_8,   AK_F7,    AK_MINUS, //16
	 255,  AK_LF,  AK_NP6,  AK_NP7,     255,         255,  AK_F10,         255, //24
	 255,   AK_W,    AK_E,    AK_T,    AK_7,        AK_I,    AK_9,        AK_0, //32
	 255,  AK_DN,  AK_NP8,  AK_NP9,     255,AK_BACKQUOTE, AK_LTGT,       AK_BS, //40
	AK_1,   AK_2,    AK_D,    AK_R,    AK_6,        AK_U,    AK_O,        AK_P, //48
 AK_LBRACKET,  AK_UP,AK_NPADD,AK_NPSUB,  AK_ENT,         255,     255,     AK_RAMI, //56
 AK_CAPSLOCK,   AK_A,    AK_X,    AK_F,    AK_Y,        AK_J,    AK_K,         255, //64
	 255, AK_RET,AK_NPDIV,     255,AK_NPDEL,         255, AK_LAMI,    AK_QUOTE, //72
	 255,   AK_S,    AK_C,    AK_G,    AK_H,        AK_N,    AK_L,AK_SEMICOLON, //80
 AK_RBRACKET, AK_DEL,     255,AK_NPMUL,     255,    AK_EQUAL, AK_LTGT,         255, //88
      AK_TAB,   AK_Z,  AK_SPC,    AK_V,    AK_B,        AK_M,AK_COMMA,   AK_PERIOD, //96
    AK_SLASH,AK_HELP,  AK_NP0,  AK_NP1,  AK_NP3,         255,     255,         255, //104
	 255,  AK_F1,   AK_F2,   AK_F3,   AK_F5,       AK_F6,   AK_F8,       AK_F9, //112
AK_BACKSLASH,  AK_RT,  AK_NP4,  AK_NP5,  AK_NP2,         255,     255,         255};//120

char dn0[256], dn1[256], dn2[256], dn3[256];
int dc0=0, dc1=0, dc2=0, dc3=0;

/***************************************************************************/


void setup_brkhandler(void)
{
}

void flush_line(int y)
{
}

void flush_block(int a, int b)
{
}

void flush_screen(int a, int b)
{
}

void calc_adjustment(void)
{
}


static int colors_allocated;

static int get_color(int r, int g, int b, xcolnr *cnp)
{
    if (colors_allocated == 256)
	return -1;
    *cnp = colors_allocated;

    os_writec(19);
    os_writec(colors_allocated);
    os_writec(16);
    os_writec(r+(r<<4));
    os_writec(g+(g<<4));
    os_writec(b+(b<<4));
    colors_allocated++;

    return 1;
}

static void init_colors(void)
{
    int rw = 5, gw = 5, bw = 5;
    colors_allocated = 0;

    if (gfxvidinfo.pixbytes == 2)
      alloc_colors64k(rw, gw, bw, 0, rw, rw+gw);
    else
      alloc_colors256(get_color);
}

void sound_output(char *b, int l)
{
  memcpy(sbuffer, b, l);
  datavox_set_memory(uaechannel, sbuffer, sbuffer+l);
  datavox_set_repeat(uaechannel, sbuffer, sbuffer+l);
  datavox_type(uaechannel, 1);
  datavox_pitch(uaechannel, sratecode);
  sound_control(uaechannel, 256+127, 0, 255);
}

void init_mouse(void)
{
  oswordpointer_bbox_block bbox;

  bbox.op=oswordpointer_OP_SET_BBOX;
  bbox.x0=-32768;
  bbox.y0=-32768;
  bbox.x1=32767;
  bbox.y1=32767;
  oswordpointer_set_bbox(&bbox);
}

void setwimpmode(void)
{
  wimp_set_mode(&wimpmode);
  while(osbyte2(145,0,0)!=0);
}

void setuaemode(void)
{
  os_vdu_var_list varlist[2]={149,-1};
  int valuelist[1];
  os_mode m;

  m=osscreenmode_current();
  memcpy(&wimpmode, m, os_SIZEOF_MODE_SELECTOR(3));

  osscreenmode_select(&uaemode);
  os_read_vdu_variables(varlist, valuelist);
  scr=(void *)valuelist[0];
  gfxvidinfo.bufmem=scr;

  os_remove_cursors();

  init_colors();
  init_mouse();

  flush_block(0, numscrlines-1);
}

void setwimpsound(void)
{
  int s,t;

  sound_attach_voice(uaechannel, deskvoice, &s, &t);
  datavox_de_allocate_channel(uaechannel, taskhandle);
}

void setuaesound(void)
{
  int s;

  sound_volume(127);
  uaechannel=datavox_allocate_channel(taskhandle);
  printf("%d\n", uaechannel);
  sound_attach_voice(uaechannel, 0, &s, &deskvoice);
  sound_attach_named_voice(uaechannel, "DataVox-Voice");
}

int graphics_init(void)
{
  __uname_control=6;

  switch(color_mode)
  {
    case 1:
    case 2:
    case 5:
      uaemode.log2_bpp=4;
      gfxvidinfo.pixbytes=2;
      break;
    default:
      uaemode.log2_bpp=3;
      gfxvidinfo.pixbytes=1;
      break;
  }

  uaemode.xres=gfx_requested_width;
  uaemode.yres=gfx_requested_height;

  gfxvidinfo.rowbytes=gfx_requested_width*gfxvidinfo.pixbytes;
  gfxvidinfo.maxlinetoscr=gfx_requested_width;
  gfxvidinfo.maxline=gfx_requested_height;
  gfxvidinfo.maxblocklines=0;

  setuaemode();
  setuaesound();

  return 1;
}

void graphics_leave(void)
{
}


void readmouse(void)
{
  int x,y;
  bits buttons;
  os_t t;

  os_mouse(&x, &y, &buttons, &t);
  lastmx=x>>1;
  lastmy=gfx_requested_height-(y>>1);
  buttonstate[0]=(buttons & 4)>>2;
  buttonstate[1]=(buttons & 2)>>1;
  buttonstate[2]=buttons & 1;
  newmousecounters=0;
}


void processkey(char k, char release)
{
  if(k==29 && release==1)
  {
    uae_running=0;
    setwimpmode();
    setwimpsound();
  }

  if(translate[k]!=255)
    record_key((translate[k]<<1)+release);
}


void readkeyboard(void)
{
  char c,l,k,q;

  for(l=0, k=osbyte1(121, 0, 0), q=0; !q; l=k+1, k=osbyte1(121, l, 0))
  {
    if(k==0xff)
    {
      k=128;
      q=1;
    }
    else
    {
      if(pressed[k]==0)
      {
	if (translate[k]!=0)
	  processkey(k, 0);
	pressed[k]=1;
      }
    }
    for(c=l; c<k; c++)
    {
      if(pressed[c]!=0)
      {
	if (translate[c]!=0)
	  processkey(c, 1);
	pressed[c]=0;
      }
    }
  }
}

void handle_events(void)
{
  readmouse();
  readkeyboard();

  if(dc0==1)
  {
    dc0=0;
    disk_insert(0, dn0);
    strncpy(df0, dn0, 255);
  }

  if(dc1==1)
  {
    dc1=0;
    disk_insert(1, dn1);
    strncpy(df1, dn1, 255);
  }

  if(dc2==1)
  {
    dc2=0;
    disk_insert(2, dn2);
    strncpy(df2, dn2, 255);
  }

  if(dc3==1)
  {
    dc3=0;
    disk_insert(3, dn3);
    strncpy(df3, dn3, 255);
  }

  if(dc0>1) dc0--;
  if(dc1>1) dc1--;
  if(dc2>1) dc2--;
  if(dc3>1) dc3--;

  if (uae_running==0)
    gui_process();
}

int debuggable(void)
{
    return 0;
}

int needmousehack(void)
{
    return 0;
}

void LED(int on)
{
}

static void sigchldhandler(int foo)
{
}


/***************************************************************************/


int gui_init(void)
{
  int vout;

  quit_program=0;
  uae_running=0;
  uae_startedup=0;

  taskhandle=wimp_initialise(wimp_VERSION_RO35, "UAE", &messages, &vout);
  gui_process();

  return 0;
}

void changedisk(int n, char *f)
{
  if(uae_startedup)
  {
    switch(n)
    {
      case 0:
	if(strcmp(df0, f)!=0)
	{
	  strncpy(dn0, f, 255);
	  dc0=3;
	  disk_eject(0);
	  strncpy(df0, "", 255);
	}
	break;
      case 1:
	if(strcmp(df1, f)!=0)
	{
	  strncpy(dn1, f, 255);
	  dc1=3;
	  disk_eject(1);
	  strncpy(df1, "", 255);
	}
	break;
      case 2:
	if(strcmp(df2, f)!=0)
	{
	  strncpy(dn2, f, 255);
	  dc2=3;
	  disk_eject(2);
	  strncpy(df2, "", 255);
	}
	break;
      case 3:
	if(strcmp(df3, f)!=0)
	{
	  strncpy(dn3, f, 255);
	  dc3=3;
	  disk_eject(3);
	  strncpy(df3, "", 255);
	}
	break;
    }
  }
  else
  {
    switch(n)
    {
      case 0:
	strncpy(df0, f, 255);
	break;
      case 1:
	strncpy(df1, f, 255);
	break;
      case 2:
	strncpy(df2, f, 255);
	break;
      case 3:
	strncpy(df3, f, 255);
	break;
    }
  }
}

void setsonst(int *reserved)
{
  if(!uae_startedup)
  {
    gfx_requested_width=reserved[0];
    gfx_requested_xcenter=reserved[1];
    gfx_requested_lores=reserved[2];
    gfx_requested_height=reserved[3];
    gfx_requested_ycenter=reserved[4];
    gfx_requested_linedbl=reserved[5];
    gfx_requested_correct_aspect=reserved[6];
    switch(reserved[7])
    {
      case 256:
	color_mode=0;
	break;
      case 32768:
	color_mode=1;
	break;
    }
    framerate=reserved[8];
    emul_accuracy=reserved[9];
    blits_32bit_enabled=reserved[10];
    immediate_blits=reserved[11];
    fake_joystick=reserved[12];
    bogomem_size=reserved[14];
    chipmem_size=reserved[15];
    fastmem_size=reserved[16];
    produce_sound=reserved[17];
    sound_desired_freq=reserved[18];
    sound_desired_bsiz=reserved[19];
  }
}

void sendtofront(int *reserved)
{
  int *words=mblock.data.reserved;

  mblock.size=256;
  mblock.sender=taskhandle;
  mblock.my_ref=778;
  mblock.your_ref=777;
  switch(*reserved)
  {
    case 0:
      mblock.action=A_disk;
      if(dc0==0)
	strncpy(words, df0, 235);
      else
	strncpy(words, dn0, 235);
      break;
    case 1:
      mblock.action=A_disk+1;
      if(dc0==0)
	strncpy(words, df1, 235);
      else
	strncpy(words, dn1, 235);
      break;
    case 2:
      mblock.action=A_disk+2;
      if(dc0==0)
	strncpy(words, df2, 235);
      else
	strncpy(words, dn2, 235);
      break;
    case 3:
      mblock.action=A_disk+3;
      if(dc0==0)
	strncpy(words, df3, 235);
      else
	strncpy(words, dn3, 235);
      break;
  }
  wimp_send_message(wimp_USER_MESSAGE, &mblock, frontendhandle);
}

void gui_messagereceive(void)
{
  switch(block.message.action)
  {
    case message_QUIT:
      if(uae_startedup)
      {
	set_special (SPCFLAG_BRK);
	quit_program=1;
	uae_running=1;
      }
      else
      {
	if(uaedevfd!=-1)
	{
	  close(uaedevfd);
	}
	wimp_close_down(taskhandle);
      }
      break;
    case A_switch:
      if(uae_startedup)
      {
	uae_running=1;
	setuaemode();
	setuaesound();
      }
      else
      {
	frontendhandle=block.message.sender;
	uae_startedup=1;
	uae_running=1;
      }
      break;
    case A_disk:
      changedisk(0, block.message.data.reserved);
      break;
    case A_disk+1:
      changedisk(1, block.message.data.reserved);
      break;
    case A_disk+2:
      changedisk(2, block.message.data.reserved);
      break;
    case A_disk+3:
      changedisk(3, block.message.data.reserved);
      break;
    case A_sonst:
      setsonst(block.message.data.reserved);
      break;
    case A_rom:
      strncpy(romfile, block.message.data.reserved, 235);
      break;
    case A_mount:
      strncpy(mountpoint, block.message.data.reserved, 63);
      break;
    case A_mount+1:
      strncpy(mountdir, block.message.data.reserved, 235);
      add_filesys_unit(mountpoint, mountdir, 0);
      break;
    case A_mount+2:
      strncpy(mountdir, block.message.data.reserved, 235);
      add_filesys_unit(mountpoint, mountdir, 1);
      break;
    case A_uaeask:
      sendtofront(block.message.data.reserved);
      break;
  }
}

void gui_process(void)
{
  wimp_event_no event;

  while(uae_running==0)
  {
    event=wimp_poll(wimp_MASK_NULL, &block, 0);

    switch(event)
    {
      case wimp_USER_MESSAGE:
      case wimp_USER_MESSAGE_RECORDED:
	gui_messagereceive();
	break;
    }
  }
}

void gui_exit(void)
{
}

void gui_led(int led, int on)
{
}

void gui_filename(int num, char *name)
{
}

void gui_handle_events(void)
{
}

int gui_update(void)
{
}

