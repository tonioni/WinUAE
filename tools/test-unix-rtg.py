#!/usr/bin/env python3
"""Run portable regressions against the Unix RTG implementation helpers."""
import os
from pathlib import Path
import re
import shlex
import subprocess
import tempfile
import sys

root = Path(sys.argv[1]) if len(sys.argv) > 1 else Path(__file__).resolve().parents[1]
source = (root / 'od-unix/rtg.cpp').read_text()

def function(name):
    start = source.rfind('\n', 0, source.index(name + '(')) + 1
    end = source.index('{', start)
    depth = 1
    end += 1
    while depth:
        depth += (source[end] == '{') - (source[end] == '}')
        end += 1
    return source[start:end] + '\n'

header = (root / 'include/picasso96.h').read_text()
enum = re.search(r'typedef enum \{[^}]+\} BLIT_OPCODE;', header).group()
program = '''
#include <cassert>
#include <cstdint>
#include <cstdio>
using uae_u8 = uint8_t;
using uae_u16 = uint16_t;
using uae_u32 = uint32_t;
#include "rtgmodes.h"
''' + enum + '\n' + ''.join(function(n) for n in (
    'unix_picasso_blit_op', 'unix_picasso_blit_op_long', 'unix_picasso_rgb_full_mask')) + r'''
int main() {
    for (unsigned op=0;op<16;op++) for (unsigned s=0;s<256;s++) for (unsigned d=0;d<256;d++) {
        unsigned expected=0;
        for (unsigned bit=0;bit<8;bit++) {
            unsigned index=(((s>>bit)&1)<<1)|((d>>bit)&1);
            expected|=((op>>index)&1)<<bit;
        }
        assert(unix_picasso_blit_op(s,d,(BLIT_OPCODE)op)==expected);
        assert((unix_picasso_blit_op_long(s,~s,d,~d,(BLIT_OPCODE)op)&255)==expected);
    }
    assert(unix_picasso_rgb_full_mask(RGBFB_A8R8G8B8)==0x00ffffff);
    assert(unix_picasso_rgb_full_mask(RGBFB_B8G8R8A8)==0xffffff00);
    assert(unix_picasso_rgb_full_mask(RGBFB_R5G5B5PC)==0xff7f);
    assert(unix_picasso_rgb_full_mask(RGBFB_R5G5B5)==0x7fff);
    assert((0x12345678 ^ unix_picasso_rgb_full_mask(RGBFB_A8R8G8B8))==0x12cba987);
    puts("RTG minterms and pixel masks passed");
}
'''
with tempfile.TemporaryDirectory(prefix='winuae-rtg-') as tmp:
    cpp = Path(tmp) / 'test.cpp'
    exe = Path(tmp) / 'test'
    cpp.write_text(program)
    subprocess.run(shlex.split(os.environ.get('CXX', 'c++')) +
                   ['-std=c++11', '-O2', '-Wall', '-Wextra', '-I', str(root / 'include'),
                    str(cpp), '-o', str(exe)], check=True)
    subprocess.run([str(exe)], check=True)
