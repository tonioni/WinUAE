!define PRODUCT_NAME "WinUAE"
!define PRODUCT_VERSION "2.4.0"
!define PRODUCT_PUBLISHER "Arabuusimiehet"
!define PRODUCT_WEB_SITE "http://www.winuae.net/"
!define PRODUCT_DIR_REGKEY "Software\Microsoft\Windows\CurrentVersion\App Paths\winuae.exe"
!define PRODUCT_UNINST_KEY "Software\Microsoft\Windows\CurrentVersion\Uninstall\${PRODUCT_NAME}"
!define PRODUCT_UNINST_ROOT_KEY "HKLM"

;-- Your path here
!define DISTPATH "c:\projects\winuae\distribution"

SetCompressor /solid lzma
RequestExecutionLevel admin

!include "StrFunc.nsh"
!include "WinMessages.nsh"

; MUI begins ---
!include "MUI2.nsh"
; MUI Settings
!define MUI_ABORTWARNING
!define MUI_COMPONENTSPAGE_SMALLDESC
;!define MUI_FINISHPAGE_NOAUTOCLOSE
!define MUI_FINISHPAGE_RUN_NOTCHECKED
!define MUI_ICON "graphics\installer_icon.ico"
!define MUI_UNICON "graphics\installer_icon.ico"
; MUI Bitmaps
!define MUI_HEADERIMAGE
!define MUI_HEADERIMAGE_BITMAP "graphics\amiga_header.bmp"
!define MUI_WELCOMEFINISHPAGE_BITMAP "graphics\amiga_welcome.bmp"
; Welcome page
!insertmacro MUI_PAGE_WELCOME
; Components page
!insertmacro MUI_PAGE_COMPONENTS
; Directory page
!insertmacro MUI_PAGE_DIRECTORY
; Instfiles page
!insertmacro MUI_PAGE_INSTFILES
; Finish page
!define MUI_FINISHPAGE_RUN "$INSTDIR\winuae.exe"
!define MUI_FINISHPAGE_SHOWREADME "$INSTDIR\Docs\Readme.txt"
!insertmacro MUI_PAGE_FINISH
; Uninstaller pages
!insertmacro MUI_UNPAGE_INSTFILES
; Language files
!insertmacro MUI_LANGUAGE "English"
; MUI end ---

Function .onInit
  ;Find WinUAE Properties Window and close it when it's open
  System::Call 'kernel32::CreateMutexA(i 0, i 0, t "WinUAE Instantiated") i .r1 ?e'
  Pop $1
  StrCmp $1 183 0 Continue
  MessageBox MB_OK|MB_ICONEXCLAMATION "WinUAE is still running in the background, the installer will terminate it.$\nYou can do this by yourself as well before proceeding with the installation."
  FindWindow $2 "" "WinUAE Properties"
  FindWindow $3 "" "WinUAE"
  SendMessage $2 ${WM_CLOSE} 0 0
  SendMessage $3 ${WM_CLOSE} 0 0

 Continue:
  ReadRegStr $0 HKCU "Software\Arabuusimiehet\WinUAE" "InstallDir"
  StrCmp $0 "" No_WinUAE
  ;Code if WinUAE is installed
  StrCpy $INSTDIR $0
  Goto +2
 No_WinUAE:
  ;Code if WinUAE is not installed
  StrCpy $INSTDIR "$PROGRAMFILES\WinUAE"
FunctionEnd

Name "${PRODUCT_NAME} ${PRODUCT_VERSION}"
OutFile "InstallWinUAE.exe"
InstallDirRegKey HKLM "${PRODUCT_DIR_REGKEY}" ""
ShowInstDetails show
ShowUnInstDetails show

InstType "Complete" ;1
InstType "Basic"    ;2
InstType "Basic with Shortcuts" ;3

Section "WinUAE (required)" secWinUAE_files
  SectionIn 1 2 3 RO
  SetOutPath "$INSTDIR\"
  ;SetOverwrite ifnewer
 ;-- Copy WinUAE
  File "${DISTPATH}\winuae.exe"
  SetOutPath "$INSTDIR\Docs"
  File "${DISTPATH}\Docs\Readme.txt"
 ;-- Creates the necessary registry entrys
  WriteRegStr HKCU "Software\Arabuusimiehet\WinUAE" "InstallDir" "$INSTDIR"
 ;-- force ROM rescan after install
  DeleteRegKey HKCU "Software\Arabuusimiehet\WinUAE\DetectedROMs"
SectionEnd

