Name "WinUAE"
OutFile "InstallWinUAE.exe"
Caption "WinUAE installer"
ShowInstDetails show

; Some default compiler settings (uncomment and change at will):
SetCompress auto ; (can be off or force)
SetDatablockOptimize on ; (can be off)
CRCCheck on ; (can be off)
; AutoCloseWindow false ; (can be true for the window go away automatically at end)
; ShowInstDetails hide ; (can be show to have them shown, or nevershow to disable)
; SetDateSave off ; (can be on to have files restored to their orginal date)

InstallDir "$PROGRAMFILES\WinUAE"
InstallDirRegKey HKEY_CURRENT_USER "SOFTWARE\Arabuusimiehet\WinUAE" "InstallDir"
DirShow show ; (make this hide to not let the user change it)
DirText "Select the directory to install WinUAE in:"
Icon "c:\projects\winuae\src\od-win32\resources\winuae.ico"
WindowIcon on
AllowRootDirInstall true
AutoCloseWindow true

Section "" ; (default section)
SetOutPath "$INSTDIR"
File "c:\projects\winuae\distribution\*"
SetOutPath "$INSTDIR\Amiga Programs"
File "c:\projects\winuae\distribution\Amiga Programs\*"
SetOutPath "$INSTDIR\Docs"
File "c:\projects\winuae\distribution\Docs\*"
SetOutPath "$INSTDIR\Docs\Windows"
File "c:\projects\winuae\distribution\Docs\Windows\*"
CreateDirectory "$INSTDIR\Configurations"
WriteRegStr HKEY_CURRENT_USER "SOFTWARE\Arabuusimiehet\WinUAE" "InstallDir" "$INSTDIR"

ExecShell open "$INSTDIR\Docs\Readme.txt"

; add files / whatever that need to be installed here.

SectionEnd ; end of default section

; eof

