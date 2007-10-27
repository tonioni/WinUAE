/*
 * UAE - the Un*x Amiga Emulator
 *
 * Yet Another User Interface for the X11 version
 *
 * Copyright 1997, 1998 Bernd Schmidt
 * Copyright 1998 Michael Krause
 *
 * The Tk GUI doesn't work.
 * The X Forms Library isn't available as source, and there aren't any
 * binaries compiled against glibc
 *
 * So let's try this...
 */

#include "sysconfig.h"
#include "sysdeps.h"

#include "config.h"
#include "options.h"
#include "uae.h"
#include "memory.h"
#include "custom.h"
#include "gui.h"
#include "newcpu.h"
#include "autoconf.h"
#include "threaddep/thread.h"
#include "sounddep/sound.h"
#include "savestate.h"
#include "compemu.h"

#include <gtk/gtk.h>
#include <gdk/gdk.h>

/* One of the 1.1.6 "features" is a gratuitous name change */
#ifndef HAVE_GTK_FEATURES_1_1_6
#define gtk_container_set_border_width gtk_container_border_width
#endif
/* Likewise for 1.1.8.  */
#ifndef HAVE_GTK_FEATURES_1_1_8
#define gtk_label_set_text gtk_label_set
#endif
/* This is beginning to suck... */
#ifndef HAVE_GTK_FEATURES_1_1_13
#define gtk_toggle_button_set_active gtk_toggle_button_set_state
#endif

static int gui_active;

static GtkWidget *gui_window;

static GtkWidget *pause_uae_widget, *snap_save_widget, *snap_load_widget;

static GtkWidget *chipsize_widget[5];
static GtkWidget *bogosize_widget[4];
static GtkWidget *fastsize_widget[5];
static GtkWidget *z3size_widget[10];
static GtkWidget *p96size_widget[7];
static GtkWidget *rom_text_widget, *key_text_widget;
static GtkWidget *rom_change_widget, *key_change_widget;

static GtkWidget *disk_insert_widget[4], *disk_eject_widget[4], *disk_text_widget[4];
static char *new_disk_string[4];

static GtkAdjustment *cpuspeed_adj;
static GtkWidget *cpuspeed_widgets[4], *cpuspeed_scale;
static GtkWidget *cpu_widget[5], *a24m_widget, *ccpu_widget;
static GtkWidget *sound_widget[4], *sound_bits_widget[2], *sound_freq_widget[3], *sound_ch_widget[3];

static GtkWidget *coll_widget[4], *cslevel_widget[4];
static GtkWidget *fcop_widget;

static GtkAdjustment *framerate_adj;
static GtkWidget *bimm_widget, *b32_widget, *afscr_widget, *pfscr_widget;

static GtkWidget *compbyte_widget[4], *compword_widget[4], *complong_widget[4];
static GtkWidget *compaddr_widget[4], *compnf_widget[2], *comp_midopt_widget[2];
static GtkWidget *comp_lowopt_widget[2], *compfpu_widget[2], *comp_hardflush_widget[2];
static GtkWidget *comp_constjump_widget[2];
static GtkAdjustment *cachesize_adj;

static GtkWidget *joy_widget[2][6];

static GtkWidget *led_widgets[5];
static GdkColor led_on[5], led_off[5];
static unsigned int prevledstate;

static GtkWidget *hdlist_widget;
static int selected_hd_row;
static GtkWidget *hdchange_button, *hddel_button;
static GtkWidget *volname_entry, *path_entry;
static GtkWidget *dirdlg;
static char dirdlg_volname[256], dirdlg_path[256];

static smp_comm_pipe to_gui_pipe, from_gui_pipe;
static uae_sem_t gui_sem, gui_init_sem, gui_quit_sem; /* gui_sem protects the DFx fields */

static volatile int quit_gui = 0, quitted_gui = 0;

static void save_config (void)
{
    FILE *f;
    char tmp[257];

    /* Backup the options file.  */
    strcpy (tmp, optionsfile);
    strcat (tmp, "~");
    rename (optionsfile, tmp);

    f = fopen (optionsfile, "w");
    if (f == NULL) {
	write_log ("Error saving options file!\n");
	return;
    }
    save_options (f, &currprefs);
    fclose (f);
}

static int nr_for_led (GtkWidget *led)
{
    int i;
    i = 0;
    while (led_widgets[i] != led)
	i++;
    return i;
}

static void enable_disk_buttons (int enable)
{
    int i;
    for (i = 0; i < 4; i++) {
	gtk_widget_set_sensitive (disk_insert_widget[i], enable);
	gtk_widget_set_sensitive (disk_eject_widget[i], enable);
    }
}

static void enable_snap_buttons (int enable)
{
    gtk_widget_set_sensitive (snap_save_widget, enable);
    gtk_widget_set_sensitive (snap_load_widget, enable);
}

static void set_cpu_state (void)
{
    int i;

    gtk_toggle_button_set_active (GTK_TOGGLE_BUTTON (a24m_widget), changed_prefs.address_space_24 != 0);
    gtk_widget_set_sensitive (a24m_widget, changed_prefs.cpu_level > 1 && changed_prefs.cpu_level < 4);
    gtk_toggle_button_set_active (GTK_TOGGLE_BUTTON (ccpu_widget), changed_prefs.cpu_compatible != 0);
    gtk_widget_set_sensitive (ccpu_widget, changed_prefs.cpu_level == 0);
    gtk_widget_set_sensitive (cpuspeed_scale, changed_prefs.m68k_speed > 0);
    for (i = 0; i < 10; i++)
	gtk_widget_set_sensitive (z3size_widget[i],
				  changed_prefs.cpu_level >= 2 && ! changed_prefs.address_space_24);
}

static void set_cpu_widget (void)
{
    int nr = changed_prefs.cpu_level;

    gtk_toggle_button_set_active (GTK_TOGGLE_BUTTON (cpu_widget[nr]), TRUE);
    nr = currprefs.m68k_speed + 1 < 3 ? currprefs.m68k_speed + 1 : 2;
    gtk_toggle_button_set_active (GTK_TOGGLE_BUTTON (cpuspeed_widgets[nr]), TRUE);

}

static void set_gfx_state (void)
{
    gtk_toggle_button_set_active (GTK_TOGGLE_BUTTON (bimm_widget), currprefs.immediate_blits != 0);
#if 0
    gtk_toggle_button_set_active (GTK_TOGGLE_BUTTON (b32_widget), currprefs.blits_32bit_enabled != 0);
    gtk_toggle_button_set_active (GTK_TOGGLE_BUTTON (afscr_widget), currprefs.gfx_afullscreen != 0);
    gtk_toggle_button_set_active (GTK_TOGGLE_BUTTON (pfscr_widget), currprefs.gfx_pfullscreen != 0);
#endif
}

static void set_chipset_state (void)
{
    int t0 = 0;
    gtk_toggle_button_set_active (GTK_TOGGLE_BUTTON (coll_widget[currprefs.collision_level]), TRUE);
    if (currprefs.chipset_mask & CSMASK_AGA)
	t0 = 3;
    else if (currprefs.chipset_mask & CSMASK_ECS_DENISE)
	t0 = 2;
    else if (currprefs.chipset_mask & CSMASK_ECS_AGNUS)
	t0 = 1;
    gtk_toggle_button_set_active (GTK_TOGGLE_BUTTON (cslevel_widget[t0]), TRUE);
    gtk_toggle_button_set_active (GTK_TOGGLE_BUTTON (fcop_widget), currprefs.fast_copper != 0);
}

static void set_sound_state (void)
{
    int stereo = currprefs.stereo + currprefs.mixed_stereo;
    gtk_toggle_button_set_active (GTK_TOGGLE_BUTTON (sound_widget[currprefs.produce_sound]), 1);
    gtk_toggle_button_set_active (GTK_TOGGLE_BUTTON (sound_ch_widget[stereo]), 1);
    gtk_toggle_button_set_active (GTK_TOGGLE_BUTTON (sound_bits_widget[currprefs.sound_bits == 16]), 1);
}

static void set_mem_state (void)
{
    int t, t2;

    t = 0;
    t2 = currprefs.chipmem_size;
    while (t < 4 && t2 > 0x80000)
	t++, t2 >>= 1;
    gtk_toggle_button_set_active (GTK_TOGGLE_BUTTON (chipsize_widget[t]), 1);

    t = 0;
    t2 = currprefs.bogomem_size;
    while (t < 3 && t2 >= 0x80000)
	t++, t2 >>= 1;
    gtk_toggle_button_set_active (GTK_TOGGLE_BUTTON (bogosize_widget[t]), 1);

    t = 0;
    t2 = currprefs.fastmem_size;
    while (t < 4 && t2 >= 0x100000)
	t++, t2 >>= 1;
    gtk_toggle_button_set_active (GTK_TOGGLE_BUTTON (fastsize_widget[t]), 1);

    t = 0;
    t2 = currprefs.z3fastmem_size;
    while (t < 9 && t2 >= 0x100000)
	t++, t2 >>= 1;
    gtk_toggle_button_set_active (GTK_TOGGLE_BUTTON (z3size_widget[t]), 1);

    t = 0;
    t2 = currprefs.gfxmem_size;
    while (t < 6 && t2 >= 0x100000)
	t++, t2 >>= 1;
    gtk_toggle_button_set_active (GTK_TOGGLE_BUTTON (p96size_widget[t]), 1);

    gtk_label_set_text (GTK_LABEL (rom_text_widget), currprefs.romfile);
    gtk_label_set_text (GTK_LABEL (key_text_widget), currprefs.keyfile);
}

