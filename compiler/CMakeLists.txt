cmake_minimum_required(VERSION 3.22)
project(kas3 CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE Release)
endif()

option(KAS3_SANITIZE "Build everything with address and undefined-behaviour sanitizers" OFF)
if(KAS3_SANITIZE)
    add_compile_options(-fsanitize=address,undefined -fno-omit-frame-pointer -g)
    add_link_options(-fsanitize=address,undefined)
endif()

# The front end must be loadable into the Godot editor, which is built without exceptions or RTTI.
file(GLOB KAS3_SOURCES CONFIGURE_DEPENDS src/*.cpp)
add_library(kas3front STATIC ${KAS3_SOURCES})
target_include_directories(kas3front PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/src)
target_compile_options(kas3front PRIVATE -fno-exceptions -fno-rtti -Wall -Wextra -Werror)

enable_testing()
file(GLOB KAS3_TESTS CONFIGURE_DEPENDS tests/test_*.cpp)
foreach(test_source ${KAS3_TESTS})
    get_filename_component(test_name ${test_source} NAME_WE)
    add_executable(${test_name} ${test_source})
    target_link_libraries(${test_name} PRIVATE kas3front)
    target_include_directories(${test_name} PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/tests)
    target_compile_definitions(${test_name} PRIVATE KAS3_TESTS_DIR="${CMAKE_CURRENT_SOURCE_DIR}/tests")
    add_test(NAME ${test_name} COMMAND ${test_name})
endforeach()

# The command-line tool.
add_executable(kas3c tools/kas3c.cpp)
target_link_libraries(kas3c PRIVATE kas3front)
target_compile_options(kas3c PRIVATE -fno-exceptions -fno-rtti -Wall -Wextra -Werror)

find_package(Python3 COMPONENTS Interpreter REQUIRED)
add_test(NAME cli COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/tests/test_cli.py $<TARGET_FILE:kas3c>)
add_test(NAME golden COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/tests/run_golden.py
    $<TARGET_FILE:kas3c> ${CMAKE_CURRENT_SOURCE_DIR}/tests)
