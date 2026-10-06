# W1: the deployment manifest of the optional DirectShow playback adapter
# (docs/qt/proposals/backend-interfaces.md: manifests enumerate dynamically
# loaded native libraries). Read from hikari_directshow_probe, the adapter
# alone in an executable:
# - static: the DirectShow base classes it compiles in (Thirdparty/BaseClasses,
#   as legacy linked them);
# - operatingSystemLibraries: its direct imports, which must all be Windows'
#   own (the compiler runtime listed apart);
# - comServers: the COM classes it creates itself (the filter graph manager and
#   the DirectSound renderer) with the DLL registered for each;
# - systemFilters: the splitters and decoders a graph adds come from the
#   filters registered on the machine; none ships with the adapter.
#
#   cmake -DPROBE=<hikari_directshow_probe.exe> -DOUT=<manifest.json> -P DirectShowManifest.cmake
cmake_minimum_required(VERSION 3.28)
foreach(var PROBE OUT)
    if(NOT DEFINED ${var})
        message(FATAL_ERROR "DirectShowManifest: ${var} is required")
    endif()
endforeach()
if(NOT CMAKE_HOST_WIN32)
    message(FATAL_ERROR "DirectShowManifest: the DirectShow adapter exists on Windows only")
endif()
set(problems "")

find_program(DUMPBIN dumpbin REQUIRED)
execute_process(COMMAND "${DUMPBIN}" /nologo /dependents "${PROBE}" OUTPUT_VARIABLE imports RESULT_VARIABLE rc)
if(NOT rc EQUAL 0)
    list(APPEND problems "dumpbin failed on ${PROBE}")
endif()
string(REGEX MATCHALL "[A-Za-z0-9_.-]+\\.[Dd][Ll][Ll]" dlls "${imports}")
list(REMOVE_DUPLICATES dlls)
set(os_libs "")
set(runtime_libs "")
foreach(dll IN LISTS dlls)
    string(TOLOWER "${dll}" lower)
    if(lower MATCHES "^(msvcp|vcruntime|concrt|vccorlib)[0-9_]*\\.dll$")
        list(APPEND runtime_libs "${lower}")
    elseif(lower MATCHES "^api-ms-win-crt-" OR EXISTS "$ENV{SystemRoot}/System32/${dll}")
        list(APPEND os_libs "${lower}")
    else()
        list(APPEND problems "dynamic dependency ${dll} is not an operating-system library")
    endif()
endforeach()
# DirectShow itself is reached through COM, so it must be among the imports.
foreach(required ole32.dll)
    if(NOT required IN_LIST os_libs)
        list(APPEND problems "${required} is not imported")
    endif()
endforeach()

# The COM servers the adapter instantiates (CoCreateInstance).
set(servers "FilterGraph|{E436EBB3-524F-11CE-9F53-0020AF0BA770}" "DSoundRender|{79376820-07D0-11CF-A24D-0020AFD79767}")
set(server_json "")
foreach(server IN LISTS servers)
    string(REPLACE "|" ";" server "${server}")
    list(GET server 0 name)
    list(GET server 1 clsid)
    execute_process(COMMAND reg query "HKCR\\CLSID\\${clsid}\\InprocServer32" /ve
        OUTPUT_VARIABLE reg RESULT_VARIABLE rc ERROR_QUIET)
    set(dll "")
    if(rc EQUAL 0 AND reg MATCHES "REG_(EXPAND_)?SZ[ \t]+([^\r\n]+)")
        string(STRIP "${CMAKE_MATCH_2}" dll)
        get_filename_component(dll "${dll}" NAME)
        string(TOLOWER "${dll}" dll)
    else()
        list(APPEND problems "CLSID_${name} ${clsid} is not registered")
    endif()
    string(APPEND server_json "    {\"class\": \"CLSID_${name}\", \"clsid\": \"${clsid}\", \"inprocServer\": \"${dll}\"},\n")
endforeach()
string(REGEX REPLACE ",\n$" "\n" server_json "${server_json}")

function(json_strings out)
    set(text "")
    foreach(item IN LISTS ARGN)
        string(APPEND text "    \"${item}\",\n")
    endforeach()
    string(REGEX REPLACE ",\n$" "\n" text "${text}")
    set(${out} "${text}" PARENT_SCOPE)
endfunction()
list(SORT os_libs)
list(SORT runtime_libs)
json_strings(os_json ${os_libs})
json_strings(runtime_json ${runtime_libs})
file(SHA256 "${PROBE}" probe_sha)
get_filename_component(probe_file "${PROBE}" NAME)
file(WRITE "${OUT}" "{
  \"component\": \"directshow-adapter\",
  \"optional\": true,
  \"platform\": \"windows\",
  \"executable\": {\"file\": \"${probe_file}\", \"sha256\": \"${probe_sha}\"},
  \"static\": [
    {\"source\": \"Thirdparty/BaseClasses\", \"description\": \"DirectShow base classes from the Windows SDK samples, locally patched (legacy's baseclasses.lib)\"}
  ],
  \"operatingSystemLibraries\": [
${os_json}  ],
  \"compilerRuntime\": [
${runtime_json}  ],
  \"comServers\": [
${server_json}  ],
  \"systemFilters\": \"the splitters and decoders the filter graph inserts are the machine's registered DirectShow filters and DMOs; none ships with HikariSub\"
}
")
if(problems)
    list(JOIN problems "\n  " text)
    message(FATAL_ERROR "FAIL: directshow-adapter manifest is incomplete:\n  ${text}")
endif()
list(JOIN os_libs ", " os_text)
message(STATUS "PASS directshow-adapter manifest: ${os_text}; ${OUT}")
