/*************************************************************************
* Handling mistaken direct memory access                                *
*************************************************************************/

#ifdef NATMEM_OFFSET
#ifdef _WIN32 // %%% BRIAN KING WAS HERE %%%
#include <winbase.h>
#else
#ifndef __USE_GNU
#define __USE_GNU
#endif
#include <sys/ucontext.h>
#endif
#include <signal.h>

#define SIG_READ 1
#define SIG_WRITE 2

static int in_handler=0;
static uae_u8 *veccode;

#ifdef _WIN32

typedef LPEXCEPTION_POINTERS CONTEXT_T;
#define HAVE_CONTEXT_T 1

#define CONTEXT_EIP(context) (context->ContextRecord->Eip)
#define CONTEXT_EAX(context) (context->ContextRecord->Eax)
#define CONTEXT_ECX(context) (context->ContextRecord->Ecx)
#define CONTEXT_EDX(context) (context->ContextRecord->Edx)
#define CONTEXT_EBX(context) (context->ContextRecord->Ebx)
#define CONTEXT_ESP(context) (context->ContextRecord->Esp)
#define CONTEXT_EBP(context) (context->ContextRecord->Ebp)
#define CONTEXT_ESI(context) (context->ContextRecord->Esi)
#define CONTEXT_EDI(context) (context->ContextRecord->Edi)

#define CONTEXT_RIP(context) (context->ContextRecord->Rip)
#define CONTEXT_RAX(context) (context->ContextRecord->Rax)
#define CONTEXT_RCX(context) (context->ContextRecord->Rcx)
#define CONTEXT_RDX(context) (context->ContextRecord->Rdx)
#define CONTEXT_RBX(context) (context->ContextRecord->Rbx)
#define CONTEXT_RSP(context) (context->ContextRecord->Rsp)
#define CONTEXT_RBP(context) (context->ContextRecord->Rbp)
#define CONTEXT_RSI(context) (context->ContextRecord->Rsi)
#define CONTEXT_RDI(context) (context->ContextRecord->Rdi)

#define CONTEXT_CR2(context) ((uae_u32)(context->ExceptionRecord->ExceptionInformation[1]))

#elif HAVE_STRUCT_UCONTEXT_UC_MCONTEXT_GREGS

#ifdef CPU_x86_64
#define CONTEXT_RIP(context) (((struct ucontext *) context)->uc_mcontext.gregs[REG_RIP])
#define CONTEXT_RAX(context) (((struct ucontext *) context)->uc_mcontext.gregs[REG_RAX])
#define CONTEXT_RCX(context) (((struct ucontext *) context)->uc_mcontext.gregs[REG_RCX])
#define CONTEXT_RDX(context) (((struct ucontext *) context)->uc_mcontext.gregs[REG_RDX])
#define CONTEXT_RBX(context) (((struct ucontext *) context)->uc_mcontext.gregs[REG_RBX])
#define CONTEXT_RSP(context) (((struct ucontext *) context)->uc_mcontext.gregs[REG_RSP])
#define CONTEXT_RBP(context) (((struct ucontext *) context)->uc_mcontext.gregs[REG_RBP])
#define CONTEXT_RSI(context) (((struct ucontext *) context)->uc_mcontext.gregs[REG_RSI])
#define CONTEXT_RDI(context) (((struct ucontext *) context)->uc_mcontext.gregs[REG_RDI])
#else
typedef void *CONTEXT_T;
#define HAVE_CONTEXT_T 1
#define CONTEXT_EIP(context) (((struct ucontext *) context)->uc_mcontext.gregs[REG_EIP])
#define CONTEXT_EAX(context) (((struct ucontext *) context)->uc_mcontext.gregs[REG_EAX])
#define CONTEXT_ECX(context) (((struct ucontext *) context)->uc_mcontext.gregs[REG_ECX])
#define CONTEXT_EDX(context) (((struct ucontext *) context)->uc_mcontext.gregs[REG_EDX])
#define CONTEXT_EBX(context) (((struct ucontext *) context)->uc_mcontext.gregs[REG_EBX])
#define CONTEXT_ESP(context) (((struct ucontext *) context)->uc_mcontext.gregs[REG_ESP])
#define CONTEXT_EBP(context) (((struct ucontext *) context)->uc_mcontext.gregs[REG_EBP])
#define CONTEXT_ESI(context) (((struct ucontext *) context)->uc_mcontext.gregs[REG_ESI])
#define CONTEXT_EDI(context) (((struct ucontext *) context)->uc_mcontext.gregs[REG_EDI])
#define CONTEXT_CR2(context) (((struct ucontext *) context)->uc_mcontext.cr2)
#endif

