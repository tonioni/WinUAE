del *.zip
copy d:\amiga\winuae.exe d:\projects\winuae\distribution
copy resourcedll\release\resourcedll.dll d:\amiga\WinUAE_default.dll
makensis.exe winuae
cd d:\projects\winuae\distribution
copy docs\windows\translation.txt d:\amiga
zip -9 -r d:\projects\winuae\src\od-win32\winuae.zip *
cd d:\projects\winuae\src\od-win32
copy installwinuae.exe d:\amiga\InstallWinUAE%1.exe
copy winuae.zip d:\amiga\WinUAE%1.zip
cd d:\amiga
zip -9 WinUAE%1_translation WinUAE_default.dll translation.txt
del WinUAE_default.dll
del translation.txt
zip -9 WinUAEMini%1 winuae_mini.exe
cd d:\projects\winuae\src\od-win32
del *.zip

