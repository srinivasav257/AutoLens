; ============================================================================
; AutoLens NSIS Installer Script
; ============================================================================
; NSIS (Nullsoft Scriptable Install System) is a free Windows installer creator.
; This script is compiled by makensis.exe to produce a single Setup.exe that:
;   1. Copies all application files to Program Files\AutoLens
;   2. Creates a Start Menu shortcut
;   3. Creates an uninstaller and registers it in "Add/Remove Programs"
;
; NSIS docs: https://nsis.sourceforge.io/Docs/
; ============================================================================

; --- Defines (set at compile time via /D flag from the batch file) ----------
; If not passed via command line, fall back to defaults here:
!ifndef APP_VERSION
  !define APP_VERSION "1.0.0"
!endif
!ifndef DIST_DIR
  !define DIST_DIR "..\dist\AutoLens"
!endif
!ifndef OUTPUT_DIR
  !define OUTPUT_DIR "..\dist"
!endif

; Application metadata
!define APP_NAME        "AutoLens"
!define APP_PUBLISHER   "AutoLens Dev Team"
!define APP_URL         "https://github.com/your-repo/AutoLens"
!define APP_EXE         "AutoLens.exe"
!define INSTALL_DIR     "$PROGRAMFILES64\${APP_NAME}"
!define UNINSTALL_REG   "Software\Microsoft\Windows\CurrentVersion\Uninstall\${APP_NAME}"

; ============================================================================
;  MUI2 — Modern User Interface (wizard-style installer pages)
;  MUI2 provides the professional-looking pages (Welcome, License, Directory,
;  Installing, Finish) that users expect from Windows installers.
; ============================================================================
!include "MUI2.nsh"

; MUI settings
!define MUI_ABORTWARNING                          ; Ask "are you sure?" on cancel
!define MUI_ICON    "${NSISDIR}\Contrib\Graphics\Icons\modern-install.ico"
!define MUI_UNICON  "${NSISDIR}\Contrib\Graphics\Icons\modern-uninstall.ico"

; Optional: custom header/wizard graphics (uncomment if you add image files)
; !define MUI_HEADERIMAGE
; !define MUI_HEADERIMAGE_BITMAP "header.bmp"
; !define MUI_WELCOMEFINISHPAGE_BITMAP "sidebar.bmp"

; ============================================================================
;  Installer Attributes
; ============================================================================
Name                "${APP_NAME} ${APP_VERSION}"
OutFile             "${OUTPUT_DIR}\AutoLens-v${APP_VERSION}-Setup.exe"
InstallDir          "${INSTALL_DIR}"
InstallDirRegKey    HKLM "Software\${APP_NAME}" "InstallDir"
RequestExecutionLevel admin   ; Require admin rights (writing to Program Files)
SetCompressor       /SOLID lzma  ; LZMA = best compression ratio (like 7-Zip)
ShowInstDetails     show
ShowUnInstDetails   show

; ============================================================================
;  Installer Pages (in order they appear to the user)
; ============================================================================
!insertmacro MUI_PAGE_WELCOME           ; "Welcome to the AutoLens Setup Wizard"
!insertmacro MUI_PAGE_LICENSE           "..\LICENSE.txt" ; Show license (if exists)
; Note: If LICENSE.txt doesn't exist, comment out the line above and uncomment:
; !insertmacro MUI_PAGE_LICENSE          "AutoLens_License.txt"
!insertmacro MUI_PAGE_DIRECTORY         ; Let user choose install folder
!insertmacro MUI_PAGE_INSTFILES         ; Show progress bar while copying files
!insertmacro MUI_PAGE_FINISH            ; "Installation complete, launch now?"

; Uninstaller Pages
!insertmacro MUI_UNPAGE_CONFIRM         ; "Are you sure you want to uninstall?"
!insertmacro MUI_UNPAGE_INSTFILES       ; Show progress while removing files
!insertmacro MUI_UNPAGE_FINISH          ; "Uninstallation complete"

; Language (must come AFTER page macros)
!insertmacro MUI_LANGUAGE "English"

