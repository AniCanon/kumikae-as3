cmake_minimum_required(VERSION 3.22)
project(kas3_spike CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE Release)
endif()

# Flags a release export of generated code would use, and nothing more.
set(KAS3_FLAGS -O2 -fno-exceptions -fno-rtti -ffp-contract=off)

set(GODOTCPP_API_VERSION "4.7" CACHE STRING "" FORCE)
set(GODOTCPP_TARGET "template_release" CACHE STRING "" FORCE)
add_subdirectory(third_party/godot-cpp EXCLUDE_FROM_ALL)

add_library(kas3_spike SHARED host/register.cpp)
target_compile_options(kas3_spike PRIVATE ${KAS3_FLAGS})
target_include_directories(kas3_spike PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(kas3_spike PRIVATE godot-cpp)
set_target_properties(kas3_spike PROPERTIES
    LIBRARY_OUTPUT_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}/project/bin
    OUTPUT_NAME kas3_spike.macos.arm64)

# The AS3-shaped runtime core and its tests. Neither links Godot.
add_library(as3rt STATIC runtime/as3rt.cpp)
target_compile_options(as3rt PRIVATE ${KAS3_FLAGS} -fPIC)
target_include_directories(as3rt PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(kas3_spike PRIVATE as3rt)

enable_testing()
add_executable(as3rt_tests tests/runtime_tests.cpp)
target_compile_options(as3rt_tests PRIVATE ${KAS3_FLAGS})
target_link_libraries(as3rt_tests PRIVATE as3rt)
add_test(NAME as3rt_tests COMMAND as3rt_tests)
