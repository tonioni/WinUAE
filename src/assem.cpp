
static const TCHAR *fpsizes[] = {
	_T("L"),
	_T("S"),
	_T("X"),
	_T("P"),
	_T("W"),
	_T("D"),
	_T("B"),
	_T("P")
};
static const int fpsizeconv[] = {
	sz_long,
	sz_single,
	sz_extended,
	sz_packed,
	sz_word,
	sz_double,
	sz_byte,
	sz_packed
};
static const int datasizes[] = {
	1,
	2,
	4,
	4,
	8,
	12,
	12
};

static void showea_val(TCHAR *buffer, uae_u16 opcode, uaecptr addr, int size)
{
	struct mnemolookup *lookup;
	instr *table = &table68k[opcode];

	if (addr >= 0xe90000 && addr < 0xf00000)
		goto skip;
	if (addr >= 0xdff000 && addr < 0xe00000)
		goto skip;

	for (lookup = lookuptab; lookup->mnemo != table->mnemo; lookup++)
		;
	if (!(lookup->flags & 1))
		goto skip;
	buffer += _tcslen(buffer);
	if (debug_safe_addr(addr, datasizes[size])) {
		bool cached = false;
		switch (size)
		{
			case sz_byte:
			{
				uae_u8 v = get_byte_cache_debug(addr, &cached);
				uae_u8 v2 = v;
				if (cached)
					v2 = get_byte_debug(addr);
				if (v != v2) {
					_stprintf(buffer, _T(" [%02x:%02x]"), v, v2);
				} else {
					_stprintf(buffer, _T(" [%s%02x]"), cached ? _T("*") : _T(""), v);
				}
			}
			break;
			case sz_word:
			{
				uae_u16 v = get_word_cache_debug(addr, &cached);
				uae_u16 v2 = v;
				if (cached)
					v2 = get_word_debug(addr);
				if (v != v2) {
					_stprintf(buffer, _T(" [%04x:%04x]"), v, v2);
				} else {
					_stprintf(buffer, _T(" [%s%04x]"), cached ? _T("*") : _T(""), v);
				}
			}
			break;
			case sz_long:
			{
				uae_u32 v = get_long_cache_debug(addr, &cached);
				uae_u32 v2 = v;
				if (cached)
					v2 = get_long_debug(addr);
				if (v != v2) {
					_stprintf(buffer, _T(" [%08x:%08x]"), v, v2);
				} else {
					_stprintf(buffer, _T(" [%s%08x]"), cached ? _T("*") : _T(""), v);
				}
			}
			break;
			case sz_single:
			{
				fpdata fp;
				fpp_to_single(&fp, get_long_debug(addr));
				_stprintf(buffer, _T("[%s]"), fpp_print(&fp, 0));
			}
			break;
			case sz_double:
			{
				fpdata fp;
				fpp_to_double(&fp, get_long_debug(addr), get_long_debug(addr + 4));
				_stprintf(buffer, _T("[%s]"), fpp_print(&fp, 0));
			}
			break;
			case sz_extended:
			{
				fpdata fp;
				fpp_to_exten(&fp, get_long_debug(addr), get_long_debug(addr + 4), get_long_debug(addr + 8));
				_stprintf(buffer, _T("[%s]"), fpp_print(&fp, 0));
				break;
			}
			case sz_packed:
				_stprintf(buffer, _T("[%08x%08x%08x]"), get_long_debug(addr), get_long_debug(addr + 4), get_long_debug(addr + 8));
				break;
		}
	}
skip:
	for (int i = 0; i < size; i++) {
		TCHAR name[256];
		if (debugmem_get_symbol(addr + i, name, sizeof(name) / sizeof(TCHAR))) {
			_stprintf(buffer + _tcslen(buffer), _T(" %s"), name);
		}
	}
}

