del *.zip
copy d:\amiga\winuae.exe c:\projects\winuae\distribution
copy resourcedll\release\resourcedll.dll d:\amiga\WinUAE_default.dll
"c:\Program Files\NSIS\makensis.exe" winuae_install
cd c:\projects\winuae\distribution
copy docs\windows\translation.txt d:\amiga
zip -9 -r c:\projects\winuae\src\od-win32\winuae.zip *
cd c:\projects\winuae\src\od-win32
copy installwinuae.exe d:\amiga\InstallWinUAE%1.exe
copy winuae.zip d:\amiga\WinUAE%1.zip
cdd d:\amiga
zip -9 WinUAE%1_translation WinUAE_default.dll translation.txt
del WinUAE_default.dll
del translation.txt
zip -9 WinUAEMini%1 winuae_mini.exe
cdd c:\projects\winuae\src\od-win32
zip -9 winuaedebug%1 winuae_msvc\release\winuae.pdb
copy winuaedebug%1.zip d:\amiga\winuaepackets
del *.zip
