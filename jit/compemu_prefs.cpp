/********************************************************************
 * Preferences handling. This is just a convenient place to put it  *
 ********************************************************************/
extern bool have_done_picasso;

bool check_prefs_changed_comp (void)
{
	bool changed = 0;
	static int cachesize_prev, comptrust_prev;
	static bool canbang_prev;

	if (currprefs.comptrustbyte != changed_prefs.comptrustbyte ||
		currprefs.comptrustword != changed_prefs.comptrustword ||
		currprefs.comptrustlong != changed_prefs.comptrustlong ||
		currprefs.comptrustnaddr!= changed_prefs.comptrustnaddr ||
		currprefs.compnf != changed_prefs.compnf ||
		currprefs.comp_hardflush != changed_prefs.comp_hardflush ||
		currprefs.comp_constjump != changed_prefs.comp_constjump ||
		currprefs.comp_oldsegv != changed_prefs.comp_oldsegv ||
		currprefs.compfpu != changed_prefs.compfpu ||
		currprefs.fpu_strict != changed_prefs.fpu_strict)
		changed = 1;

	currprefs.comptrustbyte = changed_prefs.comptrustbyte;
	currprefs.comptrustword = changed_prefs.comptrustword;
	currprefs.comptrustlong = changed_prefs.comptrustlong;
	currprefs.comptrustnaddr= changed_prefs.comptrustnaddr;
	currprefs.compnf = changed_prefs.compnf;
	currprefs.comp_hardflush = changed_prefs.comp_hardflush;
	currprefs.comp_constjump = changed_prefs.comp_constjump;
	currprefs.comp_oldsegv = changed_prefs.comp_oldsegv;
	currprefs.compfpu = changed_prefs.compfpu;
	currprefs.fpu_strict = changed_prefs.fpu_strict;

	if (currprefs.cachesize != changed_prefs.cachesize) {
		if (currprefs.cachesize && !changed_prefs.cachesize) {
			cachesize_prev = currprefs.cachesize;
			comptrust_prev = currprefs.comptrustbyte;
			canbang_prev = canbang;
		} else if (!currprefs.cachesize && changed_prefs.cachesize == cachesize_prev) {
			changed_prefs.comptrustbyte = currprefs.comptrustbyte = comptrust_prev;
			changed_prefs.comptrustword = currprefs.comptrustword = comptrust_prev;
			changed_prefs.comptrustlong = currprefs.comptrustlong = comptrust_prev;
			changed_prefs.comptrustnaddr = currprefs.comptrustnaddr = comptrust_prev;
		}
		currprefs.cachesize = changed_prefs.cachesize;
		alloc_cache();
		changed = 1;
	}

	// Turn off illegal-mem logging when using JIT...
	if(currprefs.cachesize)
		currprefs.illegal_mem = changed_prefs.illegal_mem;// = 0;

	currprefs.comp_midopt = changed_prefs.comp_midopt;
	currprefs.comp_lowopt = changed_prefs.comp_lowopt;

	if ((!canbang || !currprefs.cachesize) && currprefs.comptrustbyte != 1) {
		// Set all of these to indirect when canbang == 0
		// Basically, set the compforcesettings option...
		currprefs.comptrustbyte = 1;
		currprefs.comptrustword = 1;
		currprefs.comptrustlong = 1;
		currprefs.comptrustnaddr= 1;

		changed_prefs.comptrustbyte = 1;
		changed_prefs.comptrustword = 1;
		changed_prefs.comptrustlong = 1;
		changed_prefs.comptrustnaddr= 1;

		changed = 1;

		if (currprefs.cachesize)
			write_log (_T("JIT: Reverting to \"indirect\" access, because canbang is zero!\n"));
	}

	if (changed)
		write_log (_T("JIT: cache=%d. b=%d w=%d l=%d fpu=%d nf=%d inline=%d hard=%d\n"),
		currprefs.cachesize,
		currprefs.comptrustbyte, currprefs.comptrustword, currprefs.comptrustlong, 
		currprefs.compfpu, currprefs.compnf, currprefs.comp_constjump, currprefs.comp_hardflush);

#if 0
	if (!currprefs.compforcesettings) {
		int stop=0;
		if (currprefs.comptrustbyte!=0 && currprefs.comptrustbyte!=3)
			stop = 1, write_log (_T("JIT: comptrustbyte is not 'direct' or 'afterpic'\n"));
		if (currprefs.comptrustword!=0 && currprefs.comptrustword!=3)
			stop = 1, write_log (_T("JIT: comptrustword is not 'direct' or 'afterpic'\n"));
		if (currprefs.comptrustlong!=0 && currprefs.comptrustlong!=3)
			stop = 1, write_log (_T("JIT: comptrustlong is not 'direct' or 'afterpic'\n"));
		if (currprefs.comptrustnaddr!=0 && currprefs.comptrustnaddr!=3)
			stop = 1, write_log (_T("JIT: comptrustnaddr is not 'direct' or 'afterpic'\n"));
		if (currprefs.compnf!=1)
			stop = 1, write_log (_T("JIT: compnf is not 'yes'\n"));
		if (currprefs.cachesize<1024)
			stop = 1, write_log (_T("JIT: cachesize is less than 1024\n"));
		if (currprefs.comp_hardflush)
			stop = 1, write_log (_T("JIT: comp_flushmode is 'hard'\n"));
		if (!canbang)
			stop = 1, write_log (_T("JIT: Cannot use most direct memory access,\n")
			"     and unable to recover from failed guess!\n");
		if (stop) {
			gui_message("JIT: Configuration problems were detected!\n"
				"JIT: These will adversely affect performance, and should\n"
				"JIT: not be used. For more info, please see README.JIT-tuning\n"
				"JIT: in the UAE documentation directory. You can force\n"
				"JIT: your settings to be used by setting\n"
				"JIT:      'compforcesettings=yes'\n"
				"JIT: in your config file\n");
			exit(1);
		}
	}
#endif
	return changed;
}
