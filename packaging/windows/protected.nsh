; Whether an install folder is protected from the user who runs this process: the uninstaller must not
; relaunch itself elevated from a folder that user could change (they could swap the file before the UAC prompt).
; Each right is probed by opening with it alone (CreateFileW on the reparse point itself), so Windows checks this
; process's unelevated token; anything but "access denied" counts as granted.
; Same bytes on the main and legacy branches.

; FILE_WRITE_ATTRIBUTES counts: CreateFileW refuses writes to a read-only file whatever its rights.
!define HS_CHANGES  0xD0146 ; FILE_WRITE_DATA|FILE_APPEND_DATA|FILE_DELETE_CHILD|FILE_WRITE_ATTRIBUTES|DELETE|WRITE_DAC|WRITE_OWNER
!define HS_REPLACES 0xD0040 ; FILE_DELETE_CHILD|DELETE|WRITE_DAC|WRITE_OWNER

; HS_PROTECTED_FUNCTIONS needs this inserted with the same UN prefix.
!macro HS_PLAIN_PATH_FUNCTION UN

; $R0 = a folder. $R2 = 1 when it is a plain "X:\..." path that GetFullPathNameW leaves unchanged; Windows drops
; a trailing space or dot in a name and ":" names a stream, so those are refused.
Function ${UN}HsPlainPath
  Push $0
  Push $1
  Push $2
  Push $3
  Push $R1
  StrCpy $R2 0
  StrCpy $0 $R0 1
  StrCpy $1 $R0 2 1
  ${If} $1 == ":\"
    ; A letter: NSIS compares strings without case.
    StrCpy $2 "abcdefghijklmnopqrstuvwxyz"
    StrCpy $3 0
    ${Do}
      StrCpy $1 $2 1 $3
      ${If} $1 == ""
        ${Break}
      ${ElseIf} $1 == $0
        StrCpy $R2 1
        ${Break}
      ${EndIf}
      IntOp $3 $3 + 1
    ${Loop}
  ${EndIf}
  ${If} $R2 = 1
    StrCpy $0 $R0 "" 2
    StrCpy $0 "$0\"
    StrLen $2 $0
    StrCpy $3 1
    StrCpy $1 ""
    ${Do}
      ${If} $3 >= $2
      ${OrIf} $R2 <> 1
        ${Break}
      ${EndIf}
      StrCpy $R1 $0 1 $3
      ${If} $R1 == "/"
      ${OrIf} $R1 == ":"
      ${OrIf} $R1 == "*"
      ${OrIf} $R1 == "?"
      ${OrIf} $R1 == '"'
      ${OrIf} $R1 == "<"
      ${OrIf} $R1 == ">"
      ${OrIf} $R1 == "|"
        StrCpy $R2 0
      ${ElseIf} $R1 == "\"
        StrCpy $R1 $1 "" -1
        ${If} $1 == ""
        ${OrIf} $R1 == "."
        ${OrIf} $R1 == " "
          StrCpy $R2 0
        ${EndIf}
        StrCpy $1 ""
      ${Else}
        StrCpy $1 "$1$R1"
      ${EndIf}
      IntOp $3 $3 + 1
    ${Loop}
  ${EndIf}
  ${If} $R2 = 1
    System::Call 'kernel32::GetFullPathNameW(w R0, i ${NSIS_MAX_STRLEN}, w .r1, p 0) i .r2'
    ${If} $2 = 0
    ${OrIf} $2 >= ${NSIS_MAX_STRLEN}
    ${OrIf} $1 S!= $R0
      StrCpy $R2 0
    ${EndIf}
  ${EndIf}
  Pop $R1
  Pop $3
  Pop $2
  Pop $1
  Pop $0
FunctionEnd

!macroend

!macro HS_PROTECTED_FUNCTIONS UN

; $R0 = a path, $R1 = rights. $R2 = 1 when this process could open $R0 with any one of them, or cannot tell.
Function ${UN}HsMayOpen
  Push $0
  Push $1
  Push $2
  Push $3
  StrCpy $R2 0
  StrCpy $2 1
  ${Do}
    IntOp $3 $R1 & $2
    ${If} $3 <> 0
      ; FILE_SHARE_READ|WRITE|DELETE, OPEN_EXISTING, FILE_FLAG_BACKUP_SEMANTICS (folders) | FILE_FLAG_OPEN_REPARSE_POINT.
      System::Call 'kernel32::CreateFileW(w R0, i r3, i 7, p 0, i 3, i 0x02200000, p 0) p .r0 ?e'
      Pop $1
      ${If} $0 P<> -1
        System::Call 'kernel32::CloseHandle(p r0)'
        StrCpy $R2 1
        ${Break}
      ${ElseIf} $1 <> 5
        StrCpy $R2 1
        ${Break}
      ${EndIf}
    ${EndIf}
    IntOp $2 $2 << 1
    ${If} $2 > ${HS_CHANGES}
      ${Break}
    ${EndIf}
  ${Loop}
  Pop $3
  Pop $2
  Pop $1
  Pop $0
FunctionEnd

