/********************************************************************
* CPU functions exposed to gencomp. Both CREATE and EMIT time      *
********************************************************************/

/*
 *  RULES FOR HANDLING REGISTERS:
 *
 *  * In the function headers, order the parameters
 *     - 1st registers written to
 *     - 2nd read/modify/write registers
 *     - 3rd registers read from
 *  * Before calling raw_*, you must call readreg, writereg or rmw for
 *    each register
 *  * The order for this is
 *     - 1st call remove_offset for all registers written to with size<4
 *     - 2nd call readreg for all registers read without offset
 *     - 3rd call rmw for all rmw registers
 *     - 4th call readreg_offset for all registers that can handle offsets
 *     - 5th call get_offset for all the registers from the previous step
 *     - 6th call writereg for all written-to registers
 *     - 7th call raw_*
 *     - 8th unlock all registers that were locked
 */

MIDFUNC(0,live_flags,(void))
{
	live.flags_on_stack=TRASH;
	live.flags_in_flags=VALID;
	live.flags_are_important=1;
}
MENDFUNC(0,live_flags,(void))

MIDFUNC(0,dont_care_flags,(void))
{
	live.flags_are_important=0;
}
MENDFUNC(0,dont_care_flags,(void))

/*
 * Copy m68k C flag into m68k X flag
 *
 * FIXME: This needs to be moved into the machdep
 * part of the source because it depends on what bit
 * is used to hold X.
 */
MIDFUNC(0,duplicate_carry,(void))
{
	evict(FLAGX);
	make_flags_live_internal();
	COMPCALL(setcc_m)((uae_u32)live.state[FLAGX].mem + 1,2);
}
MENDFUNC(0,duplicate_carry,(void))

/*
 * Set host C flag from m68k X flag.
 *
 * FIXME: This needs to be moved into the machdep
 * part of the source because it depends on what bit
 * is used to hold X.
 */
MIDFUNC(0,restore_carry,(void))
{
	if (!have_rat_stall) { /* Not a P6 core, i.e. no partial stalls */
		bt_l_ri_noclobber(FLAGX, 8);
	}
	else {  /* Avoid the stall the above creates.
			This is slow on non-P6, though.
			*/
		COMPCALL(rol_w_ri(FLAGX, 8));
		isclean(FLAGX);
		/* Why is the above faster than the below? */
		//raw_rol_b_mi((uae_u32)live.state[FLAGX].mem,8);
	}
}
MENDFUNC(0,restore_carry,(void))

MIDFUNC(0,start_needflags,(void))
{
	needflags=1;
}
MENDFUNC(0,start_needflags,(void))

MIDFUNC(0,end_needflags,(void))
{
	needflags=0;
}
MENDFUNC(0,end_needflags,(void))

MIDFUNC(0,make_flags_live,(void))
{
	make_flags_live_internal();
}
MENDFUNC(0,make_flags_live,(void))

MIDFUNC(1,fflags_into_flags,(W2 tmp))
{
	clobber_flags();
	fflags_into_flags_internal(tmp);
}
MENDFUNC(1,fflags_into_flags,(W2 tmp))

MIDFUNC(2,bt_l_ri,(R4 r, IMM i)) /* This is defined as only affecting C */
{
	int size=4;
	if (i<16)
		size=2;
	CLOBBER_BT;
	r=readreg(r,size);
	raw_bt_l_ri(r,i);
	unlock(r);
}
MENDFUNC(2,bt_l_ri,(R4 r, IMM i)) /* This is defined as only affecting C */

MIDFUNC(2,bt_l_rr,(R4 r, R4 b)) /* This is defined as only affecting C */
{
	CLOBBER_BT;
	r=readreg(r,4);
	b=readreg(b,4);
	raw_bt_l_rr(r,b);
	unlock(r);
	unlock(b);
}
MENDFUNC(2,bt_l_rr,(R4 r, R4 b)) /* This is defined as only affecting C */

MIDFUNC(2,btc_l_ri,(RW4 r, IMM i))
{
	int size=4;
	if (i<16)
		size=2;
	CLOBBER_BT;
	r=rmw(r,size,size);
	raw_btc_l_ri(r,i);
	unlock(r);
}
MENDFUNC(2,btc_l_ri,(RW4 r, IMM i))

MIDFUNC(2,btc_l_rr,(RW4 r, R4 b))
{
	CLOBBER_BT;
	b=readreg(b,4);
	r=rmw(r,4,4);
	raw_btc_l_rr(r,b);
	unlock(r);
	unlock(b);
}
MENDFUNC(2,btc_l_rr,(RW4 r, R4 b))

MIDFUNC(2,btr_l_ri,(RW4 r, IMM i))
{
	int size=4;
	if (i<16)
		size=2;
	CLOBBER_BT;
	r=rmw(r,size,size);
	raw_btr_l_ri(r,i);
	unlock(r);
}
MENDFUNC(2,btr_l_ri,(RW4 r, IMM i))

MIDFUNC(2,btr_l_rr,(RW4 r, R4 b))
{
	CLOBBER_BT;
	b=readreg(b,4);
	r=rmw(r,4,4);
	raw_btr_l_rr(r,b);
	unlock(r);
	unlock(b);
}
MENDFUNC(2,btr_l_rr,(RW4 r, R4 b))

MIDFUNC(2,bts_l_ri,(RW4 r, IMM i))
{
	int size=4;
	if (i<16)
		size=2;
	CLOBBER_BT;
	r=rmw(r,size,size);
	raw_bts_l_ri(r,i);
	unlock(r);
}
MENDFUNC(2,bts_l_ri,(RW4 r, IMM i))

MIDFUNC(2,bts_l_rr,(RW4 r, R4 b))
{
	CLOBBER_BT;
	b=readreg(b,4);
	r=rmw(r,4,4);
	raw_bts_l_rr(r,b);
	unlock(r);
	unlock(b);
}
MENDFUNC(2,bts_l_rr,(RW4 r, R4 b))

MIDFUNC(2,mov_l_rm,(W4 d, IMM s))
{
	CLOBBER_MOV;
	d=writereg(d,4);
	raw_mov_l_rm(d,s);
	unlock(d);
}
MENDFUNC(2,mov_l_rm,(W4 d, IMM s))

MIDFUNC(1,call_r,(R4 r)) /* Clobbering is implicit */
{
	r=readreg(r,4);
	raw_call_r(r);
	unlock(r);
}
MENDFUNC(1,call_r,(R4 r)) /* Clobbering is implicit */

MIDFUNC(2,sub_l_mi,(IMM d, IMM s))
{
	CLOBBER_SUB;
	raw_sub_l_mi(d,s) ;
}
MENDFUNC(2,sub_l_mi,(IMM d, IMM s))

MIDFUNC(2,mov_l_mi,(IMM d, IMM s))
{
	CLOBBER_MOV;
	raw_mov_l_mi(d,s) ;
}
MENDFUNC(2,mov_l_mi,(IMM d, IMM s))

MIDFUNC(2,mov_w_mi,(IMM d, IMM s))
{
	CLOBBER_MOV;
	raw_mov_w_mi(d,s) ;
}
MENDFUNC(2,mov_w_mi,(IMM d, IMM s))

MIDFUNC(2,mov_b_mi,(IMM d, IMM s))
{
	CLOBBER_MOV;
	raw_mov_b_mi(d,s) ;
}
MENDFUNC(2,mov_b_mi,(IMM d, IMM s))

MIDFUNC(2,rol_b_ri,(RW1 r, IMM i))
{
	if (!i && !needflags)
		return;
	CLOBBER_ROL;
	r=rmw(r,1,1);
	raw_rol_b_ri(r,i);
	unlock(r);
}
MENDFUNC(2,rol_b_ri,(RW1 r, IMM i))

MIDFUNC(2,rol_w_ri,(RW2 r, IMM i))
{
	if (!i && !needflags)
		return;
	CLOBBER_ROL;
	r=rmw(r,2,2);
	raw_rol_w_ri(r,i);
	unlock(r);
}
MENDFUNC(2,rol_w_ri,(RW2 r, IMM i))

MIDFUNC(2,rol_l_ri,(RW4 r, IMM i))
{
	if (!i && !needflags)
		return;
	CLOBBER_ROL;
	r=rmw(r,4,4);
	raw_rol_l_ri(r,i);
	unlock(r);
}
MENDFUNC(2,rol_l_ri,(RW4 r, IMM i))

MIDFUNC(2,rol_l_rr,(RW4 d, R1 r))
{
	if (isconst(r)) {
		COMPCALL(rol_l_ri)(d,(uae_u8)live.state[r].val);
		return;
	}
	CLOBBER_ROL;
	r=readreg_specific(r,1,SHIFTCOUNT_NREG);
	d=rmw(d,4,4);
	Dif (r!=1) {
		jit_abort (_T("JIT: Illegal register %d in raw_rol_b\n"),r);
	}
	raw_rol_l_rr(d,r) ;
	unlock(r);
	unlock(d);
}
MENDFUNC(2,rol_l_rr,(RW4 d, R1 r))

MIDFUNC(2,rol_w_rr,(RW2 d, R1 r))
{ /* Can only do this with r==1, i.e. cl */

	if (isconst(r)) {
		COMPCALL(rol_w_ri)(d,(uae_u8)live.state[r].val);
		return;
	}
	CLOBBER_ROL;
	r=readreg_specific(r,1,SHIFTCOUNT_NREG);
	d=rmw(d,2,2);
	Dif (r!=1) {
		jit_abort (_T("JIT: Illegal register %d in raw_rol_b\n"),r);
	}
	raw_rol_w_rr(d,r) ;
	unlock(r);
	unlock(d);
}
MENDFUNC(2,rol_w_rr,(RW2 d, R1 r))

MIDFUNC(2,rol_b_rr,(RW1 d, R1 r))
{ /* Can only do this with r==1, i.e. cl */

	if (isconst(r)) {
		COMPCALL(rol_b_ri)(d,(uae_u8)live.state[r].val);
		return;
	}

	CLOBBER_ROL;
	r=readreg_specific(r,1,SHIFTCOUNT_NREG);
	d=rmw(d,1,1);
	Dif (r!=1) {
		jit_abort (_T("JIT: Illegal register %d in raw_rol_b\n"),r);
	}
	raw_rol_b_rr(d,r) ;
	unlock(r);
	unlock(d);
}
MENDFUNC(2,rol_b_rr,(RW1 d, R1 r))

