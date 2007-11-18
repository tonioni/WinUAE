#include <intuition/intuition.h>
#include <hardware/custom.h>
#include <exec/ports.h>
#include <exec/interrupts.h>
#include <exec/memory.h>
#include <exec/execbase.h>
#include <proto/exec.h>
#include <proto/graphics.h>
#include <proto/intuition.h>
#include <hardware/intbits.h>
#include "/v.h"
#include <tools.h>
#include <libraries/dos.h>
#include <string.h>

struct MIDITool {
    struct Tool tool;
    unsigned short status;
    unsigned short lownote, highnote;
};

static struct ToolMaster master;

extern struct Functions *functions;

static UWORD chip MidiIn[]=
{
/*-------- plane # 0 --------*/

  0x0000,  0x0000,
  0xffff,  0xfc00,
  0xc000,  0x0c00,
  0xc000,  0x0c00,
  0xc000,  0x0c00,
  0xc000,  0x0c00,
  0xc400,  0x4c00,
  0xc102,  0x0c00,
  0xc030,  0x0c00,
  0xc000,  0x0c00,
  0xffff,  0xfc00,
  0x0000,  0x0000,

/*-------- plane # 1 --------*/

  0x0000,  0x0000,
  0x0000,  0x0c00,
  0x3e31,  0xfc00,
  0x3030,  0x3c00,
  0x2000,  0x1f00,
  0x0000,  0x0f00,
  0x0000,  0x0f00,
  0x2000,  0x1f00,
  0x3000,  0x3c00,
  0x3e01,  0xfc00,
  0x7fff,  0xfc00,
  0x0000,  0x0000
};

static struct Image MidiInimage=
{
  0,0,24,12,2,
  MidiIn,
  0x3,0x0,NULL
};

#define BUFFLEN 32766

extern struct Custom far custom;

static gettime(short lasttime)

{
    unsigned long time = ((custom.vhposr & 0xFF00) >> 8);
    time += (custom.vposr << 8);
    time &= 0x7FFF;
    if (time == 0x1FF) time = lasttime;
    return(time);
}

static short max;

static void setmax()

{
    short i = max;
    Disable();
    max = gettime(0);
    for (;;) {
	i = gettime(i);
	if (i >= max) max = i;
	else break;
    }
    max -= 2;
    Enable();
}

static short lasttime;

static void reset()

{
    lasttime = gettime(lasttime);
}

static timeout()

{
    short newtime = gettime(lasttime);
    if (newtime < lasttime) newtime += max;
    return (newtime > (lasttime + 60));
}



static char *buffer[2] = { 0, 0} ;
static long bufferlength[2] = { 0,0 };
static char bufferselect = 0;
static char allocated = 0;

allocbuffers()

{
    struct Track *track = functions->tracklist;
    struct MIDITool *tool;
    short sysex = 0;
    short i;
    for (;track;track = track->next) {
	tool = (struct MIDITool *) track->toollist;
	if (tool->tool.toolid == ID_MDIN) {
	    sysex |= tool->status;
	}
    }
    if (sysex & 128) {
	if (allocated < 2) {
	    for (i=0;i<2;i++) {
		if (!buffer[i]) {
		    buffer[i] = (char *) (*functions->myalloc)(BUFFLEN,MEMF_PUBLIC);
		    if (buffer[i]) {
			bufferlength[i] = BUFFLEN;
			allocated++;
		    }
		    else bufferlength[i] = 0;
		}
	    }
	    bufferselect = 0;
	}
    }
    else {
	if (allocated) {
	    for (i=0;i<2;i++) {
		if (buffer[i]) {
		    allocated--;
		    (*functions->myfree)(buffer[i],bufferlength[i]);
		    bufferlength[i] = 0;
		    buffer[i] = 0;
		}
	    }
	    bufferselect = 0;
	}
	allocated = 0;
    }
    return(allocated);
}

struct Tool *createtoolcode(copy)

struct MIDITool *copy;