#endif

#if defined(CPU_64_BIT)
#ifdef CONTEXT_RIP
#define CONTEXT_PC(context) CONTEXT_RIP(context)
#endif
#else
#ifdef CONTEXT_EIP
#define CONTEXT_PC(context) CONTEXT_EIP(context)
#endif
#endif

static inline uae_u16 swap16(uae_u16 x)
{
	return ((x&0xff00)>>8)|((x&0x00ff)<<8);
}

static inline uae_u32 swap32(uae_u32 x)
{
	return ((x&0xff00)<<8)|((x&0x00ff)<<24)|((x&0xff0000)>>8)|((x&0xff000000)>>24);
}

#ifdef HAVE_CONTEXT_T
/*
 * Try to handle faulted memory access in compiled code
 *
 * Returns 1 if handled, 0 otherwise
 */
static int handle_access(CONTEXT_T context)
{
	uae_u8  *i    = (uae_u8 *) CONTEXT_EIP(context);
	uae_u32  addr =            CONTEXT_CR2(context);

	int r=-1;
	int size=4;
	int dir=-1;
	int len=0;

#ifdef JIT_DEBUG
	write_log (_T("JIT: fault address is 0x%x at 0x%x\n"),addr,i);
#endif
	if (!canbang || !currprefs.cachesize)
		return 0;

	if (in_handler)
		write_log (_T("JIT: Argh --- Am already in a handler. Shouldn't happen!\n"));

	if (canbang && i>=compiled_code && i<=current_compile_p) {
		if (*i==0x66) {
			i++;
			size=2;
			len++;
		}

		switch(i[0]) {
		case 0x8a:
			if ((i[1]&0xc0)==0x80) {
				r=(i[1]>>3)&7;
				dir=SIG_READ;
				size=1;
				len+=6;
				break;
			}
			break;
		case 0x88:
			if ((i[1]&0xc0)==0x80) {
				r=(i[1]>>3)&7;
				dir=SIG_WRITE;
				size=1;
				len+=6;
				break;
			}
			break;
		case 0x8b:
			switch(i[1]&0xc0) {
			case 0x80:
				r=(i[1]>>3)&7;
				dir=SIG_READ;
				len+=6;
				break;
			case 0x40:
				r=(i[1]>>3)&7;
				dir=SIG_READ;
				len+=3;
				break;
			case 0x00:
				r=(i[1]>>3)&7;
				dir=SIG_READ;
				len+=2;
				break;
			default:
				break;
			}
			break;
		case 0x89:
			switch(i[1]&0xc0) {
			case 0x80:
				r=(i[1]>>3)&7;
				dir=SIG_WRITE;
				len+=6;
				break;
			case 0x40:
				r=(i[1]>>3)&7;
				dir=SIG_WRITE;
				len+=3;
				break;
			case 0x00:
				r=(i[1]>>3)&7;
				dir=SIG_WRITE;
				len+=2;
				break;
			}
			break;
		}
	}

	if (r!=-1) {
		void* pr=NULL;
#ifdef JIT_DEBUG
		write_log (_T("JIT: register was %d, direction was %d, size was %d\n"),r,dir,size);
#endif

		switch(r) {
#if defined(CPU_64_BIT)
		case 0: pr = &(CONTEXT_RAX(context)); break;
		case 1: pr = &(CONTEXT_RCX(context)); break;
		case 2: pr = &(CONTEXT_RDX(context)); break;
		case 3: pr = &(CONTEXT_RBX(context)); break;
		case 4: pr = (size > 1) ? NULL
					: (((uae_u8*)&(CONTEXT_RAX(context)))+1);
			break;
		case 5: pr = (size > 1) ? (void*)          (&(CONTEXT_RBP(context)))
					: (void*)(((uae_u8*)&(CONTEXT_RCX(context))) + 1);
			break;
		case 6: pr = (size > 1) ? (void*)          (&(CONTEXT_RSI(context)))
					: (void*)(((uae_u8*)&(CONTEXT_RDX(context))) + 1);
			break;
		case 7: pr = (size > 1) ? (void*)          (&(CONTEXT_RDI(context)))
					: (void*)(((uae_u8*)&(CONTEXT_RBX(context))) + 1);
			break;
#else
		case 0: pr = &(CONTEXT_EAX(context)); break;
		case 1: pr = &(CONTEXT_ECX(context)); break;
		case 2: pr = &(CONTEXT_EDX(context)); break;
		case 3: pr = &(CONTEXT_EBX(context)); break;
		case 4: pr = (size > 1) ? NULL
					: (((uae_u8*)&(CONTEXT_EAX(context)))+1);
			break;
		case 5: pr = (size > 1) ? (void*)          (&(CONTEXT_EBP(context)))
					: (void*)(((uae_u8*)&(CONTEXT_ECX(context))) + 1);
			break;
		case 6: pr = (size > 1) ? (void*)          (&(CONTEXT_ESI(context)))
					: (void*)(((uae_u8*)&(CONTEXT_EDX(context))) + 1);
			break;
		case 7: pr = (size > 1) ? (void*)          (&(CONTEXT_EDI(context)))
					: (void*)(((uae_u8*)&(CONTEXT_EBX(context))) + 1);
			break;
#endif
	    default:
		    abort ();
		}
		if (pr) {
			blockinfo* bi;

			if (currprefs.comp_oldsegv) {
				addr-=(uae_u32)NATMEM_OFFSET;

#ifdef JIT_DEBUG
				if ((addr>=0x10000000 && addr<0x40000000) ||
					(addr>=0x50000000)) {
						write_log (_T("JIT: Suspicious address 0x%x in SEGV handler.\n"),addr);
				}
#endif
				if (dir==SIG_READ) {
					switch (size) {
					case 1: *((uae_u8*)pr)=get_byte (addr); break;
					case 2: *((uae_u16*)pr)=swap16(get_word (addr)); break;
					case 4: *((uae_u32*)pr)=swap32(get_long (addr)); break;
					default: abort();
					}
				}
				else { /* write */
					switch (size) {
					case 1: put_byte (addr,*((uae_u8*)pr)); break;
					case 2: put_word (addr,swap16(*((uae_u16*)pr))); break;
					case 4: put_long (addr,swap32(*((uae_u32*)pr))); break;
					default: abort();
					}
				}
#ifdef JIT_DEBUG
				write_log (_T("JIT: Handled one access!\n"));
#endif
				fflush(stdout);
				segvcount++;
				CONTEXT_PC(context) += len;
			}
			else {
				void* tmp=target;
				int i;
				uae_u8 vecbuf[5];

				addr-=(uae_u32)NATMEM_OFFSET;

#ifdef JIT_DEBUG
				if ((addr>=0x10000000 && addr<0x40000000) ||
					(addr>=0x50000000)) {
						write_log (_T("JIT: Suspicious address 0x%x in SEGV handler.\n"),addr);
				}
#endif

				target = (uae_u8*) CONTEXT_PC(context);
				for (i=0;i<5;i++)
					vecbuf[i]=target[i];
				emit_byte(0xe9);
				emit_long((uae_u32)veccode-(uae_u32)target-4);
#ifdef JIT_DEBUG

				write_log (_T("JIT: Create jump to %p\n"),veccode);
				write_log (_T("JIT: Handled one access!\n"));
#endif
				segvcount++;

				target=veccode;

				if (dir==SIG_READ) {
					switch(size) {
					case 1: raw_mov_b_ri(r,get_byte (addr)); break;
					case 2: raw_mov_w_ri(r,swap16(get_word (addr))); break;
					case 4: raw_mov_l_ri(r,swap32(get_long (addr))); break;
					default: abort();
					}
				}
				else { /* write */
					switch(size) {
					case 1: put_byte (addr,*((uae_u8*)pr)); break;
					case 2: put_word (addr,swap16(*((uae_u16*)pr))); break;
					case 4: put_long (addr,swap32(*((uae_u32*)pr))); break;
					default: abort();
					}
				}
				for (i=0;i<5;i++)
					raw_mov_b_mi(CONTEXT_PC(context) + i, vecbuf[i]);
				raw_mov_l_mi((uae_u32)&in_handler,0);
				emit_byte(0xe9);
				emit_long(CONTEXT_PC(context) + len - (uae_u32)target - 4);
				in_handler=1;
				target=(uae_u8*)tmp;
			}
			bi=active;
			while (bi) {
				if (bi->handler &&
						(uae_u8*)bi->direct_handler<=i &&
						(uae_u8*)bi->nexthandler>i) {
#ifdef JIT_DEBUG
					write_log (_T("JIT: deleted trigger (%p<%p<%p) %p\n"),
							bi->handler,
							i,
							bi->nexthandler,
							bi->pc_p);
#endif
					invalidate_block(bi);
					raise_in_cl_list(bi);
					set_special(0);
					return 1;
				}
				bi=bi->next;
			}
			/* Not found in the active list. Might be a rom routine that
			is in the dormant list */
			bi=dormant;
			while (bi) {
				if (bi->handler &&
						(uae_u8*)bi->direct_handler<=i &&
						(uae_u8*)bi->nexthandler>i) {
#ifdef JIT_DEBUG
					write_log (_T("JIT: deleted trigger (%p<%p<%p) %p\n"),
							bi->handler,
							i,
							bi->nexthandler,
							bi->pc_p);
#endif
					invalidate_block(bi);
					raise_in_cl_list(bi);
					set_special(0);
					return 1;
				}
				bi=bi->next;
			}
#ifdef JIT_DEBUG
			write_log (_T("JIT: Huh? Could not find trigger!\n"));
#endif
			return 1;
		}
	}
	write_log (_T("JIT: Can't handle access %08X!\n"), i);
#if 0
	if (i)
	{
		int j;

		for (j=0;j<10;j++) {
			write_log (_T("JIT: instruction byte %2d is 0x%02x\n"),j,i[j]);
		}
	}
	write_log (_T("Please send the above info (starting at \"fault address\") to\n"));
	write_log (_T("bmeyer@csse.monash.edu.au\n"));
	write_log (_T("This shouldn't happen ;-)\n"));
#endif
	return 0;
}
#endif /* CONTEXT_T */

