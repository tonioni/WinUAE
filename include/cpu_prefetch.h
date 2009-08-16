
STATIC_INLINE uae_u32 get_word_prefetch (int o)
{
    uae_u32 v = regs.irc;
    regs.irc = get_wordi (m68k_getpc () + o);
    return v;
}
STATIC_INLINE uae_u32 get_long_prefetch (int o)
{
    uae_u32 v = get_word_prefetch (o) << 16;
    v |= get_word_prefetch (o + 2);
    return v;
}

#ifdef CPUEMU_13

STATIC_INLINE uae_u32 mem_access_delay_long_read_020 (uaecptr addr)
{
    switch (ce_banktype[(addr >> 16) & 0xff])
    {
	case CE_MEMBANK_CHIP:
	return wait_cpu_cycle_read (addr, -1);
	case CE_MEMBANK_FAST:
	do_cycles_ce (2 * CYCLE_UNIT / 2);
	break;
	case CE_MEMBANK_FAST16BIT:
	do_cycles_ce (4 * CYCLE_UNIT / 2);
	break;
    }
    return get_long (addr);
}
STATIC_INLINE uae_u32 mem_access_delay_longi_read_020 (uaecptr addr)
{
    switch (ce_banktype[(addr >> 16) & 0xff])
    {
	case CE_MEMBANK_CHIP:
	return wait_cpu_cycle_read (addr, -1);
	case CE_MEMBANK_FAST:
	case CE_MEMBANK_FAST16BIT:
	do_cycles_ce (2 * CYCLE_UNIT / 2);
	break;
    }
    return get_longi (addr);
}

STATIC_INLINE uae_u32 mem_access_delay_word_read_020 (uaecptr addr)
{
    switch (ce_banktype[(addr >> 16) & 0xff])
    {
	case CE_MEMBANK_CHIP:
	return wait_cpu_cycle_read (addr, 1);
	case CE_MEMBANK_FAST:
	case CE_MEMBANK_FAST16BIT:
	do_cycles_ce (2 * CYCLE_UNIT / 2);
	break;
    }
    return get_word (addr);
}
STATIC_INLINE uae_u32 mem_access_delay_wordi_read_020 (uaecptr addr)
{
    switch (ce_banktype[(addr >> 16) & 0xff])
    {
	case CE_MEMBANK_CHIP:
	return wait_cpu_cycle_read (addr, 1);
	case CE_MEMBANK_FAST:
	case CE_MEMBANK_FAST16BIT:
	do_cycles_ce (2 * CYCLE_UNIT / 2);
	break;
    }
    return get_wordi (addr);
}

STATIC_INLINE uae_u32 mem_access_delay_byte_read_020 (uaecptr addr)
{
    switch (ce_banktype[(addr >> 16) & 0xff])
    {
	case CE_MEMBANK_CHIP:
	return wait_cpu_cycle_read (addr, 0);
	case CE_MEMBANK_FAST:
	case CE_MEMBANK_FAST16BIT:
	do_cycles_ce (2 * CYCLE_UNIT / 2);
	break;
	
    }
    return get_byte (addr);
}
STATIC_INLINE void mem_access_delay_byte_write_020 (uaecptr addr, uae_u32 v)
{
    switch (ce_banktype[(addr >> 16) & 0xff])
    {
	case CE_MEMBANK_CHIP:
	wait_cpu_cycle_write (addr, 0, v);
	return;
	case CE_MEMBANK_FAST:
	case CE_MEMBANK_FAST16BIT:
	do_cycles_ce (2 * CYCLE_UNIT / 2);
	break;
    }
    put_byte (addr, v);
}
STATIC_INLINE void mem_access_delay_word_write_020 (uaecptr addr, uae_u32 v)
{
    switch (ce_banktype[(addr >> 16) & 0xff])
    {
	case CE_MEMBANK_CHIP:
	wait_cpu_cycle_write (addr, 1, v);
	return;
	break;
	case CE_MEMBANK_FAST:
	case CE_MEMBANK_FAST16BIT:
	do_cycles_ce (2 * CYCLE_UNIT / 2);
	break;
    }
    put_word (addr, v);
}
STATIC_INLINE void mem_access_delay_long_write_020 (uaecptr addr, uae_u32 v)
{
    switch (ce_banktype[(addr >> 16) & 0xff])
    {
	case CE_MEMBANK_CHIP:
	wait_cpu_cycle_write (addr, -1, v);
	return;
	break;
	case CE_MEMBANK_FAST:
	case CE_MEMBANK_FAST16BIT:
	do_cycles_ce (2 * CYCLE_UNIT / 2);
	break;
    }
    put_long (addr, v);
}