; $R0 = a folder. $R2 = 1 when the user could change it or anything under it, or it holds a reparse point.
Function ${UN}HsTreeChangeable
  Push $0
  Push $1
  Push $2
  Push $R0
  Push $R1
  StrCpy $R1 ${HS_CHANGES}
  Call ${UN}HsMayOpen
  ${If} $R2 = 0
    ClearErrors
    FindFirst $0 $1 "$R0\*"
    ${If} ${Errors}
      StrCpy $R2 1
    ${EndIf}
    ${Do}
      ${If} $1 == ""
      ${OrIf} $R2 <> 0
        ${Break}
      ${EndIf}
      ${If} $1 != "."
      ${AndIf} $1 != ".."
        Push $R0
        StrCpy $R0 "$R0\$1"
        System::Call 'kernel32::GetFileAttributesW(w R0) i .r2'
        ${If} $2 = -1
          StrCpy $R2 1
        ${Else}
          IntOp $2 $2 & 0x410 ; FILE_ATTRIBUTE_REPARSE_POINT | DIRECTORY
          ${If} $2 >= 0x400
            StrCpy $R2 1
          ${ElseIf} $2 = 0x10
            Call ${UN}HsTreeChangeable
          ${Else}
            StrCpy $R1 ${HS_CHANGES}
            Call ${UN}HsMayOpen
          ${EndIf}
        ${EndIf}
        Pop $R0
      ${EndIf}
      ${If} $R2 = 0
        FindNext $0 $1
      ${EndIf}
    ${Loop}
    FindClose $0
  ${EndIf}
  Pop $R1
  Pop $R0
  Pop $2
  Pop $1
  Pop $0
FunctionEnd

; $R0 = an install folder. $R2 = 1 when the user who runs this process cannot change it.
Function ${UN}HsProtectedFromUser
  Push $0
  Push $1
  Push $2
  Push $R0
  Push $R1
  Call ${UN}HsPlainPath
  ${If} $R2 = 1
    Call ${UN}HsTreeChangeable
    ${If} $R2 = 0
      StrCpy $0 $R0
      ${Do}
        StrLen $1 $0
        ${Do}
          IntOp $1 $1 - 1
          StrCpy $2 $0 1 $1
          ${If} $2 == "\"
            ${Break}
          ${EndIf}
        ${Loop}
        ${If} $1 <= 2
          StrCpy $0 $0 3
        ${Else}
          StrCpy $0 $0 $1
        ${EndIf}
        StrCpy $R0 $0
        StrCpy $R1 ${HS_REPLACES}
        Call ${UN}HsMayOpen
        ${If} $R2 = 1
        ${OrIf} $1 <= 2
          ${Break}
        ${EndIf}
      ${Loop}
    ${EndIf}
    IntOp $R2 1 - $R2
  ${EndIf}
  Pop $R1
  Pop $R0
  Pop $2
  Pop $1
  Pop $0
FunctionEnd

!macroend

; Sets $HsSelfHold and $HsHeld, which the includer declares.
!macro HS_HOLD_SELF_FUNCTIONS
; UAC reopens $EXEPATH by path, so this setup and every folder above it stay held (no write, rename or delete)
; until exit; through a link, on a network path, or when a hold fails, there is no relaunch: run setup as administrator.
Function HsHoldSelf
  Push $0
  Push $1
  Push $2
  StrCpy $HsHeld 0
  ; GENERIC_READ, FILE_SHARE_READ, OPEN_EXISTING, FILE_FLAG_OPEN_REPARSE_POINT.
  System::Call 'kernel32::CreateFileW(w "$EXEPATH", i 0x80000000, i 1, p 0, i 3, i 0x00200000, p 0) p .r0'
  StrCpy $HsSelfHold $0
  StrCpy $1 $EXEPATH 2
  ${If} $HsSelfHold P<> -1
  ${AndIf} $1 != "\\"
    Push $HsSelfHold
    Call HsIsLink
    Pop $2
    ${If} $2 = 0
      StrCpy $HsHeld 1
      StrCpy $1 $EXEPATH
      ${Do}
        ${GetParent} $1 $1
        StrLen $2 $1
        ${If} $2 <= 3
          ${Break}
        ${EndIf}
        ; FILE_LIST_DIRECTORY, FILE_SHARE_READ|FILE_SHARE_WRITE, OPEN_EXISTING, FILE_FLAG_BACKUP_SEMANTICS|FILE_FLAG_OPEN_REPARSE_POINT.
        System::Call 'kernel32::CreateFileW(w "$1", i 1, i 3, p 0, i 3, i 0x02200000, p 0) p .r0'
        ${If} $0 P= -1
          StrCpy $HsHeld 0
          ${Break}
        ${EndIf}
        Push $0
        Call HsIsLink
        Pop $2
        ${If} $2 <> 0
          StrCpy $HsHeld 0
          ${Break}
        ${EndIf}
      ${Loop}
    ${EndIf}
  ${EndIf}
  Pop $2
  Pop $1
  Pop $0
FunctionEnd

; Whether the open handle on the stack is a name-surrogate reparse point (junction, symlink), or cannot be told: 1 or 0 on the stack.
Function HsIsLink
  Exch $0
  Push $1
  Push $2
  Push $3
  ; FILE_ATTRIBUTE_TAG_INFO { FileAttributes, ReparseTag }, FileAttributeTagInfo = 9.
  System::Call '*(i 0, i 0) p .r1'
  System::Call 'kernel32::GetFileInformationByHandleEx(p r0, i 9, p r1, i 8) i .r2'
  System::Call '*$1(i .r3, i .r0)'
  System::Free $1
  ${If} $2 = 0
    StrCpy $0 1
  ${Else}
    IntOp $3 $3 & 0x400
    IntOp $0 $0 & 0x20000000
    ${If} $3 <> 0
    ${AndIf} $0 <> 0
      StrCpy $0 1
    ${Else}
      StrCpy $0 0
    ${EndIf}
  ${EndIf}
  Pop $3
  Pop $2
  Pop $1
  Exch $0
FunctionEnd
!macroend
