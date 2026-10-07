#!/usr/bin/env python3
"""Exercise the actual Unix P96 feature callbacks with a small guest ABI model."""
import os
from pathlib import Path
import re
import shlex
import subprocess
import sys
import tempfile

root = Path(sys.argv[1]) if len(sys.argv) > 1 else Path(__file__).resolve().parents[1]
rtg = (root / 'od-unix/rtg.cpp').read_text()
gfx = (root / 'od-unix/graphics.cpp').read_text()

def function(source, name):
    start = source.rfind('\n', 0, source.index(name + '(')) + 1
    end = source.index('{', start) + 1
    depth = 1
    while depth:
        depth += (source[end] == '{') - (source[end] == '}')
        end += 1
    return source[start:end] + '\n'

program = r'''
#include <cassert>
#include <cstdint>
#include <cstring>
#include <cstdio>
#include <climits>
using uae_u8=uint8_t; using uae_u16=uint16_t; using uae_u32=uint32_t;
using uae_s16=int16_t; using uae_s32=int32_t; using uae_s64=int64_t;
using uae_u64=uint64_t; using uaecptr=uint32_t;
#define REGPARAM2
#include "rtgmodes.h"
#include "rtg_overlay.h"
struct TrapContext { uint32_t a[8]={},d[8]={}; };
static uint8_t mem[65536];
static bool trap_valid_address(TrapContext*,uaecptr p,uint32_t n) {return p < sizeof mem && n<=sizeof mem-p;}
static uint32_t trap_get_long(TrapContext*,uaecptr p) {assert(p+4<=sizeof mem);return uint32_t(mem[p])<<24|uint32_t(mem[p+1])<<16|uint32_t(mem[p+2])<<8|mem[p+3];}
static uint16_t trap_get_word(TrapContext*,uaecptr p) {assert(p+2<=sizeof mem);return mem[p]<<8|mem[p+1];}
static void trap_put_long(TrapContext*,uaecptr p,uint32_t v) {assert(p+4<=sizeof mem);for(int i=3;i>=0;i--){mem[p+i]=v;v>>=8;}}
static void trap_put_word(TrapContext*,uaecptr p,uint16_t v) {assert(p+2<=sizeof mem);mem[p]=v>>8;mem[p+1]=v;}
static uint32_t trap_get_areg(TrapContext*c,int i){return c->a[i];}
static uint32_t trap_get_dreg(TrapContext*c,int i){return c->d[i];}
static void trap_call_add_areg(TrapContext*c,int i,uint32_t v){c->a[i]=v;}
static void trap_call_add_dreg(TrapContext*c,int i,uint32_t v){c->d[i]=v;}
static uint32_t uae_AllocMem(TrapContext*,uint32_t,uint32_t,uint32_t){return 0x4000;}
static void uae_FreeMem(TrapContext*,uint32_t p,uint32_t,uint32_t){assert(p==0x4000);}
static int unix_rtg_monitor_id(){return 0;}
static const uaecptr unix_picasso_boardinfo=0x100;
#define UNIX_PSSO_BoardInfo_AllocBitMap 506
#define UNIX_PSSO_BoardInfo_FreeBitMap 510
struct {bool rtg_overlay=true;} currprefs;
struct {uint32_t clut[256]={}; int full_refresh=0;} picasso_vidinfo[1];
struct addrbank {uint32_t start,allocated_size; uint8_t *baseaddr;};
static uint8_t vram[1024*1024];
static addrbank bank={0x10000000,sizeof vram,vram};
static addrbank *gfxmem_banks[]={&bank};
static bool bad_bitmap=false;
static int freed=0;
''' + function(rtg, 'unix_picasso_bytes_per_pixel') + r'''
static uint32_t trap_call_func(TrapContext*c,uint32_t func){
    assert(c->a[0]==unix_picasso_boardinfo);
    if(func==0x2000){
        assert(trap_get_long(c,c->a[1])==0x80000002);
        int bpp=unix_picasso_bytes_per_pixel(trap_get_long(c,c->a[1]+4));
        trap_put_word(c,0x3000,c->d[0]*bpp);
        trap_put_word(c,0x3002,c->d[1]);
        trap_put_long(c,0x3008,bank.start+(bad_bitmap?bank.allocated_size-1:0));
        return 0x3000;
    }
    assert(func==0x2002 && c->a[1]==0x3000 && !c->a[2]);
    freed++;
    return 0;
}
''' + rtg[rtg.index('// Feature tags use'):rtg.index('static void unix_picasso_init_board(')]
# The renderer's conversion constants and helpers are compiled unchanged.
program += re.search(r'enum \{[^}]*RGBFB_CLUT_RGBFB_32[^}]+\};', gfx).group() + '\n'
for name in ('unix_picasso_load_host_u16', 'unix_yuv_to_rgb16',
             'unix_picasso_convert_scaled_pixel', 'unix_picasso_overlay_pixels',
             'unix_picasso_overlay_source'):
    program += function(gfx, name)
