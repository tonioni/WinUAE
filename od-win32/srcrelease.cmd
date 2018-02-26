
copy d:\amiga\text\winuaechangelog.txt /y
copy d:\amiga\text\winuaechangelog.txt ..\..\distribution\docs /y

cd ..

del ..\winuaesrc.7z

7z a -r ..\winuaesrc *.c *.cpp *.h *.sln *.vcxproj* *.ico *.rc *.bmp *.cur *.manifest *.png *.txt

copy ..\winuaesrc.7z e:\amiga\winuaepackets\winuaesrc%1.7z
copy ..\winuaesrc.7z e:\amiga /y

cd od-win32

7z a winuaedebug%1 winuae_msvc15\fullrelease\winuae.pdb winuae_msvc15\x64\fullrelease\winuae.pdb
move winuaedebug%1.7z e:\amiga\winuaepackets\debug\
copy winuae_msvc15\fullrelease\winuae.pdb d:\amiga\dump\winuae.pdb
copy winuae_msvc15\x64\fullrelease\winuae.pdb  d:\amiga\dump\winuae64.pdb
copy d:\amiga\winuae.exe d:\amiga\dump
copy d:\amiga\winuae64.exe d:\amiga\dump
