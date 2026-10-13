; A test program for packaging/windows/protected.nsh, the rule the
; uninstaller applies before it starts itself elevated (installers spec,
; 6.4). It runs silently as the user who starts it and exits 0 when the
; folder after /DIR= is protected from that user, 1 when it is not:
;
;   protected-test.exe /DIR=C:\Program Files\HikariSub
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

!insertmacro HS_PROTECTED_FUNCTIONS ""

Section
  ${GetParameters} $0
  ClearErrors
  ${GetOptions} $0 "/DIR=" $R0
  ${If} ${Errors}
    SetErrorLevel 2
    Quit
  ${EndIf}
  Call HsProtectedFromUser
  ${If} $R2 = 1
    SetErrorLevel 0
  ${Else}
    SetErrorLevel 1
  ${EndIf}
SectionEnd