; ============================================================================
;  Version Info (shows in Properties → Details tab of the Setup.exe)
; ============================================================================
VIProductVersion "${APP_VERSION}.0"
VIAddVersionKey "ProductName"      "${APP_NAME}"
VIAddVersionKey "ProductVersion"   "${APP_VERSION}"
VIAddVersionKey "CompanyName"      "${APP_PUBLISHER}"
VIAddVersionKey "LegalCopyright"   "Copyright 2026 ${APP_PUBLISHER}"
VIAddVersionKey "FileDescription"  "${APP_NAME} Installer"
VIAddVersionKey "FileVersion"      "${APP_VERSION}"

; ============================================================================
;  SECTION: Install — the main installation logic
;  Each Section {} block is a component the user can optionally select.
;  We have one mandatory section here (no optional components).
; ============================================================================
Section "MainSection" SEC01

    ; Set the output path — all File commands below copy TO this directory
    SetOutPath "$INSTDIR"

    ; -----------------------------------------------------------------------
    ;  Copy all files from the staged distribution folder
    ;  /r = recursive (includes all sub-directories like platforms\, qml\, etc.)
    ; -----------------------------------------------------------------------
    File /r "${DIST_DIR}\*.*"

    ; -----------------------------------------------------------------------
    ;  Create Start Menu shortcut
    ;  $SMPROGRAMS = "C:\ProgramData\Microsoft\Windows\Start Menu\Programs"
    ; -----------------------------------------------------------------------
    CreateDirectory "$SMPROGRAMS\${APP_NAME}"
    CreateShortcut  "$SMPROGRAMS\${APP_NAME}\${APP_NAME}.lnk" \
                    "$INSTDIR\${APP_EXE}" \
                    "" \
                    "$INSTDIR\${APP_EXE}" 0

    ; -----------------------------------------------------------------------
    ;  Create Desktop shortcut (optional — users can delete it)
    ; -----------------------------------------------------------------------
    CreateShortcut  "$DESKTOP\${APP_NAME}.lnk" \
                    "$INSTDIR\${APP_EXE}" \
                    "" \
                    "$INSTDIR\${APP_EXE}" 0

    ; -----------------------------------------------------------------------
    ;  Write registry keys for "Add/Remove Programs" (Control Panel)
    ;  Without this, Windows won't show the app in the uninstall list.
    ; -----------------------------------------------------------------------
    WriteRegStr   HKLM "${UNINSTALL_REG}" "DisplayName"          "${APP_NAME} ${APP_VERSION}"
    WriteRegStr   HKLM "${UNINSTALL_REG}" "DisplayVersion"        "${APP_VERSION}"
    WriteRegStr   HKLM "${UNINSTALL_REG}" "Publisher"             "${APP_PUBLISHER}"
    WriteRegStr   HKLM "${UNINSTALL_REG}" "URLInfoAbout"          "${APP_URL}"
    WriteRegStr   HKLM "${UNINSTALL_REG}" "InstallLocation"       "$INSTDIR"
    WriteRegStr   HKLM "${UNINSTALL_REG}" "UninstallString"       "$INSTDIR\Uninstall.exe"
    WriteRegStr   HKLM "${UNINSTALL_REG}" "QuietUninstallString"  "$INSTDIR\Uninstall.exe /S"
    WriteRegDWORD HKLM "${UNINSTALL_REG}" "NoModify"              1
    WriteRegDWORD HKLM "${UNINSTALL_REG}" "NoRepair"              1

    ; -----------------------------------------------------------------------
    ;  Record install location so re-runs can find it
    ; -----------------------------------------------------------------------
    WriteRegStr HKLM "Software\${APP_NAME}" "InstallDir" "$INSTDIR"

    ; -----------------------------------------------------------------------
    ;  Create the uninstaller executable inside the install folder
    ; -----------------------------------------------------------------------
    WriteUninstaller "$INSTDIR\Uninstall.exe"

SectionEnd

; ============================================================================
;  SECTION: Uninstall — mirror of the install section (removes everything)
; ============================================================================
Section "Uninstall"

    ; Remove files and all sub-directories
    ; /r = recursive delete of folder and contents
    RMDir /r "$INSTDIR"

    ; Remove Start Menu shortcuts
    Delete "$SMPROGRAMS\${APP_NAME}\${APP_NAME}.lnk"
    RMDir  "$SMPROGRAMS\${APP_NAME}"

    ; Remove Desktop shortcut
    Delete "$DESKTOP\${APP_NAME}.lnk"

    ; Remove registry keys
    DeleteRegKey HKLM "${UNINSTALL_REG}"
    DeleteRegKey HKLM "Software\${APP_NAME}"

SectionEnd
