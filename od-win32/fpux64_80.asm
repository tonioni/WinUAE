
; 64-bit assembly functions for native 80-bit FPU emulation

global xfp_int
global xfp_mov
global xfp_fldcw
global xfp_to_single
global xfp_from_single
global xfp_to_double
global xfp_from_double
global xfp_from_int
global xfp_to_int
global xfp_x_to_double
global xfp_x_from_double
global xfp_round_single
global xfp_round_double

global xfp_div
global xfp_mul
global xfp_abs
global xfp_neg
global xfp_add
global xfp_sub
global xfp_sqrt

global xfp_rem
global xfp_rem1
global xfp_getexp
global xfp_getman
global xfp_scale
global xfp_twotox
global xfp_etox
global xfp_etoxm1
global xfp_tentox
global xfp_log2
global xfp_log10
global xfp_logn
global xfp_lognp1

global xfp_sin
global xfp_cos
global xfp_tan
global xfp_atan
global xfp_asin
global xfp_acos
global xfp_atanh
global xfp_sinh
global xfp_cosh
global xfp_tanh

global xfp_get_status
global xfp_clear_status

section .text

bits 64

%macro loadfp1 0
	fld tword[rdx]
%endmacro

%macro reloadfp1 0
	fld tword[rdx]
%endmacro

%macro loadfp2 0
	fld tword[rcx]
	fld tword[rdx]
%endmacro

%macro loadfp2swap 0
	fld tword[rdx]
	fld tword[rcx]
%endmacro

%macro storefp 0
	fstp tword[rcx]
%endmacro


xfp_fldcw:
	fldcw word[rcx]
	ret

xfp_get_status:
	fnstsw ax
	ret

xfp_clear_status:
	fnclex
	ret

xfp_int:
	loadfp1
	frndint
	storefp
	ret

xfp_x_to_double:
	fld tword[rcx]
	fstp qword[rdx]
	ret

xfp_x_from_double:
	fld qword[rdx]
	fstp tword[rcx]
	ret

xfp_round_single:
	fld tword[rdx]
	fstp dword[rcx]
	fld dword[rcx]
	fstp tword[rcx]
	ret

xfp_round_double:
	fld tword[rdx]
	fstp qword[rcx]
	fld qword[rcx]
	fstp tword[rcx]
	ret

xfp_to_single:
	fld dword[rdx]
	fstp tword[rcx]
	ret

xfp_from_single:
	fld tword[rcx]
	fstp dword[rdx]
	ret

xfp_to_double:
	fld qword[rdx]
	fstp tword[rcx]
	ret

xfp_from_double:
	fld tword[rcx]
	fstp qword[rdx]
	ret

xfp_to_int:
	fld tword[rcx]
	fistp qword[rdx]
	ret

xfp_from_int:
	fild dword[rdx]
	fstp tword[rcx]
	ret

xfp_mov:
	loadfp1
	storefp
	ret
	
xfp_add:
	loadfp2
	faddp
	storefp
    ret

xfp_sub:
	loadfp2
	fsubp
	storefp
    ret

xfp_div:
	loadfp2
	fdivp
	storefp
    ret

xfp_mul:
	loadfp2
	fmulp
	storefp
    ret

xfp_sqrt:
	loadfp1
	fsqrt
	storefp
    ret

xfp_neg:
	loadfp1
	fchs
	storefp
    ret

xfp_abs:
	loadfp1
	fabs
	storefp
    ret

xfp_cos:
	loadfp1
	fcos
	storefp
	ret
	
xfp_sin:
	loadfp1
	fsin
	storefp
	ret

xfp_tan:
	loadfp1
	fptan
	fstp st0
	storefp
	ret

xfp_atan:
	loadfp1
	fld1
	fpatan
	storefp
	ret


xfp_rem:
	loadfp2swap
	fprem
	fstp st1
	storefp
	ret

xfp_rem1:
	loadfp2swap
	fprem1
	fstp st1	
	storefp
	ret

xfp_getexp:
	loadfp1
	fxtract
	fstp st0
	storefp
	ret

xfp_getman:
	loadfp1
	fxtract
	fstp st1
	storefp
	ret

xfp_scale:
	loadfp2swap
	fscale
	fstp st1
	storefp
	ret

xfp_twotox:
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

xfp_etox:
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

xfp_etoxm1:
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

xfp_tentox:
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

xfp_log2:
	loadfp1
	fld1
	fxch
	fyl2x
	storefp
	ret

xfp_log10:
	loadfp1
	fldlg2
	fxch
	fyl2x
	storefp
	ret

xfp_logn:
	loadfp1
	fldln2
	fxch
	fyl2x
	storefp
	ret

xfp_lognp1:
	loadfp1
	fldln2
	fxch
	fyl2xp1
	storefp
	ret


xfp_asin:
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

xfp_acos:
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

xfp_atanh:
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

xfp_sinh:
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

xfp_cosh:
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

xfp_tanh:
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
