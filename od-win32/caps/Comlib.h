#undef DllSub
#undef DllVar
#undef ExtSub
#undef ExtVar

#ifdef LIB_USER

#define DllSub DllImport
#define DllVar DllImport

#else

#define DllSub DllExport
#define DllVar extern DllExport

#endif

#define ExtSub
#define ExtVar

