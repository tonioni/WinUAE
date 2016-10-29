#ifndef UAE_A2065_H
#define UAE_A2065_H

#ifdef A2065

extern bool a2065_init (struct autoconfig_info *aci);
extern void a2065_free (void);
extern void a2065_reset (void);
extern void a2065_hsync_handler (void);

extern bool ariadne_init(struct autoconfig_info *aci);


extern void rethink_a2065 (void);

#endif

#endif /* UAE_A2065_H */
