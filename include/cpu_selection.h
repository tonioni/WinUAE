#ifndef UAE_CPU_SELECTION_H
#define UAE_CPU_SELECTION_H

// Selection rules shared by frontends. These do not apply accelerator presets
// or resolve JIT versus cycle-exact precedence as the final config fixup does.
static inline bool cpu_selection_external_fpu(int cpu, bool compatible)
{
	return cpu < 68040 && (cpu >= 68020 || !compatible);
}

static inline int cpu_selection_fpu(int cpu, int fpu, bool compatible, bool memory_cycle_exact)
{
	if (!fpu || (cpu < 68020 && (compatible || memory_cycle_exact))) {
		return 0;
	}
	if (cpu >= 68040) {
		return cpu;
	}
	return fpu == 68882 ? 68882 : 68881;
}

static inline bool cpu_selection_address24(int cpu, int oldcpu, bool address24)
{
	if (cpu >= 68040 || (cpu == 68030 && cpu != oldcpu)) {
		return false;
	}
	if (cpu <= 68010 && cpu != oldcpu) {
		return true;
	}
	return address24;
}

static inline bool cpu_selection_mmu(int cpu, bool jit)
{
	return cpu >= 68030 && !jit;
}

static inline bool cpu_selection_jit(int cpu, bool address24, bool mmu)
{
	return cpu >= 68020 && !address24 && !mmu;
}

static inline bool cpu_selection_data_cache(int cpu, bool compatible, bool jit)
{
	return cpu_selection_mmu(cpu, jit) && compatible;
}

static inline bool cpu_selection_compatible(int cpu, bool memory_cycle_exact, bool jit)
{
	return !memory_cycle_exact && !(jit && cpu >= 68040);
}

#endif
