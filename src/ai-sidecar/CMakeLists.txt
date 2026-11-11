hikari_api_version(_hikari_api_version)
set(_hikari_ai_dir "${CMAKE_CURRENT_SOURCE_DIR}")
set(_hikari_ai_installed "${CMAKE_CURRENT_BINARY_DIR}/installed.stamp")
set(_hikari_ai_out "${CMAKE_CURRENT_BINARY_DIR}/hikari-ai")
set(HIKARI_AI_RUNTIME_FILE "${_hikari_ai_out}/hikari-ai-runtime${CMAKE_EXECUTABLE_SUFFIX}"
    CACHE INTERNAL "The sidecar runtime this build makes")
set(HIKARI_AI_BUNDLE_FILE "${_hikari_ai_out}/hikari-ai.js" CACHE INTERNAL "The sidecar bundle this build makes")
file(GLOB_RECURSE _hikari_ai_sources CONFIGURE_DEPENDS "${_hikari_ai_dir}/src/*.ts" "${_hikari_ai_dir}/src/*.json"
    "${_hikari_ai_dir}/tools/*.ts" "${_hikari_ai_dir}/licenses/*" "${_hikari_ai_dir}/skills/*")

add_custom_command(OUTPUT "${_hikari_ai_installed}"
    COMMAND "${HIKARI_BUN}" ci --ignore-scripts --omit=optional
    COMMAND "${CMAKE_COMMAND}" -E touch "${_hikari_ai_installed}"
    WORKING_DIRECTORY "${_hikari_ai_dir}"
    DEPENDS "${_hikari_ai_dir}/package.json" "${_hikari_ai_dir}/bun.lock" "${_hikari_ai_dir}/bunfig.toml"
    COMMENT "Installing the AI sidecar's locked packages"
    VERBATIM)
set(_hikari_ai_sdk_files)
foreach(_sdk anthropic gemini openai)
    list(APPEND _hikari_ai_sdk_files "${_hikari_ai_out}/hikari-ai-${_sdk}.js" "${_hikari_ai_out}/hikari-ai-${_sdk}.js.jsc")
endforeach()
add_custom_command(OUTPUT "${HIKARI_AI_RUNTIME_FILE}" "${HIKARI_AI_BUNDLE_FILE}" "${HIKARI_AI_BUNDLE_FILE}.jsc"
        ${_hikari_ai_sdk_files} "${_hikari_ai_out}/build.json"
    COMMAND "${HIKARI_BUN}" tools/build.ts --target "${HIKARI_BUN_HOST_PLATFORM}" --runtime "${HIKARI_BUN}"
        --out "${_hikari_ai_out}" --version 0.0.0-dev --build-id "${HIKARI_AI_BUILD_ID}"
        --api-version "${_hikari_api_version}"
    WORKING_DIRECTORY "${_hikari_ai_dir}"
    DEPENDS "${_hikari_ai_installed}" ${_hikari_ai_sources} "${_hikari_ai_dir}/NOTICES.txt"
    COMMENT "Building the AI sidecar"
    VERBATIM)
add_custom_target(hikari_ai_sidecar ALL DEPENDS "${HIKARI_AI_RUNTIME_FILE}" "${HIKARI_AI_BUNDLE_FILE}")

if(BUILD_TESTING)
    add_test(NAME hikari_ai_sidecar_bun_tests COMMAND "${HIKARI_BUN}" test WORKING_DIRECTORY "${_hikari_ai_dir}")
    set_tests_properties(hikari_ai_sidecar_bun_tests PROPERTIES LABELS ai-sidecar
        ENVIRONMENT "HIKARI_AI_SIDECAR_RUNTIME=${HIKARI_BUN}")
    add_test(NAME hikari_ai_sidecar_types COMMAND "${HIKARI_BUN}" node_modules/typescript/bin/tsc --noEmit
        WORKING_DIRECTORY "${_hikari_ai_dir}")
    set_tests_properties(hikari_ai_sidecar_types PROPERTIES LABELS ai-sidecar)
endif()
