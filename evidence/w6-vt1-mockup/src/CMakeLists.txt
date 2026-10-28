# Module skeleton (B3). Target boundaries follow docs/qt/architecture.md; the
# allowed dependency directions are enforced by cmake/HikariModules.cmake.
include("${PROJECT_SOURCE_DIR}/cmake/HikariModules.cmake")
add_subdirectory(core)
add_subdirectory(application)
add_subdirectory(backends)
add_subdirectory(helpers/media)
add_subdirectory(helpers/lua)
add_subdirectory(ui)
add_subdirectory(app)
hikari_check_module_dependencies()