Section "Host-Configurations" secExConfig
  SectionIn 1
  SetOutPath "$INSTDIR\Configurations\Host"
  SetOverwrite ifnewer
 ;-- Copy Example Host Configurations
  File "${DISTPATH}\Configurations\Host\Fullscreen (640x480).uae"
  File "${DISTPATH}\Configurations\Host\Fullscreen (800x600).uae"
  File "${DISTPATH}\Configurations\Host\FullwindowD3D.uae"
  File "${DISTPATH}\Configurations\Host\Windowed.uae"
SectionEnd

SubSection "Additional files" secAdditionalFiles
 Section "Docs" secDocs
  SectionIn 1
  SetOutPath "$INSTDIR\Docs"
  SetOverwrite ifnewer
 ;-- Copy Docs
  File "${DISTPATH}\Docs\Whatsnew-jit"
  File "${DISTPATH}\Docs\README.umisef"
  File "${DISTPATH}\Docs\README.pci"
  File "${DISTPATH}\Docs\README.JIT-tuning"
  File "${DISTPATH}\Docs\README.compemu"
  File "${DISTPATH}\Docs\History_old.txt"
  File "${DISTPATH}\Docs\winuaechangelog.txt"
 ;-- Copy Docs for Windows
  SetOutPath "$INSTDIR\Docs\Windows"
  File "${DISTPATH}\Docs\Windows\UAEHowTo.txt"
  File "${DISTPATH}\Docs\Windows\Translation.txt"
  File "${DISTPATH}\Docs\Windows\AmigaProg.txt"
 SectionEnd

 Section "Amiga programs" secAmigaprograms
  SectionIn 1
  SetOutPath "$INSTDIR\Amiga Programs"
  SetOverwrite ifnewer
 ;-- Copy the Amiga Programs
  File "${DISTPATH}\Amiga Programs\winxpprinthelper.info"
  File "${DISTPATH}\Amiga Programs\winxpprinthelper"
  File "${DISTPATH}\Amiga Programs\winuaeenforcer.txt"
  File "${DISTPATH}\Amiga Programs\winuaeenforcer"
  File "${DISTPATH}\Amiga Programs\winuaeclip.txt"
  File "${DISTPATH}\Amiga Programs\winuaeclip.info"
  File "${DISTPATH}\Amiga Programs\winuaeclip"
  File "${DISTPATH}\Amiga Programs\uaectrl"
  File "${DISTPATH}\Amiga Programs\uae-control.info"
  File "${DISTPATH}\Amiga Programs\uae-control"
  File "${DISTPATH}\Amiga Programs\uae-configuration"
  File "${DISTPATH}\Amiga Programs\uae_rcli"
  File "${DISTPATH}\Amiga Programs\UAE_German.info"
  File "${DISTPATH}\Amiga Programs\UAE_German"
  File "${DISTPATH}\Amiga Programs\transrom"
  File "${DISTPATH}\Amiga Programs\transdisk"
  File "${DISTPATH}\Amiga Programs\timehack"
  File "${DISTPATH}\Amiga Programs\rtg.library"
  File "${DISTPATH}\Amiga Programs\p96refresh"
  File "${DISTPATH}\Amiga Programs\german_KeyMap_new.zip"
  File "${DISTPATH}\Amiga Programs\amigaprog.txt"
  File "${DISTPATH}\Amiga Programs\ahidriver.zip"
  File "${DISTPATH}\Amiga Programs\sources.zip"
 SectionEnd
SubSectionEnd

;SubSection "Translations" secTranslations
; Section "German" secTransGerman
;  SectionIn 1
;  SetOutPath "$INSTDIR\"
;  File "${DISTPATH}\WinUAE_German.dll"
; SectionEnd
 
; Section "Foo" secTransFoo
;  SectionIn 1
;  SetOutPath "$INSTDIR\"
;  File "${DISTPATH}\WinUAE_Foo.dll"
; SectionEnd
;SubSectionEnd

SubSection "Shortcuts" secShortcuts
 Section "Startmenu" secStartmenu
  SectionIn 1 3
  CreateDirectory "$SMPROGRAMS\WinUAE"
  CreateShortCut "$SMPROGRAMS\WinUAE\WinUAE.lnk" "$INSTDIR\winuae.exe"
  CreateShortCut "$SMPROGRAMS\WinUAE\ReadMe.lnk" "$INSTDIR\Docs\Readme.txt"
  CreateShortCut "$SMPROGRAMS\WinUAE\Uninstall.lnk" "$INSTDIR\uninstall_winuae.exe"
 SectionEnd

 Section "Desktop" secDesktop
  SectionIn 1 3
  CreateShortCut "$DESKTOP\WinUAE.lnk" "$INSTDIR\winuae.exe"
 SectionEnd

 Section "Quick Launch" secQuickLaunch
  SectionIn 1 3
  CreateShortcut "$QUICKLAUNCH\WinUAE.lnk" "$INSTDIR\winuae.exe"
 SectionEnd
