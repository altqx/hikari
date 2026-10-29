add_library(hikari_a11y_testing STATIC a11y_tree.cpp)
target_include_directories(hikari_a11y_testing PUBLIC .)
target_link_libraries(hikari_a11y_testing PUBLIC Qt6::Gui Qt6::Quick)
