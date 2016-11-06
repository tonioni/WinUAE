cd c:\projects\winuae_bak
rm -rf bak
mkdir bak

copy d:\amiga\text\winuaechangelog.txt c:\projects\winuae\src\od-win32
copy d:\amiga\text\winuaechangelog.txt c:\projects\winuae\distribution\docs

copy c:\projects\winuae\src\* c:\projects\winuae_bak\bak\
copy /s c:\projects\winuae\src\archivers\* c:\projects\winuae_bak\bak\archivers\
mkdir bak\include
copy c:\projects\winuae\src\include\* c:\projects\winuae_bak\bak\include\
mkdir bak\jit
copy c:\projects\winuae\src\jit\* c:\projects\winuae_bak\bak\jit\
copy /s c:\projects\winuae\src\qemuvga\* c:\projects\winuae_bak\bak\qemuvga\
copy /s c:\projects\winuae\src\slirp\* c:\projects\winuae_bak\bak\slirp\
copy /s c:\projects\winuae\src\od-win32\* c:\projects\winuae_bak\bak\od-win32\

copy d:\amiga\amiga\filesys.asm c:\projects\winuae_bak\bak

cd bak
del *.obj *.ilk *.exe *.pdb *.pch *.idb *.ncb *.sln *.suo *.ncb *.sdf *.db /s

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
del cpuemu_13.cpp
del cpuemu_20.cpp
del cpuemu_21.cpp
del cpuemu_22.cpp
del cpuemu_31.cpp
del cpuemu_32.cpp
del cpuemu_33.cpp
del linetoscr.cpp
del aros.rom.cpp

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

cd prowizard
rm -rf debug
rm -rf release
rm -rf x64
cd ..

cd unpackers
rm -rf debug
rm -rf release
rm -rf x64
cd ..

cd genlinetoscr_msvc
rm -f genlinetoscr.exe
rm -f *.cpp
rm -f *.h
rm -rf debug
rm -rf release
rm -rf fullrelease
rm -rf test
cd ..

cd build68k_msvc
rm -f build68k.exe
rm -f *.cpp
rm -f *.h
rm -rf debug
rm -rf release
rm -rf fullrelease
rm -rf test
cd ..

cd genblitter_msvc
rm -f genblitter.exe 
rm -f *.cpp
rm -f *.h
rm -rf debug
rm -rf release
rm -rf fullrelease
rm -rf test
cd ..

cd gencomp_msvc
rm -f gencomp.exe
rm -f *.cpp
rm -f *.h
rm -rf debug
rm -rf release
rm -rf fullrelease
rm -rf test
cd ..

cd gencpu_msvc
rm -f gencpu.exe
rm -f *.cpp
rm -f *.h
rm -rf debug
rm -rf release
rm -rf fullrelease
rm -rf test
cd ..

cd winuae_msvc14
rm -rf debug
rm -rf release
rm -rf fullrelease
rm -rf test
rm -rf x64
rm -rf packages
cd ..

cd winuae_msvc15
rm -rf debug
rm -rf release
rm -rf fullrelease
rm -rf test
rm -rf x64
rm -rf packages
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

cd consolewrapper
rm -rf debug
rm -rf release
rm -rf fullrelease
cd ..

cd wix
rm -rf bin
rm -rf obj
cd ..


cd ..

7z a -r winuaesrc *

copy winuaesrc.7z e:\amiga\winuaepackets\winuaesrc%1.7z
move winuaesrc.7z e:\amiga
cd c:\projects\winuae\src\od-win32
7z a winuaedebug%1 winuae_msvc15\fullrelease\winuae.pdb winuae_msvc15\x64\fullrelease\winuae.pdb
move winuaedebug%1.7z e:\amiga\winuaepackets\debug\
copy winuae_msvc15\fullrelease\winuae.pdb d:\amiga\dump\winuae.pdb
copy winuae_msvc15\x64\fullrelease\winuae.pdb  d:\amiga\dump\winuae64.pdb
copy d:\amiga\winuae.exe d:\amiga\dump
copy d:\amiga\winuae64.exe d:\amiga\dump