MIDFUNC(2,shll_l_rr,(RW4 d, R1 r))
{
	if (isconst(r)) {
		COMPCALL(shll_l_ri)(d,(uae_u8)live.state[r].val);
		return;
	}
	CLOBBER_SHLL;
	r=readreg_specific(r,1,SHIFTCOUNT_NREG);
	d=rmw(d,4,4);
	Dif (r!=1) {
		jit_abort (_T("JIT: Illegal register %d in raw_rol_b\n"),r);
	}
	raw_shll_l_rr(d,r) ;
	unlock(r);
	unlock(d);
}
MENDFUNC(2,shll_l_rr,(RW4 d, R1 r))

MIDFUNC(2,shll_w_rr,(RW2 d, R1 r))
{ /* Can only do this with r==1, i.e. cl */

	if (isconst(r)) {
		COMPCALL(shll_w_ri)(d,(uae_u8)live.state[r].val);
		return;
	}
	CLOBBER_SHLL;
	r=readreg_specific(r,1,SHIFTCOUNT_NREG);
	d=rmw(d,2,2);
	Dif (r!=1) {
		jit_abort (_T("JIT: Illegal register %d in raw_shll_b\n"),r);
	}
	raw_shll_w_rr(d,r) ;
	unlock(r);
	unlock(d);
}
MENDFUNC(2,shll_w_rr,(RW2 d, R1 r))

MIDFUNC(2,shll_b_rr,(RW1 d, R1 r))
{ /* Can only do this with r==1, i.e. cl */

	if (isconst(r)) {
		COMPCALL(shll_b_ri)(d,(uae_u8)live.state[r].val);
		return;
	}

	CLOBBER_SHLL;
	r=readreg_specific(r,1,SHIFTCOUNT_NREG);
	d=rmw(d,1,1);
	Dif (r!=1) {
		jit_abort (_T("JIT: Illegal register %d in raw_shll_b\n"),r);
	}
	raw_shll_b_rr(d,r) ;
	unlock(r);
	unlock(d);
}
MENDFUNC(2,shll_b_rr,(RW1 d, R1 r))

MIDFUNC(2,ror_b_ri,(R1 r, IMM i))
{
	if (!i && !needflags)
		return;
	CLOBBER_ROR;
	r=rmw(r,1,1);
	raw_ror_b_ri(r,i);
	unlock(r);
}
MENDFUNC(2,ror_b_ri,(R1 r, IMM i))

MIDFUNC(2,ror_w_ri,(R2 r, IMM i))
{
	if (!i && !needflags)
		return;
	CLOBBER_ROR;
	r=rmw(r,2,2);
	raw_ror_w_ri(r,i);
	unlock(r);
}
MENDFUNC(2,ror_w_ri,(R2 r, IMM i))

MIDFUNC(2,ror_l_ri,(R4 r, IMM i))
{
	if (!i && !needflags)
		return;
	CLOBBER_ROR;
	r=rmw(r,4,4);
	raw_ror_l_ri(r,i);
	unlock(r);
}
MENDFUNC(2,ror_l_ri,(R4 r, IMM i))

MIDFUNC(2,ror_l_rr,(R4 d, R1 r))
{
	if (isconst(r)) {
		COMPCALL(ror_l_ri)(d,(uae_u8)live.state[r].val);
		return;
	}
	CLOBBER_ROR;
	r=readreg_specific(r,1,SHIFTCOUNT_NREG);
	d=rmw(d,4,4);
	raw_ror_l_rr(d,r) ;
	unlock(r);
	unlock(d);
}
MENDFUNC(2,ror_l_rr,(R4 d, R1 r))

MIDFUNC(2,ror_w_rr,(R2 d, R1 r))
{
	if (isconst(r)) {
		COMPCALL(ror_w_ri)(d,(uae_u8)live.state[r].val);
		return;
	}
	CLOBBER_ROR;
	r=readreg_specific(r,1,SHIFTCOUNT_NREG);
	d=rmw(d,2,2);
	raw_ror_w_rr(d,r) ;
	unlock(r);
	unlock(d);
}
MENDFUNC(2,ror_w_rr,(R2 d, R1 r))

MIDFUNC(2,ror_b_rr,(R1 d, R1 r))
{
	if (isconst(r)) {
		COMPCALL(ror_b_ri)(d,(uae_u8)live.state[r].val);
		return;
	}

	CLOBBER_ROR;
	r=readreg_specific(r,1,SHIFTCOUNT_NREG);
	d=rmw(d,1,1);
	raw_ror_b_rr(d,r) ;
	unlock(r);
	unlock(d);
}
MENDFUNC(2,ror_b_rr,(R1 d, R1 r))

MIDFUNC(2,shrl_l_rr,(RW4 d, R1 r))
{
	if (isconst(r)) {
		COMPCALL(shrl_l_ri)(d,(uae_u8)live.state[r].val);
		return;
	}
	CLOBBER_SHRL;
	r=readreg_specific(r,1,SHIFTCOUNT_NREG);
	d=rmw(d,4,4);
	Dif (r!=1) {
		jit_abort (_T("JIT: Illegal register %d in raw_rol_b\n"),r);
	}
	raw_shrl_l_rr(d,r) ;
	unlock(r);
	unlock(d);
}
MENDFUNC(2,shrl_l_rr,(RW4 d, R1 r))

MIDFUNC(2,shrl_w_rr,(RW2 d, R1 r))
{ /* Can only do this with r==1, i.e. cl */

	if (isconst(r)) {
		COMPCALL(shrl_w_ri)(d,(uae_u8)live.state[r].val);
		return;
	}
	CLOBBER_SHRL;
	r=readreg_specific(r,1,SHIFTCOUNT_NREG);
	d=rmw(d,2,2);
	Dif (r!=1) {
		jit_abort (_T("JIT: Illegal register %d in raw_shrl_b\n"),r);
	}
	raw_shrl_w_rr(d,r) ;
	unlock(r);
	unlock(d);
}
MENDFUNC(2,shrl_w_rr,(RW2 d, R1 r))

MIDFUNC(2,shrl_b_rr,(RW1 d, R1 r))
{ /* Can only do this with r==1, i.e. cl */

	if (isconst(r)) {
		COMPCALL(shrl_b_ri)(d,(uae_u8)live.state[r].val);
		return;
	}

	CLOBBER_SHRL;
	r=readreg_specific(r,1,SHIFTCOUNT_NREG);
	d=rmw(d,1,1);
	Dif (r!=1) {
		jit_abort (_T("JIT: Illegal register %d in raw_shrl_b\n"),r);
	}
	raw_shrl_b_rr(d,r) ;
	unlock(r);
	unlock(d);
}
MENDFUNC(2,shrl_b_rr,(RW1 d, R1 r))

MIDFUNC(2,shll_l_ri,(RW4 r, IMM i))
{
	if (!i && !needflags)
		return;
	if (isconst(r) && !needflags) {
		live.state[r].val<<=i;
		return;
	}
	CLOBBER_SHLL;
	r=rmw(r,4,4);
	raw_shll_l_ri(r,i);
	unlock(r);
}
MENDFUNC(2,shll_l_ri,(RW4 r, IMM i))

MIDFUNC(2,shll_w_ri,(RW2 r, IMM i))
{
	if (!i && !needflags)
		return;
	CLOBBER_SHLL;
	r=rmw(r,2,2);
	raw_shll_w_ri(r,i);
	unlock(r);
}
MENDFUNC(2,shll_w_ri,(RW2 r, IMM i))

MIDFUNC(2,shll_b_ri,(RW1 r, IMM i))
{
	if (!i && !needflags)
		return;
	CLOBBER_SHLL;
	r=rmw(r,1,1);
	raw_shll_b_ri(r,i);
	unlock(r);
}
MENDFUNC(2,shll_b_ri,(RW1 r, IMM i))

MIDFUNC(2,shrl_l_ri,(RW4 r, IMM i))
{
	if (!i && !needflags)
		return;
	if (isconst(r) && !needflags) {
		live.state[r].val>>=i;
		return;
	}
	CLOBBER_SHRL;
	r=rmw(r,4,4);
	raw_shrl_l_ri(r,i);
	unlock(r);
}
MENDFUNC(2,shrl_l_ri,(RW4 r, IMM i))

MIDFUNC(2,shrl_w_ri,(RW2 r, IMM i))
{
	if (!i && !needflags)
		return;
	CLOBBER_SHRL;
	r=rmw(r,2,2);
	raw_shrl_w_ri(r,i);
	unlock(r);
}
MENDFUNC(2,shrl_w_ri,(RW2 r, IMM i))

MIDFUNC(2,shrl_b_ri,(RW1 r, IMM i))
{
	if (!i && !needflags)
		return;
	CLOBBER_SHRL;
	r=rmw(r,1,1);
	raw_shrl_b_ri(r,i);
	unlock(r);
}
MENDFUNC(2,shrl_b_ri,(RW1 r, IMM i))

MIDFUNC(2,shra_l_ri,(RW4 r, IMM i))
{
	if (!i && !needflags)
		return;
	CLOBBER_SHRA;
	r=rmw(r,4,4);
	raw_shra_l_ri(r,i);
	unlock(r);
}
MENDFUNC(2,shra_l_ri,(RW4 r, IMM i))

MIDFUNC(2,shra_w_ri,(RW2 r, IMM i))
{
	if (!i && !needflags)
		return;
	CLOBBER_SHRA;
	r=rmw(r,2,2);
	raw_shra_w_ri(r,i);
	unlock(r);
}
MENDFUNC(2,shra_w_ri,(RW2 r, IMM i))

MIDFUNC(2,shra_b_ri,(RW1 r, IMM i))
{
	if (!i && !needflags)
		return;
	CLOBBER_SHRA;
	r=rmw(r,1,1);
	raw_shra_b_ri(r,i);
	unlock(r);
}
MENDFUNC(2,shra_b_ri,(RW1 r, IMM i))

