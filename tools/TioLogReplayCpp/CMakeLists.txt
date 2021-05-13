cmake_minimum_required(VERSION 3.12.0)

project(TioLogReplayCpp VERSION 1.0.0)

add_executable(TioLogReplay)

configure_file(${CMAKE_CURRENT_SOURCE_DIR}/TioLogReplayConfig.h.in ${CMAKE_CURRENT_BINARY_DIR}/TioLogReplayConfig.h)
include_directories(${CMAKE_CURRENT_BINARY_DIR})

target_sources(TioLogReplay PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/../../client/c/tioclient.c
    TioLogReplay.cpp
    TioLogReplayConfig.h
    TioLogReplay.rc
)

target_include_directories(TioLogReplay PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/../../client/c
    ${CMAKE_CURRENT_SOURCE_DIR}/../../client/cpp
)
