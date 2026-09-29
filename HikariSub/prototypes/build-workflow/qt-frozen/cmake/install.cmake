# Stage 3-4: run the official installer against the verified local mirror.
# The only stage that receives QT_INSTALLER_JWT_TOKEN. Its value is never
# printed, cached, put in argv or copied; the installer's account file is
# written under a private HOME that is deleted before this stage ends.
include("${CMAKE_CURRENT_LIST_DIR}/common.cmake")
set(HIKARI_QT_REPOSITORY_BASE "file://${qf_mirror}/" CACHE STRING
    "Where the mirror is served from (file:// or a loopback http:// with an access log)")
# Owner authorization recorded on altqx/hikari#48, 2026-09-29, for this isolated run.
set(authorization "altqx/hikari#48 issuecomment-5890780519")
qf_status("INSTALL: started; no installation pass")

if(NOT DEFINED ENV{QT_INSTALLER_JWT_TOKEN} OR "$ENV{QT_INSTALLER_JWT_TOKEN}" STREQUAL "")
    message(FATAL_ERROR "QT_INSTALLER_JWT_TOKEN is absent; the installer run needs the owner's secret")
endif()
set(target "${qf_work}/qt")
if(EXISTS "${target}")
    message(FATAL_ERROR "Target ${target} already exists; this experiment needs an empty target")
endif()

# Re-verify the whole mirror first: a changed object must not reach the installer.
qf_mirror_entries(entries)
foreach(e IN LISTS entries)
    string(REPLACE "|" ";" f "${e}")
    list(GET f 0 path)
    list(GET f 2 len)
    list(GET f 3 sha)
    qf_verify_file("${qf_mirror}/${path}" ${len} ${sha} ok)
    if(NOT ok)
        message(FATAL_ERROR "Mirror object missing or changed before install: ${path}")
    endif()
endforeach()

# The authorization covers the reviewed license bytes only.
set(lic_dir "${qf_work}/license-check")
file(REMOVE_RECURSE "${lic_dir}")
string(JSON nroots LENGTH "${qf_lock}" roots)
math(EXPR last "${nroots} - 1")
foreach(i RANGE ${last})
    string(JSON suffix GET "${qf_lock}" roots ${i} suffix)
    if(suffix MATCHES "^all_os/license_agreements/")
        string(JSON meta GET "${qf_lock}" roots ${i} metadata name)
        file(ARCHIVE_EXTRACT INPUT "${qf_mirror}/${suffix}${meta}" DESTINATION "${lic_dir}")
    endif()
endforeach()
string(JSON nlic LENGTH "${qf_lock}" license_texts)
math(EXPR last "${nlic} - 1")
foreach(i RANGE ${last})
    string(JSON lf GET "${qf_lock}" license_texts ${i} file)
    string(JSON ls GET "${qf_lock}" license_texts ${i} sha256)
    file(SHA256 "${lic_dir}/${lf}" actual)
    if(NOT actual STREQUAL ls)
        message(FATAL_ERROR "License text ${lf} changed since owner review; authorization does not cover it")
    endif()
endforeach()

set(repos "")
math(EXPR last "${nroots} - 1")
foreach(i RANGE ${last})
    string(JSON suffix GET "${qf_lock}" roots ${i} suffix)
    list(APPEND repos "${HIKARI_QT_REPOSITORY_BASE}${suffix}")
endforeach()
list(JOIN repos "," repos_arg)
string(JSON npkg LENGTH "${qf_lock}" requested)
math(EXPR last "${npkg} - 1")
set(packages "")
foreach(i RANGE ${last})
    string(JSON p GET "${qf_lock}" requested ${i})
    list(APPEND packages "${p}")
endforeach()

set(private "${qf_work}/private")
file(REMOVE_RECURSE "${private}")
file(MAKE_DIRECTORY "${private}/home" "${private}/tmp" "${qf_work}/metadata-cache")
set(ENV{HOME} "${private}/home")
set(ENV{XDG_DATA_HOME} "${private}/home/.local/share")
set(ENV{XDG_CONFIG_HOME} "${private}/home/.config")
set(ENV{TMPDIR} "${private}/tmp")
set(ENV{QT_QPA_PLATFORM} "offscreen")

