# Stage 5: credential-free proof that the frozen installation is a usable Qt.
find_package(Qt6 6.11.2 EXACT REQUIRED COMPONENTS Core Gui Qml Quick Multimedia ShaderTools LinguistTools)
get_filename_component(frozen_root "${CMAKE_CURRENT_SOURCE_DIR}/../_work/qt" ABSOLUTE)
cmake_path(IS_PREFIX frozen_root "${Qt6_DIR}" NORMALIZE from_frozen)
if(NOT from_frozen)
    message(FATAL_ERROR "Qt6_DIR ${Qt6_DIR} is not the frozen installation; distro/other Qt is not a substitute")
endif()
file(WRITE "${CMAKE_CURRENT_SOURCE_DIR}/../_evidence/qt-origin.txt"
    "Qt6_DIR=${Qt6_DIR}\nQt6_VERSION=${Qt6_VERSION}\nCXX=${CMAKE_CXX_COMPILER_ID} ${CMAKE_CXX_COMPILER_VERSION}\n")

qt_standard_project_setup(REQUIRES 6.11)
qt_add_executable(qtproof main.cpp)
qt_add_qml_module(qtproof URI HikariProof VERSION 1.0 QML_FILES Main.qml)
qt_add_shaders(qtproof "proof-shaders" PREFIX "/" BASE "${CMAKE_CURRENT_SOURCE_DIR}"
    FILES "${CMAKE_CURRENT_SOURCE_DIR}/tint.frag")
qt_add_translations(qtproof TS_FILES "${CMAKE_CURRENT_SOURCE_DIR}/i18n/qtproof_de.ts" RESOURCE_PREFIX "/i18n")
target_link_libraries(qtproof PRIVATE Qt6::Gui Qt6::Quick Qt6::Multimedia)

add_test(NAME qtproof-selftest COMMAND qtproof)
set_tests_properties(qtproof-selftest PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen")
find_program(XVFB_RUN xvfb-run)
if(XVFB_RUN)
    add_test(NAME qtproof-render-xvfb COMMAND "${XVFB_RUN}" -a $<TARGET_FILE:qtproof> --render)
    set_tests_properties(qtproof-render-xvfb PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=xcb")
endif()