static uaecptr ShowEA_disp(uaecptr *pcp, uaecptr base, TCHAR *buffer, const TCHAR *name)
{
	uaecptr addr;
	uae_u16 dp;
	int r;
	uae_u32 dispreg;
	uaecptr pc = *pcp;
	TCHAR mult[20];

	dp = get_iword_debug(pc);
	pc += 2;

	r = (dp & 0x7000) >> 12; // REGISTER

	dispreg = dp & 0x8000 ? m68k_areg(regs, r) : m68k_dreg(regs, r);
	if (!(dp & 0x800)) { // W/L
		dispreg = (uae_s32)(uae_s16)(dispreg);
	}

	if (currprefs.cpu_model >= 68020) {
		dispreg <<= (dp >> 9) & 3; // SCALE
	}

	int m = 1 << ((dp >> 9) & 3);
	mult[0] = 0;
	if (m > 1) {
		_stprintf(mult, _T("*%d"), m);
	}

	buffer[0] = 0;
	if ((dp & 0x100) && currprefs.cpu_model >= 68020) {
		TCHAR dr[20];
		// Full format extension (68020+)
		uae_s32 outer = 0, disp = 0;
		if (dp & 0x80) { // BS (base register suppress)
			base = 0;
			name = NULL;
		}
		_stprintf(dr, _T("%c%d.%c"), dp & 0x8000 ? 'A' : 'D', (int)r, dp & 0x800 ? 'L' : 'W');
		if (dp & 0x40) { // IS (index suppress)
			dispreg = 0;
			dr[0] = 0;
		}

		_tcscpy(buffer, _T("("));
		TCHAR *p = buffer + _tcslen(buffer);

		if (dp & 3) {
			// indirect
			_stprintf(p, _T("["));
			p += _tcslen(p);
		} else {
			// (an,dn,word/long)
			if (name) {
				_stprintf(p, _T("%s,"), name);
				p += _tcslen(p);
			}
			if (dr[0]) {
				_stprintf(p, _T("%s%s,"), dr, mult);
				p += _tcslen(p);
			}
		}

		if ((dp & 0x30) == 0x20) { // BD SIZE = 2 (WORD)
			disp = (uae_s32)(uae_s16)get_iword_debug(pc);
			_stprintf(p, _T("$%04x,"), (uae_s16)disp);
			p += _tcslen(p);
			pc += 2;
			base += disp;
		} else if ((dp & 0x30) == 0x30) { // BD SIZE = 3 (LONG)
			disp = get_ilong_debug(pc);
			_stprintf(p, _T("$%08x,"), disp);
			p += _tcslen(p);
			pc += 4;
			base += disp;
		}

		if (dp & 3) {
			if (name) {
				_stprintf(p, _T("%s,"), name);
				p += _tcslen(p);
			}

			if (!(dp & 0x04)) {
				if (dr[0]) {
					_stprintf(p, _T("%s%s,"), dr, mult);
					p += _tcslen(p);
				}
			}

			if (p[-1] == ',')
				p--;
			_stprintf(p, _T("],"));
			p += _tcslen(p);

			if ((dp & 0x04)) {
				if (dr[0]) {
					_stprintf(p, _T("%s%s,"), dr, mult);
					p += _tcslen(p);
				}
			}

		}

		if ((dp & 0x03) == 0x02) {
			outer = (uae_s32)(uae_s16)get_iword_debug(pc);
			_stprintf(p, _T("$%04x,"), (uae_s16)outer);
			p += _tcslen(p);
			pc += 2;
		} else 	if ((dp & 0x03) == 0x03) {
			outer = get_ilong_debug(pc);
			_stprintf(p, _T("$%08x,"), outer);
			p += _tcslen(p);
			pc += 4;
		}

		if (p[-1] == ',')
			p--;
		_stprintf(p, _T(")"));
		p += _tcslen(p);

		if ((dp & 0x4) == 0)
			base += dispreg;
		if (dp & 0x3)
			base = get_long_debug(base);
		if (dp & 0x4)
			base += dispreg;

		addr = base + outer;

		_stprintf(p, _T(" == $%08x"), addr);
		p += _tcslen(p);

	} else {
		// Brief format extension
		TCHAR regstr[20];
		uae_s8 disp8 = dp & 0xFF;

		regstr[0] = 0;
		_stprintf(regstr, _T(",%c%d.%c"), dp & 0x8000 ? 'A' : 'D', (int)r, dp & 0x800 ? 'L' : 'W');
		addr = base + (uae_s32)((uae_s8)disp8) + dispreg;
		_stprintf(buffer, _T("(%s%s%s,$%02x) == $%08x"), name, regstr, mult, disp8, addr);
		if (dp & 0x100) {
			_tcscat(buffer, _T(" (68020+)"));
		}
	}

	*pcp = pc;
	return addr;
}

