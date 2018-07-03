
; 32-bit assembly functions for native 80-bit FPU emulation

global _xfp_int
global _xfp_mov
global _xfp_fldcw
global _xfp_to_single
global _xfp_from_single
global _xfp_to_double
global _xfp_from_double
global _xfp_from_int
global _xfp_to_int
global _xfp_x_to_double
global _xfp_x_from_double
global _xfp_round_single
global _xfp_round_double

global _xfp_div
global _xfp_mul
global _xfp_abs
global _xfp_neg
global _xfp_add
global _xfp_sub
global _xfp_sqrt

global _xfp_rem
global _xfp_rem1
global _xfp_getexp
global _xfp_getman
global _xfp_scale
global _xfp_twotox
global _xfp_etox
global _xfp_etoxm1
global _xfp_tentox
global _xfp_log2
global _xfp_log10
global _xfp_logn
global _xfp_lognp1

global _xfp_sin
global _xfp_cos
global _xfp_tan
global _xfp_atan
global _xfp_asin
global _xfp_acos
global _xfp_atanh
global _xfp_sinh
global _xfp_cosh
global _xfp_tanh

global _xfp_get_status
global _xfp_clear_status

section .text

%macro loadfp1 0
	mov eax,[esp+4]
	mov ecx,[esp+8]
	fld tword[ecx]
%endmacro

%macro reloadfp1 0
	fld tword[ecx]
%endmacro

%macro loadfp2 0
	mov eax,[esp+4]
	mov ecx,[esp+8]
	fld tword[eax]
	fld tword[ecx]
%endmacro

%macro loadfp2swap 0
	mov eax,[esp+4]
	mov ecx,[esp+8]
	fld tword[ecx]
	fld tword[eax]
%endmacro


%macro storefp 0
	fstp tword[eax]
%endmacro

_xfp_fldcw:
	mov eax,[esp+4]
	fldcw word[eax]
	ret

_xfp_get_status:
	fnstsw ax
	ret

_xfp_clear_status:
	fnclex
	ret

_xfp_int:
	loadfp1
	frndint
	storefp
	ret

_xfp_x_to_double:
	mov ecx,[esp+4]
	fld tword[ecx]
	mov eax,[esp+8]
	fstp qword[eax]
	ret

_xfp_x_from_double:
	mov eax,[esp+8]
	fld qword[eax]
	mov ecx,[esp+4]
	fstp tword[ecx]
	ret

_xfp_round_single:
	mov ecx,[esp+8]
	fld tword[ecx]
	mov eax,[esp+4]
	fstp dword[eax]
	fld dword[eax]
	fstp tword[eax]
	ret

_xfp_round_double:
	mov ecx,[esp+8]
	fld tword[ecx]
	mov eax,[esp+4]
	fstp qword[eax]
	fld qword[eax]
	fstp tword[eax]
	ret

_xfp_to_single:
	mov ecx,[esp+8]
	mov eax,[esp+4]
	fld dword[ecx]
	fstp tword[eax]
	ret

_xfp_from_single:
	mov eax,[esp+4]
	mov ecx,[esp+8]
	fld tword[eax]
	fstp dword[ecx]
	ret

_xfp_to_double:
	mov ecx,[esp+8]
	mov eax,[esp+4]
	fld qword[ecx]
	fstp tword[eax]
	ret

_xfp_from_double:
	mov eax,[esp+4]
	mov ecx,[esp+8]
	fld tword[eax]
	fstp qword[ecx]
	ret

_xfp_to_int:
	mov eax,[esp+4]
	mov ecx,[esp+8]
	fld tword[eax]
	fistp qword[ecx]
	ret

_xfp_from_int:
	mov eax,[esp+4]
	mov ecx,[esp+8]
	fild dword[ecx]
	fstp tword[eax]
	ret

_xfp_mov:
	loadfp1
	storefp
	ret
	
_xfp_add:
	loadfp2
	faddp
	storefp
    ret

_xfp_sub:
	loadfp2
	fsubp
	storefp
    ret

_xfp_div:
	loadfp2
	fdivp
	storefp
    ret

_xfp_mul:
	loadfp2
	fmulp
	storefp
    ret

_xfp_sqrt:
	loadfp1
	fsqrt
	storefp
    ret

_xfp_neg:
	loadfp1
	fchs
	storefp
    ret

_xfp_abs:
	loadfp1
	fabs
	storefp
    ret

_xfp_cos:
	loadfp1
	fcos
	storefp
	ret
	
