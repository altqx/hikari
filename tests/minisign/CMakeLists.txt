hikari_add_gtest(hikari_minisign_tests SOURCES minisign_tests.cpp LIBRARIES hikari_minisign LABELS minisign)
target_compile_definitions(hikari_minisign_tests PRIVATE HIKARI_MINISIGN_FIXTURES="${CMAKE_CURRENT_SOURCE_DIR}/fixtures")

# The release job compiles these two sources alone (release.yml), so the target builds them without hikari_application.
add_executable(hikari_verify_release_signature "${PROJECT_SOURCE_DIR}/tools/release/verify_release_signature.cpp"
    "${PROJECT_SOURCE_DIR}/src/application/update_signature.cpp")
target_include_directories(hikari_verify_release_signature PRIVATE "${PROJECT_SOURCE_DIR}/src/application/include")
target_link_libraries(hikari_verify_release_signature PRIVATE hikari_minisign)
configure_file(fixtures/message.txt "${CMAKE_CURRENT_BINARY_DIR}/signed/hikarisub-update.json" COPYONLY)
set(signed "${CMAKE_CURRENT_BINARY_DIR}/signed/hikarisub-update.json")
add_test(NAME minisign.verify-release-signature
    COMMAND hikari_verify_release_signature "${signed}" "${CMAKE_CURRENT_SOURCE_DIR}/fixtures/prehashed.minisig" v1.0.0
        "${CMAKE_CURRENT_SOURCE_DIR}/fixtures/a.pub")
add_test(NAME minisign.verify-release-signature-other-tag
    COMMAND hikari_verify_release_signature "${signed}" "${CMAKE_CURRENT_SOURCE_DIR}/fixtures/prehashed.minisig" v1.0.1
        "${CMAKE_CURRENT_SOURCE_DIR}/fixtures/a.pub")
add_test(NAME minisign.verify-release-signature-shipped-keys
    COMMAND hikari_verify_release_signature "${signed}" "${CMAKE_CURRENT_SOURCE_DIR}/fixtures/prehashed.minisig" v1.0.0)
set_tests_properties(minisign.verify-release-signature minisign.verify-release-signature-other-tag
    minisign.verify-release-signature-shipped-keys PROPERTIES LABELS minisign TIMEOUT 30)
set_tests_properties(minisign.verify-release-signature PROPERTIES PASS_REGULAR_EXPRESSION "signed for v1.0.0")
set_tests_properties(minisign.verify-release-signature-other-tag PROPERTIES
    PASS_REGULAR_EXPRESSION "is for another release or file")
set_tests_properties(minisign.verify-release-signature-shipped-keys PROPERTIES
    PASS_REGULAR_EXPRESSION "is from a key that is not trusted")
