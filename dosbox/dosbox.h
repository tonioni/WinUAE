#ifndef DOSBOX_H
#define DOSBOX_H

#ifndef CH_LIST
#define CH_LIST
#include <list>
#endif

#ifndef CH_STRING
#define CH_STRING
#include <string>
#endif

#define C_FPU 1

typedef unsigned char Bit8u;
typedef signed char Bit8s;
typedef unsigned short Bit16u;
typedef signed short Bit16s;
typedef unsigned long Bit32u;
typedef signed long Bit32s;
#if defined(_MSC_VER)
typedef unsigned __int64 Bit64u;
typedef signed __int64 Bit64s;
#else
typedef unsigned long long int Bit64u;
typedef signed long long int Bit64s;
#endif
typedef Bit32u Bitu;
typedef Bit32s Bits;
typedef double Real64;

#define C_UNALIGNED_MEMORY

#define LONGTYPE(a) a##LL

#define INLINE __inline

#define GCC_ATTRIBUTE(x)

#include "logging.h"

extern void E_Exit(char*,...);
extern void DOSBOX_RunMachine(void);

#define GCC_UNLIKELY(x) x
#define GCC_LIKELY(x) x

#define MAPPER_AddHandler(x1,x2,x3,x4,x5)

class CommandLine {
public:
	CommandLine(int argc, char const * const argv[]);
	CommandLine(char const * const name, char const * const cmdline);
	const char * GetFileName() { return file_name.c_str(); }

	bool FindExist(char const * const name, bool remove = false);
	bool FindHex(char const * const name, int & value, bool remove = false);
	bool FindInt(char const * const name, int & value, bool remove = false);
	bool FindString(char const * const name, std::string & value, bool remove = false);
	bool FindCommand(unsigned int which, std::string & value);
	bool FindStringBegin(char const * const begin, std::string & value, bool remove = false);
	bool FindStringRemain(char const * const name, std::string & value);
	bool GetStringRemain(std::string & value);
	unsigned int GetCount(void);
	void Shift(unsigned int amount = 1);
	Bit16u Get_arglength();

private:
	typedef std::list<std::string>::iterator cmd_it;
	std::list<std::string> cmds;
	std::string file_name;
	bool FindEntry(char const * const name, cmd_it & it, bool neednext = false);
};

extern int x86_fpu_enabled;

#endif
