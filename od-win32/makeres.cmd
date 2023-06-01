copy resourcedll\release\resourcedll.dll bin\WinUAE_default.dll
copy d:\amiga\translation.txt bin\translation.txt
cd bin
c:\utils\cygwin\bin\zip -9 WinUAE%1_translation WinUAE_default.dll translation.txt
copy WinUAE%1_translation.zip d:\amiga
del translation.txt
del WinUAE%1_translation.zip
cd ..
