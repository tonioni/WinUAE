#ifndef UAE_CDTVCR_H
#define UAE_CDTVCR_H

bool cdtvcr_init(struct autoconfig_info*);
void cdtvcr_reset(void);
void cdtvcr_free(void);
void rethink_cdtvcr(void);

extern void CDTVCR_hsync_handler(void);

#endif /* UAE_CDTVCR_H */
