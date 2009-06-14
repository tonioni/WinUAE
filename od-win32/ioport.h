
int ioport_init (void);
void ioport_free (void);
void ioport_write (int,uae_u8);
uae_u8 ioport_read (int);

int paraport_init (void);
int paraport_open (TCHAR*);
void paraport_free (void);
