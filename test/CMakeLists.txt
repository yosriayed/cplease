project(cplease-tests)

include(cmake/get_cpm.cmake)

set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

CPMAddPackage("gh:catchorg/Catch2@3.5.3")

enable_testing()

add_executable(${PROJECT_NAME} ) 

set(SRCS
    main.test.cpp  

    type_traits.test.cpp

    future.test.cpp
    async_tasks.test.cpp

    circbuff.test.cpp 
    channel.test.cpp
)

if(WITH_STD_EXPECTED) 
    list(APPEND SRCS expected_future.test.cpp async_expected_tasks.test.cpp)
endif()

target_sources(${PROJECT_NAME} PRIVATE ${SRCS})


target_link_libraries(${PROJECT_NAME} PRIVATE Catch2::Catch2WithMain cplease)

list(APPEND CMAKE_MODULE_PATH ${Catch2_SOURCE_DIR}/extras)

include(CTest)
include(Catch)

catch_discover_tests(${PROJECT_NAME})