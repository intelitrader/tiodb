cmake_minimum_required (VERSION 3.13)
project (tiodb VERSION 1.6.0)

configure_file(${CMAKE_CURRENT_SOURCE_DIR}/tiodb_config.h.in ${CMAKE_CURRENT_BINARY_DIR}/tiodb_config.h)
include_directories(${CMAKE_CURRENT_BINARY_DIR})

if(WIN32)
    find_package(Boost 1.72.0 EXACT)
    if(NOT Boost_FOUND)
        message(FATAL_ERROR "Boost not found; download it in https://sourceforge.net/projects/boost/files/boost-binaries/1.72.0/boost_1_72_0-msvc-14.1-64.exe/download")
    endif()
endif()

add_subdirectory(client/c)
add_subdirectory(tests/tiobench)
add_subdirectory(server/tio)
add_subdirectory(madrinha)
add_subdirectory(tools/TioLogReplayCpp)

# testes do gtest quebrando: corrigir antes de descomentar
#option(PACKAGE_TESTS "Build the tests" ON)
#if(PACKAGE_TESTS)
    #enable_testing()
    #include(GoogleTest)
    #add_subdirectory(tests/unittest)
#endif()
