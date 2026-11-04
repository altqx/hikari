hikari_api_version(_hikari_api_version)
set(_hikari_ai_dir "${CMAKE_CURRENT_SOURCE_DIR}")
set(_hikari_ai_installed "${CMAKE_CURRENT_BINARY_DIR}/installed.stamp")
set(HIKARI_AI_SIDECAR_FILE "${CMAKE_CURRENT_BINARY_DIR}/hikari-ai${CMAKE_EXECUTABLE_SUFFIX}"
    CACHE INTERNAL "The sidecar this build makes")
file(GLOB_RECURSE _hikari_ai_sources CONFIGURE_DEPENDS "${_hikari_ai_dir}/src/*.ts" "${_hikari_ai_dir}/tools/*.ts"
    "${_hikari_ai_dir}/licenses/*")

add_custom_command(OUTPUT "${_hikari_ai_installed}"
    COMMAND "${HIKARI_BUN}" ci --ignore-scripts
    COMMAND "${CMAKE_COMMAND}" -E touch "${_hikari_ai_installed}"
    WORKING_DIRECTORY "${_hikari_ai_dir}"
    DEPENDS "${_hikari_ai_dir}/package.json" "${_hikari_ai_dir}/bun.lock" "${_hikari_ai_dir}/bunfig.toml"
    COMMENT "Installing the AI sidecar's locked packages"
    VERBATIM)
add_custom_command(OUTPUT "${HIKARI_AI_SIDECAR_FILE}"
    COMMAND "${HIKARI_BUN}" tools/build.ts --target "${HIKARI_BUN_HOST_PLATFORM}" --runtime "${HIKARI_BUN}"
        --out "${HIKARI_AI_SIDECAR_FILE}" --build-id "${HIKARI_AI_BUILD_ID}" --api-version "${_hikari_api_version}"
    WORKING_DIRECTORY "${_hikari_ai_dir}"
    DEPENDS "${_hikari_ai_installed}" ${_hikari_ai_sources} "${_hikari_ai_dir}/NOTICES.txt"
    COMMENT "Building the AI sidecar"
    VERBATIM)
add_custom_target(hikari_ai_sidecar ALL DEPENDS "${HIKARI_AI_SIDECAR_FILE}")

if(BUILD_TESTING)
    add_test(NAME hikari_ai_sidecar_bun_tests COMMAND "${HIKARI_BUN}" test WORKING_DIRECTORY "${_hikari_ai_dir}")
    set_tests_properties(hikari_ai_sidecar_bun_tests PROPERTIES LABELS ai-sidecar
        ENVIRONMENT "HIKARI_AI_SIDECAR_RUNTIME=${HIKARI_BUN}")
endif()