MIDFUNC(2,shra_l_rr,(RW4 d, R1 r))
{
	if (isconst(r)) {
		COMPCALL(shra_l_ri)(d,(uae_u8)live.state[r].val);
		return;
	}
	CLOBBER_SHRA;
	r=readreg_specific(r,1,SHIFTCOUNT_NREG);
	d=rmw(d,4,4);
	Dif (r!=1) {
		jit_abort (_T("JIT: Illegal register %d in raw_rol_b\n"),r);
	}
	raw_shra_l_rr(d,r) ;
	unlock(r);
	unlock(d);
}
MENDFUNC(2,shra_l_rr,(RW4 d, R1 r))

MIDFUNC(2,shra_w_rr,(RW2 d, R1 r))
{ /* Can only do this with r==1, i.e. cl */

	if (isconst(r)) {
		COMPCALL(shra_w_ri)(d,(uae_u8)live.state[r].val);
		return;
	}
	CLOBBER_SHRA;
	r=readreg_specific(r,1,SHIFTCOUNT_NREG);
	d=rmw(d,2,2);
	Dif (r!=1) {
		jit_abort (_T("JIT: Illegal register %d in raw_shra_b\n"),r);
	}
	raw_shra_w_rr(d,r) ;
	unlock(r);
	unlock(d);
}
MENDFUNC(2,shra_w_rr,(RW2 d, R1 r))

MIDFUNC(2,shra_b_rr,(RW1 d, R1 r))
{ /* Can only do this with r==1, i.e. cl */

	if (isconst(r)) {
		COMPCALL(shra_b_ri)(d,(uae_u8)live.state[r].val);
		return;
	}

	CLOBBER_SHRA;
	r=readreg_specific(r,1,SHIFTCOUNT_NREG);
	d=rmw(d,1,1);
	Dif (r!=1) {
		jit_abort (_T("JIT: Illegal register %d in raw_shra_b\n"),r);
	}
	raw_shra_b_rr(d,r) ;
	unlock(r);
	unlock(d);
}
MENDFUNC(2,shra_b_rr,(RW1 d, R1 r))

MIDFUNC(2,setcc,(W1 d, IMM cc))
{
	CLOBBER_SETCC;
	d=writereg(d,1);
	raw_setcc(d,cc);
	unlock(d);
}
MENDFUNC(2,setcc,(W1 d, IMM cc))

MIDFUNC(2,setcc_m,(IMM d, IMM cc))
{
	CLOBBER_SETCC;
	raw_setcc_m(d,cc);
}
MENDFUNC(2,setcc_m,(IMM d, IMM cc))

MIDFUNC(3,cmov_b_rr,(RW1 d, R1 s, IMM cc))
{
	if (d==s)
		return;
	CLOBBER_CMOV;
	s=readreg(s,1);
	d=rmw(d,1,1);
	raw_cmov_b_rr(d,s,cc);
	unlock(s);
	unlock(d);
}
MENDFUNC(3,cmov_b_rr,(RW1 d, R1 s, IMM cc))

MIDFUNC(3,cmov_w_rr,(RW2 d, R2 s, IMM cc))
{
	if (d==s)
		return;
	CLOBBER_CMOV;
	s=readreg(s,2);
	d=rmw(d,2,2);
	raw_cmov_w_rr(d,s,cc);
	unlock(s);
	unlock(d);
}
MENDFUNC(3,cmov_w_rr,(RW2 d, R2 s, IMM cc))

MIDFUNC(3,cmov_l_rr,(RW4 d, R4 s, IMM cc))
{
	if (d==s)
		return;
	CLOBBER_CMOV;
	s=readreg(s,4);
	d=rmw(d,4,4);
	raw_cmov_l_rr(d,s,cc);
	unlock(s);
	unlock(d);
}
MENDFUNC(3,cmov_l_rr,(RW4 d, R4 s, IMM cc))

MIDFUNC(1,setzflg_l,(RW4 r))
{
	if (setzflg_uses_bsf) {
		CLOBBER_BSF;
		r=rmw(r,4,4);
		raw_bsf_l_rr(r,r);
		unlock(r);
	}
	else {
		Dif (live.flags_in_flags!=VALID) {
			jit_abort (_T("JIT: setzflg() wanted flags in native flags, they are %d\n"),
				live.flags_in_flags);
		}
		r=readreg(r,4);
		{
			int f=writereg(S11,4);
			int t=writereg(S12,4);
			raw_flags_set_zero(f,r,t);
			unlock(f);
			unlock(r);
			unlock(t);
		}
	}
}
MENDFUNC(1,setzflg_l,(RW4 r))

MIDFUNC(3,cmov_l_rm,(RW4 d, IMM s, IMM cc))
{
	CLOBBER_CMOV;
	d=rmw(d,4,4);
	raw_cmov_l_rm(d,s,cc);
	unlock(d);
}
MENDFUNC(3,cmov_l_rm,(RW4 d, IMM s, IMM cc))

MIDFUNC(2,bsf_l_rr,(W4 d, R4 s))
{
	CLOBBER_BSF;
	s=readreg(s,4);
	d=writereg(d,4);
	raw_bsf_l_rr(d,s);
	unlock(s);
	unlock(d);
}
MENDFUNC(2,bsf_l_rr,(W4 d, R4 s))

MIDFUNC(2,imul_32_32,(RW4 d, R4 s))
{
	CLOBBER_MUL;
	s=readreg(s,4);
	d=rmw(d,4,4);
	raw_imul_32_32(d,s);
	unlock(s);
	unlock(d);
}
MENDFUNC(2,imul_32_32,(RW4 d, R4 s))

MIDFUNC(2,imul_64_32,(RW4 d, RW4 s))
{
	CLOBBER_MUL;
	s=rmw_specific(s,4,4,MUL_NREG2);
	d=rmw_specific(d,4,4,MUL_NREG1);
	raw_imul_64_32(d,s);
	unlock(s);
	unlock(d);
}
MENDFUNC(2,imul_64_32,(RW4 d, RW4 s))

MIDFUNC(2,mul_64_32,(RW4 d, RW4 s))
{
	CLOBBER_MUL;
	s=rmw_specific(s,4,4,MUL_NREG2);
	d=rmw_specific(d,4,4,MUL_NREG1);
	raw_mul_64_32(d,s);
	unlock(s);
	unlock(d);
}
MENDFUNC(2,mul_64_32,(RW4 d, RW4 s))

MIDFUNC(2,sign_extend_16_rr,(W4 d, R2 s))
{
	int isrmw;

	if (isconst(s)) {
		set_const(d,(uae_s32)(uae_s16)live.state[s].val);
		return;
	}

	CLOBBER_SE16;
	isrmw=(s==d);
	if (!isrmw) {
		s=readreg(s,2);
		d=writereg(d,4);
	}
	else {  /* If we try to lock this twice, with different sizes, we
			are int trouble! */
		s=d=rmw(s,4,2);
	}
	raw_sign_extend_16_rr(d,s);
	if (!isrmw) {
		unlock(d);
		unlock(s);
	}
	else {
		unlock(s);
	}
}
MENDFUNC(2,sign_extend_16_rr,(W4 d, R2 s))

MIDFUNC(2,sign_extend_8_rr,(W4 d, R1 s))
{
	int isrmw;

	if (isconst(s)) {
		set_const(d,(uae_s32)(uae_s8)live.state[s].val);
		return;
	}

	isrmw=(s==d);
	CLOBBER_SE8;
	if (!isrmw) {
		s=readreg(s,1);
		d=writereg(d,4);
	}
	else {  /* If we try to lock this twice, with different sizes, we
			are int trouble! */
		s=d=rmw(s,4,1);
	}

	raw_sign_extend_8_rr(d,s);

	if (!isrmw) {
		unlock(d);
		unlock(s);
	}
	else {
		unlock(s);
	}
}
MENDFUNC(2,sign_extend_8_rr,(W4 d, R1 s))

MIDFUNC(2,zero_extend_16_rr,(W4 d, R2 s))
{
	int isrmw;

	if (isconst(s)) {
		set_const(d,(uae_u32)(uae_u16)live.state[s].val);
		return;
	}

	isrmw=(s==d);
	CLOBBER_ZE16;
	if (!isrmw) {
		s=readreg(s,2);
		d=writereg(d,4);
	}
	else {  /* If we try to lock this twice, with different sizes, we
			are int trouble! */
		s=d=rmw(s,4,2);
	}
	raw_zero_extend_16_rr(d,s);
	if (!isrmw) {
		unlock(d);
		unlock(s);
	}
	else {
		unlock(s);
	}
}
MENDFUNC(2,zero_extend_16_rr,(W4 d, R2 s))

MIDFUNC(2,zero_extend_8_rr,(W4 d, R1 s))
{
	int isrmw;
	if (isconst(s)) {
		set_const(d,(uae_u32)(uae_u8)live.state[s].val);
		return;
	}

	isrmw=(s==d);
	CLOBBER_ZE8;
	if (!isrmw) {
		s=readreg(s,1);
		d=writereg(d,4);
	}
	else {  /* If we try to lock this twice, with different sizes, we
			are int trouble! */
		s=d=rmw(s,4,1);
	}

	raw_zero_extend_8_rr(d,s);

	if (!isrmw) {
		unlock(d);
		unlock(s);
	}
	else {
		unlock(s);
	}
}
MENDFUNC(2,zero_extend_8_rr,(W4 d, R1 s))

MIDFUNC(2,mov_b_rr,(W1 d, R1 s))
{
	if (d==s)
		return;
	if (isconst(s)) {
		COMPCALL(mov_b_ri)(d,(uae_u8)live.state[s].val);
		return;
	}

	CLOBBER_MOV;
	s=readreg(s,1);
	d=writereg(d,1);
	raw_mov_b_rr(d,s);
	unlock(d);
	unlock(s);
}
MENDFUNC(2,mov_b_rr,(W1 d, R1 s))

