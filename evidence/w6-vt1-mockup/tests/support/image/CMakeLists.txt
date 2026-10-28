# Test-only rendered-image comparison (E2-img). Never linked into shipping targets.
add_library(hikari_image_testing STATIC image_compare.cpp)
target_include_directories(hikari_image_testing PUBLIC .)
target_link_libraries(hikari_image_testing PUBLIC Qt6::Gui)
