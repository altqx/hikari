; HikariSub installer and uninstaller (NSIS 3.13, built by build-makensis.sh).
;
; This file is the same on the legacy (main) and Qt (qt) branches. What
; differs per codebase and per build comes from payload.nsh, which
; make-payload-nsh.py writes. The contract (paths, keys, switches, exit
; codes) is the installers spec, section 6.
;
; makensis -DHS_PAYLOAD_NSH=<payload.nsh> -DHS_STDUTILS=<StdUtils folder>
;          -DHS_OUTFILE=<setup.exe> hikarisub.nsi
;
; Installer switches (case-insensitive):
;   /S                         silent
;   /CURRENTUSER | /ALLUSERS   scope
;   /UPDATE                    the app's updater: silent, keeps the recorded
;                              components, waits for /WAITPID and the app
;   /WAITPID=<pid>             wait up to 60 s for that process to exit
;   /RELAUNCH                  start the app, unelevated, after the install
;   /COMPONENTS=<list>         silent fresh install: assoc,desktop (default assoc)
;   /ELEVATED                  internal: the elevated relaunch for all users
;   /D=<dir>                   install folder, last
; Uninstaller switches: /S, /CURRENTUSER, /ALLUSERS.
;
; Exit codes: 0 ok, 1 cancelled, 2 other failure, 3 app still running,
; 4 elevation refused or needed, 5 downgrade refused, 6 scope conflict.

Unicode true
ManifestDPIAware true
ManifestSupportedOS all
RequestExecutionLevel user
SetCompressor /SOLID /FINAL lzma
SetCompressorDictSize 64

!ifndef HS_PAYLOAD_NSH | HS_STDUTILS | HS_OUTFILE
  !error "define HS_PAYLOAD_NSH, HS_STDUTILS and HS_OUTFILE (see the header)"
!endif

!include "${HS_PAYLOAD_NSH}"

!addplugindir /x86-unicode "${HS_STDUTILS}/Plugins/Unicode"
!addincludedir "${HS_STDUTILS}/Include"

!include MUI2.nsh
!include LogicLib.nsh
!include nsDialogs.nsh
!include Sections.nsh
!include FileFunc.nsh
!include x64.nsh
!include WinVer.nsh
!include "${__FILEDIR__}/protected.nsh"
!include StdUtils.nsh

; ---------------------------------------------------------------------------
; Identity (spec 1.1, 1.4)

