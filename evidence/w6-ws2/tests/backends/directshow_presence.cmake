# W1: whether the DirectShow adapter is compiled into an executable: its
# renderer filter's name ("HikariSub video Renderer", a wide literal) occurs
# only in the adapter. Windows builds must contain it, others must not.
#
#   cmake -DEXECUTABLE=<file> -DEXPECT=present|absent -P directshow_presence.cmake
foreach(var EXECUTABLE EXPECT)
    if(NOT DEFINED ${var})
        message(FATAL_ERROR "directshow_presence: ${var} is required")
    endif()
endforeach()
file(STRINGS "${EXECUTABLE}" wide ENCODING UTF-16LE REGEX "HikariSub video Renderer")
file(STRINGS "${EXECUTABLE}" narrow REGEX "HikariSub video Renderer")
if(wide OR narrow)
    set(found present)
else()
    set(found absent)
endif()
if(NOT found STREQUAL EXPECT)
    message(FATAL_ERROR "FAIL: the DirectShow adapter is ${found} in ${EXECUTABLE}, expected ${EXPECT}")
endif()
message(STATUS "PASS: the DirectShow adapter is ${found} in ${EXECUTABLE}")
