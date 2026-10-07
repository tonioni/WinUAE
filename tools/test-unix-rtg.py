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
#include <cstdlib>
#include <cstring>
using uae_u8 = uint8_t;
using uae_u16 = uint16_t;
using uae_u32 = uint32_t;
#include "rtgmodes.h"
static uint32_t do_get_mem_long(const uint32_t *p) { const uint8_t *b=(const uint8_t*)p;return uint32_t(b[0])<<24|uint32_t(b[1])<<16|uint32_t(b[2])<<8|b[3]; }
static uint16_t do_get_mem_word(const uint16_t *p) { const uint8_t *b=(const uint8_t*)p;return b[0]<<8|b[1]; }
static void do_put_mem_long(uint32_t *p,uint32_t v) { uint8_t *b=(uint8_t*)p;for(int i=3;i>=0;i--){b[i]=v;v>>=8;} }
static void do_put_mem_word(uint16_t *p,uint16_t v) { uint8_t *b=(uint8_t*)p;b[0]=v>>8;b[1]=v; }
''' + enum + '\n' + ''.join(function(n) for n in (
    'unix_picasso_blit_op', 'unix_picasso_blit_op_long', 'unix_picasso_rgb_full_mask',
    'unix_picasso_load_pen', 'unix_picasso_store_pen', 'unix_picasso_blit_pixels')) + r'''
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
    for (int bpp=1;bpp<=4;bpp++) for (int reverse=0;reverse<2;reverse++) {
        for (unsigned op=0;op<16;op++) {
            uint8_t actual[160], before[160], expected[160];
            for (unsigned i=0;i<sizeof actual;i++) actual[i]=(i*37+19)&255;
            memcpy(before,actual,sizeof actual);memcpy(expected,actual,sizeof actual);
            unsigned so=reverse?bpp:0, dest=reverse?0:bpp;
            uint32_t rgbmask=bpp==2?0x7fff:bpp==4?0x00ffffff:0xffffffff;
            unsigned mask=bpp==1?0x5a:255;
            for (unsigned y=0;y<3;y++) for (unsigned x=0;x<3;x++) {
                uint32_t s=unix_picasso_load_pen(before+so+y*32+x*bpp,bpp);
                uint32_t d=unix_picasso_load_pen(before+dest+y*32+x*bpp,bpp);
                uint32_t v=0;
                for(unsigned bit=0;bit<32;bit++)
                    v|=((op>>((((s>>bit)&1)<<1)|((d>>bit)&1)))&1)<<bit;
                if(op==BLIT_NOTDST) v=(~d)&rgbmask;
                if(op==BLIT_ONLYSRC) v=s&((~d)&rgbmask);
                if(op==BLIT_NOTONLYDST) v=s|((~d)&rgbmask);
                if(bpp==1) v=(v&mask)|(d&~mask);
                unix_picasso_store_pen(expected+dest+y*32+x*bpp,v,bpp);
            }
            assert(unix_picasso_blit_pixels(actual+so,actual+dest,3,3,32,32,bpp,
                rgbmask,mask,(BLIT_OPCODE)op,false,0));
            assert(!memcmp(actual,expected,sizeof actual));
        }
        uint8_t src[16]={},dst[16]={};
        uint32_t a=bpp==4?0x12345678:0x1234, b=bpp==4?0x87654321:0xabcd;
        unix_picasso_store_pen(src,a,bpp);unix_picasso_store_pen(dst,b,bpp);
        a=unix_picasso_load_pen(src,bpp);b=unix_picasso_load_pen(dst,bpp);
        assert(unix_picasso_blit_pixels(src,dst,1,1,16,16,bpp,~0u,255,BLIT_SWAP,false,0));
        assert(unix_picasso_load_pen(src,bpp)==b && unix_picasso_load_pen(dst,bpp)==a);
        assert(unix_picasso_blit_pixels(src,dst,1,1,16,16,bpp,~0u,255,BLIT_SRC,true,b));
        assert(unix_picasso_load_pen(dst,bpp)==a);
    }
    {
        uint8_t p[32], old[32];for(unsigned i=0;i<32;i++)p[i]=i;
        memcpy(old,p,32);
        assert(unix_picasso_blit_pixels(p,p+1,3,3,4,5,1,255,255,BLIT_SRC,false,0));
        for(unsigned y=0;y<3;y++)assert(!memcmp(p+1+y*5,old+y*4,3));
    }
    puts("RTG minterms, masks and rectangle operations passed");
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
