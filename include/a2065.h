#ifndef UAE_A2065_H
#define UAE_A2065_H

#ifdef A2065

extern addrbank *a2065_init (int);
extern void a2065_free (void);
extern void a2065_reset (void);
extern void a2065_hsync_handler (void);

extern void rethink_a2065 (void);

#endif

#endif /* UAE_A2065_H */
