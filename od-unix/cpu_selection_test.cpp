#include "cpu_selection.h"
#include <cstdio>

static bool check(bool value, const char *what)
{
	if (!value) {
		std::fprintf(stderr, "%s\n", what);
	}
	return value;
}

int main()
{
	bool ok = true;
	const int cpus[] = { 68000, 68010, 68020, 68030, 68040, 68060 };
	const int fpus[] = { 0, 68881, 68882, 68040, 68060 };
	const int expected[][5] = {
		{ 0, 68881, 68882, 68881, 68881 },
		{ 0, 68881, 68882, 68881, 68881 },
		{ 0, 68881, 68882, 68881, 68881 },
		{ 0, 68881, 68882, 68881, 68881 },
		{ 0, 68040, 68040, 68040, 68040 },
		{ 0, 68060, 68060, 68060, 68060 }
	};
	for (int c = 0; c < 6; c++) {
		for (int f = 0; f < 5; f++) {
			ok = check(cpu_selection_fpu(cpus[c], fpus[f], false, false) == expected[c][f],
				"FPU model remapping") && ok;
			if (c < 2) {
				ok = check(cpu_selection_fpu(cpus[c], fpus[f], true, false) == 0,
					"compatible 68000/010 cannot have an FPU") && ok;
				ok = check(cpu_selection_fpu(cpus[c], fpus[f], false, true) == 0,
					"cycle-exact 68000/010 cannot have an FPU") && ok;
			}
		}
	}
	ok = check(cpu_selection_address24(68000, 68020, false), "switch to 68000 selects 24-bit") && ok;
	ok = check(cpu_selection_address24(68020, 68000, true), "68020 retains 24-bit selection") && ok;
	ok = check(!cpu_selection_address24(68030, 68020, true), "switch to 68030 selects 32-bit") && ok;
	ok = check(cpu_selection_address24(68030, 68030, true), "68030 can retain explicit 24-bit") && ok;
	ok = check(!cpu_selection_address24(68040, 68040, true), "68040 requires 32-bit") && ok;
	ok = check(!cpu_selection_address24(68060, 68030, true), "68060 requires 32-bit") && ok;
	ok = check(cpu_selection_external_fpu(68000, false), "noncompatible 68000 permits external FPU") && ok;
	ok = check(!cpu_selection_external_fpu(68000, true), "compatible 68000 disallows external FPU") && ok;
	ok = check(cpu_selection_external_fpu(68030, true), "68030 permits external FPU") && ok;
	ok = check(!cpu_selection_external_fpu(68040, false), "68040 uses internal FPU") && ok;
	ok = check(!cpu_selection_mmu(68020, false), "68020 has no emulated MMU") && ok;
	ok = check(cpu_selection_mmu(68030, false), "68030 permits MMU") && ok;
	ok = check(!cpu_selection_mmu(68040, true), "JIT excludes MMU") && ok;
	ok = check(cpu_selection_jit(68020, false, false), "32-bit 68020 permits JIT") && ok;
	ok = check(!cpu_selection_jit(68020, true, false), "24-bit excludes JIT") && ok;
	ok = check(!cpu_selection_jit(68030, false, true), "MMU excludes JIT") && ok;
	ok = check(!cpu_selection_jit(68000, false, false), "68000 excludes JIT") && ok;
	ok = check(cpu_selection_data_cache(68030, true, false), "compatible 68030 permits data cache") && ok;
	ok = check(!cpu_selection_data_cache(68030, false, false), "data cache needs compatible mode") && ok;
	ok = check(!cpu_selection_data_cache(68040, true, true), "data cache excludes JIT") && ok;
	ok = check(!cpu_selection_compatible(68020, true, false), "cycle-exact locks compatible control") && ok;
	ok = check(cpu_selection_compatible(68030, false, true), "68030 JIT permits compatible control") && ok;
	ok = check(!cpu_selection_compatible(68040, false, true), "68040 JIT locks compatible control") && ok;
	return ok ? 0 : 1;
}