{
    struct MIDITool *tool;
    tool = (struct MIDITool *)
	(*functions->myalloc)(sizeof(struct MIDITool),MEMF_CLEAR);
    if (tool) {
	if (copy) {
	    memcpy((char *)tool,(char *)copy,sizeof(struct MIDITool));
	    tool->tool.next = 0;
	    if (tool->status & 128) allocbuffers();
	}
	tool->tool.tooltype = TOOL_INPUT;
	tool->tool.toolid = ID_MDIN;
	tool->status = 0x7F;
	tool->tool.touched = TOUCH_INIT;
    }
    return((struct Tool *)tool);
}

void deletetoolcode(tool)

struct MIDITool *tool;

{
    if (tool->status & 128) {
	tool->status = 0;
	allocbuffers();
    }
    (*functions->myfree)(tool,sizeof(struct MIDITool));
}

extern struct NewWindow midiinNewWindowStructure1;

static struct Menu TitleMenu = {
    NULL,0,0,0,0,MENUENABLED,0,NULL
};


void edittoolcode(tool)

struct MIDITool *tool;

{
    register struct IntuiMessage *message;
    register struct Window *window;
    register long class, code;
    struct Gadget *gadget;
    struct NewWindow *newwindow;
    char menuname[100];
    midiinNewWindowStructure1.Screen = functions->screen;
    if (tool->tool.touched & TOUCH_EDIT) {
	midiinNewWindowStructure1.LeftEdge = tool->tool.left;
	midiinNewWindowStructure1.TopEdge = tool->tool.top;
	midiinNewWindowStructure1.Width = tool->tool.width;
	midiinNewWindowStructure1.Height = tool->tool.height;
    }
    if (!(tool->tool.touched & TOUCH_INIT)) tool->status = 0x7F;
    newwindow = (struct NewWindow *)
	(*functions->DupeNewWindow)(&midiinNewWindowStructure1);
    if (!newwindow) return;
    newwindow->Title = 0;
    newwindow->Flags |= BORDERLESS;
    newwindow->Flags &= ~0xF;
    window = (struct Window *) (*functions->FlashyOpenWindow)(newwindow);
    if (!window) return;
    (*functions->EmbossWindowOn)(window,WINDOWCLOSE|WINDOWDEPTH|WINDOWDRAG,
	"Midi In",-1,-1,0,0);
    tool->tool.window = window;
    (*functions->EmbossOn)(window,1,1);
    (*functions->EmbossOn)(window,2,1);
    (*functions->EmbossOn)(window,3,1);
    (*functions->EmbossOn)(window,4,1);
    (*functions->EmbossOn)(window,5,1);
    (*functions->EmbossOn)(window,6,1);
    (*functions->EmbossOn)(window,7,1);
    (*functions->SelectEmbossed)(window,1,tool->status & 1);
    (*functions->SelectEmbossed)(window,2,tool->status & 4);
    (*functions->SelectEmbossed)(window,3,tool->status & 8);
    (*functions->SelectEmbossed)(window,4,tool->status & 16);
    (*functions->SelectEmbossed)(window,5,tool->status & 32);
    (*functions->SelectEmbossed)(window,6,tool->status & 64);
    (*functions->SelectEmbossed)(window,7,tool->status & 128);
    strcpy(menuname,"MIDI In v2.2 © 1991,1992 The Blue Ribbon SoundWorks");
    TitleMenu.MenuName = menuname;
    SetMenuStrip(window,&TitleMenu);
    for (;;) {
	message = (struct IntuiMessage *)(*functions->GetIntuiMessage)(window);
	class = message->Class;
	code = message->Code;
	gadget = (struct Gadget *) message->IAddress;
	class = (*functions->SystemGadgets)(window,class,gadget,code);
	ReplyMsg((struct Message *)message);
	if (class == CLOSEWINDOW) break;
	else if (class == GADGETUP) {
	    class = gadget->GadgetID;
	    switch (class) {
		case 1 :
		    tool->status ^= 3;
		    (*functions->SelectEmbossed)(window,1,tool->status & 1);
		    break;
		case 2 :
		case 3 :
		case 4 :
		case 5 :
		case 6 :
		case 7 :
		    class = 1 << (class);
		    tool->status ^= class;
		    if (class & 128) {
			if (!allocbuffers() && (tool->status & 128))
			    tool->status &= ~128;
		    }
		    (*functions->SelectEmbossed)
			(window,gadget->GadgetID,tool->status & class);
		    break;
	    }
	}
    }
    ClearMenuStrip(window);
    (*functions->EmbossOff)(window,1);
    (*functions->EmbossOff)(window,2);
    (*functions->EmbossOff)(window,3);
    (*functions->EmbossOff)(window,4);
    (*functions->EmbossOff)(window,5);
    (*functions->EmbossOff)(window,6);
    (*functions->EmbossOff)(window,7);
    (*functions->EmbossWindowOff)(window);
    tool->tool.window = 0;
    tool->tool.left = window->LeftEdge;
    tool->tool.top = window->TopEdge;
    tool->tool.width = window->Width;
    tool->tool.height = window->Height;
    tool->tool.touched = TOUCH_INIT | TOUCH_EDIT;
    (*functions->FlashyCloseWindow)(window);
    (*functions->DeleteNewWindow)(newwindow);
}

