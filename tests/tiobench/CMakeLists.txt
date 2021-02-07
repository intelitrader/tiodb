cmake_minimum_required(VERSION 3.13.0)

project(tiobench VERSION 2.0.0)

set(Boost_USE_STATIC_LIBS        ON) 
set(Boost_USE_MULTITHREADED      ON)

find_package(Boost 1.72.0 EXACT REQUIRED COMPONENTS thread program_options filesystem regex)
find_package(Threads)

add_executable(tiobench)

configure_file(${CMAKE_CURRENT_SOURCE_DIR}/tiobench_config.h.in ${CMAKE_CURRENT_BINARY_DIR}/tiobench_config.h)
include_directories(${CMAKE_CURRENT_BINARY_DIR})

target_compile_features(tiobench PUBLIC cxx_std_14)

if(Boost_FOUND)
  target_include_directories(tiobench PRIVATE ${Boost_INCLUDE_DIRS})
endif()

target_sources(tiobench PRIVATE tiobench.cpp tiobench.rc)

target_link_directories(tiobench PRIVATE ${CMAKE_CURRENT_BINARY_DIR})

target_link_libraries(tiobench PRIVATE ${Boost_LIBRARIES} ${CMAKE_THREAD_LIBS_INIT} tioclient)

if(WIN32)
  target_compile_options(tiobench PRIVATE -D_WIN32_WINNT=0x0501)
endif(WIN32)

install (TARGETS tiobench 
            CONFIGURATIONS Release
            RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
            LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
            ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR}
            COMPONENT "runtime"
            )