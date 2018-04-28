
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
global xfp_sin
global xfp_cos
global xfp_get_status
global xfp_clear_status

section .text

bits 64

%macro loadfp1 0
	fld tword[rdx]
%endmacro

%macro loadfp2 0
	fld tword[rcx]
	fld tword[rdx]
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
