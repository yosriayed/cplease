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
option(WITH_STD_EXPECTED "Support std::expected specialisations" OFF)

if(WITH_STD_EXPECTED)
    message("Building with std::expected specialisations")
	target_compile_definitions(${PROJECT_NAME} INTERFACE WITH_STD_EXPECTED)
endif()

if(BUILD_TESTS)

add_subdirectory(test)

endif() 
