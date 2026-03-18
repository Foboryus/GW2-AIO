; GW2 AIO Manager Installer Script
; NSIS 3.x

!include "MUI2.nsh"
!include "FileFunc.nsh"

; Application info
!define APPNAME "GW2 AIO Manager"
!define COMPANYNAME "GW2AIO"
!define DESCRIPTION "All-in-one overlay manager for Guild Wars 2"
!define VERSIONMAJOR 0
!define VERSIONMINOR 1
!define VERSIONBUILD 0
!define HELPURL "https://github.com/Foboryus/GW2-AIO"
!define UPDATEURL "https://github.com/Foboryus/GW2-AIO/releases"

; Installer settings
Name "${APPNAME}"
OutFile "GW2AIO_Setup_${VERSIONMAJOR}.${VERSIONMINOR}.${VERSIONBUILD}.exe"
InstallDir "$PROGRAMFILES64\${APPNAME}"
InstallDirRegKey HKLM "Software\${COMPANYNAME}\${APPNAME}" "InstallDir"
RequestExecutionLevel admin

; Interface settings
!define MUI_ABORTWARNING
!define MUI_ICON "resources\icons\app_icon.ico"
!define MUI_UNICON "resources\icons\app_icon.ico"
!define MUI_HEADERIMAGE
!define MUI_WELCOMEFINISHPAGE_BITMAP "resources\installer_banner.bmp"

; Pages
!insertmacro MUI_PAGE_WELCOME
!insertmacro MUI_PAGE_LICENSE "LICENSE"
!insertmacro MUI_PAGE_DIRECTORY
!insertmacro MUI_PAGE_INSTFILES
!insertmacro MUI_PAGE_FINISH

!insertmacro MUI_UNPAGE_CONFIRM
!insertmacro MUI_UNPAGE_INSTFILES

; Language
!insertmacro MUI_LANGUAGE "English"

; Installer sections
Section "Main Application" SecMain
    SetOutPath "$INSTDIR"
    
    ; Main executable
    File "build\Release\GW2AIO.exe"
    
    ; Helper DLL (for GPU detection and window positioning)
    File "build\Release\GW2AIOHelper.dll"
    
    ; Qt libraries
    File "build\Release\Qt6Core.dll"
    File "build\Release\Qt6Gui.dll"
    File "build\Release\Qt6Widgets.dll"
    File "build\Release\Qt6Network.dll"
    File "build\Release\Qt6Qml.dll"
    File "build\Release\Qt6Quick.dll"
    File "build\Release\Qt6OpenGL.dll"
    
    ; Qt plugins
    SetOutPath "$INSTDIR\platforms"
    File "build\Release\platforms\qwindows.dll"
    
    SetOutPath "$INSTDIR\styles"
    File "build\Release\styles\qwindowsvistastyle.dll"
    
    SetOutPath "$INSTDIR\imageformats"
    File "build\Release\imageformats\*.dll"
    
    ; Resources
    SetOutPath "$INSTDIR\resources"
    File /r "resources\*.*"
    
    ; Create data directories
    CreateDirectory "$APPDATA\${APPNAME}\logs"
    CreateDirectory "$APPDATA\${APPNAME}\MarkerPacks"
    CreateDirectory "$APPDATA\${APPNAME}\BlishModules"
    CreateDirectory "$APPDATA\${APPNAME}\RadialMenus"
    
    ; Write registry keys
    WriteRegStr HKLM "Software\${COMPANYNAME}\${APPNAME}" "InstallDir" "$INSTDIR"
    WriteRegStr HKLM "Software\${COMPANYNAME}\${APPNAME}" "Version" "${VERSIONMAJOR}.${VERSIONMINOR}.${VERSIONBUILD}"
    
    ; Uninstaller
    WriteUninstaller "$INSTDIR\Uninstall.exe"
    
    ; Add to Programs and Features
    WriteRegStr HKLM "Software\Microsoft\Windows\CurrentVersion\Uninstall\${COMPANYNAME} ${APPNAME}" \
                     "DisplayName" "${APPNAME}"
    WriteRegStr HKLM "Software\Microsoft\Windows\CurrentVersion\Uninstall\${COMPANYNAME} ${APPNAME}" \
                     "UninstallString" "$\"$INSTDIR\Uninstall.exe$\""
    WriteRegStr HKLM "Software\Microsoft\Windows\CurrentVersion\Uninstall\${COMPANYNAME} ${APPNAME}" \
                     "InstallLocation" "$\"$INSTDIR$\""
    WriteRegStr HKLM "Software\Microsoft\Windows\CurrentVersion\Uninstall\${COMPANYNAME} ${APPNAME}" \
                     "DisplayIcon" "$\"$INSTDIR\GW2AIO.exe$\""
    WriteRegStr HKLM "Software\Microsoft\Windows\CurrentVersion\Uninstall\${COMPANYNAME} ${APPNAME}" \
                     "Publisher" "${COMPANYNAME}"
    WriteRegStr HKLM "Software\Microsoft\Windows\CurrentVersion\Uninstall\${COMPANYNAME} ${APPNAME}" \
                     "HelpLink" "${HELPURL}"
    WriteRegStr HKLM "Software\Microsoft\Windows\CurrentVersion\Uninstall\${COMPANYNAME} ${APPNAME}" \
                     "URLUpdateInfo" "${UPDATEURL}"
    WriteRegStr HKLM "Software\Microsoft\Windows\CurrentVersion\Uninstall\${COMPANYNAME} ${APPNAME}" \
                     "DisplayVersion" "${VERSIONMAJOR}.${VERSIONMINOR}.${VERSIONBUILD}"
    WriteRegDWORD HKLM "Software\Microsoft\Windows\CurrentVersion\Uninstall\${COMPANYNAME} ${APPNAME}" \
                     "VersionMajor" ${VERSIONMAJOR}
    WriteRegDWORD HKLM "Software\Microsoft\Windows\CurrentVersion\Uninstall\${COMPANYNAME} ${APPNAME}" \
                     "VersionMinor" ${VERSIONMINOR}
    WriteRegDWORD HKLM "Software\Microsoft\Windows\CurrentVersion\Uninstall\${COMPANYNAME} ${APPNAME}" \
                     "NoModify" 1
    WriteRegDWORD HKLM "Software\Microsoft\Windows\CurrentVersion\Uninstall\${COMPANYNAME} ${APPNAME}" \
                     "NoRepair" 1
    
    ; Calculate size
    ${GetSize} "$INSTDIR" "/S=0K" $0 $1 $2
    IntFmt $0 "0x%08X" $0
    WriteRegDWORD HKLM "Software\Microsoft\Windows\CurrentVersion\Uninstall\${COMPANYNAME} ${APPNAME}" \
                     "EstimatedSize" "$0"