MIDFUNC(2,mov_w_rr,(W2 d, R2 s))
{
	if (d==s)
		return;
	if (isconst(s)) {
		COMPCALL(mov_w_ri)(d,(uae_u16)live.state[s].val);
		return;
	}

	CLOBBER_MOV;
	s=readreg(s,2);
	d=writereg(d,2);
	raw_mov_w_rr(d,s);
	unlock(d);
	unlock(s);
}
MENDFUNC(2,mov_w_rr,(W2 d, R2 s))

MIDFUNC(3,mov_l_rrm_indexed,(W4 d,R4 baser, R4 index))
{
	CLOBBER_MOV;
	baser=readreg(baser,4);
	index=readreg(index,4);
	d=writereg(d,4);

	raw_mov_l_rrm_indexed(d,baser,index);
	unlock(d);
	unlock(baser);
	unlock(index);
}
MENDFUNC(3,mov_l_rrm_indexed,(W4 d,R4 baser, R4 index))

MIDFUNC(3,mov_w_rrm_indexed,(W2 d, R4 baser, R4 index))
{
	CLOBBER_MOV;
	baser=readreg(baser,4);
	index=readreg(index,4);
	d=writereg(d,2);

	raw_mov_w_rrm_indexed(d,baser,index);
	unlock(d);
	unlock(baser);
	unlock(index);
}
MENDFUNC(3,mov_w_rrm_indexed,(W2 d, R4 baser, R4 index))

MIDFUNC(3,mov_b_rrm_indexed,(W1 d, R4 baser, R4 index))
{
	CLOBBER_MOV;
	baser=readreg(baser,4);
	index=readreg(index,4);
	d=writereg(d,1);

	raw_mov_b_rrm_indexed(d,baser,index);

	unlock(d);
	unlock(baser);
	unlock(index);
}
MENDFUNC(3,mov_b_rrm_indexed,(W1 d, R4 baser, R4 index))

MIDFUNC(3,mov_l_mrr_indexed,(R4 baser, R4 index, R4 s))
{
	CLOBBER_MOV;
	baser=readreg(baser,4);
	index=readreg(index,4);
	s=readreg(s,4);

	Dif (baser==s || index==s)
		jit_abort (_T("mov_l_mrr_indexed"));

	raw_mov_l_mrr_indexed(baser,index,s);
	unlock(s);
	unlock(baser);
	unlock(index);
}
MENDFUNC(3,mov_l_mrr_indexed,(R4 baser, R4 index, R4 s))

MIDFUNC(3,mov_w_mrr_indexed,(R4 baser, R4 index, R2 s))
{
	CLOBBER_MOV;
	baser=readreg(baser,4);
	index=readreg(index,4);
	s=readreg(s,2);

	raw_mov_w_mrr_indexed(baser,index,s);
	unlock(s);
	unlock(baser);
	unlock(index);
}
MENDFUNC(3,mov_w_mrr_indexed,(R4 baser, R4 index, R2 s))

MIDFUNC(3,mov_b_mrr_indexed,(R4 baser, R4 index, R1 s))
{
	CLOBBER_MOV;
	s=readreg(s,1);
	baser=readreg(baser,4);
	index=readreg(index,4);

	raw_mov_b_mrr_indexed(baser,index,s);
	unlock(s);
	unlock(baser);
	unlock(index);
}
MENDFUNC(3,mov_b_mrr_indexed,(R4 baser, R4 index, R1 s))

/* Read a long from base+4*index */
MIDFUNC(3,mov_l_rm_indexed,(W4 d, IMM base, R4 index))
{
	int indexreg=index;

	if (isconst(index)) {
		COMPCALL(mov_l_rm)(d,base+4*live.state[index].val);
		return;
	}

	CLOBBER_MOV;
	index=readreg_offset(index,4);
	base+=get_offset(indexreg)*4;
	d=writereg(d,4);

	raw_mov_l_rm_indexed(d,base,index);
	unlock(index);
	unlock(d);
}
MENDFUNC(3,mov_l_rm_indexed,(W4 d, IMM base, R4 index))

/* read the long at the address contained in s+offset and store in d */
MIDFUNC(3,mov_l_rR,(W4 d, R4 s, IMM offset))
{
	if (isconst(s)) {
		COMPCALL(mov_l_rm)(d,live.state[s].val+offset);
		return;
	}
	CLOBBER_MOV;
	s=readreg(s,4);
	d=writereg(d,4);

	raw_mov_l_rR(d,s,offset);
	unlock(d);
	unlock(s);
}
MENDFUNC(3,mov_l_rR,(W4 d, R4 s, IMM offset))

/* read the word at the address contained in s+offset and store in d */
MIDFUNC(3,mov_w_rR,(W2 d, R4 s, IMM offset))
{
	if (isconst(s)) {
		COMPCALL(mov_w_rm)(d,live.state[s].val+offset);
		return;
	}
	CLOBBER_MOV;
	s=readreg(s,4);
	d=writereg(d,2);

	raw_mov_w_rR(d,s,offset);
	unlock(d);
	unlock(s);
}
MENDFUNC(3,mov_w_rR,(W2 d, R4 s, IMM offset))

/* read the word at the address contained in s+offset and store in d */
MIDFUNC(3,mov_b_rR,(W1 d, R4 s, IMM offset))
{
	if (isconst(s)) {
		COMPCALL(mov_b_rm)(d,live.state[s].val+offset);
		return;
	}
	CLOBBER_MOV;
	s=readreg(s,4);
	d=writereg(d,1);

	raw_mov_b_rR(d,s,offset);
	unlock(d);
	unlock(s);
}
MENDFUNC(3,mov_b_rR,(W1 d, R4 s, IMM offset))

/* read the long at the address contained in s+offset and store in d */
MIDFUNC(3,mov_l_brR,(W4 d, R4 s, IMM offset))
{
	int sreg=s;
	if (isconst(s)) {
		COMPCALL(mov_l_rm)(d,live.state[s].val+offset);
		return;
	}
	CLOBBER_MOV;
	s=readreg_offset(s,4);
	offset+=get_offset(sreg);
	d=writereg(d,4);

	raw_mov_l_brR(d,s,offset);
	unlock(d);
	unlock(s);
}
MENDFUNC(3,mov_l_brR,(W4 d, R4 s, IMM offset))

/* read the word at the address contained in s+offset and store in d */
MIDFUNC(3,mov_w_brR,(W2 d, R4 s, IMM offset))
{
	int sreg=s;
	if (isconst(s)) {
		COMPCALL(mov_w_rm)(d,live.state[s].val+offset);
		return;
	}
	CLOBBER_MOV;
	remove_offset(d,-1);
	s=readreg_offset(s,4);
	offset+=get_offset(sreg);
	d=writereg(d,2);

	raw_mov_w_brR(d,s,offset);
	unlock(d);
	unlock(s);
}
MENDFUNC(3,mov_w_brR,(W2 d, R4 s, IMM offset))

/* read the word at the address contained in s+offset and store in d */
MIDFUNC(3,mov_b_brR,(W1 d, R4 s, IMM offset))
{
	int sreg=s;
	if (isconst(s)) {
		COMPCALL(mov_b_rm)(d,live.state[s].val+offset);
		return;
	}
	CLOBBER_MOV;
	remove_offset(d,-1);
	s=readreg_offset(s,4);
	offset+=get_offset(sreg);
	d=writereg(d,1);

	raw_mov_b_brR(d,s,offset);
	unlock(d);
	unlock(s);
}
MENDFUNC(3,mov_b_brR,(W1 d, R4 s, IMM offset))

MIDFUNC(3,mov_l_Ri,(R4 d, IMM i, IMM offset))
{
	int dreg=d;
	if (isconst(d)) {
		COMPCALL(mov_l_mi)(live.state[d].val+offset,i);
		return;
	}

	CLOBBER_MOV;
	d=readreg_offset(d,4);
	offset+=get_offset(dreg);
	raw_mov_l_Ri(d,i,offset);
	unlock(d);
}
MENDFUNC(3,mov_l_Ri,(R4 d, IMM i, IMM offset))

MIDFUNC(3,mov_w_Ri,(R4 d, IMM i, IMM offset))
{
	int dreg=d;
	if (isconst(d)) {
		COMPCALL(mov_w_mi)(live.state[d].val+offset,i);
		return;
	}

	CLOBBER_MOV;
	d=readreg_offset(d,4);
	offset+=get_offset(dreg);
	raw_mov_w_Ri(d,i,offset);
	unlock(d);
}
MENDFUNC(3,mov_w_Ri,(R4 d, IMM i, IMM offset))

MIDFUNC(3,mov_b_Ri,(R4 d, IMM i, IMM offset))
{
	int dreg=d;
	if (isconst(d)) {
		COMPCALL(mov_b_mi)(live.state[d].val+offset,i);
		return;
	}

	CLOBBER_MOV;
	d=readreg_offset(d,4);
	offset+=get_offset(dreg);
	raw_mov_b_Ri(d,i,offset);
	unlock(d);
}
MENDFUNC(3,mov_b_Ri,(R4 d, IMM i, IMM offset))

/* Warning! OFFSET is byte sized only! */
MIDFUNC(3,mov_l_Rr,(R4 d, R4 s, IMM offset))
{
	if (isconst(d)) {
		COMPCALL(mov_l_mr)(live.state[d].val+offset,s);
		return;
	}
	if (isconst(s)) {
		COMPCALL(mov_l_Ri)(d,live.state[s].val,offset);
		return;
	}

	CLOBBER_MOV;
	s=readreg(s,4);
	d=readreg(d,4);

	raw_mov_l_Rr(d,s,offset);
	unlock(d);
	unlock(s);
}
MENDFUNC(3,mov_l_Rr,(R4 d, R4 s, IMM offset))

MIDFUNC(3,mov_w_Rr,(R4 d, R2 s, IMM offset))
{
	if (isconst(d)) {
		COMPCALL(mov_w_mr)(live.state[d].val+offset,s);
		return;
	}
	if (isconst(s)) {
		COMPCALL(mov_w_Ri)(d,(uae_u16)live.state[s].val,offset);
		return;
	}

	CLOBBER_MOV;
	s=readreg(s,2);
	d=readreg(d,4);
	raw_mov_w_Rr(d,s,offset);
	unlock(d);
	unlock(s);
}
MENDFUNC(3,mov_w_Rr,(R4 d, R2 s, IMM offset))

