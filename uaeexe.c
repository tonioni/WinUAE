/*
 *  uaeexe.c - UAE remote cli
 *
 *  (c) 1997 by Samuel Devulder
 */

#include "sysconfig.h"
#include "sysdeps.h"

#include "config.h"
#include "options.h"
#include "uae.h"
#include "memory.h"
#include "custom.h"
#include "newcpu.h"
#include "autoconf.h"
#include "uaeexe.h"

static struct uae_xcmd *first = NULL;
static struct uae_xcmd *last  = NULL;
static char running = 0;
static uae_u32 uaeexe_server(void);

/*
 * Install the server
 */
void uaeexe_install(void)
{
    uaecptr loop;

    loop = here ();
    org(UAEEXE_ORG);
    calltrap (deftrap (uaeexe_server));
    dw(RTS);
    org(loop);
}

/*
 * Send command to the remote cli.
 *
 * To use this, just call uaeexe("command") and the command will be
 * executed by the remote cli (provided you've started it in the
 * s:user-startup for example). Be sure to add "run" if you want
 * to launch the command asynchronously. Please note also that the
 * remote cli works better if you've got the fifo-handler installed.
 */
int uaeexe(char *cmd)
{
    struct uae_xcmd *nw;

    if (!running)
	goto NORUN;

    nw = (struct uae_xcmd *)malloc (sizeof *nw);
    if (!nw)
	goto NOMEM;
    nw->cmd = (char *)malloc (strlen (cmd) + 1);
    if (!nw->cmd) {
	free (nw);
	goto NOMEM;
    }

    strcpy (nw->cmd, cmd);
    nw->prev = last;
    nw->next = NULL;

    if(!first) first  = nw;
    if(last) {
           last->next = nw;
           last       = nw;
    } else last       = nw;

    return UAEEXE_OK;
  NOMEM:
    return UAEEXE_NOMEM;
  NORUN:
    write_log("Remote cli is not running.\n");
    return UAEEXE_NOTRUNNING;
}

/*
 * returns next command to be executed
 */
static char *get_cmd(void)
{
    struct uae_xcmd *cmd;
    char *s;

    if(!first) return NULL;
    s = first->cmd; 
    cmd = first; first = first->next;
    if(!first) last = NULL;
    free(cmd);
    return s;
}

/*
 * helper function
 */
#define ARG(x) (get_long (m68k_areg (regs, 7) + 4*(x+1)))
static uae_u32 uaeexe_server(void)
{
    int len;
    char *cmd;
    char *dst;

    if(ARG(0) && !running) {
        running = 1;
        write_log("Remote CLI started.\n");
    }

    cmd = get_cmd(); if(!cmd) return 0;
    if(!ARG(0)) {running = 0;return 0;}

    dst = (char *)get_real_address(ARG(0));
    len = ARG(1);
    strncpy(dst,cmd,len);
    printf("Sending '%s' to remote cli\n",cmd); /**/
    free(cmd);
    return ARG(0);
}