SectionEnd

Section "Start Menu Shortcuts" SecStartMenu
    CreateDirectory "$SMPROGRAMS\${APPNAME}"
    CreateShortcut "$SMPROGRAMS\${APPNAME}\${APPNAME}.lnk" "$INSTDIR\GW2AIO.exe"
    CreateShortcut "$SMPROGRAMS\${APPNAME}\Uninstall.lnk" "$INSTDIR\Uninstall.exe"
SectionEnd

Section "Desktop Shortcut" SecDesktop
    CreateShortcut "$DESKTOP\${APPNAME}.lnk" "$INSTDIR\GW2AIO.exe"
SectionEnd

; Uninstaller
Section "Uninstall"
    ; Remove files
    Delete "$INSTDIR\GW2AIO.exe"
    Delete "$INSTDIR\*.dll"
    RMDir /r "$INSTDIR\platforms"
    RMDir /r "$INSTDIR\styles"
    RMDir /r "$INSTDIR\imageformats"
    RMDir /r "$INSTDIR\resources"
    Delete "$INSTDIR\Uninstall.exe"
    RMDir "$INSTDIR"
    
    ; Remove shortcuts
    Delete "$DESKTOP\${APPNAME}.lnk"
    Delete "$SMPROGRAMS\${APPNAME}\${APPNAME}.lnk"
    Delete "$SMPROGRAMS\${APPNAME}\Uninstall.lnk"
    RMDir "$SMPROGRAMS\${APPNAME}"
    
    ; Remove registry keys
    DeleteRegKey HKLM "Software\Microsoft\Windows\CurrentVersion\Uninstall\${COMPANYNAME} ${APPNAME}"
    DeleteRegKey HKLM "Software\${COMPANYNAME}\${APPNAME}"
    
    ; Note: Don't delete user data in AppData
SectionEnd

; Section descriptions
!insertmacro MUI_FUNCTION_DESCRIPTION_BEGIN
    !insertmacro MUI_DESCRIPTION_TEXT ${SecMain} "Core application files (required)"
    !insertmacro MUI_DESCRIPTION_TEXT ${SecStartMenu} "Add shortcuts to Start Menu"
    !insertmacro MUI_DESCRIPTION_TEXT ${SecDesktop} "Add shortcut to Desktop"
!insertmacro MUI_FUNCTION_DESCRIPTION_END