MIDFUNC(3,mov_b_Rr,(R4 d, R1 s, IMM offset))
{
	if (isconst(d)) {
		COMPCALL(mov_b_mr)(live.state[d].val+offset,s);
		return;
	}
	if (isconst(s)) {
		COMPCALL(mov_b_Ri)(d,(uae_u8)live.state[s].val,offset);
		return;
	}

	CLOBBER_MOV;
	s=readreg(s,1);
	d=readreg(d,4);
	raw_mov_b_Rr(d,s,offset);
	unlock(d);
	unlock(s);
}
MENDFUNC(3,mov_b_Rr,(R4 d, R1 s, IMM offset))

MIDFUNC(3,lea_l_brr,(W4 d, R4 s, IMM offset))
{
	if (isconst(s)) {
		COMPCALL(mov_l_ri)(d,live.state[s].val+offset);
		return;
	}
#if USE_OFFSET
	if (d==s) {
		add_offset(d,offset);
		return;
	}
#endif
	CLOBBER_LEA;
	s=readreg(s,4);
	d=writereg(d,4);
	raw_lea_l_brr(d,s,offset);
	unlock(d);
	unlock(s);
}
MENDFUNC(3,lea_l_brr,(W4 d, R4 s, IMM offset))

MIDFUNC(5,lea_l_brr_indexed,(W4 d, R4 s, R4 index, IMM factor, IMM offset))
{
	CLOBBER_LEA;
	s=readreg(s,4);
	index=readreg(index,4);
	d=writereg(d,4);

	raw_lea_l_brr_indexed(d,s,index,factor,offset);
	unlock(d);
	unlock(index);
	unlock(s);
}
MENDFUNC(5,lea_l_brr_indexed,(W4 d, R4 s, R4 index, IMM factor, IMM offset))

/* write d to the long at the address contained in s+offset */
MIDFUNC(3,mov_l_bRr,(R4 d, R4 s, IMM offset))
{
	int dreg=d;
	if (isconst(d)) {
		COMPCALL(mov_l_mr)(live.state[d].val+offset,s);
		return;
	}

	CLOBBER_MOV;
	s=readreg(s,4);
	d=readreg_offset(d,4);
	offset+=get_offset(dreg);

	raw_mov_l_bRr(d,s,offset);
	unlock(d);
	unlock(s);
}
MENDFUNC(3,mov_l_bRr,(R4 d, R4 s, IMM offset))

/* write the word at the address contained in s+offset and store in d */
MIDFUNC(3,mov_w_bRr,(R4 d, R2 s, IMM offset))
{
	int dreg=d;

	if (isconst(d)) {
		COMPCALL(mov_w_mr)(live.state[d].val+offset,s);
		return;
	}

	CLOBBER_MOV;
	s=readreg(s,2);
	d=readreg_offset(d,4);
	offset+=get_offset(dreg);
	raw_mov_w_bRr(d,s,offset);
	unlock(d);
	unlock(s);
}
MENDFUNC(3,mov_w_bRr,(R4 d, R2 s, IMM offset))

MIDFUNC(3,mov_b_bRr,(R4 d, R1 s, IMM offset))
{
	int dreg=d;
	if (isconst(d)) {
		COMPCALL(mov_b_mr)(live.state[d].val+offset,s);
		return;
	}

	CLOBBER_MOV;
	s=readreg(s,1);
	d=readreg_offset(d,4);
	offset+=get_offset(dreg);
	raw_mov_b_bRr(d,s,offset);
	unlock(d);
	unlock(s);
}
MENDFUNC(3,mov_b_bRr,(R4 d, R1 s, IMM offset))

MIDFUNC(1,gen_bswap_32,(RW4 r))
{
	int reg=r;

	if (isconst(r)) {
		uae_u32 oldv=live.state[r].val;
		live.state[r].val=reverse32(oldv);
		return;
	}

	CLOBBER_SW32;
	r=rmw(r,4,4);
	raw_bswap_32(r);
	unlock(r);
}
MENDFUNC(1,gen_bswap_32,(RW4 r))

MIDFUNC(1,gen_bswap_16,(RW2 r))
{
	if (isconst(r)) {
		uae_u32 oldv=live.state[r].val;
		live.state[r].val=((oldv>>8)&0xff) | ((oldv<<8)&0xff00) |
			(oldv&0xffff0000);
		return;
	}

	CLOBBER_SW16;
	r=rmw(r,2,2);

	raw_bswap_16(r);
	unlock(r);
}
MENDFUNC(1,gen_bswap_16,(RW2 r))

MIDFUNC(2,mov_l_rr,(W4 d, R4 s))
{
	int olds;

	if (d==s) { /* How pointless! */
		return;
	}
	if (isconst(s)) {
		COMPCALL(mov_l_ri)(d,live.state[s].val);
		return;
	}
#if USE_ALIAS
	olds=s;
	disassociate(d);
	s=readreg_offset(s,4);
	live.state[d].realreg=s;
	live.state[d].realind=live.nat[s].nholds;
	live.state[d].val=live.state[olds].val;
	live.state[d].validsize=4;
	live.state[d].dirtysize=4;
	set_status(d,DIRTY);

	live.nat[s].holds[live.nat[s].nholds]=d;
	live.nat[s].nholds++;
	log_clobberreg(d);

	/* write_log (_T("JIT: Added %d to nreg %d(%d), now holds %d regs\n"),
	d,s,live.state[d].realind,live.nat[s].nholds); */
	unlock(s);
#else
	CLOBBER_MOV;
	s=readreg(s,4);
	d=writereg(d,4);

	raw_mov_l_rr(d,s);
	unlock(d);
	unlock(s);
#endif
}
MENDFUNC(2,mov_l_rr,(W4 d, R4 s))

MIDFUNC(2,mov_l_mr,(IMM d, R4 s))
{
	if (isconst(s)) {
		COMPCALL(mov_l_mi)(d,live.state[s].val);
		return;
	}
	CLOBBER_MOV;
	s=readreg(s,4);

	raw_mov_l_mr(d,s);
	unlock(s);
}
MENDFUNC(2,mov_l_mr,(IMM d, R4 s))

MIDFUNC(2,mov_w_mr,(IMM d, R2 s))
{
	if (isconst(s)) {
		COMPCALL(mov_w_mi)(d,(uae_u16)live.state[s].val);
		return;
	}
	CLOBBER_MOV;
	s=readreg(s,2);

	raw_mov_w_mr(d,s);
	unlock(s);
}
MENDFUNC(2,mov_w_mr,(IMM d, R2 s))

MIDFUNC(2,mov_w_rm,(W2 d, IMM s))
{
	CLOBBER_MOV;
	d=writereg(d,2);

	raw_mov_w_rm(d,s);
	unlock(d);
}
MENDFUNC(2,mov_w_rm,(W2 d, IMM s))

MIDFUNC(2,mov_b_mr,(IMM d, R1 s))
{
	if (isconst(s)) {
		COMPCALL(mov_b_mi)(d,(uae_u8)live.state[s].val);
		return;
	}

	CLOBBER_MOV;
	s=readreg(s,1);

	raw_mov_b_mr(d,s);
	unlock(s);
}
MENDFUNC(2,mov_b_mr,(IMM d, R1 s))

MIDFUNC(2,mov_b_rm,(W1 d, IMM s))
{
	CLOBBER_MOV;
	d=writereg(d,1);

	raw_mov_b_rm(d,s);
	unlock(d);
}
MENDFUNC(2,mov_b_rm,(W1 d, IMM s))

MIDFUNC(2,mov_l_ri,(W4 d, IMM s))
{
	set_const(d,s);
	return;
}
MENDFUNC(2,mov_l_ri,(W4 d, IMM s))

MIDFUNC(2,mov_w_ri,(W2 d, IMM s))
{
	CLOBBER_MOV;
	d=writereg(d,2);

	raw_mov_w_ri(d,s);
	unlock(d);
}
MENDFUNC(2,mov_w_ri,(W2 d, IMM s))

MIDFUNC(2,mov_b_ri,(W1 d, IMM s))
{
	CLOBBER_MOV;
	d=writereg(d,1);

	raw_mov_b_ri(d,s);
	unlock(d);
}
MENDFUNC(2,mov_b_ri,(W1 d, IMM s))

MIDFUNC(2,add_l_mi,(IMM d, IMM s))
{
	CLOBBER_ADD;
	raw_add_l_mi(d,s) ;
}
MENDFUNC(2,add_l_mi,(IMM d, IMM s))

MIDFUNC(2,add_w_mi,(IMM d, IMM s))
{
	CLOBBER_ADD;
	raw_add_w_mi(d,s) ;
}
MENDFUNC(2,add_w_mi,(IMM d, IMM s))

MIDFUNC(2,add_b_mi,(IMM d, IMM s))
{
	CLOBBER_ADD;
	raw_add_b_mi(d,s) ;
}
MENDFUNC(2,add_b_mi,(IMM d, IMM s))

MIDFUNC(2,test_l_ri,(R4 d, IMM i))
{
	CLOBBER_TEST;
	d=readreg(d,4);

	raw_test_l_ri(d,i);
	unlock(d);
}
MENDFUNC(2,test_l_ri,(R4 d, IMM i))

MIDFUNC(2,test_l_rr,(R4 d, R4 s))
{
	CLOBBER_TEST;
	d=readreg(d,4);
	s=readreg(s,4);

	raw_test_l_rr(d,s);;
	unlock(d);
	unlock(s);
}
MENDFUNC(2,test_l_rr,(R4 d, R4 s))

MIDFUNC(2,test_w_rr,(R2 d, R2 s))
{
	CLOBBER_TEST;
	d=readreg(d,2);
	s=readreg(s,2);

	raw_test_w_rr(d,s);
	unlock(d);
	unlock(s);
}
MENDFUNC(2,test_w_rr,(R2 d, R2 s))

