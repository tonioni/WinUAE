del *.zip
copy d:\amiga\winuae.exe c:\projects\winuae\distribution
"c:\program files (x86)\NSIS\makensis.exe" winuae_install
cd c:\projects\winuae\distribution
copy docs\windows\translation.txt d:\amiga
zip -9 -r c:\projects\winuae\src\od-win32\winuae.zip *
cd c:\projects\winuae\src\od-win32
copy installwinuae.exe d:\amiga\InstallWinUAE%1.exe
copy winuae.zip d:\amiga\WinUAE%1.zip
copy resourcedll\release\resourcedll.dll d:\amiga\WinUAE_default.dll
cdd d:\amiga
zip -9 WinUAE%1_translation WinUAE_default.dll translation.txt
del translation.txt
cdd c:\projects\winuae\src\od-win32
zip -9 winuaedebug%1 winuae_msvc10\release\winuae.pdb winuae_msvc10\fullrelease\winuae.pdb
copy winuaedebug%1.zip d:\amiga\winuaepackets
del *.zip