#ifdef _WIN32

LONG WINAPI EvalException(LPEXCEPTION_POINTERS info)
{
	DWORD code = info->ExceptionRecord->ExceptionCode;
	if (code != STATUS_ACCESS_VIOLATION || !canbang || currprefs.cachesize == 0)
		return EXCEPTION_CONTINUE_SEARCH;

	if (handle_access(info)) {
		return EXCEPTION_CONTINUE_EXECUTION;
	}
	return EXCEPTION_CONTINUE_SEARCH;
}

#elif defined(HAVE_CONTEXT_T)

static void sigsegv_handler(int signum, struct siginfo *info, void *context)
{
	uae_u8  *i    = (uae_u8 *) CONTEXT_EIP(context);
	uae_u32  addr =            CONTEXT_CR2(context);

	if (i >= compiled_code) {
		if (handle_access (context))
			return;
		else {
			int j;
			write_log ("JIT: can't handle access!\n");
			for (j = 0 ; j < 10; j++)
				write_log ("JIT: instruction byte %2d is %02x\n", i, j[i]);
		}
	} else {
		write_log ("Caught illegal access to %08x at eip=%08x\n", addr, i);
	}

	exit (EXIT_FAILURE);
}

#endif

static void install_exception_handler(void)
{
#ifdef USE_STRUCTURED_EXCEPTION_HANDLING
	/* Structured exception handler is installed in main.cpp */
#elif defined(_WIN32)
	write_log (_T("JIT: Installing unhandled exception filter\n"));
	SetUnhandledExceptionFilter(EvalException);
#elif defined(HAVE_CONTEXT_T)
	write_log (_T("JIT: Installing segfault handler\n"));
	struct sigaction act;
	act.sa_sigaction = (void (*)(int, siginfo_t*, void*)) sigsegv_handler;
	sigemptyset (&act.sa_mask);
	act.sa_flags = SA_SIGINFO;
	sigaction(SIGSEGV, &act, NULL);
#else
	write_log (_T("JIT: No segfault handler installed\n"));
#endif
}

#endif /* NATMEM_OFFSET */
