!include "MUI2.nsh"
!include "FileFunc.nsh"

Name "Color-Screen"
OutFile "Color-Screen-Installer.exe"
InstallDir "$PROGRAMFILES64\Color-Screen"
RequestExecutionLevel admin

!define MUI_ABORTWARNING
!define MUI_ICON "icon.ico"
!define MUI_UNICON "icon.ico"

!insertmacro MUI_PAGE_WELCOME
!insertmacro MUI_PAGE_LICENSE "${SOURCE_DIR}\LICENSE"
!insertmacro MUI_PAGE_DIRECTORY
!insertmacro MUI_PAGE_INSTFILES

# Finish page with Readme option
!define MUI_FINISHPAGE_SHOWREADME "$INSTDIR\README.txt"
!insertmacro MUI_PAGE_FINISH

!insertmacro MUI_UNPAGE_WELCOME
!insertmacro MUI_UNPAGE_CONFIRM
!insertmacro MUI_UNPAGE_INSTFILES
!insertmacro MUI_UNPAGE_FINISH

!insertmacro MUI_LANGUAGE "English"

Section "Install"
    SetOutPath "$INSTDIR"
    File /r "${SOURCE_DIR}\bin\*.*"
    File /r "${SOURCE_DIR}\share"
    File "/oname=LICENSE.txt" "${SOURCE_DIR}\LICENSE"
    File "/oname=README.txt" "${SOURCE_DIR}\README.md"

    WriteUninstaller "$INSTDIR\uninstall.exe"

    # Create shortcuts
    CreateShortcut "$SMPROGRAMS\Color-Screen.lnk" "$INSTDIR\colorscreen-qt.exe" "" "$INSTDIR\colorscreen-qt.exe" 0
    CreateShortcut "$DESKTOP\Color-Screen.lnk" "$INSTDIR\colorscreen-qt.exe" "" "$INSTDIR\colorscreen-qt.exe" 0

    # Registry keys for "Color-Screen" app
    WriteRegStr HKLM "Software\Microsoft\Windows\CurrentVersion\Uninstall\Color-Screen" "DisplayName" "Color-Screen"
    WriteRegStr HKLM "Software\Microsoft\Windows\CurrentVersion\Uninstall\Color-Screen" "UninstallString" "$INSTDIR\uninstall.exe"
    WriteRegStr HKLM "Software\Microsoft\Windows\CurrentVersion\Uninstall\Color-Screen" "DisplayIcon" "$INSTDIR\colorscreen-qt.exe"

    # File associations
    !macro RegisterExtension Ext Desc
        WriteRegStr HKCR ".${Ext}" "" "Color-Screen.File"
        WriteRegStr HKCR "Color-Screen.File\shell\open\command" "" '"$INSTDIR\colorscreen-qt.exe" "%1"'
        WriteRegStr HKCR "Color-Screen.File" "" "${Desc}"
        WriteRegStr HKCR "Color-Screen.File\DefaultIcon" "" "$INSTDIR\colorscreen-qt.exe,0"
    !macroend

    !insertmacro RegisterExtension "par" "Color-Screen Parameter File"
    !insertmacro RegisterExtension "tif" "TIFF Image"
    !insertmacro RegisterExtension "tiff" "TIFF Image"
    !insertmacro RegisterExtension "jpg" "JPEG Image"
    !insertmacro RegisterExtension "jpeg" "JPEG Image"
    !insertmacro RegisterExtension "raw" "Raw Image"
    !insertmacro RegisterExtension "dng" "DNG Image"
    !insertmacro RegisterExtension "iiq" "Phase One Raw Image"
    !insertmacro RegisterExtension "nef" "Nikon Raw Image"
    !insertmacro RegisterExtension "cr2" "Canon Raw Image"
    !insertmacro RegisterExtension "eip" "Enhanced Image Package"
    !insertmacro RegisterExtension "arw" "Sony Raw Image"
    !insertmacro RegisterExtension "raf" "Fujifilm Raw Image"
    !insertmacro RegisterExtension "arq" "Sony Pixel Shift Raw Image"
    !insertmacro RegisterExtension "csprj" "Color-Screen Project"

    System::Call 'Shell32::SHChangeNotify(i 0x08000000, i 0, i 0, i 0)'
SectionEnd

Section "Uninstall"
    Delete "$SMPROGRAMS\Color-Screen.lnk"
    Delete "$DESKTOP\Color-Screen.lnk"
    RMDir /r "$INSTDIR"

    DeleteRegKey HKLM "Software\Microsoft\Windows\CurrentVersion\Uninstall\Color-Screen"
    DeleteRegKey HKCR "Color-Screen.File"
    # Note: Extension keys are usually left alone or we could clean them up if they point to us.
    
    System::Call 'Shell32::SHChangeNotify(i 0x08000000, i 0, i 0, i 0)'
SectionEnd