MIDFUNC(2,test_b_rr,(R1 d, R1 s))
{
	CLOBBER_TEST;
	d=readreg(d,1);
	s=readreg(s,1);

	raw_test_b_rr(d,s);
	unlock(d);
	unlock(s);
}
MENDFUNC(2,test_b_rr,(R1 d, R1 s))

MIDFUNC(2,and_l_ri,(RW4 d, IMM i))
{
	if (isconst (d) && ! needflags) {
		live.state[d].val &= i;
		return;
	}

	CLOBBER_AND;
	d=rmw(d,4,4);

	raw_and_l_ri(d,i);
	unlock(d);
}
MENDFUNC(2,and_l_ri,(RW4 d, IMM i))

MIDFUNC(2,and_l,(RW4 d, R4 s))
{
	CLOBBER_AND;
	s=readreg(s,4);
	d=rmw(d,4,4);

	raw_and_l(d,s);
	unlock(d);
	unlock(s);
}
MENDFUNC(2,and_l,(RW4 d, R4 s))

MIDFUNC(2,and_w,(RW2 d, R2 s))
{
	CLOBBER_AND;
	s=readreg(s,2);
	d=rmw(d,2,2);

	raw_and_w(d,s);
	unlock(d);
	unlock(s);
}
MENDFUNC(2,and_w,(RW2 d, R2 s))

MIDFUNC(2,and_b,(RW1 d, R1 s))
{
	CLOBBER_AND;
	s=readreg(s,1);
	d=rmw(d,1,1);

	raw_and_b(d,s);
	unlock(d);
	unlock(s);
}
MENDFUNC(2,and_b,(RW1 d, R1 s))

MIDFUNC(2,or_l_ri,(RW4 d, IMM i))
{
	if (isconst(d) && !needflags) {
		live.state[d].val|=i;
		return;
	}
	CLOBBER_OR;
	d=rmw(d,4,4);

	raw_or_l_ri(d,i);
	unlock(d);
}
MENDFUNC(2,or_l_ri,(RW4 d, IMM i))

MIDFUNC(2,or_l,(RW4 d, R4 s))
{
	if (isconst(d) && isconst(s) && !needflags) {
		live.state[d].val|=live.state[s].val;
		return;
	}
	CLOBBER_OR;
	s=readreg(s,4);
	d=rmw(d,4,4);

	raw_or_l(d,s);
	unlock(d);
	unlock(s);
}
MENDFUNC(2,or_l,(RW4 d, R4 s))

MIDFUNC(2,or_w,(RW2 d, R2 s))
{
	CLOBBER_OR;
	s=readreg(s,2);
	d=rmw(d,2,2);

	raw_or_w(d,s);
	unlock(d);
	unlock(s);
}
MENDFUNC(2,or_w,(RW2 d, R2 s))

MIDFUNC(2,or_b,(RW1 d, R1 s))
{
	CLOBBER_OR;
	s=readreg(s,1);
	d=rmw(d,1,1);

	raw_or_b(d,s);
	unlock(d);
	unlock(s);
}
MENDFUNC(2,or_b,(RW1 d, R1 s))

MIDFUNC(2,adc_l,(RW4 d, R4 s))
{
	CLOBBER_ADC;
	s=readreg(s,4);
	d=rmw(d,4,4);

	raw_adc_l(d,s);

	unlock(d);
	unlock(s);
}
MENDFUNC(2,adc_l,(RW4 d, R4 s))

MIDFUNC(2,adc_w,(RW2 d, R2 s))
{
	CLOBBER_ADC;
	s=readreg(s,2);
	d=rmw(d,2,2);

	raw_adc_w(d,s);
	unlock(d);
	unlock(s);
}
MENDFUNC(2,adc_w,(RW2 d, R2 s))

MIDFUNC(2,adc_b,(RW1 d, R1 s))
{
	CLOBBER_ADC;
	s=readreg(s,1);
	d=rmw(d,1,1);

	raw_adc_b(d,s);
	unlock(d);
	unlock(s);
}
MENDFUNC(2,adc_b,(RW1 d, R1 s))

MIDFUNC(2,add_l,(RW4 d, R4 s))
{
	if (isconst(s)) {
		COMPCALL(add_l_ri)(d,live.state[s].val);
		return;
	}

	CLOBBER_ADD;
	s=readreg(s,4);
	d=rmw(d,4,4);

	raw_add_l(d,s);

	unlock(d);
	unlock(s);
}
MENDFUNC(2,add_l,(RW4 d, R4 s))

MIDFUNC(2,add_w,(RW2 d, R2 s))
{
	if (isconst(s)) {
		COMPCALL(add_w_ri)(d,(uae_u16)live.state[s].val);
		return;
	}

	CLOBBER_ADD;
	s=readreg(s,2);
	d=rmw(d,2,2);

	raw_add_w(d,s);
	unlock(d);
	unlock(s);
}
MENDFUNC(2,add_w,(RW2 d, R2 s))

MIDFUNC(2,add_b,(RW1 d, R1 s))
{
	if (isconst(s)) {
		COMPCALL(add_b_ri)(d,(uae_u8)live.state[s].val);
		return;
	}

	CLOBBER_ADD;
	s=readreg(s,1);
	d=rmw(d,1,1);

	raw_add_b(d,s);
	unlock(d);
	unlock(s);
}
MENDFUNC(2,add_b,(RW1 d, R1 s))

MIDFUNC(2,sub_l_ri,(RW4 d, IMM i))
{
	if (!i && !needflags)
		return;
	if (isconst(d) && !needflags) {
		live.state[d].val-=i;
		return;
	}
#if USE_OFFSET
	if (!needflags) {
		add_offset(d,-(signed)i);
		return;
	}
#endif

	CLOBBER_SUB;
	d=rmw(d,4,4);

	raw_sub_l_ri(d,i);
	unlock(d);
}
MENDFUNC(2,sub_l_ri,(RW4 d, IMM i))

MIDFUNC(2,sub_w_ri,(RW2 d, IMM i))
{
	if (!i && !needflags)
		return;

	CLOBBER_SUB;
	d=rmw(d,2,2);

	raw_sub_w_ri(d,i);
	unlock(d);
}
MENDFUNC(2,sub_w_ri,(RW2 d, IMM i))

MIDFUNC(2,sub_b_ri,(RW1 d, IMM i))
{
	if (!i && !needflags)
		return;

	CLOBBER_SUB;
	d=rmw(d,1,1);

	raw_sub_b_ri(d,i);

	unlock(d);
}
MENDFUNC(2,sub_b_ri,(RW1 d, IMM i))

MIDFUNC(2,add_l_ri,(RW4 d, IMM i))
{
	if (!i && !needflags)
		return;
	if (isconst(d) && !needflags) {
		live.state[d].val+=i;
		return;
	}
#if USE_OFFSET
	if (!needflags) {
		add_offset(d,i);
		return;
	}
#endif
	CLOBBER_ADD;
	d=rmw(d,4,4);
	raw_add_l_ri(d,i);
	unlock(d);
}
MENDFUNC(2,add_l_ri,(RW4 d, IMM i))

MIDFUNC(2,add_w_ri,(RW2 d, IMM i))
{
	if (!i && !needflags)
		return;

	CLOBBER_ADD;
	d=rmw(d,2,2);

	raw_add_w_ri(d,i);
	unlock(d);
}
MENDFUNC(2,add_w_ri,(RW2 d, IMM i))

MIDFUNC(2,add_b_ri,(RW1 d, IMM i))
{
	if (!i && !needflags)
		return;

	CLOBBER_ADD;
	d=rmw(d,1,1);

	raw_add_b_ri(d,i);

	unlock(d);
}
MENDFUNC(2,add_b_ri,(RW1 d, IMM i))

MIDFUNC(2,sbb_l,(RW4 d, R4 s))
{
	CLOBBER_SBB;
	s=readreg(s,4);
	d=rmw(d,4,4);

	raw_sbb_l(d,s);
	unlock(d);
	unlock(s);
}
MENDFUNC(2,sbb_l,(RW4 d, R4 s))

MIDFUNC(2,sbb_w,(RW2 d, R2 s))
{
	CLOBBER_SBB;
	s=readreg(s,2);
	d=rmw(d,2,2);

	raw_sbb_w(d,s);
	unlock(d);
	unlock(s);
}
MENDFUNC(2,sbb_w,(RW2 d, R2 s))

MIDFUNC(2,sbb_b,(RW1 d, R1 s))
{
	CLOBBER_SBB;
	s=readreg(s,1);
	d=rmw(d,1,1);

	raw_sbb_b(d,s);
	unlock(d);
	unlock(s);
}
MENDFUNC(2,sbb_b,(RW1 d, R1 s))

MIDFUNC(2,sub_l,(RW4 d, R4 s))
{
	if (isconst(s)) {
		COMPCALL(sub_l_ri)(d,live.state[s].val);
		return;
	}

	CLOBBER_SUB;
	s=readreg(s,4);
	d=rmw(d,4,4);

	raw_sub_l(d,s);
	unlock(d);
	unlock(s);
}
MENDFUNC(2,sub_l,(RW4 d, R4 s))

MIDFUNC(2,sub_w,(RW2 d, R2 s))
{
	if (isconst(s)) {
		COMPCALL(sub_w_ri)(d,(uae_u16)live.state[s].val);
		return;
	}

	CLOBBER_SUB;
	s=readreg(s,2);
	d=rmw(d,2,2);

	raw_sub_w(d,s);
	unlock(d);
	unlock(s);
}
MENDFUNC(2,sub_w,(RW2 d, R2 s))

MIDFUNC(2,sub_b,(RW1 d, R1 s))
{
	if (isconst(s)) {
		COMPCALL(sub_b_ri)(d,(uae_u8)live.state[s].val);
		return;
	}

	CLOBBER_SUB;
	s=readreg(s,1);
	d=rmw(d,1,1);

	raw_sub_b(d,s);
	unlock(d);
	unlock(s);
}
MENDFUNC(2,sub_b,(RW1 d, R1 s))

