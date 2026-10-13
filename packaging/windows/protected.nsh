; Whether an install folder is protected from the user who runs this process
; (installers spec, 4.2 and 6.4): the rule the app applies before it asks for
; elevation (IsProtectedFromUser in legacy's UpdateApply, isProtectedFromUser
; in Qt's update_apply), for the uninstaller, which
; must not start a copy of itself elevated from a folder the user could
; change. A same-user process could replace that file between the check and
; the UAC prompt, and get its own code run as administrator.
;
; Each right is asked for by opening the file or folder with it alone
; (CreateFileW, the reparse point itself, not its target), so Windows makes
; the access check with this process's own, unelevated token. Anything but
; "access denied" counts as granted: a file that cannot be opened for
; another reason cannot be shown to be protected.
;
; The folder must be a plain "X:\..." path, and:
;   - the folder and everything under it grant none of HS_CHANGES, which
;     includes FILE_WRITE_ATTRIBUTES because CreateFileW refuses writes to a
;     read-only file whatever its rights, and contain no reparse point;
;   - each folder above it grants none of HS_REPLACES, which would let the
;     user delete or rename the folder, or rewrite its rights.
;
; The same file on the legacy and Qt branches.

!define HS_CHANGES  0xD0146 ; FILE_WRITE_DATA|FILE_APPEND_DATA|FILE_DELETE_CHILD|FILE_WRITE_ATTRIBUTES|DELETE|WRITE_DAC|WRITE_OWNER
!define HS_REPLACES 0xD0040 ; FILE_DELETE_CHILD|DELETE|WRITE_DAC|WRITE_OWNER

; A plain "X:\..." path, which the installer also asks of an all-users
; folder (HS_PLAIN_PATH_FUNCTION); HS_PROTECTED_FUNCTIONS needs it inserted
; with the same prefix.
!macro HS_PLAIN_PATH_FUNCTION UN

; $R0 = a folder. $R2 = 1 when it is a plain "X:\..." path: a drive letter,
; one "\" between names, no "/", no "." or ".." name, no trailing "\", no
; name that ends in a space or a dot (Windows drops those, so "C:\a\.. \b"
; is "C:\b"), none of : * ? " < > | (":" names a stream), and Windows'
; own full form of it (GetFullPathNameW) is the same string. A caller can
; then compare it with a prefix and know where it points.
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
    ; The rest, as "\name\name": each name non-empty and not "." or "..".
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
    ; What Windows makes of it must be it, case and all.
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

; $R0 = a path, $R1 = rights. $R2 = 1 when this process could open $R0 with
; any one of them, or cannot tell; else 0.
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
      ; FILE_SHARE_READ|WRITE|DELETE, OPEN_EXISTING,
      ; FILE_FLAG_BACKUP_SEMANTICS (folders) | FILE_FLAG_OPEN_REPARSE_POINT.
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

; $R0 = a folder. $R2 = 1 when the user could change it or anything under
; it, or it holds a reparse point; else 0.
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

; $R0 = an install folder. $R2 = 1 when the user who runs this process
; cannot change it (see the top of this file); else 0.
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
      ; Each folder above it, up to the drive's root.
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
          ; "X:\": the root, the last one.
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
; The all-users relaunch hands $EXEPATH to UAC, which finds the file by
; that path again. A same-user process could otherwise rename this running
; setup, or a folder above it, and put its own program at that path before
; the prompt (an unsigned setup shows no publisher to tell them apart).
; From the start, this setup's file is held open with reads shared only,
; and every folder above it, up to the drive's root, with deletes not
; shared: until it exits, no other process can write, rename or delete the
; file, or rename or delete a folder on the way, so the file UAC starts is
; this one. A path through a junction or symbolic link (whose target its
; owner could change), a network path, or anything that cannot be held,
; and there is no relaunch: the user runs setup as administrator.
Function HsHoldSelf
  Push $0
  Push $1
  Push $2
  StrCpy $HsHeld 0
  ; GENERIC_READ, FILE_SHARE_READ, OPEN_EXISTING, FILE_FLAG_OPEN_REPARSE_POINT.
  ; Closed when setup exits.
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
        ; FILE_LIST_DIRECTORY, FILE_SHARE_READ|FILE_SHARE_WRITE, OPEN_EXISTING,
        ; FILE_FLAG_BACKUP_SEMANTICS|FILE_FLAG_OPEN_REPARSE_POINT. Kept open.
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

; Whether the open handle on the stack is a junction or symbolic link (a
; name-surrogate reparse point), or cannot be told: 1 or 0 on the stack.
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