program += r'''
static void tags(TrapContext*c,const uint32_t*items,unsigned n) {
    for(unsigned i=0;i<n;i++)trap_put_long(c,0x5000+i*4,items[i]);
}
static void feature_regs(TrapContext*c) {
    c->a[0]=unix_picasso_boardinfo;c->a[1]=UNIX_OVERLAY_COOKIE;
    c->a[2]=0x5000;c->d[0]=UNIX_SFT_MEMORYWINDOW;
}
int main(){
    TrapContext c;
    trap_put_long(&c,unix_picasso_boardinfo+506,0x2000);
    trap_put_long(&c,unix_picasso_boardinfo+510,0x2002);
    uint32_t create[]={FA_SourceWidth,16,FA_SourceHeight,16,FA_Format,RGBFB_CLUT,
        FA_Width,32,FA_Height,32,FA_Active,1,0,0};
    tags(&c,create,sizeof create/4);
    feature_regs(&c);c.a[1]=0x5000;
    assert(unix_picasso_create_feature(&c)==UNIX_OVERLAY_COOKIE);
    assert(unix_rtg_get_overlay(0) && !unix_rtg_get_overlay(1));
    assert(unix_rtg_overlay.bitmap==0x3000 && unix_rtg_overlay.pitch==16);
    assert(unix_picasso_overlay_source(&unix_rtg_overlay,&bank)==vram);
    feature_regs(&c);c.a[1]=0x5000;
    assert(!unix_picasso_create_feature(&c)); // only one window per board
    uint32_t change[]={FA_Left,uint32_t(-8),FA_Occlusion,1,FA_Color,7,0,0};
    tags(&c,change,sizeof change/4);feature_regs(&c);c.a[1]=1;
    unix_picasso_set_feature_attrs(&c);assert(!unix_rtg_overlay.x);
    feature_regs(&c);unix_picasso_set_feature_attrs(&c);
    assert(unix_rtg_overlay.x==-8 && unix_rtg_overlay.color==7 && unix_rtg_overlay.occlusion);
    uint32_t query[]={FA_Left,0x6000,FA_BitMap,0x6004,FA_MaxWidth,0x6008,0,0};
    tags(&c,query,sizeof query/4);feature_regs(&c);unix_picasso_get_feature_attrs(&c);
    assert(trap_get_long(&c,0x6000)==uint32_t(-8));
    assert(trap_get_long(&c,0x6004)==0x3000 && trap_get_long(&c,0x6008)==4096);
    uint32_t cycle[]={2,0x5000};tags(&c,cycle,2);
    uaecptr ptr=0x5000;int budget=10;uint32_t tag,value;
    assert(!unix_overlay_next_tag(&c,&ptr,&budget,&tag,&value) && budget<0);
    uint32_t controls[]={1,0,3,1,FA_Left,999,FA_Left,4,0,0};
    tags(&c,controls,sizeof controls/4);ptr=0x5000;budget=10;
    assert(unix_overlay_next_tag(&c,&ptr,&budget,&tag,&value) && tag==FA_Left && value==4);
    // Load two distinct Colors32 runs, followed by the zero header.
    uint32_t colors[]={0x00010002,0xff000000,0x12000000,0x34000000,
                       0x00010003,0x56000000,0x78000000,0x9a000000,0};
    tags(&c,colors,sizeof colors/4);unix_overlay_colors(&c,&unix_rtg_overlay,0x5000,true);
    assert(unix_rtg_overlay.clut[2]==0xffff1234 && unix_rtg_overlay.clut[3]==0xff56789a);
    unix_rtg_overlay_state bounds=unix_rtg_overlay;
    bounds.vram=bank.start+bank.allocated_size-1;
    assert(!unix_picasso_overlay_source(&bounds,&bank));
    bounds=unix_rtg_overlay;bounds.format=RGBFB_Y4U1V1;bounds.source_width=17;
    bounds.pitch=17;assert(!unix_picasso_overlay_source(&bounds,&bank));
    bounds.pitch=20;assert(unix_picasso_overlay_source(&bounds,&bank));
    feature_regs(&c);assert(unix_picasso_delete_feature(&c)==1 && freed==1);
    assert(!unix_rtg_get_overlay(0));
    tags(&c,create,sizeof create/4);feature_regs(&c);c.a[1]=0x5000;bad_bitmap=true;
    assert(!unix_picasso_create_feature(&c) && freed==2 && !unix_rtg_overlay.bitmap);
    // Scaling, negative placement and guest pixel keys.
    uint8_t src[]={0,1,2,3}, screen[]={7,8,7,8};
    uint32_t clut[]={0xff112233,0xff445566,0xff778899,0xffabcdef},dst[6];
    for(auto &v:dst)v=0xdeadbeef;
    unix_picasso_overlay_pixels(src,dst,screen,4,1,-2,8,4,RGBFB_CLUT_RGBFB_32,
        nullptr,clut,false,0);
    assert(dst[0]==clut[1] && dst[1]==clut[1] && dst[2]==clut[2] && dst[3]==clut[2]);
    assert(dst[4]==0xdeadbeef);
    for(auto &v:dst)v=0xdeadbeef;
    unix_picasso_overlay_pixels(src,dst,screen,4,1,0,4,4,RGBFB_CLUT_RGBFB_32,
        nullptr,clut,true,7);
    assert(dst[0]==clut[0] && dst[1]==0xdeadbeef && dst[2]==clut[2] && dst[3]==0xdeadbeef);
    uint8_t key32[]={0x12,0x34,0x56,0x78};
    unix_picasso_overlay_pixels(src,dst,key32,1,4,0,1,1,RGBFB_CLUT_RGBFB_32,
        nullptr,clut,true,0x12345678);assert(dst[0]==clut[0]);
    uint8_t rgb[]={0x12,0x34,0x56};
    unix_picasso_overlay_pixels(rgb,dst,screen,1,1,0,1,1,RGBFB_R8G8B8_32,
        nullptr,nullptr,false,0);assert(dst[0]==0xff123456);
    // Oversized zoom still advances exactly one output pixel per iteration.
    unix_picasso_overlay_pixels(src,dst,screen,4,1,0,INT_MAX,4,RGBFB_CLUT_RGBFB_32,
        nullptr,clut,false,0);for(int i=0;i<4;i++)assert(dst[i]==clut[0]);
    unix_picasso_overlay_pixels(src,dst,screen,4,1,INT_MIN,INT_MAX,4,
        RGBFB_CLUT_RGBFB_32,nullptr,clut,false,0);
    // Packed YUV selects different luma samples in a shared chroma group.
    static uint32_t rgb16[65536];for(unsigned i=0;i<65536;i++)rgb16[i]=i;
    uint8_t yuv[]={16,128,235,128}; // YUYV, black then white
    unix_picasso_overlay_pixels(yuv,dst,screen,2,1,0,2,2,RGBFB_Y4U2V2_32,
        rgb16,nullptr,false,0);
    assert(dst[0]==0 && dst[1]==0x7fff);
    uint32_t packed=(2u<<12)|(10u<<17)|(20u<<22)|(29u<<27)|32u|(32u<<6);
    uint8_t accupak[4];for(int i=0;i<4;i++)accupak[i]=packed>>(8*i);
    unix_picasso_overlay_pixels(accupak,dst,screen,4,1,0,4,4,RGBFB_Y4U1V1_32,
        rgb16,nullptr,false,0);
    assert(dst[0]==0 && dst[1]==9*1057 && dst[2]==21*1057 && dst[3]==31*1057);
    puts("RTG feature lifecycle, tag lists and overlay composition passed");
}
'''
with tempfile.TemporaryDirectory(prefix='winuae-rtg-features-') as tmp:
    cpp=Path(tmp)/'test.cpp'; exe=Path(tmp)/'test'; cpp.write_text(program)
    subprocess.run(shlex.split(os.environ.get('CXX','c++')) +
        ['-std=c++11','-O2','-Wall','-Wextra'] +
        shlex.split(os.environ.get('CXXFLAGS', '')) + ['-I',str(root/'include'),
         '-I',str(root/'od-unix'),str(cpp),'-o',str(exe)],check=True)
    subprocess.run([str(exe)],check=True)