SubSectionend

Section -Post
  WriteUninstaller "$INSTDIR\uninstall_winuae.exe"
  WriteRegStr HKLM "${PRODUCT_DIR_REGKEY}" "" "$INSTDIR\winuae.exe"
  WriteRegStr ${PRODUCT_UNINST_ROOT_KEY} "${PRODUCT_UNINST_KEY}" "DisplayName" "$(^Name)"
  WriteRegStr ${PRODUCT_UNINST_ROOT_KEY} "${PRODUCT_UNINST_KEY}" "UninstallString" "$INSTDIR\uninstall_winuae.exe"
  WriteRegStr ${PRODUCT_UNINST_ROOT_KEY} "${PRODUCT_UNINST_KEY}" "DisplayIcon" "$INSTDIR\winuae.exe"
  WriteRegStr ${PRODUCT_UNINST_ROOT_KEY} "${PRODUCT_UNINST_KEY}" "DisplayVersion" "${PRODUCT_VERSION}"
  WriteRegStr ${PRODUCT_UNINST_ROOT_KEY} "${PRODUCT_UNINST_KEY}" "URLInfoAbout" "${PRODUCT_WEB_SITE}"
  WriteRegStr ${PRODUCT_UNINST_ROOT_KEY} "${PRODUCT_UNINST_KEY}" "Publisher" "${PRODUCT_PUBLISHER}"
SectionEnd

; Section descriptions
!insertmacro MUI_FUNCTION_DESCRIPTION_BEGIN
  !insertmacro MUI_DESCRIPTION_TEXT ${secWinUAE_files} "WinUAE (WinUAE.exe, readme.txt)"
  !insertmacro MUI_DESCRIPTION_TEXT ${secExConfig} "Example Host-Configurations"
  !insertmacro MUI_DESCRIPTION_TEXT ${secAdditionalFiles} "Additional files (Docs, Amiga programs)"
  !insertmacro MUI_DESCRIPTION_TEXT ${secAmigaprograms} "Amiga programs"
  !insertmacro MUI_DESCRIPTION_TEXT ${secDocs} "Documentation"
  !insertmacro MUI_DESCRIPTION_TEXT ${secShortcuts} "Shortcuts (Startmenu, Desktop, Quick Launch)"
  !insertmacro MUI_DESCRIPTION_TEXT ${secStartmenu} "Create a startmenu entry"
  !insertmacro MUI_DESCRIPTION_TEXT ${secDesktop} "Create a desktop icon"
  !insertmacro MUI_DESCRIPTION_TEXT ${secQuickLaunch} "Create a Quick Launch icon"
;  !insertmacro MUI_DESCRIPTION_TEXT ${secTranslations} "WinUAE Translations"
!insertmacro MUI_FUNCTION_DESCRIPTION_END


Function un.onUninstSuccess
  HideWindow
  MessageBox MB_ICONINFORMATION|MB_OK "$(^Name) was successfully removed from your computer."
FunctionEnd

Function un.onInit
  MessageBox MB_ICONQUESTION|MB_YESNO|MB_DEFBUTTON2 "Are you sure you want to completely remove $(^Name) and all of its components?" IDYES +2
  Abort
FunctionEnd

