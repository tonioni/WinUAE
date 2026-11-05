#!/usr/bin/env python3
"""Compile unchanged PCI access functions from a selected pci.cpp into a fixture.

Stubs cover unrelated autoconfig/IRQ side effects; register dispatch, window
selection, BAR selection and memory read/write dispatch are production code.

Run with Python 3 and a C++17 compiler named c++ on PATH:
    python3 tools/test-mediator-windows.py [path/to/pci.cpp]

The optional source path permits before/after regression checks. Use --include
to select matching headers when testing a different checkout.
"""
import argparse
from pathlib import Path
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[1]

def function(source, name):
    import re
    match = re.search(r'^static [^\n]*\b' + re.escape(name) + r'\([^\n]*\)\n\{', source, re.M)
    if not match:
        raise ValueError(name)
    start = source.index('{', match.start())
    depth = 1
    end = start + 1
    while depth:
        depth += (source[end] == '{') - (source[end] == '}')
        end += 1
    return source[match.start():end] + '\n'

p = argparse.ArgumentParser()
p.add_argument('source', type=Path, nargs='?', default=ROOT / 'pci.cpp')
p.add_argument('--include', type=Path, default=ROOT / 'include',
               help='WinUAE header directory (defaults to this checkout)')
args = p.parse_args()
source = args.source.read_text()
names = ('get_pci_bridge', 'get_pci_bridge_2', 'get_pci_board_state',
         'get_pci_mem', 'beswap', 'pci_mem_bget', 'pci_mem_bput',
         'mediator_set_window_offset_window', 'mediator_set_window_offset',
         'pci_bridge_bget_2', 'pci_bridge_wget_2', 'pci_bridge_bput_2',
         'pci_bridge_wput_2')
