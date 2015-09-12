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

static int in_handler = 0;
static uae_u8 *veccode;

#if defined(JIT_DEBUG)
#define DEBUG_ACCESS
#elif defined(CPU_x86_64)
#define DEBUG_ACCESS
#endif

#if defined(_WIN32) && defined(CPU_x86_64)

typedef LPEXCEPTION_POINTERS CONTEXT_T;
#define HAVE_CONTEXT_T 1
#define CONTEXT_RIP(context) (context->ContextRecord->Rip)
#define CONTEXT_RAX(context) (context->ContextRecord->Rax)
#define CONTEXT_RCX(context) (context->ContextRecord->Rcx)
#define CONTEXT_RDX(context) (context->ContextRecord->Rdx)
#define CONTEXT_RBX(context) (context->ContextRecord->Rbx)
#define CONTEXT_RSP(context) (context->ContextRecord->Rsp)
#define CONTEXT_RBP(context) (context->ContextRecord->Rbp)
#define CONTEXT_RSI(context) (context->ContextRecord->Rsi)
#define CONTEXT_RDI(context) (context->ContextRecord->Rdi)

#elif defined(_WIN32) && defined(CPU_i386)

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

#elif defined(HAVE_STRUCT_UCONTEXT_UC_MCONTEXT_GREGS) && defined(CPU_x86_64)

typedef void *CONTEXT_T;
#define HAVE_CONTEXT_T 1
#define CONTEXT_RIP(context) (((struct ucontext *) context)->uc_mcontext.gregs[REG_RIP])
#define CONTEXT_RAX(context) (((struct ucontext *) context)->uc_mcontext.gregs[REG_RAX])
#define CONTEXT_RCX(context) (((struct ucontext *) context)->uc_mcontext.gregs[REG_RCX])
#define CONTEXT_RDX(context) (((struct ucontext *) context)->uc_mcontext.gregs[REG_RDX])
#define CONTEXT_RBX(context) (((struct ucontext *) context)->uc_mcontext.gregs[REG_RBX])
#define CONTEXT_RSP(context) (((struct ucontext *) context)->uc_mcontext.gregs[REG_RSP])
#define CONTEXT_RBP(context) (((struct ucontext *) context)->uc_mcontext.gregs[REG_RBP])
#define CONTEXT_RSI(context) (((struct ucontext *) context)->uc_mcontext.gregs[REG_RSI])
#define CONTEXT_RDI(context) (((struct ucontext *) context)->uc_mcontext.gregs[REG_RDI])

#elif defined(HAVE_STRUCT_UCONTEXT_UC_MCONTEXT_GREGS) && defined(CPU_i386)

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

#endif

#if defined(CPU_x86_64)
#ifdef CONTEXT_RIP
#define CONTEXT_PC(context) CONTEXT_RIP(context)
#endif
#else
#ifdef CONTEXT_EIP
#define CONTEXT_PC(context) CONTEXT_EIP(context)
#endif
#endif

/* FIXME: replace usage with bswap16, move fallback defition to header */
static inline uae_u16 swap16(uae_u16 x)
{
	return ((x & 0xff00) >> 8) | ((x & 0x00ff) << 8);
}

/* FIXME: replace usage with bswap32, move fallback defition to header */
static inline uae_u32 swap32(uae_u32 x)
{
	return ((x & 0x0000ff00) << 8) | ((x & 0x00ff0000) << 24) |
		   ((x & 0x00ff0000) >> 8) | ((x & 0xff000000) >> 24);
}

static int delete_trigger(blockinfo *bi, void *pc)
{
	while (bi) {
		if (bi->handler && (uae_u8*)bi->direct_handler <= pc &&
				(uae_u8*)bi->nexthandler > pc) {
#ifdef DEBUG_ACCESS
			write_log(_T("JIT: Deleted trigger (%p < %p < %p) %p\n"),
					bi->handler, pc, bi->nexthandler, bi->pc_p);
#endif
			invalidate_block(bi);
			raise_in_cl_list(bi);
			set_special(0);
			return 1;
		}
		bi = bi->next;
	}
	return 0;
}

#ifdef HAVE_CONTEXT_T
/*
 * Try to handle faulted memory access in compiled code
 *
 * Returns 1 if handled, 0 otherwise
 */