struct Event *processeventcode(event)

struct Event *event;

{
    event->tool = event->tool->next;
    return(event);
}

extern long stdout;

static struct Interrupt midiintin;

static unsigned char plen[128] = {
    2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,
    2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,
    2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,
    2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,
    1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,
    1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,
    2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,
    1,1,2,1,0,0,0,0,0,0,0,0,0,0,0,0
};

/*printstring(string)

struct String *string;

{
    short i;
    unsigned long j;
    if (string) {
	dprintf("Length: %ld\n",string->length);
	for (i=0;i<string->length;i++) {
	    j = string->string[i];
	    j &= 0xFF;
	    dprintf("%lx ",j);
	}
    }
    else dprintf("No string");
    dprintf("\n");
}
*/
static struct Event eventarray[32];
static unsigned short eventindex = 0;
extern printf();

struct Task *eventtask;
long eventsignal;

static void eventcode()

{
    register struct Event *event;
    register unsigned short index;
    register struct Track *track;
    register struct Event *copy;
    register struct MIDITool *tool;
    register char channel;
    register unsigned char status;
    struct StringEvent *stringevent;
    eventsignal = 1 << AllocSignal(-1);
    index = 0;
    for (;;) {
	Wait(eventsignal);
	for (;index != eventindex;) {
	    event = &eventarray[index];
	    status = 1 << ((event->status >> 4) - 8);
	    if (status == 2) {
		if (!event->byte1) {
		    index++;
		    index &= 0x1F;
		    continue;
		}
		if (event->byte2 && functions->remotecontrol[event->byte1]) {
		    if (functions->processinputevent) {
			(*functions->processinputevent)
			    (functions->remotecontrol[event->byte1]);
			index++;
			index &= 0x1F;
			continue;
		    }
		}
	    }
	    if (status == 128) {
		if (functions->multiin) {
		    for (track = functions->tracklist;track;track = track->next) {
			tool = (struct MIDITool *) track->toollist;
			if (tool->tool.toolid == ID_MDIN) {
			    if (tool->status & 128) break;
			}
		    }
		}
		else track = master.intrack;
		if (track) {
		    tool = (struct MIDITool *) track->toollist;
		    stringevent = (struct StringEvent *)
			(*functions->fastallocevent)();
		    if (stringevent) {
			stringevent->type = EVENT_SYSX;
			stringevent->tool = tool->tool.next;
			stringevent->time = event->time;
			if (!event->tool) {
			    index++;
			    index &= 0x1F;
			    continue;
			}
			stringevent->string = (struct String *)
			    (*functions->myalloc)(event->data + 3,0);
			if (stringevent->string) {
			    stringevent->string->length = event->data + 2;
			    memcpy(stringevent->string->string,event->tool,
				event->data);
			}
			stringevent->status = MIDI_SYSX;
			(*functions->qevent)(stringevent);
		    }
		}
		index++;
		index &= 0x1F;
		continue;
	    }
	    if (functions->multiin) {
		channel = event->status & 15;
		track = (struct Track *) functions->tracklist;
		for (;track;track = track->next) {
		    tool = (struct MIDITool *) track->toollist;
		    if (tool && (tool->tool.toolid == ID_MDIN) &&
			(tool->status & status)) {
			if (track->channelin == channel) {
			    break;
			}
		    }
		}
	    }
	    else {
		track = master.intrack;
	    }
	    if (track) {
		tool = (struct MIDITool *) track->toollist;
		if (tool && (tool->tool.toolid == ID_MDIN) &&
		    (tool->status & status)) {
		    copy = (struct Event *) (*functions->fastallocevent)();
		    if (copy) {
			copy->type = EVENT_VOICE;
			copy->tool = tool->tool.next;
			copy->status = (event->status & 0xF0);
			copy->byte1 = event->byte1;
			copy->byte2 = event->byte2;
			if (copy->status == MIDI_NOTEON) {
			    if (!copy->byte2) copy->status = MIDI_NOTEOFF;
			}
			copy->time = event->time;
			(*functions->qevent)(copy);
		    }
		}
	    }
	    index++;
	    index &= 0x1F;
	    continue;
	}
    }
}