with tempfile.TemporaryDirectory(prefix='mediator-window-') as temp:
    temp = Path(temp)
    prefix = '''#include <cassert>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#define REGPARAM
#define REGPARAM2
#define REGPARAM3
#define PCI_DEBUG_MEMORY 0
#define PCI_DEBUG_BRIDGE 0
#define PCI_DEBUG_IO 0
#define PCI_DEBUG_IO_MISS 0
#define AUTOCONFIG_Z2 0xe80000
#define AUTOCONFIG_Z3 0xff000000
#define PCI_BRIDGE_MAX 2
struct addrbank {};
#include "pci_hw.h"
static pci_bridge *bridges[PCI_BRIDGE_MAX];
static int last_bridge_index, stored_board, stored_bar;
static addrbank dummy_bank, pci_config_bank, pci_io_bank;
static uaecptr expamem_board_pointer, expamem_board_size;
static uae_u16 do_byteswap_16(uae_u16 v) { return __builtin_bswap16(v); }
static void map_banks_z2(addrbank*, unsigned, unsigned) { std::abort(); }
static void map_banks(addrbank*, unsigned, unsigned, unsigned) { std::abort(); }
static bool validate_banks_z3(addrbank*, unsigned, unsigned) { std::abort(); }
static void expamem_next(addrbank*, void*) { std::abort(); }
static void expamem_shutup(addrbank*) { std::abort(); }
static void pci_rethink() {}
static void devices_rethink_all(void(*)()) {}
static void uae_quit() { std::abort(); }
'''
    suffix = r'''
static unsigned checks, failures;
static void check(bool good, const char *what) {
    checks++;
    if (!good) { failures++; if (failures < 12) std::printf("FAIL: %s\n",what); }
}
static uae_u8 memory[4096];
static uaecptr callback_address;
static uae_u32 memory_read(pci_board_state *, uaecptr address) {
    callback_address=address; return memory[address&4095];
}
static void memory_write(pci_board_state *, uaecptr address, uae_u32 data) {
    callback_address=address; memory[address&4095]=data;
}
static pci_board card;
static void fixture(pci_bridge &bridge, bool a4000, bool dual=false) {
    bridge={}; card={};
    bridge.configured=bridge.configured_2=1;
    bridge.bank_2_zorro=a4000 ? 3 : 2;
    bridge.baseaddress=a4000 ? 0x60000000 : 0x00200000;
    bridge.baseaddress_end=bridge.baseaddress+(a4000 ? 0x20000000 : 0x00800000);
    bridge.baseaddress_2=a4000 ? 0x44000000 : 0x00ea0000;
    bridge.baseaddress_end_2=bridge.baseaddress_2+(a4000 ? 0x01000000 : 0x20000);
    bridge.endian_swap_memory=-1;
    bridge.multiwindow=dual;
    bridge.boards[0].board=&card;
    bridge.boards[0].bridge=&bridge;
    bridge.boards[0].bar_enabled[1]=true;
    bridge.boards[0].bar_size[1]=4096;
    card.bars[1].bget=memory_read; card.bars[1].bput=memory_write;
    bridges[0]=&bridge; bridges[1]=nullptr;
    last_bridge_index=stored_board=stored_bar=0;
}
static void bar(pci_bridge &bridge, uaecptr address) {
    bridge.boards[0].bar_start[1]=address&~4095u;
    bridge.boards[0].bar_end[1]=(address&~4095u)+4095;
}
static void access(pci_bridge &bridge, uaecptr cpu, uaecptr pci) {
    bar(bridge,pci);
    callback_address=0;
    pci_mem_bput(cpu,0x5a);
    check(callback_address==pci,"CPU write reaches selected PCI bank");
    callback_address=0;
    check(pci_mem_bget(cpu)==0x5a,"read reaches same framebuffer byte");
    check(callback_address==pci,"CPU read reaches selected PCI bank");
}
int main() {
    pci_bridge bridge;
    // Actual failing native case: select 0x70, CPU6e010020 -> PCI7e010020.
    fixture(bridge,true);
    pci_bridge_bput_2(0x44000000,0x70);
    check(pci_bridge_bget_2(0x44000000)==0x78,"byte selector readback includes id");
    access(bridge,0x6e010020,0x7e010020);
    // Release restores ordinary identity mapping, including the upper half
    // of the 512 MiB host aperture used by existing direct-access clients.
    pci_bridge_bput_2(0x44000000,0x60);
    access(bridge,0x7e010020,0x7e010020);
    // All high-nibble selectors, several host bases, both signs of offset,
    // low nibble ignored, and no cached-BAR match carried across banks.
    const uaecptr bases[]={0x10000000,0x40000000,0x60000000,0x80000000};
    for (uaecptr base:bases) for(unsigned bank=0;bank<16;bank++) {
        fixture(bridge,true);
        bridge.baseaddress=base; bridge.baseaddress_end=base+0x20000000;
        for(unsigned low=0;low<16;low++) {
            pci_bridge_bput_2(bridge.baseaddress_2,(bank<<4)|low);
            check(pci_bridge_bget_2(bridge.baseaddress_2)==((bank<<4)|8),"all selector/id readbacks");
            access(bridge,base+0x00e01020,(bank<<28)+0x00e01020);
        }
    }
    // An A1200 writes the little-endian word at offset2. This must not be
    // changed when fixing the distinct byte-wide A4000 register.
    fixture(bridge,false);
    pci_bridge_wput_2(0x00ea0002,0x403e); // select PCI3e400000
    check(pci_bridge_wget_2(0x00ea0002)==0x603e,"1200 word selector readback");
    access(bridge,0x00201020,0x3e401020);
    // TX second bank inherits the high three bits from window0.
    fixture(bridge,false,true);
    pci_bridge_wput_2(0x00ea0002,0x003e);
    pci_bridge_wput_2(0x00ea0002,0x901e); // second bank 3e800000
    access(bridge,0x00201020,0x3e001020);
    access(bridge,0x00601020,0x3e801020);
    pci_bridge_wput_2(0x00ea0002,0x005e); // shared high bits switch to40000000
    access(bridge,0x00201020,0x5e001020);
    access(bridge,0x00601020,0x5e801020);
    std::printf("RESULT: %u checks, %u failures\n",checks,failures);
    return failures ? 1 : 0;
}
'''
    cpp = temp/'test.cpp'
    cpp.write_text(prefix+'\n'.join(function(source,n) for n in names)+suffix)
    executable=temp/'test'
    subprocess.run(['c++','-std=c++17','-Wall','-Wextra','-Wno-unused-function','-Wno-sign-compare','-I',str(temp),'-I',str(args.include),str(cpp),'-o',str(executable)],check=True)
    raise SystemExit(subprocess.run([str(executable)]).returncode)
