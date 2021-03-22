
# WinUAE

1) Requirements: Windows 7 32-bit/64-bit or newer.

2) Visual Studio 2017 Community with the following feature:
	"Desktop Development with C++" with follow option:
	-"Support Windows XP for C++"
	-"Windows 8.1 SDK UCRT SDK"
	-"Windows 10 SDK 10.0.17763.0"

3) Download and Install the Windows Driver Kit (WDK). 16299 (1709) or newer.
		
	Download Link -> https://docs.microsoft.com/en-us/windows-hardware/drivers/other-wdk-downloads
	
4) Download the zip packages include and lib "winuaeinclibs.zip" and create the directory "c:\dev" and extract all. When finished you have "c:\dev\include" and \"c:\dev\lib" 
	 	 
	Download Link -> https://download.abime.net/winuae/files/b/winuaeinclibs.zip
		
5) Download WinUAE source packages and extract all (anywhere you want).

	Download Link -> https://github.com/tonioni/WinUAE/archive/<version>.zip
	or (preferably) use git client.

6) Download the zip package aros.rom.cpp.zip and extract into WinUAE source directory.

	Download Link -> https://download.abime.net/winuae/files/b/aros.rom.cpp.zip	
				
7) Download and Install Nasm (Assembler Compiler) and put it in PATH

	https://www.nasm.us/

8) In Visual Studio click on "File"->"Open"->"Project/Solution" select the folder <source directory>\od-win32\winuae_msvc15\winuae_msvc.sln (Ignore error message "Unsupported" and click ok)

9) In The solution 'winuae_msvc' you can unload or delete the following projects (and all others not needed in step 12):
	-uaeunp
	-consolewrapper
	-decompess
	-fdrawcmd
	-ipctester
	-resourcedll
	-singlefilehelper
	-wix

10) Change to 32-bit Release mode.

11) Build following projects in following order:
	build68k
	genlinetoscr
	genblitter
	gencpu
	gencomp
	prowizard
	unpackers
		
12) Switch to Test (debug build) or FullRelease (full optimized) and select either 32-bit or 64-bit. Compile.

Finished. In "D:\Amiga\" you find winuae.exe and winuae64.exe
