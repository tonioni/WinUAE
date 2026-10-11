/* GDB target backed by the CPU debugger. Used only on the emulation thread. */
#ifndef WINUAE_GDB_TARGET_H
#define WINUAE_GDB_TARGET_H

#include "gdb_protocol.h"

// Remote entries share the debugger tables, but never take over console ones.
class GdbTarget : public winuae_gdb::Target {
public:
	winuae_gdb::Registers registers() override;
	bool write_registers(const winuae_gdb::Registers& values, uint32_t mask) override;
	bool read_memory(uint32_t address, size_t length, std::vector<uint8_t>& bytes) override;
	bool write_memory(uint32_t address, const std::vector<uint8_t>& bytes) override;
	bool breakpoint(bool insert, unsigned type, uint32_t address, uint32_t length) override;
	void interrupt() override;
	void resume(bool step) override;
	void detach() override;
protected:
	enum WatchOwner : uae_u8 { watch_none, watch_gdb };
	bool breakpoints[BREAKPOINT_TOTAL] {};
	WatchOwner watch_owner[MEMWATCH_TOTAL] {};
	bool initialized_watchpoints = false;
	bool start_memwatch();
	int free_watch_slot();
};

GdbTarget& gdb_target();

#endif