Section Uninstall
  Delete "$INSTDIR\uninstall_winuae.exe"
  Delete "$INSTDIR\Docs\Windows\AmigaProg.txt"
  Delete "$INSTDIR\Docs\Windows\Translation.txt"
  Delete "$INSTDIR\Docs\Windows\UAEHowTo.txt"
  Delete "$INSTDIR\Docs\History_old.txt"
  Delete "$INSTDIR\Docs\README.compemu"
  Delete "$INSTDIR\Docs\README.JIT-tuning"
  Delete "$INSTDIR\Docs\README.pci"
  Delete "$INSTDIR\Docs\Readme.txt"
  Delete "$INSTDIR\Docs\Readme.txt.bak"
  Delete "$INSTDIR\Docs\README.umisef"
  Delete "$INSTDIR\Docs\Whatsnew-jit"
  Delete "$INSTDIR\Docs\winuaechangelog.txt"
  Delete "$INSTDIR\Amiga Programs\ahidriver.zip"
  Delete "$INSTDIR\Amiga Programs\amigaprog.txt"
  Delete "$INSTDIR\Amiga Programs\german_KeyMap_new.zip"
  Delete "$INSTDIR\Amiga Programs\mousehack"
  Delete "$INSTDIR\Amiga Programs\p96_uae_tweak"
  Delete "$INSTDIR\Amiga Programs\p96refresh"
  Delete "$INSTDIR\Amiga Programs\picasso96fix"
  Delete "$INSTDIR\Amiga Programs\rtg.library"
  Delete "$INSTDIR\Amiga Programs\timehack"
  Delete "$INSTDIR\Amiga Programs\transdisk"
  Delete "$INSTDIR\Amiga Programs\transrom"
  Delete "$INSTDIR\Amiga Programs\UAE_German"
  Delete "$INSTDIR\Amiga Programs\UAE_German.info"
  Delete "$INSTDIR\Amiga Programs\uae_rcli"
  Delete "$INSTDIR\Amiga Programs\uae-configuration"
  Delete "$INSTDIR\Amiga Programs\uae-control"
  Delete "$INSTDIR\Amiga Programs\uae-control.info"
  Delete "$INSTDIR\Amiga Programs\uaectrl"
  Delete "$INSTDIR\Amiga Programs\winuaeclip"
  Delete "$INSTDIR\Amiga Programs\winuaeclip.info"
  Delete "$INSTDIR\Amiga Programs\winuaeclip.txt"
  Delete "$INSTDIR\Amiga Programs\winuaeenforcer"
  Delete "$INSTDIR\Amiga Programs\winuaeenforcer.txt"
  Delete "$INSTDIR\Amiga Programs\winxpprinthelper"
  Delete "$INSTDIR\Amiga Programs\winxpprinthelper.info"
  Delete "$INSTDIR\Amiga Programs\sources\p96refresh.ab2"
  Delete "$INSTDIR\Amiga Programs\sources\uae-configuration.s"
  Delete "$INSTDIR\Amiga Programs\sources\uae-configuration.c"
  Delete "$INSTDIR\Amiga Programs\sources\picasso96fix.lha"
  Delete "$INSTDIR\Amiga Programs\sources.zip"
  Delete "$INSTDIR\Configurations\Host\Fullscreen (640x480).uae"
  Delete "$INSTDIR\Configurations\Host\Fullscreen (800x600).uae"
  Delete "$INSTDIR\Configurations\Host\FullwindowD3D.uae"
  Delete "$INSTDIR\Configurations\Host\Windowed.uae"
  Delete "$INSTDIR\winuaebootlog.txt"
  Delete "$INSTDIR\winuaelog.txt"
  Delete "$INSTDIR\winuae.exe"
  Delete "$INSTDIR\zlib1.dll"
  Delete "$INSTDIR\WinUAE_German.dll"

  Delete "$SMPROGRAMS\WinUAE\Uninstall.lnk"
  Delete "$SMPROGRAMS\WinUAE\WinUAE.lnk"
  Delete "$SMPROGRAMS\WinUAE\ReadMe.lnk"
  Delete "$DESKTOP\WinUAE.lnk"
  Delete "$QUICKLAUNCH\WinUAE.lnk"

  RMDir "$INSTDIR\Docs\Windows"
  RMDir "$INSTDIR\Docs"
  RMDir "$INSTDIR\Amiga Programs"
  RMDir "$INSTDIR\Configurations\Host"
  RMDir "$INSTDIR\Configurations\Hardware"
  RMDir "$INSTDIR\Configurations"
  RMDir "$INSTDIR\Roms"
  RMDir "$INSTDIR\SaveImages"
  RMDir "$INSTDIR\SaveStates"
  RMDir "$INSTDIR\ScreenShots"
  RMDir "$INSTDIR\InputRecordings"
  RMDir "$INSTDIR\plugins\codecs"
  RMDir "$INSTDIR\plugins"

  RMDir "$SMPROGRAMS\WinUAE"
  RMDir "$INSTDIR\"
  RMDir ""

  DeleteRegKey HKCU "Software\Arabuusimiehet"
  DeleteRegKey ${PRODUCT_UNINST_ROOT_KEY} "${PRODUCT_UNINST_KEY}"
  DeleteRegKey HKLM "${PRODUCT_DIR_REGKEY}"
  SetAutoClose true
SectionEnd