_xfp_sin:
	loadfp1
	fsin
	storefp
	ret

_xfp_tan:
	loadfp1
	fptan
	fstp st0
	storefp
	ret

_xfp_atan:
	loadfp1
	fld1
	fpatan
	storefp
	ret

_xfp_rem:
	loadfp2swap
	fprem
	fstp st1
	storefp
	ret

_xfp_rem1:
	loadfp2swap
	fprem1
	fstp st1	
	storefp
	ret

_xfp_getexp:
	loadfp1
	fxtract
	fstp st0
	storefp
	ret

_xfp_getman:
	loadfp1
	fxtract
	fstp st1
	storefp
	ret

_xfp_scale:
	loadfp2swap
	fscale
	fstp st1
	storefp
	ret

_xfp_twotox:
	loadfp1
	frndint
	reloadfp1
	fsub st0,st1
	f2xm1
	fadd dword[one]
	fscale
	fstp st1
	storefp
	ret

_xfp_etox:
	loadfp1
	fldl2e
	fmul st0,st1
	fst st1
	frndint
	fxch
	fsub st0,st1
	f2xm1
	fadd dword[one]
	fscale
	fstp st1
	storefp
	ret

_xfp_etoxm1:
	loadfp1
	fldl2e
	fmul st0,st1
	fst st1
	frndint
	fxch
	fsub st0,st1
	f2xm1
	fadd dword[one]
	fscale
	fstp st1
	fsub dword[one]
	storefp
	ret

_xfp_tentox:
	loadfp1
	fldl2t
	fmul st0,st1
	fst st1
	frndint
	fxch
	fsub st0,st1
	f2xm1
	fadd dword[one]
	fscale
	fstp st1
	storefp
	ret

_xfp_log2:
	loadfp1
	fld1
	fxch
	fyl2x
	storefp
	ret

_xfp_log10:
	loadfp1
	fldlg2
	fxch
	fyl2x
	storefp
	ret

_xfp_logn:
	loadfp1
	fldln2
	fxch
	fyl2x
	storefp
	ret

_xfp_lognp1:
	loadfp1
	fldln2
	fxch
	fyl2xp1
	storefp
	ret

_xfp_asin:
	loadfp1
	fmul st0,st0
	fld1
	fsubrp
	fsqrt
	reloadfp1
	fxch
	fpatan
	storefp
	ret

_xfp_acos:
	loadfp1
	fmul st0,st0
	fld1
	fsubrp
	fsqrt
	reloadfp1
	fxch
	fpatan
	fld tword[pihalf]
	fsubrp
	storefp
	ret

_xfp_atanh:
	loadfp1
	fld1
	fadd st1,st0
	fsub st0,st2
	fdivp
	fldln2
	fxch
	fyl2x
	fld1
	fchs
	fxch
	fscale
	fstp st1
	storefp
	ret

_xfp_sinh:
	loadfp1
	fldl2e
	fmul st0,st1
	fst st1
	fchs
	fld st0
	frndint
	fxch
	fsub st0,st1
	f2xm1
	fadd dword[one]
	fscale
	fxch st2
	fst st1
	frndint
	fxch
	fsub st0,st1
	f2xm1
	fadd dword[one]
	fscale
	fstp st1
	fsubrp
	fld1
	fchs
	fxch
	fscale
	fstp st1
	storefp
	ret

_xfp_cosh:
	loadfp1
	fldl2e
	fmul st0,st1
	fst st1
	fchs
	fld st0
	frndint
	fxch
	fsub st0,st1
	f2xm1
	fadd dword[one]
	fscale
	fxch st2
	fst st1
	frndint
	fxch
	fsub st0,st1
	f2xm1
	fadd dword[one]
	fscale
	fstp st1
	faddp
	fld1
	fchs
	fxch
	fscale
	fstp st1
	storefp
	ret

_xfp_tanh:
	loadfp1
	fldl2e
	fmul st0,st1
	fst st1
	fchs
	fld st0
	frndint
	fxch
	fsub st0,st1
	f2xm1
	fadd dword[one]
	fscale
	fxch st2
	fst st1
	frndint
	fxch
	fsub st0,st1
	f2xm1
	fadd dword[one]
	fscale
	fst st1
	fadd st0,st2
	fxch st2
	fsubp
	fdivrp
	storefp
	ret

align 4

one:
	dd 1.0
pihalf:
	dd 0x2168c235,0xc90fdaa2,0x00003fff
