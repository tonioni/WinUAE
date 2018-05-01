
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

global _xfp_sin
global _xfp_cos
global _xfp_tan
global _xfp_atan

global _xfp_get_status
global _xfp_clear_status

section .text

%macro loadfp1 0
	mov eax,[esp+4]
	mov ecx,[esp+8]
	fld tword[ecx]
%endmacro

%macro loadfp2 0
	mov eax,[esp+4]
	mov ecx,[esp+8]
	fld tword[eax]
	fld tword[ecx]
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
	storefp
	ret

_xfp_atan:
	loadfp1
	fpatan
	storefp
	ret