uaecptr ShowEA (void *f, uaecptr pc, uae_u16 opcode, int reg, amodes mode, wordsizes size, TCHAR *buf, uae_u32 *eaddr, int safemode)
{
	uaecptr addr = pc;
	uae_s16 disp16;
	uae_s32 offset = 0;
	TCHAR buffer[80];

	switch (mode){
	case Dreg:
		_stprintf (buffer, _T("D%d"), reg);
		break;
	case Areg:
		_stprintf (buffer, _T("A%d"), reg);
		break;
	case Aind:
		_stprintf (buffer, _T("(A%d)"), reg);
		addr = regs.regs[reg + 8];
		showea_val(buffer, opcode, addr, size);
		break;
	case Aipi:
		_stprintf (buffer, _T("(A%d)+"), reg);
		addr = regs.regs[reg + 8];
		showea_val(buffer, opcode, addr, size);
		break;
	case Apdi:
		_stprintf (buffer, _T("-(A%d)"), reg);
		addr = regs.regs[reg + 8];
		showea_val(buffer, opcode, addr - datasizes[size], size);
		break;
	case Ad16:
		{
			TCHAR offtxt[8];
			disp16 = get_iword_debug (pc); pc += 2;
			if (disp16 < 0)
				_stprintf (offtxt, _T("-$%04x"), -disp16);
			else
				_stprintf (offtxt, _T("$%04x"), disp16);
			addr = m68k_areg (regs, reg) + disp16;
			_stprintf (buffer, _T("(A%d,%s) == $%08x"), reg, offtxt, addr);
			showea_val(buffer, opcode, addr, size);
		}
		break;
	case Ad8r:
		{
			TCHAR name[10];
			_stprintf(name, _T("A%d"), reg);
			addr = ShowEA_disp(&pc, m68k_areg(regs, reg), buffer, name);
			showea_val(buffer, opcode, addr, size);
		}
		break;
	case PC16:
		disp16 = get_iword_debug (pc); pc += 2;
		addr += (uae_s16)disp16;
		_stprintf (buffer, _T("(PC,$%04x) == $%08x"), disp16 & 0xffff, addr);
		showea_val(buffer, opcode, addr, size);
		break;
	case PC8r:
		{
			addr = ShowEA_disp(&pc, addr, buffer, _T("PC"));
			showea_val(buffer, opcode, addr, size);
		}
		break;
	case absw:
		addr = (uae_s32)(uae_s16)get_iword_debug (pc);
		_stprintf (buffer, _T("$%04x"), (uae_u16)addr);
		pc += 2;
		showea_val(buffer, opcode, addr, size);
		break;
	case absl:
		addr = get_ilong_debug (pc);
		_stprintf (buffer, _T("$%08x"), addr);
		pc += 4;
		showea_val(buffer, opcode, addr, size);
		break;
	case imm:
		switch (size){
		case sz_byte:
			_stprintf (buffer, _T("#$%02x"), (get_iword_debug (pc) & 0xff));
			pc += 2;
			break;
		case sz_word:
			_stprintf (buffer, _T("#$%04x"), (get_iword_debug (pc) & 0xffff));
			pc += 2;
			break;
		case sz_long:
			_stprintf(buffer, _T("#$%08x"), (get_ilong_debug(pc)));
			pc += 4;
			break;
		case sz_single:
			{
				fpdata fp;
				fpp_to_single(&fp, get_ilong_debug(pc));
				_stprintf(buffer, _T("#%s"), fpp_print(&fp, 0));
				pc += 4;
			}
			break;
		case sz_double:
			{
				fpdata fp;
				fpp_to_double(&fp, get_ilong_debug(pc), get_ilong_debug(pc + 4));
				_stprintf(buffer, _T("#%s"), fpp_print(&fp, 0));
				pc += 8;
			}
			break;
		case sz_extended:
		{
			fpdata fp;
			fpp_to_exten(&fp, get_ilong_debug(pc), get_ilong_debug(pc + 4), get_ilong_debug(pc + 8));
			_stprintf(buffer, _T("#%s"), fpp_print(&fp, 0));
			pc += 12;
			break;
		}
		case sz_packed:
			_stprintf(buffer, _T("#$%08x%08x%08x"), get_ilong_debug(pc), get_ilong_debug(pc + 4), get_ilong_debug(pc + 8));
			pc += 12;
			break;
		default:
			break;
		}
		break;
	case imm0:
		offset = (uae_s32)(uae_s8)get_iword_debug (pc);
		_stprintf (buffer, _T("#$%02x"), (uae_u32)(offset & 0xff));
		addr = pc + 2 + offset;
		if ((opcode & 0xf000) == 0x6000) {
			showea_val(buffer, opcode, addr, 1);
		}
		pc += 2;
		break;
	case imm1:
		offset = (uae_s32)(uae_s16)get_iword_debug (pc);
		buffer[0] = 0;
		_stprintf (buffer, _T("#$%04x"), (uae_u32)(offset & 0xffff));
		addr = pc + offset;
		if ((opcode & 0xf000) == 0x6000) {
			showea_val(buffer, opcode, addr, 2);
		}
		pc += 2;
		break;
	case imm2:
		offset = (uae_s32)get_ilong_debug (pc);
		_stprintf (buffer, _T("#$%08x"), (uae_u32)offset);
		addr = pc + offset;
		if ((opcode & 0xf000) == 0x6000) {
			showea_val(buffer, opcode, addr, 4);
		}
		pc += 4;
		break;
	case immi:
		offset = (uae_s32)(uae_s8)(reg & 0xff);
		_stprintf (buffer, _T("#$%02x"), (uae_u8)offset);
		addr = pc + offset;
		break;
	default:
		break;
	}
	if (buf == NULL)
		f_out (f, _T("%s"), buffer);
	else
		_tcscat (buf, buffer);
	if (eaddr)
		*eaddr = addr;
	return pc;
}
