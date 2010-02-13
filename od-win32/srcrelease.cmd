cd c:\projects\winuae_bak
rm -rf bak
mkdir bak
copy /s c:\projects\winuae\src\* c:\projects\winuae_bak\bak\
copy d:\amiga\text\winuaechangelog.txt c:\projects\winuae_bak\bak\od-win32
copy d:\amiga\amiga\filesys.asm c:\projects\winuae_bak\bak

cd bak
del *.obj *.ilk *.exe *.pdb *.pch *.idb *.ncb *.sln *.suo *.ncb *.sdf /s



del cpudefs.cpp
del blit.h
del blitfunc.cpp
del blitfunc.h
del blittable.cpp
del cputbl.h
del cpustbl.cpp
del compemu.cpp
del comptbl.h
del compstbl.cpp
del cpuemu_0.cpp
del cpuemu_11.cpp
del cpuemu_12.cpp
del cpuemu_20.cpp
del cpuemu_31.cpp
del linetoscr.cpp

cd jit
del compemu.cpp
del compstbl.h
del compstbl.cpp
cd ..

cd od-win32
cd ipctester
rm -rf debug
rm -rf release
rm -rf fullrelease
cd ..

cd spsutil
rm -rf debug
rm -rf release
rm -rf fullrelease
cd ..

cd genlinetoscr_msvc
rm -f genlinetoscr.exe
rm -rf debug
rm -rf release
rm -rf fullrelease
cd ..

cd build68k_msvc
rm -f build68k.exe
rm -rf debug
rm -rf release
rm -rf fullrelease
cd ..

cd genblitter_msvc
rm -f genblitter.exe 
rm -rf debug
rm -rf release
rm -rf fullrelease
cd ..

cd gencomp_msvc
rm -f gencomp.exe
rm -rf debug
rm -rf release
rm -rf fullrelease
cd ..

cd gencpu_msvc
rm -f gencpu.exe
rm -rf debug
rm -rf release
rm -rf fullrelease
cd ..

cd winuae_msvc
rm -rf debug
rm -rf release
rm -rf fullrelease
cd ..

cd winuae_msvc10
rm -rf debug
rm -rf release
rm -rf fullrelease
rm -rf ipch
cd ..

cd singlefilehelper
rm -rf debug
rm -rf release
rm -rf fullrelease
cd ..

cd resourcedll
rm -rf debug
rm -rf release
rm -rf fullrelease
cd ..

cd fdrawcmd
rm -rf debug
rm -rf release
rm -rf fullrelease
cd ..

cd uaeunp
rm -rf debug
rm -rf release
rm -rf fullrelease
cd ..

cd decompress
rm -rf debug
rm -rf release
rm -rf fullrelease
cd ..

cd ..

zip -9 -r winuaesrc *

copy winuaesrc.zip d:\amiga\winuaepackets\winuaesrc%1.zip
move winuaesrc.zip d:\amiga
cd c:\projects\winuae\src\od-win32
zip -9 winuaedebug%1 winuae_msvc\release\winuae.pdb winuae_msvc\fullrelease\winuae.pdb winuae_msvc10\release\winuae.pdb  winuae_msvc10\fullrelease\winuae.pdb 
move winuaedebug%1.zip d:\amiga\winuaepackets\
copy winuae_msvc10\fullrelease\winuae.pdb d:\amiga\dump
copy d:\amiga\winuae.exe d:\amiga\dump
