/*
 *  uaeexe.h - launch executable in UAE
 *
 *  (c) 1997 by Samuel Devulder
 */

struct uae_xcmd {
    struct uae_xcmd *prev,*next;
    char *cmd;
};

#define UAEEXE_ORG         0xF0FF90 /* sam: I hope this slot is free */

#define UAEEXE_OK          0
#define UAEEXE_NOTRUNNING  1
#define UAEEXE_NOMEM       2

extern void uaeexe_install(void);
extern int  uaeexe(char *cmd);


