; Color-Screen Windows installer.
; The MSYS2 NSIS 3.12 packages expose their page plug-ins with incompatible
; architecture registration on some runner/toolchain combinations. Built-in
; license/directory/progress pages need no external DLL and also work with
; both x86 and amd64 Unicode installer stubs.

Name "Color-Screen"
!ifdef OUTFILE
  OutFile "${OUTFILE}"
!else
  OutFile "Color-Screen-Installer.exe"
!endif
InstallDir "$PROGRAMFILES64\Color-Screen"
RequestExecutionLevel admin
Icon "icon.ico"
UninstallIcon "icon.ico"

ShowInstDetails show
ShowUninstDetails show
LicenseData "${SOURCE_DIR}\LICENSE"
Page license
Page directory
Page instfiles

UninstPage uninstConfirm
UninstPage instfiles

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
    !insertmacro RegisterExtension "cspar" "Color-Screen JSON Parameter File"
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

    ; No System plug-in: file associations become visible at the next shell refresh.
SectionEnd

Section "Uninstall"
    Delete "$SMPROGRAMS\Color-Screen.lnk"
    Delete "$DESKTOP\Color-Screen.lnk"
    RMDir /r "$INSTDIR"

    DeleteRegKey HKLM "Software\Microsoft\Windows\CurrentVersion\Uninstall\Color-Screen"
    DeleteRegKey HKCR "Color-Screen.File"
    # Note: Extension keys are usually left alone or we could clean them up if they point to us.
    
    ; No System plug-in: file associations become visible at the next shell refresh.
SectionEnd
