#ifndef UAE_WINUAE_BUILDDATE_H
#define UAE_WINUAE_BUILDDATE_H

#define MAKEBD(x,y,z) ((((x) - 2000) * 10000 + (y)) * 100 + (z))
#define GETBDY(x) ((x) / 1000000 + 2000)
#define GETBDM(x) (((x) - ((x / 10000) * 10000)) / 100)
#define GETBDD(x) ((x) % 100)

#define WINUAEDATE MAKEBD(2026, 7, 6)

#endif