MIDFUNC(2,cmp_l,(R4 d, R4 s))
{
	CLOBBER_CMP;
	s=readreg(s,4);
	d=readreg(d,4);

	raw_cmp_l(d,s);
	unlock(d);
	unlock(s);
}
MENDFUNC(2,cmp_l,(R4 d, R4 s))

MIDFUNC(2,cmp_l_ri,(R4 r, IMM i))
{
	CLOBBER_CMP;
	r=readreg(r,4);

	raw_cmp_l_ri(r,i);
	unlock(r);
}
MENDFUNC(2,cmp_l_ri,(R4 r, IMM i))

MIDFUNC(2,cmp_w,(R2 d, R2 s))
{
	CLOBBER_CMP;
	s=readreg(s,2);
	d=readreg(d,2);

	raw_cmp_w(d,s);
	unlock(d);
	unlock(s);
}
MENDFUNC(2,cmp_w,(R2 d, R2 s))

MIDFUNC(2,cmp_b,(R1 d, R1 s))
{
	CLOBBER_CMP;
	s=readreg(s,1);
	d=readreg(d,1);

	raw_cmp_b(d,s);
	unlock(d);
	unlock(s);
}
MENDFUNC(2,cmp_b,(R1 d, R1 s))

MIDFUNC(2,xor_l,(RW4 d, R4 s))
{
	CLOBBER_XOR;
	s=readreg(s,4);
	d=rmw(d,4,4);

	raw_xor_l(d,s);
	unlock(d);
	unlock(s);
}
MENDFUNC(2,xor_l,(RW4 d, R4 s))

MIDFUNC(2,xor_w,(RW2 d, R2 s))
{
	CLOBBER_XOR;
	s=readreg(s,2);
	d=rmw(d,2,2);

	raw_xor_w(d,s);
	unlock(d);
	unlock(s);
}
MENDFUNC(2,xor_w,(RW2 d, R2 s))

MIDFUNC(2,xor_b,(RW1 d, R1 s))
{
	CLOBBER_XOR;
	s=readreg(s,1);
	d=rmw(d,1,1);

	raw_xor_b(d,s);
	unlock(d);
	unlock(s);
}
MENDFUNC(2,xor_b,(RW1 d, R1 s))

MIDFUNC(5,call_r_11,(W4 out1, R4 r, R4 in1, IMM osize, IMM isize))
{
	clobber_flags();
	remove_all_offsets();
	if (osize==4) {
		if (out1!=in1 && out1!=r) {
			COMPCALL(forget_about)(out1);
		}
	}
	else {
		tomem_c(out1);
	}

	in1=readreg_specific(in1,isize,REG_PAR1);
	r=readreg(r,4);
	prepare_for_call_1();  /* This should ensure that there won't be
						   any need for swapping nregs in prepare_for_call_2
						   */
#if USE_NORMAL_CALLING_CONVENTION
	raw_push_l_r(in1);
#endif
	unlock(in1);
	unlock(r);

	prepare_for_call_2();
	raw_call_r(r);

#if USE_NORMAL_CALLING_CONVENTION
	raw_inc_sp(4);
#endif


	live.nat[REG_RESULT].holds[0]=out1;
	live.nat[REG_RESULT].nholds=1;
	live.nat[REG_RESULT].touched=touchcnt++;

	live.state[out1].realreg=REG_RESULT;
	live.state[out1].realind=0;
	live.state[out1].val=0;
	live.state[out1].validsize=osize;
	live.state[out1].dirtysize=osize;
	set_status(out1,DIRTY);
}
MENDFUNC(5,call_r_11,(W4 out1, R4 r, R4 in1, IMM osize, IMM isize))

MIDFUNC(5,call_r_02,(R4 r, R4 in1, R4 in2, IMM isize1, IMM isize2))
{
	clobber_flags();
	remove_all_offsets();
	in1=readreg_specific(in1,isize1,REG_PAR1);
	in2=readreg_specific(in2,isize2,REG_PAR2);
	r=readreg(r,4);
	prepare_for_call_1();  /* This should ensure that there won't be
						   any need for swapping nregs in prepare_for_call_2
						   */
#if USE_NORMAL_CALLING_CONVENTION
	raw_push_l_r(in2);
	raw_push_l_r(in1);
#endif
	unlock(r);
	unlock(in1);
	unlock(in2);
	prepare_for_call_2();
	raw_call_r(r);
#if USE_NORMAL_CALLING_CONVENTION
	raw_inc_sp(8);
#endif
}
MENDFUNC(5,call_r_02,(R4 r, R4 in1, R4 in2, IMM isize1, IMM isize2))

MIDFUNC(1,forget_about,(W4 r))
{
	if (isinreg(r))
		disassociate(r);
	live.state[r].val=0;
	set_status(r,UNDEF);
}
MENDFUNC(1,forget_about,(W4 r))

MIDFUNC(0,nop,(void))
{
	raw_nop();
}
MENDFUNC(0,nop,(void))

MIDFUNC(1,f_forget_about,(FW r))
{
	if (f_isinreg(r))
		f_disassociate(r);
	live.fate[r].status=UNDEF;
}
MENDFUNC(1,f_forget_about,(FW r))

MIDFUNC(1,fmov_pi,(FW r))
{
	r=f_writereg(r);
	raw_fmov_pi(r);
	f_unlock(r);
}
MENDFUNC(1,fmov_pi,(FW r))

MIDFUNC(1,fmov_log10_2,(FW r))
{
	r=f_writereg(r);
	raw_fmov_log10_2(r);
	f_unlock(r);
}
MENDFUNC(1,fmov_log10_2,(FW r))

MIDFUNC(1,fmov_log2_e,(FW r))
{
	r=f_writereg(r);
	raw_fmov_log2_e(r);
	f_unlock(r);
}
MENDFUNC(1,fmov_log2_e,(FW r))

MIDFUNC(1,fmov_loge_2,(FW r))
{
	r=f_writereg(r);
	raw_fmov_loge_2(r);
	f_unlock(r);
}
MENDFUNC(1,fmov_loge_2,(FW r))

MIDFUNC(1,fmov_1,(FW r))
{
	r=f_writereg(r);
	raw_fmov_1(r);
	f_unlock(r);
}
MENDFUNC(1,fmov_1,(FW r))

MIDFUNC(1,fmov_0,(FW r))
{
	r=f_writereg(r);
	raw_fmov_0(r);
	f_unlock(r);
}
MENDFUNC(1,fmov_0,(FW r))

MIDFUNC(2,fmov_rm,(FW r, MEMR m))
{
	r=f_writereg(r);
	raw_fmov_rm(r,m);
	f_unlock(r);
}
MENDFUNC(2,fmov_rm,(FW r, MEMR m))

MIDFUNC(2,fmovi_rm,(FW r, MEMR m))
{
	r=f_writereg(r);
	raw_fmovi_rm(r,m);
	f_unlock(r);
}
MENDFUNC(2,fmovi_rm,(FW r, MEMR m))

MIDFUNC(3,fmovi_mrb,(MEMW m, FR r, double *bounds))
{
	r=f_readreg(r);
	raw_fmovi_mrb(m,r,bounds);
	f_unlock(r);
}
MENDFUNC(3,fmovi_mrb,(MEMW m, FR r, double *bounds))

MIDFUNC(2,fmovs_rm,(FW r, MEMR m))
{
	r=f_writereg(r);
	raw_fmovs_rm(r,m);
	f_unlock(r);
}
MENDFUNC(2,fmovs_rm,(FW r, MEMR m))

MIDFUNC(2,fmovs_mr,(MEMW m, FR r))
{
	r=f_readreg(r);
	raw_fmovs_mr(m,r);
	f_unlock(r);
}
MENDFUNC(2,fmovs_mr,(MEMW m, FR r))

MIDFUNC(1,fcuts_r,(FRW r))
{
	r=f_rmw(r);
	raw_fcuts_r(r);
	f_unlock(r);
}
MENDFUNC(1,fcuts_r,(FRW r))

MIDFUNC(1,fcut_r,(FRW r))
{
	r=f_rmw(r);
	raw_fcut_r(r);
	f_unlock(r);
}
MENDFUNC(1,fcut_r,(FRW r))

MIDFUNC(2,fmov_ext_mr,(MEMW m, FR r))
{
	r=f_readreg(r);
	raw_fmov_ext_mr(m,r);
	f_unlock(r);
}
MENDFUNC(2,fmov_ext_mr,(MEMW m, FR r))

MIDFUNC(2,fmov_mr,(MEMW m, FR r))
{
	r=f_readreg(r);
	raw_fmov_mr(m,r);
	f_unlock(r);
}
MENDFUNC(2,fmov_mr,(MEMW m, FR r))

MIDFUNC(2,fmov_ext_rm,(FW r, MEMR m))
{
	r=f_writereg(r);
	raw_fmov_ext_rm(r,m);
	f_unlock(r);
}
MENDFUNC(2,fmov_ext_rm,(FW r, MEMR m))

MIDFUNC(2,fmov_rr,(FW d, FR s))
{
	if (d==s) { /* How pointless! */
		return;
	}
#if USE_F_ALIAS
	f_disassociate(d);
	s=f_readreg(s);
	live.fate[d].realreg=s;
	live.fate[d].realind=live.fat[s].nholds;
	live.fate[d].status=DIRTY;
	live.fat[s].holds[live.fat[s].nholds]=d;
	live.fat[s].nholds++;
	f_unlock(s);
#else
	s=f_readreg(s);
	d=f_writereg(d);
	raw_fmov_rr(d,s);
	f_unlock(s);
	f_unlock(d);
#endif
}
MENDFUNC(2,fmov_rr,(FW d, FR s))

MIDFUNC(2,fldcw_m_indexed,(R4 index, IMM base))
{
	index=readreg(index,4);

	raw_fldcw_m_indexed(index,base);
	unlock(index);
}
MENDFUNC(2,fldcw_m_indexed,(R4 index, IMM base))

MIDFUNC(1,ftst_r,(FR r))
{
	r=f_readreg(r);
	raw_ftst_r(r);
	f_unlock(r);
}
MENDFUNC(1,ftst_r,(FR r))

