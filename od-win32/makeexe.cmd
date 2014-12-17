del *.zip
copy d:\amiga\winuae.exe c:\projects\winuae\distribution
cd c:\projects\winuae\distribution
copy docs\windows\translation.txt d:\amiga
zip -9 -r c:\projects\winuae\src\od-win32\winuae.zip *
cd c:\projects\winuae\src\od-win32
copy winuae.zip d:\amiga\WinUAE%1.zip
copy c:\projects\winuae\src\od-win32\wix\bin\Release\winuae.msi d:\amiga\InstallWinUAE%1.exe
copy resourcedll\release\resourcedll.dll d:\amiga\WinUAE_default.dll
cdd d:\amiga
zip -9 WinUAE%1_translation WinUAE_default.dll translation.txt
del translation.txt
cdd c:\projects\winuae\src\od-win32
;zip -9 winuaedebug%1 winuae_msvc11\fullrelease\winuae.pdb winuae_msvc11\x64\fullrelease\winuae.pdb
copy winuaedebug%1.zip d:\amiga\winuaepackets\debug
del *.zip