static void set_comp_state (void)
{
    gtk_toggle_button_set_active (GTK_TOGGLE_BUTTON (compbyte_widget[currprefs.comptrustbyte]), 1);
    gtk_toggle_button_set_active (GTK_TOGGLE_BUTTON (compword_widget[currprefs.comptrustword]), 1);
    gtk_toggle_button_set_active (GTK_TOGGLE_BUTTON (complong_widget[currprefs.comptrustlong]), 1);
    gtk_toggle_button_set_active (GTK_TOGGLE_BUTTON (compaddr_widget[currprefs.comptrustnaddr]), 1);
    gtk_toggle_button_set_active (GTK_TOGGLE_BUTTON (compnf_widget[currprefs.compnf]), 1);
    gtk_toggle_button_set_active (GTK_TOGGLE_BUTTON (comp_hardflush_widget[currprefs.comp_hardflush]), 1);
    gtk_toggle_button_set_active (GTK_TOGGLE_BUTTON (comp_constjump_widget[currprefs.comp_constjump]), 1);

    gtk_toggle_button_set_active (GTK_TOGGLE_BUTTON (compfpu_widget[currprefs.compfpu]), 1);
#if USE_OPTIMIZER
    gtk_toggle_button_set_active (GTK_TOGGLE_BUTTON (comp_midopt_widget[currprefs.comp_midopt]), 1);
#endif
#if USE_LOW_OPTIMIZER
    gtk_toggle_button_set_active (GTK_TOGGLE_BUTTON (comp_lowopt_widget[currprefs.comp_lowopt]), 1);
#endif
}

static void set_joy_state (void)
{
    int j0t = changed_prefs.jport0;
    int j1t = changed_prefs.jport1;
    int i;

    if (j0t == j1t) {
	/* Can't happen */
	j0t++;
	j0t %= 6;
    }
    for (i = 0; i < 6; i++) {
	gtk_toggle_button_set_active (GTK_TOGGLE_BUTTON (joy_widget[0][i]), j0t == i);
	gtk_toggle_button_set_active (GTK_TOGGLE_BUTTON (joy_widget[1][i]), j1t == i);
	gtk_widget_set_sensitive (joy_widget[0][i], j1t != i);
	gtk_widget_set_sensitive (joy_widget[1][i], j0t != i);
    }
}

static void set_hd_state (void)
{
    char texts[9][256];
    char *tptrs[] = { texts[0], texts[1], texts[2], texts[3], texts[4], texts[5], texts[6], texts[7], texts[8] };
    int nr = nr_units (currprefs.mountinfo);
    int i;

    gtk_clist_freeze (GTK_CLIST (hdlist_widget));
    gtk_clist_clear (GTK_CLIST (hdlist_widget));
    for (i = 0; i < nr; i++) {
	int secspertrack, surfaces, reserved, blocksize, size;
	int cylinders, readonly;
	char *volname, *rootdir;
	char *failure;

	/* We always use currprefs.mountinfo for the GUI.  The filesystem
	   code makes a private copy which is updated every reset.  */
	failure = get_filesys_unit (currprefs.mountinfo, i,
				    &volname, &rootdir, &readonly,
				    &secspertrack, &surfaces, &reserved,
				    &cylinders, &size, &blocksize);

	if (is_hardfile (currprefs.mountinfo, i)) {
	    sprintf (texts[0], "DH%d", i );
	    sprintf (texts[3], "%d", surfaces);
	    sprintf (texts[4], "%d", cylinders);
	    sprintf (texts[5], "%d", secspertrack);
	    sprintf (texts[6], "%d", reserved);
	    sprintf (texts[7], "%d", size);
	    sprintf (texts[8], "%d", blocksize);
	} else {
	    strcpy (texts[0], volname);
	    strcpy (texts[3], "n/a");
	    strcpy (texts[4], "n/a");
	    strcpy (texts[5], "n/a");
	    strcpy (texts[6], "n/a");
	    strcpy (texts[7], "n/a");
	    strcpy (texts[8], "n/a");
	}
	strcpy (texts[1], rootdir);
	strcpy (texts[2], readonly ? "y" : "n");
	gtk_clist_append (GTK_CLIST (hdlist_widget), tptrs);
    }
    gtk_clist_thaw (GTK_CLIST (hdlist_widget));
    gtk_widget_set_sensitive (hdchange_button, FALSE);
    gtk_widget_set_sensitive (hddel_button, FALSE);
}

static void draw_led (int nr)
{
    GtkWidget *thing = led_widgets[nr];
    GdkWindow *window = thing->window;
    GdkGC *gc = gdk_gc_new (window);
    GdkColor *col;

    if (gui_ledstate & (1 << nr))
	col = led_on + nr;
    else
	col = led_off + nr;
    gdk_gc_set_foreground (gc, col);
    gdk_draw_rectangle (window, gc, 1, 0, 0, -1, -1);
    gdk_gc_destroy (gc);
}

static int my_idle (void)
{
    unsigned int leds = gui_ledstate;
    int i;

    if (quit_gui) {
	gtk_main_quit ();
	goto out;
    }
    while (comm_pipe_has_data (&to_gui_pipe)) {
	int cmd = read_comm_pipe_int_blocking (&to_gui_pipe);
	int n;
	switch (cmd) {
	 case 0:
	    n = read_comm_pipe_int_blocking (&to_gui_pipe);
	    gtk_label_set_text (GTK_LABEL (disk_text_widget[n]), currprefs.df[n]);
	    break;
	 case 1:
	    /* Initialization.  */
	    set_cpu_widget ();
	    set_cpu_state ();
	    set_gfx_state ();
	    set_joy_state ();
	    set_sound_state ();
	    set_comp_state ();
	    set_mem_state ();
	    set_hd_state ();
	    set_chipset_state ();

	    gtk_widget_show (gui_window);
	    uae_sem_post (&gui_init_sem);
	    gui_active = 1;
	    break;
	}
    }

    for (i = 0; i < 5; i++) {
	unsigned int mask = 1 << i;
	unsigned int on = leds & mask;

	if (on == (prevledstate & mask))
	    continue;

/*	printf(": %d %d\n", i, on);*/
	draw_led (i);
    }
    prevledstate = leds;
out:
    return 1;
}

static int find_current_toggle (GtkWidget **widgets, int count)
{
    int i;
    for (i = 0; i < count; i++)
	if (GTK_TOGGLE_BUTTON (*widgets++)->active)
	    return i;
    write_log ("GTKUI: Can't happen!\n");
    return -1;
}

static void joy_changed (void)
{
    if (! gui_active)
	return;

    changed_prefs.jport0 = find_current_toggle (joy_widget[0], 6);
    changed_prefs.jport1 = find_current_toggle (joy_widget[1], 6);
    set_joy_state ();
}

static void coll_changed (void)
{
    changed_prefs.collision_level = find_current_toggle (coll_widget, 4);
}

static void cslevel_changed (void)
{
    int t = find_current_toggle (cslevel_widget, 4);
    int t1 = 0;
    if (t > 0)
	t1 |= CSMASK_ECS_AGNUS;
    if (t > 1)
	t1 |= CSMASK_ECS_DENISE;
    if (t > 2)
	t1 |= CSMASK_AGA;
    changed_prefs.chipset_mask = t1;
}

static void custom_changed (void)
{
    changed_prefs.gfx_framerate = framerate_adj->value;
    changed_prefs.immediate_blits = GTK_TOGGLE_BUTTON (bimm_widget)->active;
    changed_prefs.fast_copper = GTK_TOGGLE_BUTTON (fcop_widget)->active;
#if 0
    changed_prefs.blits_32bit_enabled = GTK_TOGGLE_BUTTON (b32_widget)->active;
    changed_prefs.gfx_afullscreen = GTK_TOGGLE_BUTTON (afscr_widget)->active;
    changed_prefs.gfx_pfullscreen = GTK_TOGGLE_BUTTON (pfscr_widget)->active;
#endif
}

static void cpuspeed_changed (void)
{
    int which = find_current_toggle (cpuspeed_widgets, 3);
    changed_prefs.m68k_speed = (which == 0 ? -1
				: which == 1 ? 0
				: cpuspeed_adj->value);
    set_cpu_state ();
}

static void cputype_changed (void)
{
    int i, oldcl;
    if (! gui_active)
	return;

    oldcl = changed_prefs.cpu_level;

    changed_prefs.cpu_level = find_current_toggle (cpu_widget, 5);
    changed_prefs.cpu_compatible = GTK_TOGGLE_BUTTON (ccpu_widget)->active;
    changed_prefs.address_space_24 = GTK_TOGGLE_BUTTON (a24m_widget)->active;

    if (changed_prefs.cpu_level != 0)
	changed_prefs.cpu_compatible = 0;
    /* 68000/68010 always have a 24 bit address space.  */
    if (changed_prefs.cpu_level < 2)
	changed_prefs.address_space_24 = 1;
    /* Changing from 68000/68010 to 68020 should set a sane default.  */
    else if (oldcl < 2)
	changed_prefs.address_space_24 = 0;

    set_cpu_state ();
}

