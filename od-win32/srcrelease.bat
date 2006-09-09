cd d:\projects\winuae_bak
rm -rf bak
mkdir bak
copy /s d:\projects\winuae\src\*.* d:\projects\winuae_bak\bak\
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
del linetoscr.c

cd od-win32

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
rm -f winuae_msvc.ncb
rm -rf debug
rm -rf release
rm -rf debug64
rm -rf release64
rm -rf x64
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
cd ..

zip -9 -r winuaesrc *

copy winuaesrc.zip f:\amiga\winuaepackets\winuaesrc%1.zip
move winuaesrc.zip f:\amiga
cd d:\projects\winuae\src\od-win32
zip -9 winuaedebug%1 winuae_msvc\release\winuae.pdb
move winuaedebug%1.zip f:\amiga\winuaepackets\
