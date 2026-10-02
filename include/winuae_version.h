#ifndef UAE_WINUAE_VERSION_H
#define UAE_WINUAE_VERSION_H

#define UAEMAJOR 6
#define UAEMINOR 1
#define UAESUBREV 0

#define WINUAEPUBLICBETA 1

#if WINUAEPUBLICBETA
#define WINUAEBETA _T("16")
#else
#define WINUAEBETA _T("")
#endif

//#define WINUAEEXTRA _T("AmiKit Preview")
//#define WINUAEEXTRA _T("Amiga Forever Edition")

#ifndef WINUAEEXTRA
#define WINUAEEXTRA _T("")
#endif
#ifndef WINUAEREV
#define WINUAEREV _T("")
#endif

#define MAKEBD(x,y,z) ((((x) - 2000) * 10000 + (y)) * 100 + (z))
#define GETBDY(x) ((x) / 1000000 + 2000)
#define GETBDM(x) (((x) - ((x / 10000) * 10000)) / 100)
#define GETBDD(x) ((x) % 100)

#define WINUAEDATE MAKEBD(2026, 9, 27)

#endif
