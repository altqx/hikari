file(GLOB bitlib_upstream_tests CONFIGURE_DEPENDS "${BITLIB_TEST_SOURCES}/*.cpp")
hikari_add_gtest(hikari_bitlib_upstream_tests SOURCES ${bitlib_upstream_tests} LIBRARIES bitlib::bitlib LABELS bitlib)
target_include_directories(hikari_bitlib_upstream_tests PRIVATE "${BITLIB_TEST_SOURCES}")
hikari_add_gtest(hikari_bitlib_tests SOURCES bit_vector_tests.cpp LIBRARIES bitlib::bitlib LABELS bitlib)
