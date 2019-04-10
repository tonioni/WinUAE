del *.zip
cd ..\..\distribution
copy d:\amiga\winuae.exe /y
c:\utils\cygwin\bin\zip -9 -r c:\projects\winuae\src\od-win32\winuae.zip *
cd c:\projects\winuae\src\od-win32
copy winuae.zip d:\amiga\WinUAE%1.zip
copy resourcedll\release\resourcedll.dll bin\WinUAE_default.dll
cd bin
copy d:\amiga\winuae.exe /y
copy d:\amiga\winuae64.exe /y
c:\utils\cygwin\bin\zip -9 WinUAE%1_x64.zip winuae64.exe
c:\utils\cygwin\bin\zip -9 WinUAE%1_translation WinUAE_default.dll
copy WinUAE%1_x64.zip d:\amiga
copy WinUAE%1_translation.zip d:\amiga
del translation.txt
del *.zip
cd ..
