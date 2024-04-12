cmake_minimum_required(VERSION 3.28)

project(cplease
	VERSION 0.1
	LANGUAGES CXX
	DESCRIPTION "a c++ async lib"
	HOMEPAGE_URL "https://github.com/yosriayed/cplease"
)

set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

add_library(${PROJECT_NAME} INTERFACE)

target_compile_features(${PROJECT_NAME} INTERFACE cxx_std_23)
target_include_directories(${PROJECT_NAME} INTERFACE include)

include(CheckIncludeFileCXX)

function(check_include_file_cxx_and_add_def hdr def)
    check_include_file_cxx(${hdr} ${def})
    message(STATUS "Checking for header ${hdr}")

    if(${def})
        add_definitions(-D${def})
    endif()
    message(STATUS " header ${def}")

endfunction()

check_include_file_cxx_and_add_def(expected HAVE_STD_EXPECTED)

message(STATUS "HAVE_STD_EXPECTED: ${HAVE_STD_EXPECTED}")

option(BUILD_TESTS "Build tests" OFF)

if(BUILD_TESTS)

add_subdirectory(test)

endif() 
