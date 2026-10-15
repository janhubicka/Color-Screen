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

    # Register an Open With handler for all supported extensions, without
    # replacing the operator's default JPEG/TIFF/RAW application. Each format
    # needs its own ProgID so descriptions and uninstall stay independent.
    !macro RegisterExtension Ext Desc
        # An old development installer overwrote shared defaults with one
        # Color-Screen.File class; remove only that known obsolete default.
        ReadRegStr $0 HKCR ".${Ext}" ""
        StrCmp $0 "Color-Screen.File" 0 +2
            DeleteRegValue HKCR ".${Ext}" ""
        WriteRegStr HKCR ".${Ext}\OpenWithProgids" "Color-Screen.${Ext}" ""
        WriteRegStr HKCR "Color-Screen.${Ext}" "" "${Desc}"
        WriteRegStr HKCR "Color-Screen.${Ext}\shell\open\command" "" '"$INSTDIR\colorscreen-qt.exe" "%1"'
        WriteRegStr HKCR "Color-Screen.${Ext}\DefaultIcon" "" "$INSTDIR\colorscreen-qt.exe,0"
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

    # Own only Color-Screen-specific .cspar and .csprj defaults. A generic
    # .par or image extension remains under the user's existing default app.
    WriteRegStr HKCR ".cspar" "" "Color-Screen.cspar"
    WriteRegStr HKCR ".csprj" "" "Color-Screen.csprj"
    DeleteRegKey HKCR "Color-Screen.File"

    ; No System plug-in: file associations become visible at the next shell refresh.
SectionEnd

Section "Uninstall"
    Delete "$SMPROGRAMS\Color-Screen.lnk"
    Delete "$DESKTOP\Color-Screen.lnk"
    RMDir /r "$INSTDIR"

    DeleteRegKey HKLM "Software\Microsoft\Windows\CurrentVersion\Uninstall\Color-Screen"
    # Remove only our Open With entries, and clear a file-type default only
    # when it still refers to us. Never erase another application's choice.
    !macro UnregisterExtension Ext
        DeleteRegValue HKCR ".${Ext}\OpenWithProgids" "Color-Screen.${Ext}"
        DeleteRegKey /ifempty HKCR ".${Ext}\OpenWithProgids"
        ReadRegStr $0 HKCR ".${Ext}" ""
        StrCmp $0 "Color-Screen.${Ext}" 0 +2
            DeleteRegValue HKCR ".${Ext}" ""
        DeleteRegKey /ifempty HKCR ".${Ext}"
        DeleteRegKey HKCR "Color-Screen.${Ext}"
    !macroend

    !insertmacro UnregisterExtension "par"
    !insertmacro UnregisterExtension "cspar"
    !insertmacro UnregisterExtension "tif"
    !insertmacro UnregisterExtension "tiff"
    !insertmacro UnregisterExtension "jpg"
    !insertmacro UnregisterExtension "jpeg"
    !insertmacro UnregisterExtension "raw"
    !insertmacro UnregisterExtension "dng"
    !insertmacro UnregisterExtension "iiq"
    !insertmacro UnregisterExtension "nef"
    !insertmacro UnregisterExtension "cr2"
    !insertmacro UnregisterExtension "eip"
    !insertmacro UnregisterExtension "arw"
    !insertmacro UnregisterExtension "raf"
    !insertmacro UnregisterExtension "arq"
    !insertmacro UnregisterExtension "csprj"

    DeleteRegKey HKCR "Color-Screen.File"

    ; No System plug-in: file associations become visible at the next shell refresh.
SectionEnd
