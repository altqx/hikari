add_executable(hikarisub_confine confine.cpp)
# Beside HikariSub.exe, as in an install: agents' specs find it next to their bridge.
set_target_properties(hikarisub_confine PROPERTIES OUTPUT_NAME hikarisub-confine
    RUNTIME_OUTPUT_DIRECTORY "$<TARGET_FILE_DIR:hikarisub>")
target_link_libraries(hikarisub_confine PRIVATE advapi32)
hikari_module(hikarisub_confine)