static int handle_access(uintptr_t fault_addr, CONTEXT_T context)
{
	uae_u8 *fault_pc = (uae_u8 *) CONTEXT_PC(context);
#ifdef CPU_64_BIT
	if (fault_addr > 0xffffffff) {
		return 0;
	}
#endif

	int r = -1;
	int size = 4;
	int dir = -1;
	int len = 0;

#ifdef DEBUG_ACCESS
	write_log (_T("JIT: Fault address is 0x%lx at PC=%p\n"), fault_addr, fault_pc);
#endif
	if (!canbang || !currprefs.cachesize)
		return 0;

	if (in_handler)
		write_log (_T("JIT: Argh --- Am already in a handler. Shouldn't happen!\n"));

	if (canbang && fault_pc >= compiled_code && fault_pc <= current_compile_p) {
		uae_u8 *pc = fault_pc;
#ifdef CPU_x86_64
		if (*pc == 0x67) {
			/* Skip address-size override prefix */
			pc++;
			len++;
		}
#endif
		if (*pc == 0x66) {
			pc++;
			size = 2;
			len++;
		}

		switch (pc[0]) {
		case 0x8a:
			if ((pc[1]&0xc0)==0x80) {
				r=(pc[1]>>3)&7;
				dir=SIG_READ;
				size=1;
				len+=6;
				break;
			}
			break;
		case 0x88:
			if ((pc[1]&0xc0)==0x80) {
				r=(pc[1]>>3)&7;
				dir=SIG_WRITE;
				size=1;
				len+=6;
				break;
			}
			break;
		case 0x8b:
			switch (pc[1]&0xc0) {
			case 0x80:
				r=(pc[1]>>3)&7;
				dir=SIG_READ;
				len+=6;
				break;
			case 0x40:
				r=(pc[1]>>3)&7;
				dir=SIG_READ;
				len+=3;
				break;
			case 0x00:
				r=(pc[1]>>3)&7;
				dir=SIG_READ;
				len+=2;
				break;
			default:
				break;
			}
			break;
		case 0x89:
			switch (pc[1]&0xc0) {
			case 0x80:
				r=(pc[1]>>3)&7;
				dir=SIG_WRITE;
				len+=6;
				break;
			case 0x40:
				r=(pc[1]>>3)&7;
				dir=SIG_WRITE;
				len+=3;
				break;
			case 0x00:
				r=(pc[1]>>3)&7;
				dir=SIG_WRITE;
				len+=2;
				break;
			}
			break;
		}
	}

	if (r!=-1) {
		void *pr = NULL;
#ifdef DEBUG_ACCESS
		write_log (_T("JIT: Register was %d, direction was %d, size was %d\n"),
				   r, dir, size);
#endif

		switch(r) {
#if defined(CPU_x86_64)
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
			if (currprefs.comp_oldsegv) {
				uae_u32 addr = uae_p32(fault_addr) - uae_p32(NATMEM_OFFSET);
#ifdef DEBUG_ACCESS
				if ((addr >= 0x10000000 && addr < 0x40000000) ||
					(addr >= 0x50000000)) {
						write_log (_T("JIT: Suspicious address 0x%x in SEGV handler.\n"), addr);
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
#ifdef DEBUG_ACCESS
				write_log (_T("JIT: Handled one access!\n"));
#endif
				fflush(stdout);
				segvcount++;
				CONTEXT_PC(context) += len;
			}
			else {
				uae_u32 addr = uae_p32(fault_addr) - uae_p32(NATMEM_OFFSET);
#ifdef DEBUG_ACCESS
				if ((addr >= 0x10000000 && addr < 0x40000000) ||
					(addr >= 0x50000000)) {
						write_log (_T("JIT: Suspicious address 0x%x in SEGV handler.\n"), addr);
				}
#endif

				uae_u8* original_target = target;
				target = (uae_u8*) CONTEXT_PC(context);

				uae_u8 vecbuf[5];
				for (int i = 0; i < 5; i++) {
					vecbuf[i] = target[i];
				}
				raw_jmp(uae_p32(veccode));

#ifdef DEBUG_ACCESS
				write_log (_T("JIT: Create jump to %p\n"), veccode);
				write_log (_T("JIT: Handled one access!\n"));
#endif
				segvcount++;

				target = veccode;

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
				for (int i = 0; i < 5; i++) {
					raw_mov_b_mi(CONTEXT_PC(context) + i, vecbuf[i]);
				}
				raw_mov_l_mi(uae_p32(&in_handler), 0);
				raw_jmp(uae_p32(CONTEXT_PC(context)) + len);
				in_handler = 1;
				target = original_target;
			}

			if (delete_trigger(active, fault_pc)) {
				return 1;
			}
			/* Not found in the active list. Might be a rom routine that
			 * is in the dormant list */
			if (delete_trigger(dormant, fault_pc)) {
				return 1;
			}
#ifdef DEBUG_ACCESS
			write_log (_T("JIT: Huh? Could not find trigger!\n"));
#endif
			return 1;
		}
	}

	write_log (_T("JIT: Can't handle access PC=%p!\n"), fault_pc);
	if (fault_pc) {
		write_log (_T("JIT: Instruction bytes"));
		for (int j = 0; j < 10; j++) {
			write_log (_T(" %02x"), fault_pc[j]);
		}
		write_log (_T("\n"));
	}
	return 0;
}
#endif /* CONTEXT_T */

#ifdef _WIN32

LONG WINAPI EvalException(LPEXCEPTION_POINTERS info)
{
	DWORD code = info->ExceptionRecord->ExceptionCode;
	if (code != STATUS_ACCESS_VIOLATION || !canbang || currprefs.cachesize == 0)
		return EXCEPTION_CONTINUE_SEARCH;

	uintptr_t address = info->ExceptionRecord->ExceptionInformation[1];
	if (handle_access(address, info)) {
		return EXCEPTION_CONTINUE_EXECUTION;
	}
	return EXCEPTION_CONTINUE_SEARCH;
}

#elif defined(HAVE_CONTEXT_T)

static void sigsegv_handler(int signum, siginfo_t *info, void *context)
{
	uae_u8 *i = (uae_u8 *) CONTEXT_PC(context);
	uintptr_t address = (uintptr_t) info->si_addr;

	if (i >= compiled_code) {
		if (handle_access(address, context)) {
			return;
		}
	} else {
		write_log ("Caught illegal access to %08lx at eip=%p\n", address, i);
	}

	exit (EXIT_FAILURE);
}

#endif

static void install_exception_handler(void)
{
#ifdef JIT_EXCEPTION_HANDLER
	if (veccode == NULL) {
		veccode = (uae_u8 *) uae_vm_alloc(256, UAE_VM_32BIT, UAE_VM_READ_WRITE_EXECUTE);
	}
#endif
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