# No --default-answer / --accept-messages: an uncharacterized prompt reads EOF
# from /dev/null or hits the timeout, and the run fails for review.
set(args
    --root "${target}"
    --cache-path "${qf_work}/metadata-cache"
    --set-temp-repository "${repos_arg}"
    --no-default-installations
    --no-force-installations
    --max-concurrent-operations 2
    --accept-licenses
    --accept-obligations
    --confirm-command
    --auto-answer "telemetry-question=No,AssociateCommonFiletypes=No,OperationDoesNotExistError=Abort,OverwriteTargetDirectory=No,installationErrorWithCancel=Cancel,stopProcessesForUpdates=Cancel"
    --verbose
    install ${packages})
string(REPLACE ";" " " printable "${args}")
file(WRITE "${qf_evidence}/installer-argv.txt" "${printable}\nauthorization=${authorization}\n")
execute_process(COMMAND "${qf_installer}" ${args}
    INPUT_FILE /dev/null
    OUTPUT_FILE "${private}/installer.log" ERROR_FILE "${private}/installer.log"
    RESULT_VARIABLE rc TIMEOUT 1800)
file(WRITE "${qf_evidence}/installer-exit.txt" "${rc}\n")

# Sanitized excerpt only: progress/error lines, with e-mail and token-like
# strings redacted. The raw log and the account file never leave private/.
file(STRINGS "${private}/installer.log" lines)
set(excerpt "")
foreach(l IN LISTS lines)
    if(l MATCHES "(Error|error|Warning|warning|Installing|Extracting|Downloading|Preparing|component|Component|repository|Repository|license|License|obligation|Aborting|Cancel|finished|Done)")
        string(REGEX REPLACE "[A-Za-z0-9._%+-]+@[A-Za-z0-9.-]+" "<email>" l "${l}")
        string(REGEX REPLACE "eyJ[A-Za-z0-9._-]+" "<token>" l "${l}")
        string(REGEX REPLACE "[A-Za-z0-9_-]{40,}" "<long-string>" l "${l}")
        string(APPEND excerpt "${l}\n")
    endif()
endforeach()
file(WRITE "${qf_evidence}/installer-excerpt.log" "${excerpt}")
file(REMOVE_RECURSE "${private}/home")

if(NOT rc STREQUAL "0")
    message(FATAL_ERROR "Official installer failed (${rc}); see sanitized excerpt; no installation pass")
endif()

# Closure: every installed component must be locked (selected or conditional).
if(NOT EXISTS "${target}/components.xml")
    message(FATAL_ERROR "Installer reported success but wrote no components.xml")
endif()
file(READ "${target}/components.xml" comps)
string(REGEX MATCHALL "<Name>[^<]+</Name>" names "${comps}")
string(JSON nsel LENGTH "${qf_lock}" selected)
string(JSON ncond LENGTH "${qf_lock}" conditional)
set(locked "")
foreach(kind selected conditional)
    string(JSON n LENGTH "${qf_lock}" ${kind})
    math(EXPR last "${n} - 1")
    foreach(i RANGE ${last})
        string(JSON p GET "${qf_lock}" ${kind} ${i})
        list(APPEND locked "${p}")
    endforeach()
endforeach()
set(report "component\tlocked\n")
set(unknown "")
foreach(n IN LISTS names)
    string(REGEX REPLACE "</?Name>" "" n "${n}")
    if(n IN_LIST locked)
        string(APPEND report "${n}\tyes\n")
    else()
        string(APPEND report "${n}\tNO\n")
        list(APPEND unknown "${n}")
    endif()
endforeach()
file(WRITE "${qf_evidence}/installed-components.tsv" "${report}")
if(unknown)
    message(FATAL_ERROR "Installed components outside the lock: ${unknown}")
endif()
qf_status("INSTALLED: official installer consumed the local mirror; proof build not yet run")
