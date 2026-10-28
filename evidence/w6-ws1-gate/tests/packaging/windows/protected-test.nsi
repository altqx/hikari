; A test program for packaging/windows/protected.nsh, the rule the
; uninstaller applies before it starts itself elevated (installers spec,
; 6.4). It runs silently as the user who starts it and exits 0 when the
; folder after /DIR= is protected from that user, 1 when it is not:
;
;   protected-test.exe /DIR=C:\Program Files\HikariSub
;
; With /HOLDSELF it takes setup's self-hold (HsHoldSelf) instead and exits
; 11 when the file and every folder above it are held, 10 when not; with
; /HOLDSELF=<s> it keeps the holds for <s> seconds first.
;
; Built with build-protected-test.sh; tests/packaging/windows/protected-tests.ps1
; runs it against folders with made rights.

Unicode true
ManifestSupportedOS all
RequestExecutionLevel user
SilentInstall silent
Name "protected-test"
OutFile "${HS_OUTFILE}"

!include LogicLib.nsh
!include FileFunc.nsh
!include "${__FILEDIR__}/../../../packaging/windows/protected.nsh"

Var HsSelfHold
Var HsHeld

!insertmacro HS_PLAIN_PATH_FUNCTION ""
!insertmacro HS_PROTECTED_FUNCTIONS ""
!insertmacro HS_HOLD_SELF_FUNCTIONS

Section
  ${GetParameters} $0
  ClearErrors
  ${GetOptions} $0 "/HOLDSELF" $R0
  ${IfNot} ${Errors}
    Call HsHoldSelf
    StrCpy $R0 $R0 "" 1
    ${If} $R0 != ""
      IntOp $R0 $R0 * 1000
      Sleep $R0
    ${EndIf}
    IntOp $R1 $HsHeld + 10
    SetErrorLevel $R1
  ${Else}
    ClearErrors
    ${GetOptions} $0 "/DIR=" $R0
    ${If} ${Errors}
      SetErrorLevel 2
    ${Else}
      Call HsProtectedFromUser
      ${If} $R2 = 1
        SetErrorLevel 0
      ${Else}
        SetErrorLevel 1
      ${EndIf}
    ${EndIf}
  ${EndIf}
SectionEnd