static void chipsize_changed (void)
{
    int t = find_current_toggle (chipsize_widget, 5);
    changed_prefs.chipmem_size = 0x80000 << t;
    for (t = 0; t < 5; t++)
	gtk_widget_set_sensitive (fastsize_widget[t], changed_prefs.chipmem_size <= 0x200000);
    if (changed_prefs.chipmem_size > 0x200000) {
	gtk_toggle_button_set_active (GTK_TOGGLE_BUTTON (fastsize_widget[0]), 1);
	changed_prefs.fastmem_size = 0;
    }
}

static void bogosize_changed (void)
{
    int t = find_current_toggle (bogosize_widget, 4);
    changed_prefs.bogomem_size = (0x40000 << t) & ~0x40000;
}

static void fastsize_changed (void)
{
    int t = find_current_toggle (fastsize_widget, 5);
    changed_prefs.fastmem_size = (0x80000 << t) & ~0x80000;
}

static void z3size_changed (void)
{
    int t = find_current_toggle (z3size_widget, 10);
    changed_prefs.z3fastmem_size = (0x80000 << t) & ~0x80000;
}

static void p96size_changed (void)
{
    int t = find_current_toggle (p96size_widget, 7);
    changed_prefs.gfxmem_size = (0x80000 << t) & ~0x80000;
}

static void sound_changed (void)
{
    changed_prefs.produce_sound = find_current_toggle (sound_widget, 4);
    changed_prefs.stereo = find_current_toggle (sound_ch_widget, 3);
    changed_prefs.mixed_stereo = 0;
    if (changed_prefs.stereo == 2)
	changed_prefs.mixed_stereo = changed_prefs.stereo = 1;
    changed_prefs.sound_bits = (find_current_toggle (sound_bits_widget, 2) + 1) * 8;
}

static void comp_changed (void)
{
  changed_prefs.cachesize=cachesize_adj->value;
  changed_prefs.comptrustbyte = find_current_toggle (compbyte_widget, 4);
  changed_prefs.comptrustword = find_current_toggle (compword_widget, 4);
  changed_prefs.comptrustlong = find_current_toggle (complong_widget, 4);
  changed_prefs.comptrustnaddr = find_current_toggle (compaddr_widget, 4);
  changed_prefs.compnf = find_current_toggle (compnf_widget, 2);
  changed_prefs.comp_hardflush = find_current_toggle (comp_hardflush_widget, 2);
  changed_prefs.comp_constjump = find_current_toggle (comp_constjump_widget, 2);
  changed_prefs.compfpu= find_current_toggle (compfpu_widget, 2);
#if USE_OPTIMIZER
  changed_prefs.comp_midopt = find_current_toggle (comp_midopt_widget, 2);
#endif
#if USE_LOW_OPTIMIZER
  changed_prefs.comp_lowopt = find_current_toggle (comp_lowopt_widget, 2);
#endif
}

static void did_reset (void)
{
    if (quit_gui)
	return;

    write_comm_pipe_int (&from_gui_pipe, 2, 1);
}

static void did_debug (void)
{
    if (quit_gui)
	return;

    write_comm_pipe_int (&from_gui_pipe, 3, 1);
}

static void did_quit (void)
{
    if (quit_gui)
	return;

    write_comm_pipe_int (&from_gui_pipe, 4, 1);
}

static void did_eject (GtkWidget *w, gpointer data)
{
    if (quit_gui)
	return;

    write_comm_pipe_int (&from_gui_pipe, 0, 0);
    write_comm_pipe_int (&from_gui_pipe, (int)data, 1);
}

static void pause_uae (GtkWidget *widget, gpointer data)
{
    if (quit_gui)
	return;

    write_comm_pipe_int (&from_gui_pipe, GTK_TOGGLE_BUTTON (widget)->active ? 5 : 6, 1);
}

static void end_pause_uae (void)
{
    gtk_toggle_button_set_active (GTK_TOGGLE_BUTTON (pause_uae_widget), FALSE);
}

static int filesel_active = -1;
static GtkWidget *disk_selector;

static int snapsel_active = -1;
static char *gui_snapname, *gui_romname, *gui_keyname;

static void did_close_insert (gpointer data)
{
    filesel_active = -1;
    enable_disk_buttons (1);
}

static void did_insert_select (GtkObject *o)
{
    char *s = gtk_file_selection_get_filename (GTK_FILE_SELECTION (disk_selector));
    printf ("%d %s\n", filesel_active, s);
    if (quit_gui)
	return;

    uae_sem_wait (&gui_sem);
    if (new_disk_string[filesel_active] != 0)
	free (new_disk_string[filesel_active]);
    new_disk_string[filesel_active] = strdup (s);
    uae_sem_post (&gui_sem);
    write_comm_pipe_int (&from_gui_pipe, 1, 0);
    write_comm_pipe_int (&from_gui_pipe, filesel_active, 1);
    filesel_active = -1;
    enable_disk_buttons (1);
    gtk_widget_destroy (disk_selector);
}

static char fsbuffer[100];

static GtkWidget *make_file_selector (const char *title,
				      void (*insertfunc)(GtkObject *),
				      void (*closefunc)(gpointer))
{
    GtkWidget *p = gtk_file_selection_new (title);
    gtk_signal_connect (GTK_OBJECT (p), "destroy", (GtkSignalFunc) closefunc, p);

    gtk_signal_connect_object (GTK_OBJECT (GTK_FILE_SELECTION (p)->ok_button),
			       "clicked", (GtkSignalFunc) insertfunc,
			       GTK_OBJECT (p));
    gtk_signal_connect_object (GTK_OBJECT (GTK_FILE_SELECTION (p)->cancel_button),
			       "clicked", (GtkSignalFunc) gtk_widget_destroy,
			       GTK_OBJECT (p));

#if 0
    gtk_window_set_title (GTK_WINDOW (p), title);
#endif

    gtk_widget_show (p);
    return p;
}

static void filesel_set_path (GtkWidget *p, const char *path)
{
    size_t len = strlen (path);
    if (len > 0 && ! access (path, R_OK)) {
	char *tmp = xmalloc (len + 2);
	strcpy (tmp, path);
	strcat (tmp, "/");
	gtk_file_selection_set_filename (GTK_FILE_SELECTION (p),
					 tmp);
    }
}

static void did_insert (GtkWidget *w, gpointer data)
{
    int n = (int)data;
    if (filesel_active != -1)
	return;
    filesel_active = n;
    enable_disk_buttons (0);

    sprintf (fsbuffer, "Select a disk image file for DF%d", n);
    disk_selector = make_file_selector (fsbuffer, did_insert_select, did_close_insert);
    filesel_set_path (disk_selector, currprefs.path_floppy);
}

static gint driveled_event (GtkWidget *thing, GdkEvent *event)
{
    int lednr = nr_for_led (thing);

    switch (event->type) {
     case GDK_MAP:
	draw_led (lednr);
	break;
     case GDK_EXPOSE:
	draw_led (lednr);
	break;
     default:
	break;
    }

  return 0;
}

static GtkWidget *snap_selector;

static void did_close_snap (gpointer gdata)
{
    snapsel_active = -1;
    enable_snap_buttons (1);
}

static void did_snap_select (GtkObject *o)
{
    char *s = gtk_file_selection_get_filename (GTK_FILE_SELECTION (snap_selector));

    if (quit_gui)
	return;

    uae_sem_wait (&gui_sem);
    gui_snapname = strdup (s);
    uae_sem_post (&gui_sem);
    write_comm_pipe_int (&from_gui_pipe, 7, 0);
    write_comm_pipe_int (&from_gui_pipe, snapsel_active, 1);
    snapsel_active = -1;
    enable_snap_buttons (1);
    gtk_widget_destroy (snap_selector);
}

static void did_loadstate (void)
{
    if (snapsel_active != -1)
	return;
    snapsel_active = STATE_DORESTORE;
    enable_snap_buttons (0);

    snap_selector = make_file_selector ("Select a state file to restore",
					did_snap_select, did_close_snap);
}

static void did_savestate (void)
{
    if (snapsel_active != -1)
	return;
    snapsel_active = STATE_DOSAVE;
    enable_snap_buttons (0);

    snap_selector = make_file_selector ("Select a filename for the state file",
					did_snap_select, did_close_snap);
}

static GtkWidget *rom_selector;

static void did_close_rom (gpointer gdata)
{
    gtk_widget_set_sensitive (rom_change_widget, 1);
}

static void did_rom_select (GtkObject *o)
{
    char *s = gtk_file_selection_get_filename (GTK_FILE_SELECTION (rom_selector));

    if (quit_gui)
	return;

    gtk_widget_set_sensitive (rom_change_widget, 1);

    uae_sem_wait (&gui_sem);
    gui_romname = strdup (s);
    uae_sem_post (&gui_sem);
    write_comm_pipe_int (&from_gui_pipe, 8, 0);
    gtk_label_set_text (GTK_LABEL (rom_text_widget), gui_romname);
    gtk_widget_destroy (rom_selector);
}

