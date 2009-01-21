cd c:\projects\winuae_bak
rm -rf bak
mkdir bak
copy c:\projects\winuae\src\ c:\projects\winuae_bak\bak\ /s
copy d:\amiga\text\winuaechangelog.txt c:\projects\winuae_bak\bak\od-win32

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
del cpuemu_11.c
del cpuemu_12.c
del linetoscr.c

cd jit
del compemu.c
del compstbl.h
del compstbl.c
cd ..

cd od-win32
cd ipctester
rm -rf debug
rm -rf release
cd ..

cd spsutil
rm -rf debug
rm -rf release
cd ..

cd genlinetoscr_msvc
rm -f genlinetoscr.exe
rm -rf debug
rm -rf release
cd ..

cd build68k_msvc
rm -f build68k.exe
rm -rf debug
rm -rf release
cd ..

cd genblitter_msvc
rm -f genblitter.exe 
rm -rf debug
rm -rf release
cd ..

cd gencomp_msvc
rm -f gencomp.exe
rm -rf debug
rm -rf release
cd ..

cd gencpu_msvc
rm -f gencpu.exe
rm -rf debug
rm -rf release
cd ..

cd winuae_msvc
rm -f winuae_msvc.plg
rm -f winuae_msvc.8.plg
rm -f winuae_msvc.ncb
rm -f winuae_msvc.8.ncb
rm -rf debug
rm -rf release
rm -rf debug64
rm -rf release64
rm -rf x64
rm -rf fullrelease
rm -rf _UpgradeReport_Files
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

cd fdrawcmd
rm -rf debug
rm -rf release
cd ..

cd ..

zip -9 -r winuaesrc *

copy winuaesrc.zip d:\amiga\winuaepackets\winuaesrc%1.zip
move winuaesrc.zip d:\amiga
cd c:\projects\winuae\src\od-win32
zip -9 winuaedebug%1 winuae_msvc\release\winuae.pdb winuae_msvc\fullrelease\winuae.pdb
move winuaedebug%1.zip d:\amiga\winuaepackets\
copy winuae_msvc\fullrelease\winuae.pdb d:\amiga\dump
copy d:\amiga\winuae.exe d:\amiga\dump
