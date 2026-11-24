cmake_minimum_required(VERSION 3.20)
project(lib_css_tests LANGUAGES CXX)

find_package(HuxerUI 0.5.5 CONFIG REQUIRED COMPONENTS testing static HINTS "$ENV{HUXERUI_HOME}")
add_subdirectory(.. lib_css)

enable_testing()
add_executable(css_tests css.cpp)
set_target_properties(css_tests PROPERTIES CXX_EXTENSIONS OFF)
target_link_libraries(css_tests PRIVATE HuxerUI::CSS HuxerUI::testing)
add_test(NAME LibCssTests COMMAND css_tests)