static void did_romchange (GtkWidget *w, gpointer data)
{
    gtk_widget_set_sensitive (rom_change_widget, 0);

    rom_selector = make_file_selector ("Select a ROM file",
				       did_rom_select, did_close_rom);
    filesel_set_path (rom_selector, currprefs.path_rom);
}

static GtkWidget *key_selector;

static void did_close_key (gpointer gdata)
{
    gtk_widget_set_sensitive (key_change_widget, 1);
}

static void did_key_select (GtkObject *o)
{
    char *s = gtk_file_selection_get_filename (GTK_FILE_SELECTION (key_selector));

    if (quit_gui)
	return;

    gtk_widget_set_sensitive (key_change_widget, 1);

    uae_sem_wait (&gui_sem);
    gui_keyname = strdup (s);
    uae_sem_post (&gui_sem);
    write_comm_pipe_int (&from_gui_pipe, 9, 0);
    gtk_label_set_text (GTK_LABEL (key_text_widget), gui_keyname);
    gtk_widget_destroy (key_selector);
}

static void did_keychange (GtkWidget *w, gpointer data)
{
    gtk_widget_set_sensitive (key_change_widget, 0);

    key_selector = make_file_selector ("Select a Kickstart key file",
				       did_key_select, did_close_key);
    filesel_set_path (key_selector, currprefs.path_rom);
}

static void add_empty_vbox (GtkWidget *tobox)
{
    GtkWidget *thing = gtk_vbox_new (FALSE, 0);
    gtk_widget_show (thing);
    gtk_box_pack_start (GTK_BOX (tobox), thing, TRUE, TRUE, 0);
}

static void add_empty_hbox (GtkWidget *tobox)
{
    GtkWidget *thing = gtk_hbox_new (FALSE, 0);
    gtk_widget_show (thing);
    gtk_box_pack_start (GTK_BOX (tobox), thing, TRUE, TRUE, 0);
}

static void add_centered_to_vbox (GtkWidget *vbox, GtkWidget *w)
{
    GtkWidget *hbox = gtk_hbox_new (TRUE, 0);
    gtk_widget_show (hbox);
    gtk_box_pack_start (GTK_BOX (hbox), w, TRUE, FALSE, 0);
    gtk_box_pack_start (GTK_BOX (vbox), hbox, FALSE, TRUE, 0);
}

static GtkWidget *make_labelled_widget (const char *str, GtkWidget *thing)
{
    GtkWidget *label = gtk_label_new (str);
    GtkWidget *hbox2 = gtk_hbox_new (FALSE, 4);

    gtk_widget_show (label);
    gtk_widget_show (thing);

    gtk_box_pack_start (GTK_BOX (hbox2), label, FALSE, TRUE, 0);
    gtk_box_pack_start (GTK_BOX (hbox2), thing, FALSE, TRUE, 0);

    return hbox2;
}

static GtkWidget *add_labelled_widget_centered (const char *str, GtkWidget *thing, GtkWidget *vbox)
{
    GtkWidget *w = make_labelled_widget (str, thing);
    gtk_widget_show (w);
    add_centered_to_vbox (vbox, w);
    return w;
}

static int make_radio_group (const char **labels, GtkWidget *tobox,
			      GtkWidget **saveptr, gint t1, gint t2,
			      void (*sigfunc) (void), int count, GSList *group)
{
    int t = 0;

    while (*labels && (count == -1 || count-- > 0)) {
	GtkWidget *thing = gtk_radio_button_new_with_label (group, *labels++);
	group = gtk_radio_button_group (GTK_RADIO_BUTTON (thing));

	*saveptr++ = thing;
	gtk_widget_show (thing);
	gtk_box_pack_start (GTK_BOX (tobox), thing, t1, t2, 0);
	gtk_signal_connect (GTK_OBJECT (thing), "clicked", (GtkSignalFunc) sigfunc, NULL);
	t++;
    }
    return t;
}

static GtkWidget *make_radio_group_box (const char *title, const char **labels,
					GtkWidget **saveptr, int horiz,
					void (*sigfunc) (void))
{
    GtkWidget *frame, *newbox;

    frame = gtk_frame_new (title);
    newbox = (horiz ? gtk_hbox_new : gtk_vbox_new) (FALSE, 4);
    gtk_widget_show (newbox);
    gtk_container_set_border_width (GTK_CONTAINER (newbox), 4);
    gtk_container_add (GTK_CONTAINER (frame), newbox);
    make_radio_group (labels, newbox, saveptr, horiz, !horiz, sigfunc, -1, NULL);
    return frame;
}

static GtkWidget *make_radio_group_box_1 (const char *title, const char **labels,
					  GtkWidget **saveptr, int horiz,
					  void (*sigfunc) (void), int elts_per_column)
{
    GtkWidget *frame, *newbox;
    GtkWidget *column;
    GSList *group = 0;

    frame = gtk_frame_new (title);
    column = (horiz ? gtk_vbox_new : gtk_hbox_new) (FALSE, 4);
    gtk_container_add (GTK_CONTAINER (frame), column);
    gtk_widget_show (column);

    while (*labels) {
	int count;
	newbox = (horiz ? gtk_hbox_new : gtk_vbox_new) (FALSE, 4);
	gtk_widget_show (newbox);
	gtk_container_set_border_width (GTK_CONTAINER (newbox), 4);
	gtk_container_add (GTK_CONTAINER (column), newbox);
	count = make_radio_group (labels, newbox, saveptr, horiz, !horiz, sigfunc, elts_per_column, group);
	labels += count;
	saveptr += count;
	group = gtk_radio_button_group (GTK_RADIO_BUTTON (saveptr[-1]));
    }
    return frame;
}

static GtkWidget *make_led (int nr)
{
    GtkWidget *subframe, *the_led, *thing;
    GdkColormap *colormap;

    the_led = gtk_vbox_new (FALSE, 0);
    gtk_widget_show (the_led);

    thing = gtk_preview_new (GTK_PREVIEW_COLOR);
    gtk_box_pack_start (GTK_BOX (the_led), thing, TRUE, TRUE, 0);
    gtk_widget_show (thing);

    subframe = gtk_frame_new (NULL);
    gtk_box_pack_start (GTK_BOX (the_led), subframe, TRUE, TRUE, 0);
    gtk_widget_show (subframe);

    thing = gtk_drawing_area_new ();
    gtk_drawing_area_size (GTK_DRAWING_AREA (thing), 20, 5);
    gtk_widget_set_events (thing, GDK_EXPOSURE_MASK);
    gtk_container_add (GTK_CONTAINER (subframe), thing);
    colormap = gtk_widget_get_colormap (thing);
    led_on[nr].red = nr == 0 ? 0xEEEE : 0xCCCC;
    led_on[nr].green = nr == 0 ? 0: 0xFFFF;
    led_on[nr].blue = 0;
    led_on[nr].pixel = 0;
    led_off[nr].red = 0;
    led_off[nr].green = 0;
    led_off[nr].blue = 0;
    led_off[nr].pixel = 0;
    gdk_color_alloc (colormap, led_on + nr);
    gdk_color_alloc (colormap, led_off + nr);
    led_widgets[nr] = thing;
    gtk_signal_connect (GTK_OBJECT (thing), "event",
			(GtkSignalFunc) driveled_event, (gpointer) thing);
    gtk_widget_show (thing);

    thing = gtk_preview_new (GTK_PREVIEW_COLOR);
    gtk_box_pack_start (GTK_BOX (the_led), thing, TRUE, TRUE, 0);
    gtk_widget_show (thing);

    return the_led;
}

static GtkWidget *make_file_container (const char *title, GtkWidget *vbox)
{
    GtkWidget *thing = gtk_frame_new (title);
    GtkWidget *buttonbox = gtk_hbox_new (FALSE, 4);

    gtk_container_set_border_width (GTK_CONTAINER (buttonbox), 4);
    gtk_container_add (GTK_CONTAINER (thing), buttonbox);
    gtk_box_pack_start (GTK_BOX (vbox), thing, FALSE, TRUE, 0);
    gtk_widget_show (buttonbox);
    gtk_widget_show (thing);

    return buttonbox;
}

static GtkWidget *make_file_widget (GtkWidget *buttonbox)
{
    GtkWidget *thing, *subthing;
    GtkWidget *subframe = gtk_frame_new (NULL);

    gtk_frame_set_shadow_type (GTK_FRAME (subframe), GTK_SHADOW_ETCHED_OUT);
    gtk_box_pack_start (GTK_BOX (buttonbox), subframe, TRUE, TRUE, 0);
    gtk_widget_show (subframe);
    subthing = gtk_vbox_new (FALSE, 0);
    gtk_widget_show (subthing);
    gtk_container_add (GTK_CONTAINER (subframe), subthing);
    thing = gtk_label_new ("");
    gtk_widget_show (thing);
    gtk_box_pack_start (GTK_BOX (subthing), thing, TRUE, TRUE, 0);

    return thing;
}