MIDFUNC(0,dont_care_fflags,(void))
{
	f_disassociate(FP_RESULT);
}
MENDFUNC(0,dont_care_fflags,(void))

MIDFUNC(2,fsqrt_rr,(FW d, FR s))
{
	s=f_readreg(s);
	d=f_writereg(d);
	raw_fsqrt_rr(d,s);
	f_unlock(s);
	f_unlock(d);
}
MENDFUNC(2,fsqrt_rr,(FW d, FR s))

MIDFUNC(2,fabs_rr,(FW d, FR s))
{
	s=f_readreg(s);
	d=f_writereg(d);
	raw_fabs_rr(d,s);
	f_unlock(s);
	f_unlock(d);
}
MENDFUNC(2,fabs_rr,(FW d, FR s))

MIDFUNC(2,frndint_rr,(FW d, FR s))
{
	s=f_readreg(s);
	d=f_writereg(d);
	raw_frndint_rr(d,s);
	f_unlock(s);
	f_unlock(d);
}
MENDFUNC(2,frndint_rr,(FW d, FR s))

MIDFUNC(2,fgetexp_rr,(FW d, FR s))
{
	s=f_readreg(s);
	d=f_writereg(d);
	raw_fgetexp_rr(d,s);
	f_unlock(s);
	f_unlock(d);
}
MENDFUNC(2,fgetexp_rr,(FW d, FR s))

MIDFUNC(2,fgetman_rr,(FW d, FR s))
{
	s=f_readreg(s);
	d=f_writereg(d);
	raw_fgetman_rr(d,s);
	f_unlock(s);
	f_unlock(d);
}
MENDFUNC(2,fgetman_rr,(FW d, FR s))

MIDFUNC(2,fsin_rr,(FW d, FR s))
{
	s=f_readreg(s);
	d=f_writereg(d);
	raw_fsin_rr(d,s);
	f_unlock(s);
	f_unlock(d);
}
MENDFUNC(2,fsin_rr,(FW d, FR s))

MIDFUNC(2,fcos_rr,(FW d, FR s))
{
	s=f_readreg(s);
	d=f_writereg(d);
	raw_fcos_rr(d,s);
	f_unlock(s);
	f_unlock(d);
}
MENDFUNC(2,fcos_rr,(FW d, FR s))

MIDFUNC(2,ftan_rr,(FW d, FR s))
{
	s=f_readreg(s);
	d=f_writereg(d);
	raw_ftan_rr(d,s);
	f_unlock(s);
	f_unlock(d);
}
MENDFUNC(2,ftan_rr,(FW d, FR s))

MIDFUNC(3,fsincos_rr,(FW d, FW c, FR s))
{
	s=f_readreg(s);  /* s for source */
	d=f_writereg(d); /* d for sine   */
	c=f_writereg(c); /* c for cosine */
	raw_fsincos_rr(d,c,s);
	f_unlock(s);
	f_unlock(d);
	f_unlock(c);
}
MENDFUNC(3,fsincos_rr,(FW d, FW c, FR s))

MIDFUNC(2,fscale_rr,(FRW d, FR s))
{
	s=f_readreg(s);
	d=f_rmw(d);
	raw_fscale_rr(d,s);
	f_unlock(s);
	f_unlock(d);
}
MENDFUNC(2,fscale_rr,(FRW d, FR s))

MIDFUNC(2,ftwotox_rr,(FW d, FR s))
{
	s=f_readreg(s);
	d=f_writereg(d);
	raw_ftwotox_rr(d,s);
	f_unlock(s);
	f_unlock(d);
}
MENDFUNC(2,ftwotox_rr,(FW d, FR s))

MIDFUNC(2,fetox_rr,(FW d, FR s))
{
	s=f_readreg(s);
	d=f_writereg(d);
	raw_fetox_rr(d,s);
	f_unlock(s);
	f_unlock(d);
}
MENDFUNC(2,fetox_rr,(FW d, FR s))

MIDFUNC(2,fetoxM1_rr,(FW d, FR s))
{
	s=f_readreg(s);
	d=f_writereg(d);
	raw_fetoxM1_rr(d,s);
	f_unlock(s);
	f_unlock(d);
}
MENDFUNC(2,fetoxM1_rr,(FW d, FR s))

MIDFUNC(2,ftentox_rr,(FW d, FR s))
{
	s=f_readreg(s);
	d=f_writereg(d);
	raw_ftentox_rr(d,s);
	f_unlock(s);
	f_unlock(d);
}
MENDFUNC(2,ftentox_rr,(FW d, FR s))

MIDFUNC(2,flog2_rr,(FW d, FR s))
{
	s=f_readreg(s);
	d=f_writereg(d);
	raw_flog2_rr(d,s);
	f_unlock(s);
	f_unlock(d);
}
MENDFUNC(2,flog2_rr,(FW d, FR s))

MIDFUNC(2,flogN_rr,(FW d, FR s))
{
	s=f_readreg(s);
	d=f_writereg(d);
	raw_flogN_rr(d,s);
	f_unlock(s);
	f_unlock(d);
}
MENDFUNC(2,flogN_rr,(FW d, FR s))

MIDFUNC(2,flogNP1_rr,(FW d, FR s))
{
	s=f_readreg(s);
	d=f_writereg(d);
	raw_flogNP1_rr(d,s);
	f_unlock(s);
	f_unlock(d);
}
MENDFUNC(2,flogNP1_rr,(FW d, FR s))

MIDFUNC(2,flog10_rr,(FW d, FR s))
{
	s=f_readreg(s);
	d=f_writereg(d);
	raw_flog10_rr(d,s);
	f_unlock(s);
	f_unlock(d);
}
MENDFUNC(2,flog10_rr,(FW d, FR s))

MIDFUNC(2,fasin_rr,(FW d, FR s))
{
	s=f_readreg(s);
	d=f_writereg(d);
	raw_fasin_rr(d,s);
	f_unlock(s);
	f_unlock(d);
}
MENDFUNC(2,fasin_rr,(FW d, FR s))

MIDFUNC(2,facos_rr,(FW d, FR s))
{
	s=f_readreg(s);
	d=f_writereg(d);
	raw_facos_rr(d,s);
	f_unlock(s);
	f_unlock(d);
}
MENDFUNC(2,facos_rr,(FW d, FR s))

MIDFUNC(2,fatan_rr,(FW d, FR s))
{
	s=f_readreg(s);
	d=f_writereg(d);
	raw_fatan_rr(d,s);
	f_unlock(s);
	f_unlock(d);
}
MENDFUNC(2,fatan_rr,(FW d, FR s))

MIDFUNC(2,fatanh_rr,(FW d, FR s))
{
	s=f_readreg(s);
	d=f_writereg(d);
	raw_fatanh_rr(d,s);
	f_unlock(s);
	f_unlock(d);
}
MENDFUNC(2,fatanh_rr,(FW d, FR s))

MIDFUNC(2,fsinh_rr,(FW d, FR s))
{
	s=f_readreg(s);
	d=f_writereg(d);
	raw_fsinh_rr(d,s);
	f_unlock(s);
	f_unlock(d);
}
MENDFUNC(2,fsinh_rr,(FW d, FR s))

MIDFUNC(2,fcosh_rr,(FW d, FR s))
{
	s=f_readreg(s);
	d=f_writereg(d);
	raw_fcosh_rr(d,s);
	f_unlock(s);
	f_unlock(d);
}
MENDFUNC(2,fcosh_rr,(FW d, FR s))

MIDFUNC(2,ftanh_rr,(FW d, FR s))
{
	s=f_readreg(s);
	d=f_writereg(d);
	raw_ftanh_rr(d,s);
	f_unlock(s);
	f_unlock(d);
}
MENDFUNC(2,ftanh_rr,(FW d, FR s))

MIDFUNC(2,fneg_rr,(FW d, FR s))
{
	s=f_readreg(s);
	d=f_writereg(d);
	raw_fneg_rr(d,s);
	f_unlock(s);
	f_unlock(d);
}
MENDFUNC(2,fneg_rr,(FW d, FR s))

MIDFUNC(2,fadd_rr,(FRW d, FR s))
{
	s=f_readreg(s);
	d=f_rmw(d);
	raw_fadd_rr(d,s);
	f_unlock(s);
	f_unlock(d);
}
MENDFUNC(2,fadd_rr,(FRW d, FR s))

MIDFUNC(2,fsub_rr,(FRW d, FR s))
{
	s=f_readreg(s);
	d=f_rmw(d);
	raw_fsub_rr(d,s);
	f_unlock(s);
	f_unlock(d);
}
MENDFUNC(2,fsub_rr,(FRW d, FR s))

MIDFUNC(2,fcmp_rr,(FR d, FR s))
{
	d=f_readreg(d);
	s=f_readreg(s);
	raw_fcmp_rr(d,s);
	f_unlock(s);
	f_unlock(d);
}
MENDFUNC(2,fcmp_rr,(FR d, FR s))

MIDFUNC(2,fdiv_rr,(FRW d, FR s))
{
	s=f_readreg(s);
	d=f_rmw(d);
	raw_fdiv_rr(d,s);
	f_unlock(s);
	f_unlock(d);
}
MENDFUNC(2,fdiv_rr,(FRW d, FR s))

MIDFUNC(2,frem_rr,(FRW d, FR s))
{
	s=f_readreg(s);
	d=f_rmw(d);
	raw_frem_rr(d,s);
	f_unlock(s);
	f_unlock(d);
}
MENDFUNC(2,frem_rr,(FRW d, FR s))

MIDFUNC(2,frem1_rr,(FRW d, FR s))
{
	s=f_readreg(s);
	d=f_rmw(d);
	raw_frem1_rr(d,s);
	f_unlock(s);
	f_unlock(d);
}
MENDFUNC(2,frem1_rr,(FRW d, FR s))

MIDFUNC(2,fmul_rr,(FRW d, FR s))
{
	s=f_readreg(s);
	d=f_rmw(d);
	raw_fmul_rr(d,s);
	f_unlock(s);
	f_unlock(d);
}
MENDFUNC(2,fmul_rr,(FRW d, FR s))