static void midiincode()

/*      Midi in interrupt code.
*/

{
    static unsigned char midiinlen;
    static unsigned char midiinpos;
    static char unknownstatus = 1;
    static unsigned char sysexon = 0;
    static unsigned char miditimecode = 0;
    register struct Event *event;
    static unsigned char status;
    register unsigned short data;
    struct Event timeevent;

    static unsigned short bufferindex;
    static char *bufferpoint = 0;

    for (data = custom.serdatr;;data = custom.serdatr) {
	if (!(data & 0x4000)) {
	    if (sysexon && master.readsysex && !timeout()) continue;
	    return;
	}
	if (data & 0x8000) unknownstatus = 1;
	if (sysexon && master.readsysex) reset();
	event = &eventarray[eventindex];
	custom.intreq = INTF_RBF;
	data &= 0xFF;
	if (data & 0x80) {
	    if (data >= MIDI_CLOCK) {
		if (data == MIDI_CLOCK) {
		    timeevent.status = MIDI_CLOCK;
		    if (functions->midiclock)
			(*functions->processmidiclock)(&timeevent);
		    continue;
		}
		if (data == MIDI_START) {
		    timeevent.status = MIDI_START;
		    if (functions->midiclock)
			(*functions->processmidiclock)(&timeevent);
		    continue;
		}
		if (data == MIDI_CONTINUE) {
		    timeevent.status = MIDI_CONTINUE;
		    if (functions->midiclock)
			(*functions->processmidiclock)(&timeevent);
		    continue;
		}
		if (data == MIDI_STOP) {
		    timeevent.status = MIDI_STOP;
		    if (functions->midiclock)
			(*functions->processmidiclock)(&timeevent);
		    continue;
		}
		continue;
	    }
	    else if (data == MIDI_MTC) {
		miditimecode = 1;
		continue;
	    }
	    else if (sysexon) {  /* End of Sysex. */
		if (master.readsysex) {
		    (*master.readsysex)(MIDI_EOX);
		}
		else if (allocated) {
		    bufferpoint[bufferindex++] = MIDI_EOX;
		    if (bufferindex >= BUFFLEN) bufferindex = (BUFFLEN - 1);
		    eventindex++;
		    eventindex &= 0x1F;
		    event->status = MIDI_SYSX;
		    event->time = functions->timenow;
		    event->byte1 = bufferselect;
		    event->data = bufferindex;
		    event->tool = (struct Tool *) bufferpoint;
		    Signal(eventtask,eventsignal);
		    bufferselect = !bufferselect;
		    if (bufferselect >= allocated) bufferselect = 0;
		    bufferpoint = buffer[bufferselect];
		}
		sysexon = 0;
		unknownstatus = 1;
		if (data == MIDI_EOX) continue;
	    }
	    status = data;
	    midiinlen = plen[data & 0x7F];
	    midiinpos = 0;
	    unknownstatus = 0;
	    if (data == MIDI_SYSX) {
		sysexon = 1;
		if (master.readsysex) {
		    reset();
		    (*master.readsysex)(MIDI_SYSX);
		}
		else if (allocated) {
		    bufferindex = 0;
		    bufferpoint = buffer[bufferselect];
		    bufferpoint[bufferindex++] = MIDI_SYSX;
		}
		continue;
	    }
	}
	else if (miditimecode) {
//          if (functions->smpteclock) {
		timeevent.status = MIDI_MTC;
		timeevent.byte1 = data;
		(*functions->processsmpteclock)(&timeevent);
//          }
	    miditimecode = 0;
	    continue;
	}
	else if (sysexon) {
	    if (master.readsysex) {
		(*master.readsysex)(data);
	    }
	    else if (allocated) {
		bufferpoint[bufferindex++] = data;
		if (bufferindex >= BUFFLEN) bufferindex = (BUFFLEN - 1);
	    }
	    continue;
	}
	else if (unknownstatus) {
	    continue;
	}
	else if (++midiinpos == 1) event->byte1 = data;
	else event->byte2 = data;
	if (midiinpos >= midiinlen) {
	    event->status = status;
	    midiinpos = 0;
	    if (status == MIDI_SONGPP) {
		if (functions->midiclock)
		    (*functions->processmidiclock)(event);
		continue;
	    }
	    else {
		eventindex++;
		eventindex &= 0x1F;
		event->time = functions->timenow;
		Signal(eventtask,eventsignal);
		continue;
	    }
	}
    }
}