static void make_floppy_disks (GtkWidget *vbox)
{
    GtkWidget *thing, *subthing, *subframe, *buttonbox;
    char buf[5];
    int i;

    add_empty_vbox (vbox);

    for (i = 0; i < 4; i++) {
	/* Frame with an hbox and the "DFx:" title */
	sprintf (buf, "DF%d:", i);
	buttonbox = make_file_container (buf, vbox);

	/* LED */
	subthing = make_led (i + 1);
	gtk_box_pack_start (GTK_BOX (buttonbox), subthing, FALSE, TRUE, 0);

	/* Current file display */
	disk_text_widget[i] = make_file_widget (buttonbox);

	/* Now, the buttons.  */
	thing = gtk_button_new_with_label ("Eject");
	gtk_box_pack_start (GTK_BOX (buttonbox), thing, FALSE, TRUE, 0);
	gtk_widget_show (thing);
	disk_eject_widget[i] = thing;
	gtk_signal_connect (GTK_OBJECT (thing), "clicked", (GtkSignalFunc) did_eject, (gpointer) i);

	thing = gtk_button_new_with_label ("Insert");
	gtk_box_pack_start (GTK_BOX (buttonbox), thing, FALSE, TRUE, 0);
	gtk_widget_show (thing);
	disk_insert_widget[i] = thing;
	gtk_signal_connect (GTK_OBJECT (thing), "clicked", (GtkSignalFunc) did_insert, (gpointer) i);
    }

    add_empty_vbox (vbox);
}

static GtkWidget *make_cpu_speed_sel (void)
{
    int t;
    static const char *labels[] = {
	"Optimize for host CPU speed","Approximate 68000/7MHz speed", "Adjustable",
	NULL
    };
    GtkWidget *frame, *newbox;

    frame = gtk_frame_new ("CPU speed");
    newbox = gtk_vbox_new (FALSE, 4);
    gtk_widget_show (newbox);
    gtk_container_set_border_width (GTK_CONTAINER (newbox), 4);
    gtk_container_add (GTK_CONTAINER (frame), newbox);
    make_radio_group (labels, newbox, cpuspeed_widgets, 0, 1, cpuspeed_changed, -1, NULL);

    t = currprefs.m68k_speed > 0 ? currprefs.m68k_speed : 4 * CYCLE_UNIT;
    cpuspeed_adj = GTK_ADJUSTMENT (gtk_adjustment_new (t, 1.0, 5120.0, 1.0, 1.0, 1.0));
    gtk_signal_connect (GTK_OBJECT (cpuspeed_adj), "value_changed",
			GTK_SIGNAL_FUNC (cpuspeed_changed), NULL);

    cpuspeed_scale = gtk_hscale_new (cpuspeed_adj);
    gtk_range_set_update_policy (GTK_RANGE (cpuspeed_scale), GTK_UPDATE_DELAYED);
    gtk_scale_set_digits (GTK_SCALE (cpuspeed_scale), 0);
    gtk_scale_set_value_pos (GTK_SCALE (cpuspeed_scale), GTK_POS_RIGHT);
    cpuspeed_scale = add_labelled_widget_centered ("Cycles per instruction:", cpuspeed_scale, newbox);

    return frame;
}

static void make_cpu_widgets (GtkWidget *vbox)
{
    int i;
    GtkWidget *newbox, *hbox, *frame;
    GtkWidget *thing;
    static const char *radiolabels[] = {
	"68000", "68010", "68020", "68020+68881", "68040",
	NULL
    };

    add_empty_vbox (vbox);

    hbox = gtk_hbox_new (FALSE, 0);
    add_empty_vbox (hbox);

    newbox = make_radio_group_box ("CPU type", radiolabels, cpu_widget, 0, cputype_changed);
    gtk_widget_show (newbox);
    gtk_box_pack_start (GTK_BOX (hbox), newbox, FALSE, FALSE, 0);

    newbox = make_cpu_speed_sel ();
    gtk_widget_show (newbox);
    gtk_box_pack_start (GTK_BOX (hbox), newbox, FALSE, FALSE, 0);

    add_empty_vbox (hbox);
    gtk_widget_show (hbox);
    gtk_box_pack_start (GTK_BOX (vbox), hbox, FALSE, FALSE, 0);

    frame = gtk_frame_new ("CPU flags");
    add_centered_to_vbox (vbox, frame);
    gtk_widget_show (frame);
    newbox = gtk_vbox_new (FALSE, 4);
    gtk_widget_show (newbox);
    gtk_container_set_border_width (GTK_CONTAINER (newbox), 4);
    gtk_container_add (GTK_CONTAINER (frame), newbox);

    a24m_widget = gtk_check_button_new_with_label ("24 bit address space");
    add_centered_to_vbox (newbox, a24m_widget);
    gtk_widget_show (a24m_widget);
    ccpu_widget = gtk_check_button_new_with_label ("Slow but compatible");
    add_centered_to_vbox (newbox, ccpu_widget);
    gtk_widget_show (ccpu_widget);

    add_empty_vbox (vbox);

    gtk_signal_connect (GTK_OBJECT (ccpu_widget), "clicked",
			(GtkSignalFunc) cputype_changed, NULL);
    gtk_signal_connect (GTK_OBJECT (a24m_widget), "clicked",
			(GtkSignalFunc) cputype_changed, NULL);
}

static void make_gfx_widgets (GtkWidget *vbox)
{
    GtkWidget *thing, *frame, *newbox, *hbox;
    static const char *p96labels[] = {
	"None", "1 MB", "2 MB", "4 MB", "8 MB", "16 MB", "32 MB", NULL
    };

    add_empty_vbox (vbox);

    hbox = gtk_hbox_new (FALSE, 10);
    gtk_widget_show (hbox);
    add_centered_to_vbox (vbox, hbox);

    frame = make_radio_group_box_1 ("P96 RAM", p96labels, p96size_widget, 0, p96size_changed, 4);
    gtk_widget_show (frame);
    gtk_box_pack_start (GTK_BOX (hbox), frame, FALSE, TRUE, 0);

    frame = gtk_frame_new ("Miscellaneous");
    gtk_box_pack_start (GTK_BOX (hbox), frame, FALSE, FALSE, 0);

    gtk_widget_show (frame);
    newbox = gtk_vbox_new (FALSE, 4);
    gtk_widget_show (newbox);
    gtk_container_set_border_width (GTK_CONTAINER (newbox), 4);
    gtk_container_add (GTK_CONTAINER (frame), newbox);

    framerate_adj = GTK_ADJUSTMENT (gtk_adjustment_new (currprefs.gfx_framerate, 1.0, 21.0, 1.0, 1.0, 1.0));
    gtk_signal_connect (GTK_OBJECT (framerate_adj), "value_changed",
			GTK_SIGNAL_FUNC (custom_changed), NULL);

    thing = gtk_hscale_new (framerate_adj);
    gtk_range_set_update_policy (GTK_RANGE (thing), GTK_UPDATE_DELAYED);
    gtk_scale_set_digits (GTK_SCALE (thing), 0);
    gtk_scale_set_value_pos (GTK_SCALE (thing), GTK_POS_RIGHT);
    add_labelled_widget_centered ("Framerate:", thing, newbox);

    b32_widget = gtk_check_button_new_with_label ("32 bit blitter");
    add_centered_to_vbox (newbox, b32_widget);
#if 0
    gtk_widget_show (b32_widget);
#endif
    bimm_widget = gtk_check_button_new_with_label ("Immediate blits");
    add_centered_to_vbox (newbox, bimm_widget);
    gtk_widget_show (bimm_widget);

    afscr_widget = gtk_check_button_new_with_label ("Amiga modes fullscreen");
    add_centered_to_vbox (newbox, afscr_widget);
#if 0
    gtk_widget_show (afscr_widget);
#endif
    pfscr_widget = gtk_check_button_new_with_label ("Picasso modes fullscreen");
    add_centered_to_vbox (newbox, pfscr_widget);
#if 0
    gtk_widget_show (pfscr_widget);
#endif
    add_empty_vbox (vbox);

    gtk_signal_connect (GTK_OBJECT (bimm_widget), "clicked",
			(GtkSignalFunc) custom_changed, NULL);
#if 0
    gtk_signal_connect (GTK_OBJECT (b32_widget), "clicked",
			(GtkSignalFunc) custom_changed, NULL);
    gtk_signal_connect (GTK_OBJECT (afscr_widget), "clicked",
			(GtkSignalFunc) custom_changed, NULL);
    gtk_signal_connect (GTK_OBJECT (pfscr_widget), "clicked",
			(GtkSignalFunc) custom_changed, NULL);
#endif
}

static void make_chipset_widgets (GtkWidget *vbox)
{
    GtkWidget *frame, *newbox, *hbox;
    static const char *colllabels[] = {
	"None (fastest)", "Sprites only", "Sprites & playfields", "Full (very slow)",
	NULL
    };
    static const char *cslevellabels[] = {
	"OCS", "ECS Agnus", "Full ECS", "AGA", NULL
    };

    add_empty_vbox (vbox);

    hbox = gtk_hbox_new (FALSE, 10);
    gtk_widget_show (hbox);
    add_centered_to_vbox (vbox, hbox);

    newbox = make_radio_group_box ("Sprite collisions", colllabels, coll_widget, 0, coll_changed);
    gtk_widget_show (newbox);
    gtk_box_pack_start (GTK_BOX (hbox), newbox, FALSE, TRUE, 0);

    newbox = make_radio_group_box ("Chipset", cslevellabels, cslevel_widget, 0, cslevel_changed);
    gtk_widget_show (newbox);
    gtk_box_pack_start (GTK_BOX (hbox), newbox, FALSE, TRUE, 0);

    fcop_widget = gtk_check_button_new_with_label ("Enable copper speedup code");
    add_centered_to_vbox (vbox, fcop_widget);
    gtk_widget_show (fcop_widget);

    gtk_signal_connect (GTK_OBJECT (fcop_widget), "clicked",
			(GtkSignalFunc) custom_changed, NULL);

    add_empty_vbox (vbox);
}

