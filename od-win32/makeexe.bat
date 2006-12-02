del *.zip
copy f:\amiga\winuae.exe d:\projects\winuae\distribution
copy resourcedll\release\resourcedll.dll f:\amiga\WinUAE_default.dll
"d:\winutils\NSIS\makensis.exe" winuae_install
cd d:\projects\winuae\distribution
copy docs\windows\translation.txt f:\amiga
zip -9 -r d:\projects\winuae\src\od-win32\winuae.zip *
cd d:\projects\winuae\src\od-win32
copy installwinuae.exe f:\amiga\InstallWinUAE%1.exe
copy winuae.zip f:\amiga\WinUAE%1.zip
cdd f:\amiga
zip -9 WinUAE%1_translation WinUAE_default.dll translation.txt
del WinUAE_default.dll
del translation.txt
rem zip -9 WinUAEMini%1 winuae_mini.exe
cdd d:\projects\winuae\src\od-win32
zip -9 winuaedebug%1 winuae_msvc\release\winuae.pdb
copy winuaedebug%1.zip f:\amiga\winuaepackets
del *.zip
