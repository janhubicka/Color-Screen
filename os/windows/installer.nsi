; Color-Screen Windows installer.
; Use only built-in NSIS pages so both GCC and Clang Windows packages work
; without optional NSIS plug-in DLLs. Install per user, without an interactive
; UAC elevation request; this also permits unattended installer regression
; testing and avoids changing other users' file associations.

Name "Color-Screen"
!ifdef OUTFILE
  OutFile "${OUTFILE}"
!else
  OutFile "Color-Screen-Installer.exe"
!endif
InstallDir "$LOCALAPPDATA\Programs\Color-Screen"
RequestExecutionLevel user
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

    # Add/Remove Programs entry is per-user, matching the installation.
    # Registry keys for "Color-Screen" app
    WriteRegStr HKCU "Software\Microsoft\Windows\CurrentVersion\Uninstall\Color-Screen" "DisplayName" "Color-Screen"
    # Programs and Features must handle user-profile paths containing spaces.
    WriteRegStr HKCU "Software\Microsoft\Windows\CurrentVersion\Uninstall\Color-Screen" "UninstallString" '"$INSTDIR\uninstall.exe"'
    WriteRegStr HKCU "Software\Microsoft\Windows\CurrentVersion\Uninstall\Color-Screen" "QuietUninstallString" '"$INSTDIR\uninstall.exe" /S'
    WriteRegStr HKCU "Software\Microsoft\Windows\CurrentVersion\Uninstall\Color-Screen" "InstallLocation" "$INSTDIR"
    WriteRegStr HKCU "Software\Microsoft\Windows\CurrentVersion\Uninstall\Color-Screen" "DisplayIcon" '"$INSTDIR\colorscreen-qt.exe"'

    # Register an Open With handler for all supported extensions, without
    # replacing the operator's default JPEG/TIFF/RAW application. Each format
    # needs its own ProgID so descriptions and uninstall stay independent.
    !macro RegisterExtension Ext Desc
        # An old development installer overwrote shared defaults with one
        # Color-Screen.File class; remove only that known obsolete default.
        ReadRegStr $0 HKCU "Software\Classes\.${Ext}" ""
        StrCmp $0 "Color-Screen.File" 0 +2
            DeleteRegValue HKCU "Software\Classes\.${Ext}" ""
        WriteRegStr HKCU "Software\Classes\.${Ext}\OpenWithProgids" "Color-Screen.${Ext}" ""
        WriteRegStr HKCU "Software\Classes\Color-Screen.${Ext}" "" "${Desc}"
        WriteRegStr HKCU "Software\Classes\Color-Screen.${Ext}\shell\open\command" "" '"$INSTDIR\colorscreen-qt.exe" "%1"'
        WriteRegStr HKCU "Software\Classes\Color-Screen.${Ext}\DefaultIcon" "" "$INSTDIR\colorscreen-qt.exe,0"
    !macroend

    # Native .cspar now opens a linked source scan (with Locate fallback)
    # before applying parameters, so Explorer can launch it as a document.
    # Legacy .par remains an image-less calibration preset and is deliberately
    # imported through File -> Open Parameters instead.
    !insertmacro RegisterExtension "cspar" "Color-Screen Document"
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

    # Color-Screen native documents/projects are direct Explorer-open inputs.
    # Keep .par (which has no source-image link) independent of shell defaults.
    WriteRegStr HKCU "Software\Classes\.cspar" "" "Color-Screen.cspar"
    WriteRegStr HKCU "Software\Classes\.csprj" "" "Color-Screen.csprj"
    DeleteRegKey HKCU "Software\Classes\Color-Screen.File"

    ; No System plug-in: file associations become visible at the next shell refresh.
SectionEnd

Section "Uninstall"
    Delete "$SMPROGRAMS\Color-Screen.lnk"
    Delete "$DESKTOP\Color-Screen.lnk"
    RMDir /r "$INSTDIR"

    DeleteRegKey HKCU "Software\Microsoft\Windows\CurrentVersion\Uninstall\Color-Screen"
    # Remove only our Open With entries, and clear a file-type default only
    # when it still refers to us. Never erase another application's choice.
    !macro UnregisterExtension Ext
        DeleteRegValue HKCU "Software\Classes\.${Ext}\OpenWithProgids" "Color-Screen.${Ext}"
        DeleteRegKey /ifempty HKCU "Software\Classes\.${Ext}\OpenWithProgids"
        ReadRegStr $0 HKCU "Software\Classes\.${Ext}" ""
        StrCmp $0 "Color-Screen.${Ext}" 0 +2
            DeleteRegValue HKCU "Software\Classes\.${Ext}" ""
        DeleteRegKey /ifempty HKCU "Software\Classes\.${Ext}"
        DeleteRegKey HKCU "Software\Classes\Color-Screen.${Ext}"
    !macroend

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
    !insertmacro UnregisterExtension "cspar"
    !insertmacro UnregisterExtension "csprj"

    DeleteRegKey HKCU "Software\Classes\Color-Screen.File"

    ; No System plug-in: file associations become visible at the next shell refresh.
SectionEnd