static void make_sound_widgets (GtkWidget *vbox)
{
    GtkWidget *frame, *newbox;
    int i;
    GtkWidget *hbox;
    static const char *soundlabels1[] = {
	"None", "No output", "Normal", "Accurate",
	NULL
    }, *soundlabels2[] = {
	"8 bit", "16 bit",
	NULL
    }, *soundlabels3[] = {
	"Mono", "Stereo", "Mixed",
	NULL
    };

    add_empty_vbox (vbox);

    newbox = make_radio_group_box ("Mode", soundlabels1, sound_widget, 1, sound_changed);
    gtk_widget_show (newbox);
    add_centered_to_vbox (vbox, newbox);

    hbox = gtk_hbox_new (FALSE, 10);
    gtk_widget_show (hbox);
    add_centered_to_vbox (vbox, hbox);
    newbox = make_radio_group_box ("Channels", soundlabels3, sound_ch_widget, 1, sound_changed);
    gtk_widget_show (newbox);
    gtk_box_pack_start (GTK_BOX (hbox), newbox, FALSE, TRUE, 0);
    newbox = make_radio_group_box ("Resolution", soundlabels2, sound_bits_widget, 1, sound_changed);
    gtk_widget_show (newbox);
    gtk_box_pack_start (GTK_BOX (hbox), newbox, FALSE, TRUE, 0);

    add_empty_vbox (vbox);
}

static void make_mem_widgets (GtkWidget *vbox)
{
    GtkWidget *hbox = gtk_hbox_new (FALSE, 10);
    GtkWidget *label, *frame;

    static const char *chiplabels[] = {
	"512 KB", "1 MB", "2 MB", "4 MB", "8 MB", NULL
    };
    static const char *bogolabels[] = {
	"None", "512 KB", "1 MB", "1.8 MB", NULL
    };
    static const char *fastlabels[] = {
	"None", "1 MB", "2 MB", "4 MB", "8 MB", NULL
    };
    static const char *z3labels[] = {
	"None", "1 MB", "2 MB", "4 MB", "8 MB",
	"16 MB", "32 MB", "64 MB", "128 MB", "256 MB",
	NULL
    };

    add_empty_vbox (vbox);

    {
	GtkWidget *buttonbox = make_file_container ("Kickstart ROM file:", vbox);
	GtkWidget *thing = gtk_button_new_with_label ("Change");

	/* Current file display */
	rom_text_widget = make_file_widget (buttonbox);

	gtk_box_pack_start (GTK_BOX (buttonbox), thing, FALSE, TRUE, 0);
	gtk_widget_show (thing);
	rom_change_widget = thing;
	gtk_signal_connect (GTK_OBJECT (thing), "clicked", (GtkSignalFunc) did_romchange, 0);
    }

    {
	GtkWidget *buttonbox = make_file_container ("ROM key file for Cloanto Amiga Forever:", vbox);
	GtkWidget *thing = gtk_button_new_with_label ("Change");

	/* Current file display */
	key_text_widget = make_file_widget (buttonbox);

	gtk_box_pack_start (GTK_BOX (buttonbox), thing, FALSE, TRUE, 0);
	gtk_widget_show (thing);
	key_change_widget = thing;
	gtk_signal_connect (GTK_OBJECT (thing), "clicked", (GtkSignalFunc) did_keychange, 0);
    }

    gtk_widget_show (hbox);
    add_centered_to_vbox (vbox, hbox);

    add_empty_vbox (vbox);

    label = gtk_label_new ("These settings take effect after the next reset.");
    gtk_widget_show (label);
    add_centered_to_vbox (vbox, label);

    frame = make_radio_group_box ("Chip Mem", chiplabels, chipsize_widget, 0, chipsize_changed);
    gtk_widget_show (frame);
    gtk_box_pack_start (GTK_BOX (hbox), frame, FALSE, TRUE, 0);

    frame = make_radio_group_box ("Slow Mem", bogolabels, bogosize_widget, 0, bogosize_changed);
    gtk_widget_show (frame);
    gtk_box_pack_start (GTK_BOX (hbox), frame, FALSE, TRUE, 0);

    frame = make_radio_group_box ("Fast Mem", fastlabels, fastsize_widget, 0, fastsize_changed);
    gtk_widget_show (frame);
    gtk_box_pack_start (GTK_BOX (hbox), frame, FALSE, TRUE, 0);

    frame = make_radio_group_box_1 ("Z3 Mem", z3labels, z3size_widget, 0, z3size_changed, 5);
    gtk_widget_show (frame);
    gtk_box_pack_start (GTK_BOX (hbox), frame, FALSE, TRUE, 0);
}

static void make_comp_widgets (GtkWidget *vbox)
{
    GtkWidget *frame, *newbox;
    int i;
    GtkWidget *hbox;
    static const char *complabels1[] = {
	"Direct", "Indirect", "Indirect for KS", "Direct after Picasso",
	NULL
    },*complabels2[] = {
	"Direct", "Indirect", "Indirect for KS", "Direct after Picasso",
	NULL
    },*complabels3[] = {
	"Direct", "Indirect", "Indirect for KS", "Direct after Picasso",
	NULL
    },*complabels3a[] = {
	"Direct", "Indirect", "Indirect for KS", "Direct after Picasso",
	NULL
    }, *complabels4[] = {
      "Always generate", "Only generate when needed",
	NULL
    }, *complabels5[] = {
      "Disable", "Enable",
	NULL
    }, *complabels6[] = {
      "Disable", "Enable",
	NULL
    }, *complabels7[] = {
      "Disable", "Enable",
	NULL
    }, *complabels8[] = {
      "Soft", "Hard",
	NULL
    }, *complabels9[] = {
      "Disable", "Enable",
	NULL
    };
    GtkWidget *thing;

    add_empty_vbox (vbox);

    newbox = make_radio_group_box ("Byte access", complabels1, compbyte_widget, 1, comp_changed);
    gtk_widget_show (newbox);
    add_centered_to_vbox (vbox, newbox);
    newbox = make_radio_group_box ("Word access", complabels2, compword_widget, 1, comp_changed);
    gtk_widget_show (newbox);
    add_centered_to_vbox (vbox, newbox);
    newbox = make_radio_group_box ("Long access", complabels3, complong_widget, 1, comp_changed);
    gtk_widget_show (newbox);
    add_centered_to_vbox (vbox, newbox);
    newbox = make_radio_group_box ("Address lookup", complabels3a, compaddr_widget, 1, comp_changed);
    gtk_widget_show (newbox);
    add_centered_to_vbox (vbox, newbox);

    newbox = make_radio_group_box ("Flags", complabels4, compnf_widget, 1, comp_changed);
    gtk_widget_show (newbox);
    add_centered_to_vbox (vbox, newbox);

    newbox = make_radio_group_box ("Icache flushes", complabels8, comp_hardflush_widget, 1, comp_changed);
    gtk_widget_show (newbox);
    add_centered_to_vbox (vbox, newbox);

    newbox = make_radio_group_box ("Compile through uncond branch", complabels9, comp_constjump_widget, 1, comp_changed);
    gtk_widget_show (newbox);
    add_centered_to_vbox (vbox, newbox);

    newbox = make_radio_group_box ("JIT FPU compiler", complabels7, compfpu_widget, 1, comp_changed);
    gtk_widget_show (newbox);
    add_centered_to_vbox (vbox, newbox);

#if USE_OPTIMIZER
    newbox = make_radio_group_box ("Mid Level Optimizer", complabels5, comp_midopt_widget, 1, comp_changed);
    gtk_widget_show (newbox);
    add_centered_to_vbox (vbox, newbox);
#endif

#if USE_LOW_OPTIMIZER
    newbox = make_radio_group_box ("Low Level Optimizer", complabels6, comp_lowopt_widget, 1, comp_changed);
    gtk_widget_show (newbox);
    add_centered_to_vbox (vbox, newbox);
#endif

    cachesize_adj = GTK_ADJUSTMENT (gtk_adjustment_new (currprefs.cachesize, 0.0, 16384.0, 1.0, 1.0, 1.0));
    gtk_signal_connect (GTK_OBJECT (cachesize_adj), "value_changed",
			GTK_SIGNAL_FUNC (comp_changed), NULL);

    thing = gtk_hscale_new (cachesize_adj);
    gtk_range_set_update_policy (GTK_RANGE (thing), GTK_UPDATE_DELAYED);
    gtk_scale_set_digits (GTK_SCALE (thing), 0);
    gtk_scale_set_value_pos (GTK_SCALE (thing), GTK_POS_RIGHT);
    add_labelled_widget_centered ("Translation buffer(kB):", thing, vbox);

    add_empty_vbox (vbox);
}


