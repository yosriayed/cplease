cmake_minimum_required(VERSION 3.8)

project(cplease
	VERSION 0.1
	LANGUAGES CXX
	DESCRIPTION "a c++ async lib"
	HOMEPAGE_URL "https://github.com/yosriayed/cplease"
)

add_library(${PROJECT_NAME} INTERFACE)

target_compile_features(${PROJECT_NAME} INTERFACE cxx_std_20)
target_include_directories(${PROJECT_NAME} INTERFACE include)

option(BUILD_TESTS "Build tests" OFF)

if(BUILD_TESTS)

add_subdirectory(test)
 
endif() 
 