STATIC_INLINE uae_u32 get_long_ce_020 (uaecptr addr)
{
    return mem_access_delay_long_read_020 (addr);
}
STATIC_INLINE uae_u32 get_word_ce_020 (uaecptr addr)
{
    return mem_access_delay_word_read_020 (addr);
}
STATIC_INLINE uae_u32 get_byte_ce_020 (uaecptr addr)
{
    return mem_access_delay_byte_read_020 (addr);
}

STATIC_INLINE void put_long_ce020 (uaecptr addr, uae_u32 v)
{
    mem_access_delay_long_write_020 (addr, v);
}
STATIC_INLINE void put_word_ce020 (uaecptr addr, uae_u16 v)
{
    mem_access_delay_word_write_020 (addr, v);
}
STATIC_INLINE void put_byte_ce020 (uaecptr addr, uae_u8 v)
{
    mem_access_delay_byte_write_020 (addr, v);
}

STATIC_INLINE void fill_cache020 (uae_u32 pc)
{
    regs.prefetch020pc = pc;
    if ((pc >= regs.prefetch020ptr && pc - regs.prefetch020ptr < 256) && (regs.cacr & 1)) {
	regs.prefetch020 = get_long (pc);
	do_cycles_ce (1 * CYCLE_UNIT);
    } else {
	if (!(regs.cacr & 2))
	    regs.prefetch020ptr = pc;
	regs.prefetch020 = mem_access_delay_long_read_020 (pc);
    }
}

STATIC_INLINE uae_u32 get_word_ce020_prefetch (int o)
{
    uae_u32 pc = m68k_getpc () + o;
    if (pc == regs.prefetch020pc)
	return regs.prefetch020 >> 16;
    if (pc == regs.prefetch020pc + 2)
	return regs.prefetch020 & 0xffff;
    fill_cache020 (pc);
    return regs.prefetch020 >> 16;
}

STATIC_INLINE uae_u32 get_long_ce020_prefetch (int o)
{
    uae_u32 pc = m68k_getpc () + o;
    if (pc == regs.prefetch020pc)
	return regs.prefetch020;
    fill_cache020 (pc);
    return regs.prefetch020;
}

STATIC_INLINE uae_u32 next_iword_020ce (void)
{
    uae_u32 r = get_word_ce020_prefetch (0);
    m68k_incpc (2);
    return r;
}
STATIC_INLINE uae_u32 next_ilong_020ce (void)
{
    uae_u32 r = get_long_ce020_prefetch (0);
    m68k_incpc (4);
    return r;
}
#endif

#ifdef CPUEMU_12

STATIC_INLINE uae_u32 mem_access_delay_word_read (uaecptr addr)
{
    switch (ce_banktype[(addr >> 16) & 0xff])
    {
	case CE_MEMBANK_CHIP:
	return wait_cpu_cycle_read (addr, 1);
	case CE_MEMBANK_FAST:
	case CE_MEMBANK_FAST16BIT:
	do_cycles_ce (4 * CYCLE_UNIT / 2);
	break;
    }
    return get_word (addr);
}
STATIC_INLINE uae_u32 mem_access_delay_wordi_read (uaecptr addr)
{
    switch (ce_banktype[(addr >> 16) & 0xff])
    {
	case CE_MEMBANK_CHIP:
	return wait_cpu_cycle_read (addr, 1);
	case CE_MEMBANK_FAST:
	case CE_MEMBANK_FAST16BIT:
	do_cycles_ce (4 * CYCLE_UNIT / 2);
	break;
    }
    return get_wordi (addr);
}