static void make_joy_widgets (GtkWidget *dvbox)
{
    int i;
    GtkWidget *hbox = gtk_hbox_new (FALSE, 10);
    static const char *joylabels[] = {
	"Joystick 0", "Joystick 1", "Mouse", "Numeric pad",
	"Cursor keys/Right Ctrl", "T/F/H/B/Left Alt",
	NULL
    };

    add_empty_vbox (dvbox);
    gtk_widget_show (hbox);
    add_centered_to_vbox (dvbox, hbox);

    for (i = 0; i < 2; i++) {
	GtkWidget *vbox, *frame;
	GtkWidget *thing;
	char buffer[20];
	int j;

	sprintf (buffer, "Port %d", i);
	frame = make_radio_group_box (buffer, joylabels, joy_widget[i], 0, joy_changed);
	gtk_widget_show (frame);
	gtk_box_pack_start (GTK_BOX (hbox), frame, FALSE, TRUE, 0);
    }

    add_empty_vbox (dvbox);
}

static int hd_change_mode;

static void newdir_ok (void)
{
    int n;
    strcpy (dirdlg_volname, gtk_entry_get_text (GTK_ENTRY (volname_entry)));
    strcpy (dirdlg_path, gtk_entry_get_text (GTK_ENTRY (path_entry)));

    n = strlen (dirdlg_volname);
    /* Strip colons from the end.  */
    if (n > 0) {
	if (dirdlg_volname[n - 1] == ':')
	    dirdlg_volname[n - 1] = '\0';
    }
    if (strlen (dirdlg_volname) == 0 || strlen (dirdlg_path) == 0) {
	/* Uh, no messageboxes in gtk?  */
    } else if (hd_change_mode) {
	set_filesys_unit (currprefs.mountinfo, selected_hd_row, dirdlg_volname, dirdlg_path,
			  0, 0, 0, 0, 0);
	set_hd_state ();
    } else {
	add_filesys_unit (currprefs.mountinfo, dirdlg_volname, dirdlg_path,
			  0, 0, 0, 0, 0);
	set_hd_state ();
    }
    gtk_widget_destroy (dirdlg);
}

static GtkWidget *create_dirdlg (const char *title)
{
    GtkWidget *vbox, *hbox, *thing, *label1, *button;

    dirdlg = gtk_dialog_new ();

    gtk_window_set_title (GTK_WINDOW (dirdlg), title);
    gtk_window_set_position (GTK_WINDOW (dirdlg), GTK_WIN_POS_MOUSE);
    gtk_window_set_modal (GTK_WINDOW (dirdlg), TRUE);
    gtk_widget_show (dirdlg);

    vbox = GTK_DIALOG (dirdlg)->vbox;
    hbox = gtk_hbox_new (FALSE, 10);
    gtk_widget_show (hbox);
    gtk_box_pack_start (GTK_BOX (vbox), hbox, TRUE, TRUE, 10);
    label1 = gtk_label_new ("Path:");
    gtk_box_pack_start (GTK_BOX (hbox), label1, FALSE, TRUE, 10);
    gtk_widget_show (label1);
    thing = gtk_entry_new_with_max_length (255);
    gtk_box_pack_start (GTK_BOX (hbox), thing, TRUE, TRUE, 10);
    gtk_widget_show (thing);
    path_entry = thing;

    hbox = gtk_hbox_new (FALSE, 10);
    gtk_widget_show (hbox);
    gtk_box_pack_start (GTK_BOX (vbox), hbox, TRUE, TRUE, 10);
    thing = gtk_label_new ("Volume name:");
    gtk_box_pack_start (GTK_BOX (hbox), thing, FALSE, TRUE, 10);
    gtk_widget_show (thing);
    thing = gtk_entry_new_with_max_length (255);
    gtk_box_pack_start (GTK_BOX (hbox), thing, TRUE, TRUE, 10);
    gtk_widget_show (thing);
    gtk_widget_set_usize (thing, 200, -1);
    volname_entry = thing;

    hbox = GTK_DIALOG (dirdlg)->action_area;
    button = gtk_button_new_with_label ("OK");
    gtk_signal_connect (GTK_OBJECT (button), "clicked",
			GTK_SIGNAL_FUNC(newdir_ok), NULL);
    GTK_WIDGET_SET_FLAGS (button, GTK_CAN_DEFAULT);
    gtk_box_pack_start (GTK_BOX (hbox), button, TRUE, TRUE, 0);
    gtk_widget_grab_default (button);
    gtk_widget_show (button);

    button = gtk_button_new_with_label ("Cancel");
    gtk_signal_connect_object (GTK_OBJECT (button), "clicked",
			       GTK_SIGNAL_FUNC (gtk_widget_destroy),
			       GTK_OBJECT (dirdlg));
    gtk_box_pack_start (GTK_BOX (hbox), button, TRUE, TRUE, 0);
    gtk_widget_show (button);
}

static void did_newdir (void)
{
    hd_change_mode = 0;
    create_dirdlg ("Add a new mounted directory");
}
static void did_newhdf (void)
{
    hd_change_mode = 0;
}

static void did_hdchange (void)
{
    int secspertrack, surfaces, reserved, blocksize, size;
    int cylinders, readonly;
    char *volname, *rootdir;
    char *failure;

    failure = get_filesys_unit (currprefs.mountinfo, selected_hd_row,
				&volname, &rootdir, &readonly,
				&secspertrack, &surfaces, &reserved,
				&cylinders, &size, &blocksize);

    hd_change_mode = 1;
    if (is_hardfile (currprefs.mountinfo, selected_hd_row)) {
    } else {
	create_dirdlg ("Change a mounted directory");
	gtk_entry_set_text (GTK_ENTRY (volname_entry), volname);
	gtk_entry_set_text (GTK_ENTRY (path_entry), rootdir);
   }
}
static void did_hddel (void)
{
    kill_filesys_unit (currprefs.mountinfo, selected_hd_row);
    set_hd_state ();
}

static void hdselect (GtkWidget *widget, gint row, gint column, GdkEventButton *bevent,
		      gpointer user_data)
{
    selected_hd_row = row;
    gtk_widget_set_sensitive (hdchange_button, TRUE);
    gtk_widget_set_sensitive (hddel_button, TRUE);
}

static void hdunselect (GtkWidget *widget, gint row, gint column, GdkEventButton *bevent,
			gpointer user_data)
{
    gtk_widget_set_sensitive (hdchange_button, FALSE);
    gtk_widget_set_sensitive (hddel_button, FALSE);
}


static GtkWidget *make_buttons (const char *label, GtkWidget *box, void (*sigfunc) (void), GtkWidget *(*create)(const char *label))
{
    GtkWidget *thing = create (label);
    gtk_widget_show (thing);
    gtk_signal_connect (GTK_OBJECT (thing), "clicked", (GtkSignalFunc) sigfunc, NULL);
    gtk_box_pack_start (GTK_BOX (box), thing, TRUE, TRUE, 0);

    return thing;
}
#define make_button(label, box, sigfunc) make_buttons(label, box, sigfunc, gtk_button_new_with_label)

static void make_hd_widgets (GtkWidget *dvbox)
{
    GtkWidget *thing, *buttonbox, *hbox;
    char *titles [] = {
	"Volume", "File/Directory", "R/O", "Heads", "Cyl.", "Sec.", "Rsrvd", "Size", "Blksize"
    };

    thing = gtk_clist_new_with_titles (9, titles);
    gtk_clist_set_selection_mode (GTK_CLIST (thing), GTK_SELECTION_SINGLE);
    gtk_signal_connect (GTK_OBJECT (thing), "select_row", (GtkSignalFunc) hdselect, NULL);
    gtk_signal_connect (GTK_OBJECT (thing), "unselect_row", (GtkSignalFunc) hdunselect, NULL);
    hdlist_widget = thing;
    gtk_widget_set_usize (thing, -1, 200);

    gtk_widget_show (thing);
    add_centered_to_vbox (dvbox, thing);

    hbox = gtk_hbox_new (FALSE, 10);
    gtk_widget_show (hbox);
    gtk_box_pack_start (GTK_BOX (dvbox), hbox, FALSE, TRUE, 0);

    /* The buttons */
    buttonbox = gtk_hbox_new (TRUE, 4);
    gtk_widget_show (buttonbox);
    gtk_box_pack_start (GTK_BOX (hbox), buttonbox, TRUE, TRUE, 0);
    make_button ("New filesystem...", buttonbox, did_newdir);
#if 0 /* later... */
    make_button ("New hardfile...", buttonbox, did_newhdf);
#endif
    hdchange_button = make_button ("Change...", buttonbox, did_hdchange);
    hddel_button = make_button ("Delete", buttonbox, did_hddel);

    thing = gtk_label_new ("These settings take effect after the next reset.");
    gtk_widget_show (thing);
    add_centered_to_vbox (dvbox, thing);
}

