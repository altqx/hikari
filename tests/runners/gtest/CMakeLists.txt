# Controls proving the GoogleTest runner reports both outcomes correctly.
hikari_add_gtest(hikari_gtest_controls SOURCES controls.cpp LABELS control)

# A deliberately failing case, run on its own and expected to fail: if CTest
# ever reports it as passing, failure reporting is broken.
add_executable(hikari_gtest_failing_control failing_control.cpp)
target_link_libraries(hikari_gtest_failing_control PRIVATE GTest::gtest_main)
add_test(NAME gtest.failing-control COMMAND hikari_gtest_failing_control)
set_tests_properties(gtest.failing-control PROPERTIES WILL_FAIL TRUE LABELS "gtest;control" TIMEOUT 60)
