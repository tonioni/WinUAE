del *.zip
copy d:\amiga\winuae.exe c:\projects\winuae\distribution
cd c:\projects\winuae\distribution
copy docs\windows\translation.txt d:\amiga
zip -9 -r c:\projects\winuae\src\od-win32\winuae.zip *
cd c:\projects\winuae\src\od-win32
copy winuae.zip d:\amiga\WinUAE%1.zip
copy c:\projects\winuae\src\od-win32\wix\bin\Release\winuae.msi d:\amiga\InstallWinUAE%1.msi
copy resourcedll\release\resourcedll.dll d:\amiga\WinUAE_default.dll
cdd d:\amiga
zip -9 WinUAE%1_x64.zip winuae64.exe
zip -9 WinUAE%1_translation WinUAE_default.dll translation.txt
del translation.txt
cdd c:\projects\winuae\src\od-win32
del *.zip