!define HS_NAME          "HikariSub"
!define HS_PUBLISHER     "altqx"
!define HS_AUMID         "io.github.altqx.HikariSub"
!define HS_EXE           "HikariSub.exe"
!define HS_AVX_EXE       "HikariSub_AVX.exe"
!define HS_UNINSTALLER   "Uninstall HikariSub.exe"
!define HS_UNINST_KEY    "Software\Microsoft\Windows\CurrentVersion\Uninstall\HikariSub"
!define HS_APP_KEY       "Software\HikariSub"
!define HS_INSTALL_KEY   "Software\HikariSub\Install"
!define HS_CAPS_KEY      "Software\HikariSub\Capabilities"
!define HS_APPPATHS_KEY  "Software\Microsoft\Windows\CurrentVersion\App Paths\HikariSub.exe"
!define HS_REGAPPS_KEY   "Software\RegisteredApplications"
!define HS_CLASSES       "Software\Classes"
!define HS_MUTEX         "Global\io.github.altqx.HikariSub.Running"
; Held by an /UPDATE run until it exits: an app started meanwhile leaves
; and sets HS_START_EVENT, and HikariSub starts afterwards (HsTakeStartRequest).
!define HS_SETUP_MUTEX   "Global\io.github.altqx.HikariSub.Installing"
!define HS_START_EVENT   "Global\io.github.altqx.HikariSub.StartAfterUpdate"
!define HS_URL           "https://github.com/altqx/hikari"
; DX1 (#367): the homepage Apps & features links to; GitHub stays the issue tracker.
!define HS_HOMEPAGE      "https://hikarisub.altqx.com"

Name "${HS_NAME}"
Caption "${HS_NAME} ${HS_VERSION} Setup"
UninstallCaption "Uninstall ${HS_NAME}"
BrandingText "${HS_NAME} ${HS_VERSION}"
OutFile "${HS_OUTFILE}"
; No InstallDir: an empty $INSTDIR in .onInit means no /D= was given.
ShowInstDetails hide
ShowUninstDetails hide

VIProductVersion "${HS_VI_VERSION}"
VIFileVersion "${HS_VI_VERSION}"
VIAddVersionKey "ProductName" "${HS_NAME}"
VIAddVersionKey "CompanyName" "${HS_PUBLISHER}"
VIAddVersionKey "FileDescription" "${HS_NAME} Setup"
VIAddVersionKey "FileVersion" "${HS_VERSION}"
VIAddVersionKey "ProductVersion" "${HS_VERSION}"
VIAddVersionKey "LegalCopyright" "GPL-3.0-or-later"

; ---------------------------------------------------------------------------
; State

Var HsScope           ; user | machine
Var HsCmdScope        ; scope from /CURRENTUSER or /ALLUSERS, else empty
Var HsExistScope      ; scope of an existing install, else empty
Var HsExistDir
Var HsExistVersion
Var HsExistComponents
Var HsHasExistComponents
Var HsDirArg          ; /D= value, else empty
Var HsUpdate
Var HsWaitPid
Var HsRelaunch
Var HsElevated
Var HsIsAdmin
Var HsCmdComponents
Var HsHasCmdComponents
Var HsUserLocalAppData
Var HsRadioUser
Var HsRadioMachine
Var HsSelfHold        ; this setup's own file, held against change (HsHoldSelf)
Var HsHeld            ; 1 when the file and every folder above it are held (HsHoldSelf)
Var HsNewMachineDir   ; the new all-users folder HsMachineDirOk accepted
Var HsSetupMutex      ; HS_SETUP_MUTEX, held until exit (/UPDATE)
Var HsStartEvent      ; HS_START_EVENT, or 0

; ---------------------------------------------------------------------------
; Pages

!ifdef HS_INSTALLER_ICON
  !define MUI_ICON "${HS_INSTALLER_ICON}"
  !define MUI_UNICON "${HS_INSTALLER_ICON}"
!endif
!define MUI_ABORTWARNING
!define MUI_COMPONENTSPAGE_NODESC

Page custom HsScopePageCreate HsScopePageLeave
!insertmacro MUI_PAGE_COMPONENTS
!define MUI_PAGE_CUSTOMFUNCTION_PRE HsDirectoryPagePre
!define MUI_PAGE_CUSTOMFUNCTION_LEAVE HsDirectoryPageLeave
!insertmacro MUI_PAGE_DIRECTORY
!insertmacro MUI_PAGE_INSTFILES
!define MUI_FINISHPAGE_RUN
!define MUI_FINISHPAGE_RUN_TEXT "Start ${HS_NAME}"
!define MUI_FINISHPAGE_RUN_FUNCTION HsLaunchApp
!insertmacro MUI_PAGE_FINISH

!insertmacro MUI_UNPAGE_CONFIRM
!insertmacro MUI_UNPAGE_INSTFILES

!insertmacro MUI_LANGUAGE "English"

; ---------------------------------------------------------------------------
; Functions shared by the installer and the uninstaller

!macro HS_SHARED_FUNCTIONS UN

; $HsIsAdmin = 1 when this process runs elevated. UserInfo reports "Admin"
; only for a full token, not for a UAC-filtered one.
Function ${UN}HsCheckAdmin
  Push $0
  StrCpy $HsIsAdmin 0
  UserInfo::GetAccountType
  Pop $0
  ${If} $0 == "Admin"
    StrCpy $HsIsAdmin 1
  ${EndIf}
  Pop $0
FunctionEnd

; Is HikariSub running? $R9 = 1 or 0. The app holds the named mutex (spec 4.2);
; a running HikariSub.exe in $INSTDIR also counts, which covers builds that
; predate the mutex.
Function ${UN}HsAppRunning
  Push $0
  Push $1
  StrCpy $R9 0
  ; A mutex we may not open (another user's) exists all the same.
  System::Call 'kernel32::OpenMutexW(i 0x00100000, i 0, w "${HS_MUTEX}") p .r0 ?e'
  Pop $1
  ${If} $0 <> 0
    System::Call 'kernel32::CloseHandle(p r0)'
    StrCpy $R9 1
  ${ElseIf} $1 = 5
    StrCpy $R9 1
  ${EndIf}
  ${If} $R9 = 0
  ${AndIf} ${FileExists} "$INSTDIR\${HS_EXE}"
    ClearErrors
    FileOpen $0 "$INSTDIR\${HS_EXE}" a
    ${If} ${Errors}
      StrCpy $R9 1
    ${Else}
      FileClose $0
    ${EndIf}
  ${EndIf}
  ${If} $R9 = 0
  ${AndIf} ${FileExists} "$INSTDIR\${HS_AVX_EXE}"
    ClearErrors
    FileOpen $0 "$INSTDIR\${HS_AVX_EXE}" a
    ${If} ${Errors}
      StrCpy $R9 1
    ${Else}
      FileClose $0
    ${EndIf}
  ${EndIf}
  Pop $1
  Pop $0
FunctionEnd

; Wait up to 30 s for HikariSub to stop. $R9 = 1 if it still runs.
Function ${UN}HsWaitAppGone
  Push $0
  StrCpy $0 0
  ${Do}
    Call ${UN}HsAppRunning
    ${If} $R9 = 0
    ${OrIf} $0 >= 60
      ${Break}
    ${EndIf}
    Sleep 500
    IntOp $0 $0 + 1
  ${Loop}
  Pop $0
FunctionEnd

; ShellExecuteEx with the runas verb. $R0 = file, $R1 = parameters.
; $R2 = "ok", "cancelled" (UAC declined) or "error".
Function ${UN}HsRunElevated
  Push $0
  Push $1
  Push $2
  System::Call '*(&l4, i 0, p $HWNDPARENT, w "runas", w R0, w R1, w "", i 1, p, p, w, p, i, p, p) p .r0'
  System::Call 'shell32::ShellExecuteExW(p r0) i .r1 ?e'
  Pop $2
  System::Free $0
  ${If} $1 <> 0
    StrCpy $R2 "ok"
  ${ElseIf} $2 = 1223
    StrCpy $R2 "cancelled"
  ${Else}
    StrCpy $R2 "error"
  ${EndIf}
  Pop $2
  Pop $1
  Pop $0
FunctionEnd

; $R0: a manifest line. Strips the line end; empties $R0 when the path is not
; a plain relative path (drive, root, .., wildcards).
Function ${UN}HsCleanLine
  Push $0
  Push $1
  Push $2
  ${Do}
    StrCpy $0 $R0 1 -1
    ${If} $0 == "$\r"
    ${OrIf} $0 == "$\n"
      StrCpy $R0 $R0 -1
    ${Else}
      ${Break}
    ${EndIf}
  ${Loop}
  StrCpy $0 $R0 1
  ${If} $0 == "\"
  ${OrIf} $0 == "/"
    StrCpy $R0 ""
  ${EndIf}
  StrLen $1 $R0
  StrCpy $2 0
  ${DoWhile} $2 < $1
    StrCpy $0 $R0 1 $2
    ${If} $0 == ":"
    ${OrIf} $0 == "*"
    ${OrIf} $0 == "?"
      StrCpy $R0 ""
      ${Break}
    ${EndIf}
    StrCpy $0 $R0 2 $2
    ${If} $0 == ".."
      StrCpy $R0 ""
      ${Break}
    ${EndIf}
    IntOp $2 $2 + 1
  ${Loop}
  Pop $2
  Pop $1
  Pop $0
FunctionEnd

; $R0: a relative file path. Removes each parent folder below $INSTDIR that
; is now empty, deepest first.
Function ${UN}HsRemoveEmptyParents
  Push $0
  Push $1
  Push $2
  StrCpy $0 $R0
  ${Do}
    StrLen $1 $0
    ${Do}
      IntOp $1 $1 - 1
      ${If} $1 < 0
        ${Break}
      ${EndIf}
      StrCpy $2 $0 1 $1
      ${If} $2 == "\"
        ${Break}
      ${EndIf}
    ${Loop}
    ${If} $1 <= 0
      ${Break}
    ${EndIf}
    StrCpy $0 $0 $1
    RMDir "$INSTDIR\$0"
  ${Loop}
  Pop $2
  Pop $1
  Pop $0
FunctionEnd

; Removes this install's associations from SHCTX (spec 1.5): only keys and
; values that still point at $INSTDIR, and an extension's default only when it
; still names our ProgID and that ProgID is gone.
Function ${UN}HsRemoveAssociations
  Push $0
  !insertmacro HS_ASSOC_REMOVE ass
  !insertmacro HS_ASSOC_REMOVE ssa
  !insertmacro HS_ASSOC_REMOVE srt
  !insertmacro HS_ASSOC_REMOVE sub
  DeleteRegKey /ifempty SHCTX "${HS_CLASSES}\Applications\${HS_EXE}\SupportedTypes"
  DeleteRegKey /ifempty SHCTX "${HS_CLASSES}\Applications\${HS_EXE}"
  ReadRegStr $0 SHCTX "${HS_REGAPPS_KEY}" "${HS_NAME}"
  ${If} $0 == "${HS_CAPS_KEY}"
    DeleteRegValue SHCTX "${HS_REGAPPS_KEY}" "${HS_NAME}"
  ${EndIf}
  DeleteRegKey SHCTX "${HS_CAPS_KEY}"
  System::Call 'shell32::SHChangeNotify(i 0x08000000, i 0, p 0, p 0)'
  Pop $0
FunctionEnd

!macroend

!macro HS_ASSOC_REMOVE EXT
  ReadRegStr $0 SHCTX "${HS_CLASSES}\HikariSub.${EXT}\shell\open\command" ""
  ${If} $0 == '"$INSTDIR\${HS_EXE}" "%1"'
    DeleteRegKey SHCTX "${HS_CLASSES}\HikariSub.${EXT}"
    DeleteRegValue SHCTX "${HS_CLASSES}\.${EXT}\OpenWithProgids" "HikariSub.${EXT}"
    DeleteRegValue SHCTX "${HS_CLASSES}\Applications\${HS_EXE}\SupportedTypes" ".${EXT}"
  ${EndIf}
  ClearErrors
  EnumRegKey $0 SHCTX "${HS_CLASSES}\HikariSub.${EXT}" 0
  ${If} ${Errors}
    ReadRegStr $0 SHCTX "${HS_CLASSES}\.${EXT}" ""
    ${If} $0 == "HikariSub.${EXT}"
      DeleteRegValue SHCTX "${HS_CLASSES}\.${EXT}" ""
    ${EndIf}
  ${EndIf}
  DeleteRegKey /ifempty SHCTX "${HS_CLASSES}\.${EXT}\OpenWithProgids"
  DeleteRegKey /ifempty SHCTX "${HS_CLASSES}\.${EXT}"
!macroend

!macro HS_ASSOC_WRITE EXT DESC ICON
  WriteRegStr SHCTX "${HS_CLASSES}\HikariSub.${EXT}" "" "${DESC}"
  WriteRegStr SHCTX "${HS_CLASSES}\HikariSub.${EXT}\DefaultIcon" "" '"$INSTDIR\${HS_EXE}",-${ICON}'
  WriteRegStr SHCTX "${HS_CLASSES}\HikariSub.${EXT}\shell\open\command" "" '"$INSTDIR\${HS_EXE}" "%1"'
  WriteRegNone SHCTX "${HS_CLASSES}\.${EXT}\OpenWithProgids" "HikariSub.${EXT}"
  ReadRegStr $0 SHCTX "${HS_CLASSES}\.${EXT}" ""
  ${If} $0 == ""
    WriteRegStr SHCTX "${HS_CLASSES}\.${EXT}" "" "HikariSub.${EXT}"
  ${EndIf}
  WriteRegStr SHCTX "${HS_CLASSES}\Applications\${HS_EXE}\SupportedTypes" ".${EXT}" ""
  WriteRegStr SHCTX "${HS_CAPS_KEY}\FileAssociations" ".${EXT}" "HikariSub.${EXT}"
!macroend

!insertmacro HS_SHARED_FUNCTIONS ""
!insertmacro HS_SHARED_FUNCTIONS "un."
!insertmacro HS_PLAIN_PATH_FUNCTION ""
!insertmacro HS_PLAIN_PATH_FUNCTION "un."
!insertmacro HS_PROTECTED_FUNCTIONS "un."
!insertmacro HS_PAYLOAD_PATH_FUNCTION ""

; ---------------------------------------------------------------------------
; Installer-only helpers

; Start HikariSub from $INSTDIR, never elevated: an elevated installer goes
; through the shell of the desktop user.
Function HsLaunchApp
  Push $0
  ${If} ${FileExists} "$INSTDIR\${HS_EXE}"
    SetOutPath "$INSTDIR"
    ${If} $HsIsAdmin = 1
      ${StdUtils.ExecShellAsUser} $0 "$INSTDIR\${HS_EXE}" "open" ""
    ${Else}
      Exec '"$INSTDIR\${HS_EXE}"'
    ${EndIf}
  ${EndIf}
  Pop $0
FunctionEnd

; HS_START_EVENT: a manual-reset event, not set, that any signed-in user
; may set (EVENT_MODIFY_STATE and SYNCHRONIZE for Authenticated Users),
; held until exit. An event, not a file: an all-users setup that another
; administrator approved runs in that account's profile, not the user's.
!if ${NSIS_PTR_SIZE} != 4
  !error "HsMakeStartEvent lays out a 32-bit SECURITY_ATTRIBUTES"
!endif
Function HsMakeStartEvent
  Push $0
  Push $1
  Push $2
  StrCpy $HsStartEvent 0
  System::Call 'advapi32::ConvertStringSecurityDescriptorToSecurityDescriptorW(w "D:(A;;GA;;;SY)(A;;GA;;;BA)(A;;0x100002;;;AU)S:(ML;;NW;;;ME)", i 1, *p .r1, p 0) i .r0'
  ${If} $0 <> 0
    System::Call '*(i 12, p r1, i 0) p .r2'
    System::Call 'kernel32::CreateEventW(p r2, i 1, i 0, w "${HS_START_EVENT}") p .r0'
    StrCpy $HsStartEvent $0
    System::Free $2
    System::Call 'kernel32::LocalFree(p r1)'
  ${EndIf}
  Pop $2
  Pop $1
  Pop $0
FunctionEnd

; An app started while this /UPDATE ran left at once and set HS_START_EVENT:
; HikariSub starts when this ends, as with /RELAUNCH (as the desktop user).
Function HsTakeStartRequest
  Push $0
  ${If} $HsUpdate = 1
  ${AndIf} $HsStartEvent <> 0
    System::Call 'kernel32::WaitForSingleObject(p $HsStartEvent, i 0) i .r0'
    ${If} $0 = 0
      StrCpy $HsRelaunch 1
    ${EndIf}
  ${EndIf}
  Pop $0
FunctionEnd

; Reports a failure and exits. $R8 = exit code, $R7 = message.
Function HsFail
  Push $0
  ${If} $HsUpdate = 1
    ; Under the copy's key (the app's CopyKey): another copy that shares
    ; this cache does not take this result for its own.
    StrCpy $0 $HsScope
    ${If} $0 == ""
      StrCpy $0 $HsCmdScope
    ${EndIf}
    ${If} $0 == "machine"
      StrCpy $0 "nsis-machine"
    ${Else}
      StrCpy $0 "nsis-user"
    ${EndIf}
    CreateDirectory "$HsUserLocalAppData\HikariSub\Updates"
    ClearErrors
    FileOpen $0 "$HsUserLocalAppData\HikariSub\Updates\last-result-$0.json" w
    ${IfNot} ${Errors}
      FileWrite $0 '{"version": "${HS_VERSION}", "ok": false, "code": $R8, "message": "$R7"}$\r$\n'
      FileClose $0
    ${EndIf}
  ${EndIf}
  Call HsTakeStartRequest
  ${If} $HsRelaunch = 1
    Call HsLaunchApp
  ${EndIf}
  MessageBox MB_OK|MB_ICONSTOP "$R7" /SD IDOK
  SetErrorLevel $R8
  Pop $0
  Quit
FunctionEnd

; $9 = string, $8 = one separator character.
; $7 = text before the first separator (all of $9 if none), $6 = text after.
Function HsSplitFirst
  Push $5
  Push $4
  Push $3
  StrLen $3 $9
  StrCpy $4 0
  StrCpy $7 $9
  StrCpy $6 ""
  ${DoWhile} $4 < $3
    StrCpy $5 $9 1 $4
    ${If} $5 S== $8
      StrCpy $7 $9 $4
      IntOp $4 $4 + 1
      StrCpy $6 $9 "" $4
      ${Break}
    ${EndIf}
    IntOp $4 $4 + 1
  ${Loop}
  Pop $3
  Pop $4
  Pop $5
FunctionEnd

; $9 = string. $7 = 1 if it is one or more ASCII digits, else 0.
Function HsIsDigits
  Push $4
  Push $3
  Push $2
  StrLen $3 $9
  StrCpy $7 0
  ${If} $3 > 0
    StrCpy $7 1
    StrCpy $4 0
    ${DoWhile} $4 < $3
      StrCpy $2 $9 1 $4
      ${If}   $2 S!= "0"
      ${AndIf} $2 S!= "1"
      ${AndIf} $2 S!= "2"
      ${AndIf} $2 S!= "3"
      ${AndIf} $2 S!= "4"
      ${AndIf} $2 S!= "5"
      ${AndIf} $2 S!= "6"
      ${AndIf} $2 S!= "7"
      ${AndIf} $2 S!= "8"
      ${AndIf} $2 S!= "9"
        StrCpy $7 0
        ${Break}
      ${EndIf}
      IntOp $4 $4 + 1
    ${Loop}
  ${EndIf}
  Pop $2
  Pop $3
  Pop $4
FunctionEnd

; SemVer 2.0.0 precedence. $R0 = a, $R1 = b. $R2 = -1, 0 or 1.
Function HsSemverCompare
  Push $R1
  Push $R3
  Push $R4
  Push $R5
  Push $R6
  Push $0
  Push $1
  Push $2
  Push $3
  Push $5
  Push $6
  Push $7
  Push $8
  Push $9
  ; Drop build metadata, then split core and prerelease.
  StrCpy $9 $R0
  StrCpy $8 "+"
  Call HsSplitFirst
  StrCpy $9 $7
  StrCpy $8 "-"
  Call HsSplitFirst
  StrCpy $0 $7          ; a core
  StrCpy $1 $6          ; a prerelease
  StrCpy $2 0
  ${If} $7 != $9
    StrCpy $2 1         ; a has a prerelease
  ${EndIf}
  StrCpy $9 $R1
  StrCpy $8 "+"
  Call HsSplitFirst
  StrCpy $9 $7
  StrCpy $8 "-"
  Call HsSplitFirst
  StrCpy $3 $6          ; b prerelease
  StrCpy $5 0
  ${If} $7 != $9
    StrCpy $5 1
  ${EndIf}
  StrCpy $R1 $7         ; b core
  StrCpy $R2 0
  ; Core: MAJOR.MINOR.PATCH, numerically.
  StrCpy $8 "."
  ${ForEach} $R3 1 3 + 1
    StrCpy $9 $0
    Call HsSplitFirst
    StrCpy $0 $6
    StrCpy $R4 $7
    StrCpy $9 $R1
    Call HsSplitFirst
    StrCpy $R1 $6
    ${If} $R4 < $7
      StrCpy $R2 -1
      ${Break}
    ${ElseIf} $R4 > $7
      StrCpy $R2 1
      ${Break}
    ${EndIf}
  ${Next}
  ${If} $R2 = 0
    ${If} $2 = 0
    ${AndIf} $5 = 1
      StrCpy $R2 1
    ${ElseIf} $2 = 1
    ${AndIf} $5 = 0
      StrCpy $R2 -1
    ${ElseIf} $2 = 1
      ; Both prereleases: identifier by identifier.
      ${Do}
        ${If} $1 == ""
          ${If} $3 != ""
            StrCpy $R2 -1
          ${EndIf}
          ${Break}
        ${ElseIf} $3 == ""
          StrCpy $R2 1
          ${Break}
        ${EndIf}
        StrCpy $9 $1
        Call HsSplitFirst
        StrCpy $1 $6
        StrCpy $R4 $7
        StrCpy $9 $3
        Call HsSplitFirst
        StrCpy $3 $6
        StrCpy $R5 $7
        StrCpy $9 $R4
        Call HsIsDigits
        StrCpy $R6 $7
        StrCpy $9 $R5
        Call HsIsDigits
        ${If} $R6 = 1
        ${AndIf} $7 = 1
          ${If} $R4 < $R5
            StrCpy $R2 -1
          ${ElseIf} $R4 > $R5
            StrCpy $R2 1
          ${EndIf}
        ${ElseIf} $R6 = 1
          StrCpy $R2 -1
        ${ElseIf} $7 = 1
          StrCpy $R2 1
        ${Else}
          ; ASCII order: CompareStringOrdinal gives 1 (<), 2 (=), 3 (>).
          System::Call 'kernel32::CompareStringOrdinal(w R4, i -1, w R5, i -1, i 0) i .r9'
          IntOp $R2 $9 - 2
        ${EndIf}
        ${If} $R2 <> 0
          ${Break}
        ${EndIf}
      ${Loop}
    ${EndIf}
  ${EndIf}
  Pop $9
  Pop $8
  Pop $7
  Pop $6
  Pop $5
  Pop $3
  Pop $2
  Pop $1
  Pop $0
  Pop $R6
  Pop $R5
  Pop $R4
  Pop $R3
  Pop $R1
FunctionEnd

; $9 = comma list, $8 = item. $7 = 1 if the item is in the list.
Function HsListHas
  Push $5
  Push $4
  Push $3
  Push $2
  Push $1
  StrCpy $1 ",$9,"
  StrCpy $2 ",$8,"
  StrLen $3 $2
  StrCpy $4 0
  StrCpy $7 0
  ${Do}
    StrCpy $5 $1 $3 $4
    ${If} $5 == ""
      ${Break}
    ${EndIf}
    ${If} $5 == $2
      StrCpy $7 1
      ${Break}
    ${EndIf}
    IntOp $4 $4 + 1
  ${Loop}
  Pop $1
  Pop $2
  Pop $3
  Pop $4
  Pop $5
FunctionEnd

Function HsParseArgs
  Push $R0
  Push $R1
  ${GetParameters} $R0
  StrCpy $HsUpdate 0
  StrCpy $HsRelaunch 0
  StrCpy $HsElevated 0
  StrCpy $HsCmdScope ""
  StrCpy $HsWaitPid ""
  StrCpy $HsHasCmdComponents 0
  ClearErrors
  ${GetOptions} $R0 "/UPDATE" $R1
  ${IfNot} ${Errors}
    StrCpy $HsUpdate 1
  ${EndIf}
  ClearErrors
  ${GetOptions} $R0 "/RELAUNCH" $R1
  ${IfNot} ${Errors}
    StrCpy $HsRelaunch 1
  ${EndIf}
  ClearErrors
  ${GetOptions} $R0 "/ELEVATED" $R1
  ${IfNot} ${Errors}
    StrCpy $HsElevated 1
  ${EndIf}
  ClearErrors
  ${GetOptions} $R0 "/CURRENTUSER" $R1
  ${IfNot} ${Errors}
    StrCpy $HsCmdScope "user"
  ${EndIf}
  ClearErrors
  ${GetOptions} $R0 "/ALLUSERS" $R1
  ${IfNot} ${Errors}
    StrCpy $HsCmdScope "machine"
  ${EndIf}
  ClearErrors
  ${GetOptions} $R0 "/WAITPID=" $R1
  ${IfNot} ${Errors}
    IntOp $HsWaitPid $R1 + 0
  ${EndIf}
  ClearErrors
  ${GetOptions} $R0 "/COMPONENTS=" $R1
  ${IfNot} ${Errors}
    StrCpy $HsCmdComponents $R1
    StrCpy $HsHasCmdComponents 1
  ${EndIf}
  Pop $R1
  Pop $R0
FunctionEnd

; Reads an existing install of either scope (spec 6.2).
Function HsFindExisting
  Push $0
  Push $1
  StrCpy $HsExistScope ""
  SetShellVarContext all
  ClearErrors
  ReadRegStr $0 SHCTX "${HS_UNINST_KEY}" "DisplayName"
  ${IfNot} ${Errors}
    StrCpy $HsExistScope "machine"
  ${EndIf}
  SetShellVarContext current
  ClearErrors
  ; The elevated relaunch for all users, and an all-users update, run as
  ; whichever administrator approved UAC: their HKCU is not the user's, and
  ; a per-user install of theirs has nothing to do with this one.
  ${If} $HsElevated = 1
  ${OrIf} $HsUpdate = 1
  ${AndIf} $HsCmdScope == "machine"
    SetErrors
  ${Else}
    ReadRegStr $0 SHCTX "${HS_UNINST_KEY}" "DisplayName"
  ${EndIf}
  ${IfNot} ${Errors}
    ${If} $HsExistScope == "machine"
      MessageBox MB_OK|MB_ICONEXCLAMATION "${HS_NAME} is installed both for all users and for you alone. This setup updates the installation for all users. Uninstall the other one from Settings > Apps." /SD IDOK
    ${Else}
      StrCpy $HsExistScope "user"
    ${EndIf}
  ${EndIf}
  ${If} $HsExistScope == "machine"
    SetShellVarContext all
  ${EndIf}
  ${If} $HsExistScope != ""
    ReadRegStr $HsExistDir SHCTX "${HS_UNINST_KEY}" "InstallLocation"
    ReadRegStr $HsExistVersion SHCTX "${HS_INSTALL_KEY}" "Version"
    ${If} $HsExistVersion == ""
      ReadRegStr $HsExistVersion SHCTX "${HS_UNINST_KEY}" "DisplayVersion"
    ${EndIf}
    StrCpy $HsHasExistComponents 0
    ClearErrors
    ReadRegStr $HsExistComponents SHCTX "${HS_INSTALL_KEY}" "Components"
    ${IfNot} ${Errors}
      StrCpy $HsHasExistComponents 1
    ${EndIf}
  ${EndIf}
  SetShellVarContext current
  Pop $1
  Pop $0
FunctionEnd

; Fails with code 2 when an all-users install would go to a folder
; HsMachineDirOk refuses.
Function HsCheckMachineDir
  ${If} $HsScope == "machine"
    Call HsMachineDirOk
    ${If} $R2 <> 1
      StrCpy $R8 2
      StrCpy $R7 "For all users, HikariSub installs in a folder under $PROGRAMFILES64, not in $INSTDIR."
      Call HsFail
    ${EndIf}
  ${EndIf}
FunctionEnd

; Sets the shell context and the default folder for $HsScope (spec 6.2).
Function HsApplyScope
  ${If} $HsScope == "machine"
    SetShellVarContext all
  ${Else}
    SetShellVarContext current
  ${EndIf}
  ${If} $HsDirArg != ""
    StrCpy $INSTDIR $HsDirArg
  ${ElseIf} $HsExistDir != ""
  ${AndIf} $HsExistScope == $HsScope
    StrCpy $INSTDIR $HsExistDir
  ${ElseIf} $HsScope == "machine"
    StrCpy $INSTDIR "$PROGRAMFILES64\${HS_NAME}"
  ${Else}
    StrCpy $INSTDIR "$HsUserLocalAppData\Programs\${HS_NAME}"
  ${EndIf}
FunctionEnd

; $R2 = 1 when $INSTDIR may hold an all-users install (spec 6.2): a new
; folder directly in Program Files, as a plain path, which only
; administrators can create there and which takes Program Files' rights; or
; the folder of the all-users install already there, which an administrator
; chose. A folder elsewhere, or one that is already there (another program's
; folder may let users change it, or hold a junction), could be the user's
; to change, who could then replace the program that every user, and
; administrators, start from the shortcuts and file types. A new folder this
; setup has already accepted ($HsNewMachineDir) stays accepted once made.
Function HsMachineDirOk
  Push $0
  Push $1
  Push $R0
  Push $2
  StrCpy $R2 0
  ${If} $HsExistScope == "machine"
  ${AndIf} $HsExistDir != ""
  ${AndIf} $INSTDIR == $HsExistDir
    StrCpy $R2 1
  ${Else}
    StrCpy $R0 $INSTDIR
    Call HsPlainPath
    ${If} $R2 = 1
      StrLen $1 "$PROGRAMFILES64\"
      StrCpy $0 $INSTDIR $1
      StrCpy $R0 $INSTDIR "" $1
      ${If} $0 != "$PROGRAMFILES64\"
      ${OrIf} $R0 == ""
        StrCpy $R2 0
      ${EndIf}
    ${EndIf}
    ; Directly in Program Files: one name, no "\".
    ${If} $R2 = 1
      StrLen $1 $R0
      StrCpy $0 0
      ${Do}
        ${If} $0 >= $1
          ${Break}
        ${EndIf}
        StrCpy $2 $R0 1 $0
        ${If} $2 == "\"
          StrCpy $R2 0
          ${Break}
        ${EndIf}
        IntOp $0 $0 + 1
      ${Loop}
    ${EndIf}
    ; New: not there yet, unless this setup accepted it and made it.
    ${If} $R2 = 1
      ${If} $INSTDIR S== $HsNewMachineDir
        StrCpy $R2 1
      ${ElseIf} ${FileExists} "$INSTDIR"
        StrCpy $R2 0
      ${Else}
        StrCpy $HsNewMachineDir $INSTDIR
      ${EndIf}
    ${EndIf}
  ${EndIf}
  Pop $2
  Pop $R0
  Pop $1
  Pop $0
FunctionEnd

; Waits for HikariSub to exit before files are replaced (spec 4.2, 6.4).
Function HsWaitForApp
  Push $0
  Push $1
  ${If} $HsUpdate = 1
    ${If} $HsWaitPid != ""
    ${AndIf} $HsWaitPid <> 0
      System::Call 'kernel32::OpenProcess(i 0x00100000, i 0, i $HsWaitPid) p .r0'
      ${If} $0 <> 0
        System::Call 'kernel32::WaitForSingleObject(p r0, i 60000) i .r1'
        System::Call 'kernel32::CloseHandle(p r0)'
        ${If} $1 = 258
          StrCpy $R8 3
          StrCpy $R7 "HikariSub did not close within 60 seconds, so the update was not installed."
          Call HsFail
        ${EndIf}
      ${EndIf}
    ${EndIf}
    Call HsWaitAppGone
    ${If} $R9 = 1
      StrCpy $R8 3
      StrCpy $R7 "HikariSub is still running, so the update was not installed."
      Call HsFail
    ${EndIf}
  ${ElseIf} ${Silent}
    Call HsWaitAppGone
    ${If} $R9 = 1
      StrCpy $R8 3
      StrCpy $R7 "HikariSub is running. Close it, then run the setup again."
      Call HsFail
    ${EndIf}
  ${Else}
    ${Do}
      Call HsAppRunning
      ${If} $R9 = 0
        ${Break}
      ${EndIf}
      ${If} ${Cmd} `MessageBox MB_RETRYCANCEL|MB_ICONEXCLAMATION "HikariSub is running. Close it, then choose Retry." IDCANCEL`
        SetErrorLevel 1
        Quit
      ${EndIf}
    ${Loop}
  ${EndIf}
  Pop $1
  Pop $0
FunctionEnd

; Deletes the files of the previous install that this payload no longer has
; (spec 6.6): only paths listed in the old install-manifest.txt.
Function HsDeleteOrphans
  Push $0
  Push $R0
  Push $R1
  ${If} ${FileExists} "$PLUGINSDIR\old-manifest.txt"
    FileOpen $0 "$PLUGINSDIR\old-manifest.txt" r
    ${Do}
      ClearErrors
      FileRead $0 $R0
      ${If} ${Errors}
        ${Break}
      ${EndIf}
      Call HsCleanLine
      ${If} $R0 != ""
        Call HsIsPayloadPath
        ${If} $R1 = 0
          Delete "$INSTDIR\$R0"
          Call HsRemoveEmptyParents
        ${EndIf}
      ${EndIf}
    ${Loop}
    FileClose $0
  ${EndIf}
  Pop $R1
  Pop $R0
  Pop $0
FunctionEnd

; Sets the AppUserModelID of the shortcut $R0 (IPropertyStore on the link).
Function HsSetShortcutAumid
  Push $0
  Push $1
  Push $2
  Push $3
  Push $4
  Push $5
  System::Call 'ole32::CoCreateInstance(g "{00021401-0000-0000-C000-000000000046}", p 0, i 1, g "{000214F9-0000-0000-C000-000000000046}", *p .r1) i .r0'
  ${If} $0 = 0
    System::Call '$1->0(g "{0000010b-0000-0000-C000-000000000046}", *p .r2) i .r0'
    ${If} $0 = 0
      System::Call '$2->5(w R0, i 2) i .r0'
      ${If} $0 = 0
        System::Call '$1->0(g "{886D8EEB-8CF2-4446-8D02-CDBA1DBDCF99}", *p .r3) i .r0'
        ${If} $0 = 0
          System::Call '*(&g16 "{9F4C2855-9F79-4B39-A8D0-E1D42DE1D5F3}", i 5) p .r4'
          System::Call '*(&i2 31, &i2 0, &i2 0, &i2 0, w "${HS_AUMID}", i 0, i 0) p .r5'
          System::Call '$3->6(p r4, p r5) i .r0'
          ${If} $0 = 0
            System::Call '$3->7() i .r0'
          ${EndIf}
          System::Free $4
          System::Free $5
          System::Call '$3->2()'
          ${If} $0 = 0
            System::Call '$2->6(p 0, i 1) i .r0'
          ${EndIf}
        ${EndIf}
      ${EndIf}
      System::Call '$2->2()'
    ${EndIf}
    System::Call '$1->2()'
  ${EndIf}
  Pop $5
  Pop $4
  Pop $3
  Pop $2
  Pop $1
  Pop $0
FunctionEnd

; ---------------------------------------------------------------------------
; Sections

Section "-Program files" SecCore
  SectionIn RO
  ; Again here: the folder page may have been skipped or changed.
  Call HsCheckMachineDir
  SetOutPath "$INSTDIR"
  Call HsWaitForApp

  ; The previous payload's file list, for the orphan sweep below.
  InitPluginsDir
  Delete "$PLUGINSDIR\old-manifest.txt"
  ${If} ${FileExists} "$INSTDIR\install-manifest.txt"
    CopyFiles /SILENT /FILESONLY "$INSTDIR\install-manifest.txt" "$PLUGINSDIR\old-manifest.txt"
  ${EndIf}

  SetOverwrite on
  ClearErrors
  !insertmacro HS_PAYLOAD_FILES
  ${If} ${Errors}
    StrCpy $R8 2
    StrCpy $R7 "Some files of HikariSub could not be written. Close HikariSub and run the setup again."
    Call HsFail
  ${EndIf}
  Call HsDeleteOrphans

  ; The package kind the app reads beside its executable (spec 3).
  FileOpen $0 "$INSTDIR\package-kind.txt" w
  ${If} $HsScope == "machine"
    FileWrite $0 "nsis-machine"
  ${Else}
    FileWrite $0 "nsis-user"
  ${EndIf}
  FileClose $0

  WriteUninstaller "$INSTDIR\${HS_UNINSTALLER}"

  ${If} $HsScope == "machine"
    StrCpy $1 "/ALLUSERS"
    StrCpy $2 "machine"
  ${Else}
    StrCpy $1 "/CURRENTUSER"
    StrCpy $2 "user"
  ${EndIf}
  WriteRegStr SHCTX "${HS_UNINST_KEY}" "DisplayName" "${HS_NAME}"
  WriteRegStr SHCTX "${HS_UNINST_KEY}" "DisplayVersion" "${HS_VERSION}"
  WriteRegStr SHCTX "${HS_UNINST_KEY}" "Publisher" "${HS_PUBLISHER}"
  WriteRegStr SHCTX "${HS_UNINST_KEY}" "DisplayIcon" '"$INSTDIR\${HS_EXE}",0'
  WriteRegStr SHCTX "${HS_UNINST_KEY}" "InstallLocation" "$INSTDIR"
  WriteRegStr SHCTX "${HS_UNINST_KEY}" "UninstallString" '"$INSTDIR\${HS_UNINSTALLER}" $1'
  WriteRegStr SHCTX "${HS_UNINST_KEY}" "QuietUninstallString" '"$INSTDIR\${HS_UNINSTALLER}" $1 /S'
  WriteRegStr SHCTX "${HS_UNINST_KEY}" "URLInfoAbout" "${HS_HOMEPAGE}"
  WriteRegStr SHCTX "${HS_UNINST_KEY}" "HelpLink" "${HS_URL}/issues"
  WriteRegDWORD SHCTX "${HS_UNINST_KEY}" "NoModify" 1
  WriteRegDWORD SHCTX "${HS_UNINST_KEY}" "NoRepair" 1
  WriteRegDWORD SHCTX "${HS_UNINST_KEY}" "EstimatedSize" ${HS_PAYLOAD_KB}
  WriteRegStr SHCTX "${HS_UNINST_KEY}" "InstallScope" "$2"

  WriteRegStr SHCTX "${HS_INSTALL_KEY}" "InstallLocation" "$INSTDIR"
  WriteRegStr SHCTX "${HS_INSTALL_KEY}" "InstallScope" "$2"
  WriteRegStr SHCTX "${HS_INSTALL_KEY}" "Version" "${HS_VERSION}"

  WriteRegStr SHCTX "${HS_APPPATHS_KEY}" "" "$INSTDIR\${HS_EXE}"
  WriteRegStr SHCTX "${HS_APPPATHS_KEY}" "Path" "$INSTDIR"

  CreateShortcut "$SMPROGRAMS\${HS_NAME}.lnk" "$INSTDIR\${HS_EXE}" "" "$INSTDIR\${HS_EXE}" 0
  StrCpy $R0 "$SMPROGRAMS\${HS_NAME}.lnk"
  Call HsSetShortcutAumid
SectionEnd

Section "Associate subtitle files (.ass, .ssa, .srt, .sub)" SecAssoc
SectionEnd

Section /o "Desktop shortcut" SecDesktop
SectionEnd

Section "-Finish" SecFinish
  StrCpy $1 ""
  ${If} ${SectionIsSelected} ${SecAssoc}
    !insertmacro HS_ASSOC_WRITE ass "ASS subtitles" ${HS_ICON_ID_ass}
    !insertmacro HS_ASSOC_WRITE ssa "SSA subtitles" ${HS_ICON_ID_ssa}
    !insertmacro HS_ASSOC_WRITE srt "SRT subtitles" ${HS_ICON_ID_srt}
    !insertmacro HS_ASSOC_WRITE sub "SUB subtitles" ${HS_ICON_ID_sub}
    WriteRegStr SHCTX "${HS_CAPS_KEY}" "ApplicationName" "${HS_NAME}"
    WriteRegStr SHCTX "${HS_CAPS_KEY}" "ApplicationDescription" "Subtitle editor and player"
    WriteRegStr SHCTX "${HS_REGAPPS_KEY}" "${HS_NAME}" "${HS_CAPS_KEY}"
    System::Call 'shell32::SHChangeNotify(i 0x08000000, i 0, p 0, p 0)'
    StrCpy $1 "assoc"
  ${Else}
    Call HsRemoveAssociations
  ${EndIf}

  ${If} ${SectionIsSelected} ${SecDesktop}
    CreateShortcut "$DESKTOP\${HS_NAME}.lnk" "$INSTDIR\${HS_EXE}" "" "$INSTDIR\${HS_EXE}" 0
    StrCpy $R0 "$DESKTOP\${HS_NAME}.lnk"
    Call HsSetShortcutAumid
    ${If} $1 == ""
      StrCpy $1 "desktop"
    ${Else}
      StrCpy $1 "$1,desktop"
    ${EndIf}
  ${Else}
    Delete "$DESKTOP\${HS_NAME}.lnk"
  ${EndIf}
  WriteRegStr SHCTX "${HS_INSTALL_KEY}" "Components" "$1"

  ; Written last: its presence means the payload above is complete.
  SetOutPath "$INSTDIR"
  File "/oname=install-manifest.txt" "${HS_MANIFEST}"
SectionEnd

; ---------------------------------------------------------------------------
; Installer callbacks and pages

; Selects the optional components from a comma list in $9.
Function HsSelectComponents
  Push $7
  Push $8
  StrCpy $8 "assoc"
  Call HsListHas
  ${If} $7 = 1
    !insertmacro SelectSection ${SecAssoc}
  ${Else}
    !insertmacro UnselectSection ${SecAssoc}
  ${EndIf}
  StrCpy $8 "desktop"
  Call HsListHas
  ${If} $7 = 1
    !insertmacro SelectSection ${SecDesktop}
  ${Else}
    !insertmacro UnselectSection ${SecDesktop}
  ${EndIf}
  Pop $8
  Pop $7
FunctionEnd

; HsHoldSelf (protected.nsh): this setup's file and the folders above it.
!insertmacro HS_HOLD_SELF_FUNCTIONS

Function .onInit
  SetRegView 64
  SetShellVarContext current
  Call HsHoldSelf
  StrCpy $HsUserLocalAppData $LOCALAPPDATA
  StrCpy $HsDirArg $INSTDIR
  Call HsParseArgs
  ${If} $HsUpdate = 1
    SetSilent silent
    ; Taken first, before the wait for the app: the app's own handoff
    ; waits for it, so a start after the app's quit finds it.
    Call HsMakeStartEvent
    System::Call 'kernel32::CreateMutexW(p 0, i 1, w "${HS_SETUP_MUTEX}") p .r0'
    StrCpy $HsSetupMutex $0
  ${EndIf}
  Call HsCheckAdmin

  ${IfNot} ${RunningX64}
    StrCpy $R8 2
    StrCpy $R7 "HikariSub needs 64-bit Windows."
    Call HsFail
  ${EndIf}
  ${IfNot} ${AtLeastBuild} 17763
    StrCpy $R8 2
    StrCpy $R7 "HikariSub needs Windows 10 version 1809 or later."
    Call HsFail
  ${EndIf}

  Call HsFindExisting

  ${If} $HsElevated = 1
    StrCpy $HsScope "machine"
  ${ElseIf} $HsCmdScope != ""
    StrCpy $HsScope $HsCmdScope
  ${ElseIf} $HsExistScope != ""
    StrCpy $HsScope $HsExistScope
  ${Else}
    StrCpy $HsScope "user"
  ${EndIf}

  ${If} $HsExistScope != ""
  ${AndIf} $HsScope != $HsExistScope
    ${If} $HsExistScope == "machine"
      StrCpy $R7 "HikariSub is already installed for all users. To install it for yourself alone, uninstall it first."
    ${Else}
      StrCpy $R7 "HikariSub is already installed for you alone. To install it for all users, uninstall it first."
    ${EndIf}
    StrCpy $R8 6
    Call HsFail
  ${EndIf}

  ${If} $HsScope == "machine"
  ${AndIf} $HsIsAdmin <> 1
  ${AndIf} ${Silent}
    StrCpy $R8 4
    StrCpy $R7 "Installing HikariSub for all users needs administrator rights."
    Call HsFail
  ${EndIf}

  Call HsApplyScope
  Call HsCheckMachineDir

  ${If} $HsExistVersion != ""
    StrCpy $R0 $HsExistVersion
    StrCpy $R1 "${HS_VERSION}"
    Call HsSemverCompare
    ${If} $R2 > 0
      ${If} ${Silent}
        StrCpy $R8 5
        StrCpy $R7 "HikariSub $HsExistVersion is installed, which is newer than ${HS_VERSION}."
        Call HsFail
      ${EndIf}
      ${If} ${Cmd} `MessageBox MB_OKCANCEL|MB_ICONEXCLAMATION "HikariSub $HsExistVersion is installed, which is newer than ${HS_VERSION}.$\n$\nChoose OK to replace it with the older version." IDCANCEL`
        SetErrorLevel 1
        Quit
      ${EndIf}
    ${EndIf}
  ${EndIf}

  ${If} $HsExistScope != ""
  ${AndIf} $HsHasExistComponents = 1
    StrCpy $9 $HsExistComponents
  ${ElseIf} $HsExistScope == ""
  ${AndIf} ${Silent}
  ${AndIf} $HsHasCmdComponents = 1
    StrCpy $9 $HsCmdComponents
  ${Else}
    StrCpy $9 "assoc"
  ${EndIf}
  Call HsSelectComponents
FunctionEnd

Function .onInstSuccess
  Call HsTakeStartRequest
  ${If} $HsRelaunch = 1
    Call HsLaunchApp
  ${EndIf}
FunctionEnd

Function .onInstFailed
  ${If} $HsUpdate = 1
    StrCpy $R8 2
    StrCpy $R7 "The update could not be installed."
    Call HsFail
  ${EndIf}
  SetErrorLevel 2
FunctionEnd

Function HsScopePageCreate
  ${If} $HsElevated = 1
    Abort
  ${EndIf}
  !insertmacro MUI_HEADER_TEXT "Install for" "Choose who can use ${HS_NAME} on this computer."
  nsDialogs::Create 1018
  Pop $0
  ${If} $0 == error
    Abort
  ${EndIf}
  ${NSD_CreateRadioButton} 0 6u 100% 12u "&Just me (recommended)"
  Pop $HsRadioUser
  ${NSD_CreateLabel} 12u 20u -12u 18u "Installs in your user folder. No administrator rights are needed."
  Pop $0
  ${NSD_CreateRadioButton} 0 44u 100% 12u "&All users (requires administrator)"
  Pop $HsRadioMachine
  ${NSD_CreateLabel} 12u 58u -12u 18u "Installs in Program Files for everyone who uses this computer."
  Pop $0
  ${If} $HsScope == "machine"
    ${NSD_Check} $HsRadioMachine
  ${Else}
    ${NSD_Check} $HsRadioUser
  ${EndIf}
  ${If} $HsExistScope != ""
    EnableWindow $HsRadioUser 0
    EnableWindow $HsRadioMachine 0
    ${If} $HsExistScope == "machine"
      StrCpy $1 "HikariSub is already installed for all users, in $HsExistDir. It is updated there. To install it for yourself alone instead, uninstall it first."
    ${Else}
      StrCpy $1 "HikariSub is already installed for you alone, in $HsExistDir. It is updated there. To install it for all users instead, uninstall it first."
    ${EndIf}
    ${NSD_CreateLabel} 0 90u 100% 36u "$1"
    Pop $0
  ${EndIf}
  nsDialogs::Show
FunctionEnd

Function HsScopePageLeave
  ${NSD_GetState} $HsRadioMachine $0
  ${If} $0 = ${BST_CHECKED}
    StrCpy $HsScope "machine"
  ${Else}
    StrCpy $HsScope "user"
  ${EndIf}
  ${If} $HsScope == "machine"
  ${AndIf} $HsIsAdmin <> 1
    ; Start an elevated copy for all users and leave (spec 6.2).
    ${GetParameters} $R1
    StrCpy $R1 "/ALLUSERS /ELEVATED $R1"
    ${If} $HsDirArg != ""
      StrCpy $R1 "$R1 /D=$HsDirArg"
    ${EndIf}
    ${If} $HsHeld <> 1
      MessageBox MB_OK|MB_ICONSTOP "To install HikariSub for all users, close this setup and run it as administrator."
      Abort
    ${EndIf}
    StrCpy $R0 "$EXEPATH"
    Call HsRunElevated
    ${If} $R2 == "ok"
      SetErrorLevel 0
      Quit
    ${ElseIf} $R2 == "cancelled"
      MessageBox MB_OK|MB_ICONINFORMATION "Installing for all users needs administrator approval. Choose Just me, or choose All users again and approve the prompt."
    ${Else}
      MessageBox MB_OK|MB_ICONSTOP "The setup could not start with administrator rights."
    ${EndIf}
    Abort
  ${EndIf}
  Call HsApplyScope
FunctionEnd

Function HsDirectoryPagePre
  ; An existing install is updated where it is.
  ${If} $HsExistScope != ""
  ${AndIf} $HsExistDir != ""
    Abort
  ${EndIf}
FunctionEnd

Function HsDirectoryPageLeave
  ${If} $HsScope == "machine"
    Call HsMachineDirOk
    ${If} $R2 <> 1
      MessageBox MB_OK|MB_ICONEXCLAMATION "For all users, HikariSub installs in a new folder directly in $PROGRAMFILES64. Choose a folder name there that is not in use, or go back and choose Just me."
      Abort
    ${EndIf}
  ${EndIf}
  ClearErrors
  CreateDirectory "$INSTDIR"
  FileOpen $0 "$INSTDIR\.hikarisub-setup-write-test" w
  ${If} ${Errors}
    MessageBox MB_OK|MB_ICONEXCLAMATION "Setup cannot write to $INSTDIR. Choose another folder, or go back and choose All users."
    Abort
  ${EndIf}
  FileClose $0
  Delete "$INSTDIR\.hikarisub-setup-write-test"
FunctionEnd

; ---------------------------------------------------------------------------
; Uninstaller

Function un.onInit
  SetRegView 64
  SetShellVarContext current
  StrCpy $HsUserLocalAppData $LOCALAPPDATA
  Call un.HsCheckAdmin
  ${GetParameters} $R0
  StrCpy $HsScope ""
  StrCpy $HsElevated 0
  ClearErrors
  ${GetOptions} $R0 "/CURRENTUSER" $R1
  ${IfNot} ${Errors}
    StrCpy $HsScope "user"
  ${EndIf}
  ClearErrors
  ${GetOptions} $R0 "/ALLUSERS" $R1
  ${IfNot} ${Errors}
    StrCpy $HsScope "machine"
  ${EndIf}
  ClearErrors
  ${GetOptions} $R0 "/ELEVATED" $R1
  ${IfNot} ${Errors}
    StrCpy $HsElevated 1
    StrCpy $HsScope "machine"
  ${EndIf}
  ${If} $HsScope == ""
    StrCpy $HsScope "user"
    ClearErrors
    FileOpen $0 "$INSTDIR\package-kind.txt" r
    ${IfNot} ${Errors}
      FileRead $0 $1
      FileClose $0
      StrCpy $1 $1 12
      ${If} $1 == "nsis-machine"
        StrCpy $HsScope "machine"
      ${EndIf}
    ${EndIf}
  ${EndIf}

  ${If} $HsScope == "machine"
  ${AndIf} $HsIsAdmin <> 1
    ${If} ${Silent}
    ${OrIf} $HsElevated = 1
      SetErrorLevel 4
      Quit
    ${EndIf}
    ; Only from a folder the user cannot change: package-kind.txt, which
    ; says all users, and the uninstaller started elevated would otherwise
    ; be the user's to rewrite, and a same-user process could have its own
    ; code run as administrator (spec 6.4).
    StrCpy $R0 $INSTDIR
    Call un.HsProtectedFromUser
    ${If} $R2 <> 1
      MessageBox MB_OK|MB_ICONSTOP "HikariSub's folder, $INSTDIR, can be changed without administrator rights, so the uninstaller does not ask for them. To uninstall HikariSub for all users, run the uninstaller as administrator."
      SetErrorLevel 4
      Quit
    ${EndIf}
    StrCpy $R0 "$INSTDIR\${HS_UNINSTALLER}"
    StrCpy $R1 "/ALLUSERS /ELEVATED"
    Call un.HsRunElevated
    ${If} $R2 == "ok"
      SetErrorLevel 0
      Quit
    ${EndIf}
    MessageBox MB_OK|MB_ICONSTOP "Uninstalling HikariSub for all users needs administrator approval."
    SetErrorLevel 4
    Quit
  ${EndIf}

  ${If} $HsScope == "machine"
    SetShellVarContext all
  ${Else}
    SetShellVarContext current
  ${EndIf}
FunctionEnd

Section "Uninstall"
  ; Never while HikariSub runs (spec 6.4).
  ${If} ${Silent}
    Call un.HsWaitAppGone
    ${If} $R9 = 1
      SetErrorLevel 3
      Quit
    ${EndIf}
  ${Else}
    ${Do}
      Call un.HsAppRunning
      ${If} $R9 = 0
        ${Break}
      ${EndIf}
      ${If} ${Cmd} `MessageBox MB_RETRYCANCEL|MB_ICONEXCLAMATION "HikariSub is running. Close it, then choose Retry." IDCANCEL`
        SetErrorLevel 1
        Quit
      ${EndIf}
    ${Loop}
  ${EndIf}

  ; The payload, by its manifest; never anything else in the folder.
  ${If} ${FileExists} "$INSTDIR\install-manifest.txt"
    FileOpen $0 "$INSTDIR\install-manifest.txt" r
    ${Do}
      ClearErrors
      FileRead $0 $R0
      ${If} ${Errors}
        ${Break}
      ${EndIf}
      Call un.HsCleanLine
      ${If} $R0 != ""
        Delete "$INSTDIR\$R0"
        Call un.HsRemoveEmptyParents
      ${EndIf}
    ${Loop}
    FileClose $0
  ${EndIf}
  Delete "$INSTDIR\package-kind.txt"
  Delete "$INSTDIR\install-manifest.txt"
  Delete "$INSTDIR\${HS_UNINSTALLER}"
  RMDir "$INSTDIR"

  Delete "$SMPROGRAMS\${HS_NAME}.lnk"
  Delete "$DESKTOP\${HS_NAME}.lnk"

  Call un.HsRemoveAssociations

  ReadRegStr $0 SHCTX "${HS_APPPATHS_KEY}" ""
  ${If} $0 == "$INSTDIR\${HS_EXE}"
    DeleteRegKey SHCTX "${HS_APPPATHS_KEY}"
  ${EndIf}
  DeleteRegKey SHCTX "${HS_INSTALL_KEY}"
  DeleteRegKey /ifempty SHCTX "${HS_APP_KEY}"
  DeleteRegKey SHCTX "${HS_UNINST_KEY}"
SectionEnd
