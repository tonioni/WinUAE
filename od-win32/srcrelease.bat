cd c:\projects\winuae_bak
rm -rf bak
mkdir bak
copy /s c:\projects\winuae\src\*.* c:\projects\winuae_bak\bak\
cd bak
del *.obj *.ilk *.exe *.pdb *.pch *.idb /s

del cpudefs.c
del blit.h
del blitfunc.c
del blitfunc.h
del blittable.c
del cputbl.h
del cpustbl.c
del compemu.c
del comptbl.h
del compstbl.c
del cpuemu_0.c
del cpuemu_5.c
del cpuemu_6.c

cd od-win32

cd build68k_msvc
rm -f build68k.exe build68k_msvc.plg
rm -rf debug
rm -rf release
cd ..

cd genblitter_msvc
rm -f genblitter.exe genblitter_msvc.plg
rm -rf debug
rm -rf release
cd ..

cd gencomp_msvc
rm -f gencomp.exe gencomp_msvc.plg
rm -rf debug
rm -rf release
cd ..

cd gencpu_msvc
rm -f gencpu.exe gencpu_msvc.plg
rm -rf debug
rm -rf release
cd ..

cd winuae_msvc
rm -f winuae_msvc.plg
rm -f winuae_msvc.ncb
rm -rf debug
rm -rf release
cd ..

cd miniuae
rm -f winuae_msvc.plg
rm -f winuae_msvc.ncb
rm -rf debug
rm -rf release
cd ..

cd winuae_nogui
rm -rf debug
rm -rf release
cd ..

cd soundcheck
rm -rf debug
rm -rf release
cd ..

cd singlefilehelper
rm -rf debug
rm -rf release
cd ..

cd resourcedll
rm -rf debug
rm -rf release
cd ..
cd ..

zip -9 -r winuaesrc *

copy winuaesrc.zip d:\amiga\winuaepackets\winuaesrc%1.zip
move winuaesrc.zip d:\amiga
cd c:\projects\winuae\src\od-win32
zip -9 winuaedebug%1 winuae_msvc\release\winuae.pdb
move winuaedebug%1.zip d:\amiga\winuaepackets\