STATIC_INLINE uae_u32 mem_access_delay_byte_read (uaecptr addr)
{
    switch (ce_banktype[(addr >> 16) & 0xff])
    {
	case CE_MEMBANK_CHIP:
	return wait_cpu_cycle_read (addr, 0);
	case CE_MEMBANK_FAST:
	case CE_MEMBANK_FAST16BIT:
	do_cycles_ce (4 * CYCLE_UNIT / 2);
	break;
	
    }
    return get_byte (addr);
}
STATIC_INLINE void mem_access_delay_byte_write (uaecptr addr, uae_u32 v)
{
    switch (ce_banktype[(addr >> 16) & 0xff])
    {
	case CE_MEMBANK_CHIP:
	wait_cpu_cycle_write (addr, 0, v);
	return;
	case CE_MEMBANK_FAST:
	case CE_MEMBANK_FAST16BIT:
	do_cycles_ce (4 * CYCLE_UNIT / 2);
	break;
    }
    put_byte (addr, v);
}
STATIC_INLINE void mem_access_delay_word_write (uaecptr addr, uae_u32 v)
{
    switch (ce_banktype[(addr >> 16) & 0xff])
    {
	case CE_MEMBANK_CHIP:
	wait_cpu_cycle_write (addr, 1, v);
	return;
	break;
	case CE_MEMBANK_FAST:
	case CE_MEMBANK_FAST16BIT:
	do_cycles_ce (4 * CYCLE_UNIT / 2);
	break;
    }
    put_word (addr, v);
}

STATIC_INLINE uae_u32 get_word_ce (uaecptr addr)
{
    return mem_access_delay_word_read (addr);
}
STATIC_INLINE uae_u32 get_wordi_ce (uaecptr addr)
{
    return mem_access_delay_wordi_read (addr);
}

STATIC_INLINE uae_u32 get_byte_ce (uaecptr addr)
{
    return mem_access_delay_byte_read (addr);
}

STATIC_INLINE uae_u32 get_word_ce_prefetch (int o)
{
    uae_u32 v = regs.irc;
    regs.irc = get_wordi_ce (m68k_getpc () + o);
    return v;
}

STATIC_INLINE void put_word_ce (uaecptr addr, uae_u16 v)
{
    mem_access_delay_word_write (addr, v);
}

STATIC_INLINE void put_byte_ce (uaecptr addr, uae_u8 v)
{
    mem_access_delay_byte_write (addr, v);
}

STATIC_INLINE void m68k_do_rts_ce (void)
{
    uaecptr pc;
    pc = get_word_ce (m68k_areg (regs, 7)) << 16;
    pc |= get_word_ce (m68k_areg (regs, 7) + 2);
    m68k_areg (regs, 7) += 4;
    if (pc & 1)
	exception3 (0x4e75, m68k_getpc (), pc);
    else
	m68k_setpc (pc);
}

STATIC_INLINE void m68k_do_bsr_ce (uaecptr oldpc, uae_s32 offset)
{
    m68k_areg (regs, 7) -= 4;
    put_word_ce (m68k_areg (regs, 7), oldpc >> 16);
    put_word_ce (m68k_areg (regs, 7) + 2, oldpc);
    m68k_incpc (offset);
}

STATIC_INLINE void m68k_do_jsr_ce (uaecptr oldpc, uaecptr dest)
{
    m68k_areg (regs, 7) -= 4;
    put_word_ce (m68k_areg (regs, 7), oldpc >> 16);
    put_word_ce (m68k_areg (regs, 7) + 2, oldpc);
    m68k_setpc (dest);
}
#endif