static void make_about_widgets (GtkWidget *dvbox)
{
    GtkWidget *thing;
    GtkStyle *style;
    GdkFont *font;
    char t[20];

    add_empty_vbox (dvbox);

    sprintf (t, "UAE %d.%d.%d", UAEMAJOR, UAEMINOR, UAESUBREV);
    thing = gtk_label_new (t);
    gtk_widget_show (thing);
    add_centered_to_vbox (dvbox, thing);

    font = gdk_font_load ("-*-helvetica-medium-r-normal--*-240-*-*-*-*-*-*");
    if (font) {
	style = gtk_style_copy (GTK_WIDGET (thing)->style);
	gdk_font_unref (style->font);
	style->font = font;
	gdk_font_ref (style->font);
	gtk_widget_push_style (style);
	gtk_widget_set_style (thing, style);
    }
    thing = gtk_label_new ("Choose your settings, then deselect the Pause button to start!");
    gtk_widget_show (thing);
    add_centered_to_vbox (dvbox, thing);

    add_empty_vbox (dvbox);
}


static void create_guidlg (void)
{
    GtkWidget *window, *notebook;
    GtkWidget *buttonbox, *vbox, *hbox;
    GtkWidget *thing;
    int i;
    int argc = 1;
    char *a[] = {"UAE"};
    char **argv = a;
    static const struct _pages {
	const char *title;
	void (*createfunc)(GtkWidget *);
    } pages[] = {
	/* ??? If this isn't the first page, there are errors in draw_led.  */
	{ "Floppy disks", make_floppy_disks },
	{ "Memory", make_mem_widgets },
	{ "CPU emulation", make_cpu_widgets },
	{ "Graphics", make_gfx_widgets },
	{ "Chipset", make_chipset_widgets },
	{ "Sound", make_sound_widgets },
	{ "JIT", make_comp_widgets },
	{ "Game ports", make_joy_widgets },
	{ "Harddisks", make_hd_widgets },
	{ "About", make_about_widgets }
    };

    gtk_init (&argc, &argv);
    gtk_rc_parse ("uaegtkrc");

    gui_window = gtk_window_new (GTK_WINDOW_TOPLEVEL);
    gtk_window_set_title (GTK_WINDOW (gui_window), "UAE control");

    vbox = gtk_vbox_new (FALSE, 4);
    gtk_container_add (GTK_CONTAINER (gui_window), vbox);
    gtk_container_set_border_width (GTK_CONTAINER (gui_window), 10);

    /* First line - buttons and power LED */
    hbox = gtk_hbox_new (FALSE, 10);
    gtk_widget_show (hbox);
    gtk_box_pack_start (GTK_BOX (vbox), hbox, FALSE, TRUE, 0);

    /* The buttons */
    buttonbox = gtk_hbox_new (TRUE, 4);
    gtk_widget_show (buttonbox);
    gtk_box_pack_start (GTK_BOX (hbox), buttonbox, TRUE, TRUE, 0);
    make_button ("Reset", buttonbox, did_reset);
    make_button ("Debug", buttonbox, did_debug);
    make_button ("Quit", buttonbox, did_quit);
    make_button ("Save config", buttonbox, save_config);
    pause_uae_widget = make_buttons ("Pause", buttonbox, pause_uae, gtk_toggle_button_new_with_label);

    /* The LED */
    thing = make_led (0);
    thing = make_labelled_widget ("Power:", thing);
    gtk_widget_show (thing);
    gtk_box_pack_start (GTK_BOX (hbox), thing, FALSE, TRUE, 0);

    /* More buttons */
    buttonbox = gtk_hbox_new (TRUE, 4);
    gtk_widget_show (buttonbox);
    gtk_box_pack_start (GTK_BOX (vbox), buttonbox, TRUE, TRUE, 0);
    snap_save_widget = make_button ("Save state", buttonbox, did_savestate);
    snap_load_widget = make_button ("Load state", buttonbox, did_loadstate);

    /* Place a separator below those buttons.  */
    thing = gtk_hseparator_new ();
    gtk_box_pack_start (GTK_BOX (vbox), thing, FALSE, TRUE, 0);
    gtk_widget_show (thing);

    /* Now the notebook */
    notebook = gtk_notebook_new ();
    gtk_box_pack_start (GTK_BOX (vbox), notebook, TRUE, TRUE, 0);
    gtk_widget_show (notebook);

    for (i = 0; i < sizeof pages / sizeof (struct _pages); i++) {
	thing = gtk_vbox_new (FALSE, 4);
	gtk_widget_show (thing);
	gtk_container_set_border_width (GTK_CONTAINER (thing), 10);
	pages[i].createfunc (thing);
	gtk_notebook_append_page (GTK_NOTEBOOK (notebook), thing, gtk_label_new (pages[i].title));
    }

    /* Put "about" screen first.  */
    gtk_notebook_set_page (GTK_NOTEBOOK (notebook), i - 1);
    enable_disk_buttons (1);
    enable_snap_buttons (1);

    gtk_widget_show (vbox);

    filesel_active = -1;
    snapsel_active = -1;

    gtk_timeout_add (1000, (GtkFunction)my_idle, 0);
}

static void *gtk_gui_thread (void *dummy)
{
    gtk_main ();

    quitted_gui = 1;
    uae_sem_post (&gui_quit_sem);
    return 0;
}

void gui_changesettings(void)
{

}

void gui_fps (int x)
{
}

void gui_led (int num, int on)
{
    if (no_gui)
	return;

/*    if (num == 0)
	return;
    printf("LED %d %d\n", num, on);
    write_comm_pipe_int (&to_gui_pipe, 1, 0);
    write_comm_pipe_int (&to_gui_pipe, num == 0 ? 4 : num - 1, 0);
    write_comm_pipe_int (&to_gui_pipe, on, 1);
    printf("#LED %d %d\n", num, on);*/
}

void gui_filename (int num, const char *name)
{
    if (no_gui)
	return;

    write_comm_pipe_int (&to_gui_pipe, 0, 0);
    write_comm_pipe_int (&to_gui_pipe, num, 1);

/*    gui_update ();*/
}

void gui_handle_events (void)
{
    int pause_uae = FALSE;

    if (no_gui)
	return;

    do {
	while (pause_uae || comm_pipe_has_data (&from_gui_pipe)) {
	    int cmd = read_comm_pipe_int_blocking (&from_gui_pipe);
	    int n;
	    switch (cmd) {
	    case 0:
		n = read_comm_pipe_int_blocking (&from_gui_pipe);
		changed_prefs.df[n][0] = '\0';
		break;
	    case 1:
		n = read_comm_pipe_int_blocking (&from_gui_pipe);
		uae_sem_wait (&gui_sem);
		strncpy (changed_prefs.df[n], new_disk_string[n], 255);
		free (new_disk_string[n]);
		new_disk_string[n] = 0;
		changed_prefs.df[n][255] = '\0';
		uae_sem_post (&gui_sem);
		break;
	    case 2:
		uae_reset ();
		end_pause_uae ();
		break;
	    case 3:
		activate_debugger ();
		end_pause_uae ();
		break;
	    case 4:
		uae_quit ();
		end_pause_uae ();
		break;
	    case 5:
		pause_uae = TRUE;
		break;
	    case 6:
		pause_uae = FALSE;
		break;
	    case 7:
		printf ("STATESAVE\n");
		savestate_state = read_comm_pipe_int_blocking (&from_gui_pipe);
		uae_sem_wait (&gui_sem);
		savestate_filename = gui_snapname;
		uae_sem_post (&gui_sem);
		break;
	    case 8:
		uae_sem_wait (&gui_sem);
		strncpy (changed_prefs.romfile, gui_romname, 255);
		changed_prefs.romfile[255] = '\0';
		free (gui_romname);
		uae_sem_post (&gui_sem);
		break;
	    case 9:
		uae_sem_wait (&gui_sem);
		strncpy (changed_prefs.keyfile, gui_keyname, 255);
		changed_prefs.keyfile[255] = '\0';
		free (gui_keyname);
		uae_sem_post (&gui_sem);
		break;
	    }
	}
    } while (pause_uae);
}

void gui_update_gfx (void)
{
#if 0 /* This doesn't work... */
    set_gfx_state ();
#endif
}

int gui_init (void)
{
    uae_thread_id tid;

    gui_active = 0;

    init_comm_pipe (&to_gui_pipe, 20, 1);
    init_comm_pipe (&from_gui_pipe, 20, 1);
    uae_sem_init (&gui_sem, 0, 1);
    uae_sem_init (&gui_init_sem, 0, 0);
    uae_sem_init (&gui_quit_sem, 0, 0);

    create_guidlg ();
    uae_start_thread (gtk_gui_thread, NULL, &tid);
    gui_update ();

    if (currprefs.start_gui == 1) {
	gtk_toggle_button_set_active (GTK_TOGGLE_BUTTON (pause_uae_widget), TRUE);
	write_comm_pipe_int (&from_gui_pipe, 5, 1);
	/* Handle events until Pause is unchecked.  */
	gui_handle_events ();
	/* Quit requested?  */
	if (quit_program == -1) {
	    gui_exit ();
	    return -2;
	}
    }

    return 1;
}

int gui_update (void)
{
    if (no_gui)
	return 0;

    write_comm_pipe_int (&to_gui_pipe, 1, 1);
    uae_sem_wait (&gui_init_sem);
    return 0;
}

void gui_exit (void)
{
    if (no_gui)
	return;

    quit_gui = 1;
    uae_sem_wait (&gui_quit_sem);
}

void gui_lock (void)
{
    uae_sem_wait (&gui_sem);
}

void gui_unlock (void)
{
    uae_sem_post (&gui_sem);
}
