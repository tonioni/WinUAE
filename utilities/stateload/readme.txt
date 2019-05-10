
ussload: load UAE state files on real hardware.

Supported hardware configurations:

- Common 68000 A500 configurations. (chip ram, "slow" ram and fast ram supported)
- A1200 68020 configuration ("slow" ram and fast ram is also supported)

Information:

CPU should match statefile config but it only causes warning. Mismatched CPU most likely won't work but it is fully supported by ussload.
RAM config must match and at least one RAM address space must be 512k larger.
Both compressed and uncompressed statefiles are supported.
HD compatible (statefile is completely loaded before system take over)
KS ROM does not need to match if loaded program has already completely taken over the system.
All, even ancient statefiles should be supported, confirmed with UAE 0.8.22 created statefile.
Floppy state restore is not tested but at least motor state and track number is restored.
Statefile restore can for example fail if statefile was saved when blitter was active or program was executing self-modifying code.

RAM config examples:

512k chip ram statefile: hardware must have 1M chip or 512k chip+512k "slow" ram or 512k chip+512k real fast.
512k+512k statefile: hardware must have 1M+512k or 512k+1M or 512k+512k+512k real fast.

Note that uncompressed statefiles require at least 1M contiguous extra RAM because all statefile RAM address spaces need to fit in RAM before system take over.

A1200 chip ram only statefiles require at least 1M fast ram.

Map ROM hardware support:

Currently ACA500, ACA500plus, ACA1221, ACA1221EC and most ACA123x variants map rom hardware is supported.
If statefile ROM is not same as hardware ROM, ROM image is automatically loaded from devs:kickstarts and enabled if found.

Command line parameters:

- debug = show debug information.
- test = parse and load statefile, exit before system take over.
- nomaprom = do not use map rom.
- nocache = disable caches before starting loaded program (68020+ only)
- pal = force PAL mode (ECS/AGA only)
- ntsc = force NTSC mode (ECS/AGA only)

Background colors:

- purple = map rom copy
- red = decompressing/copying chip ram state
- green = decompressing/copying slow ram state
- blue = decompressing/copying fast ram (0x00200000) state
- yellow = setting floppy drives (seek rw head)