void stealint();
struct Interrupt *oldserin = 0;

static void releaseint()

{
    custom.intena = INTF_RBF;
    Disable();
    SetIntVector(INTB_RBF,oldserin);
    Enable();
    if (oldserin) custom.intena = INTF_SETCLR | INTF_RBF;
    functions->releasemidi = 0;
    functions->stealmidi = stealint;
}

void removetoolcode()

{
    releaseint();
    DeleteTask(eventtask);
    functions->stealmidi = 0;
}

void stealint()

{
    midiintin.is_Node.ln_Name = "Midi receiver";
    midiintin.is_Node.ln_Pri = 20;
    midiintin.is_Node.ln_Type = NT_INTERRUPT;
    midiintin.is_Code = midiincode;
    Disable();
    oldserin = (struct Interrupt *) SetIntVector(INTB_RBF,&midiintin);
    Enable();
    custom.serper = 114;
    custom.intena = INTF_SETCLR | INTF_RBF;
    functions->releasemidi = releaseint;
    functions->stealmidi = 0;
}

struct ToolMaster *inittoolmaster()

{
    midiintin.is_Node.ln_Name = "Midi receiver";
    midiintin.is_Node.ln_Pri = 20;
    midiintin.is_Node.ln_Type = NT_INTERRUPT;
    midiintin.is_Code = midiincode;
    eventtask = CreateTask("midi in",40,eventcode,4000);
    if (functions->releasemidi) {
	(*functions->releasemidi)();
    }
    functions->releasemidi = releaseint;
    stealint();
    memset((char *)&master,0,sizeof(struct ToolMaster));
    master.toolid = ID_MDIN;
    master.image = &MidiInimage;
    strcpy(master.name,"MIDI In");
    master.edittool = edittoolcode;
    master.processevent = processeventcode;
    master.createtool = createtoolcode;
    master.deletetool = deletetoolcode;
    master.tooltype = TOOL_INPUT | TOOL_MIDI;
    master.toolsize = sizeof(struct MIDITool);
    master.removetool = removetoolcode;
    setmax();
    return(&master);
}
