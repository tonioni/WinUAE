 /*
  * UAE - The Un*x Amiga Emulator
  *
  * Serial Line Emulation
  *
  * Copyright 1996, 1997 Stefan Reinauer <stepan@linux.de>
  * Copyright 1997 Christian Schmitt <schmitt@freiburg.linux.de>
  */

extern void serial_init(void);
extern void serial_exit(void);
extern void serial_dtr_off(void);

extern uae_u16 SERDATR(void);
extern int   SERDATS(void);
extern void  SERPER(uae_u16 w);
extern void  SERDAT(uae_u16 w);

extern uae_u8 serial_writestatus(uae_u8, uae_u8);
extern uae_u8 serial_readstatus (uae_u8);
extern void serial_uartbreak (int);
extern uae_u16 serdat;

extern int doreadser, serstat;

extern void serial_flush_buffer(void);

extern void serial_hsynchandler (void);
extern void serial_check_irq (void);