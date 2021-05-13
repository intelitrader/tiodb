cmake_minimum_required(VERSION 3.12)

set(Boost_USE_STATIC_LIBS ON)
set(Boost_USE_MULTITHREADED ON)
find_package(Boost 1.72 EXACT COMPONENTS program_options REQUIRED)

project(madrinha VERSION 1.0.0)

configure_file(${CMAKE_CURRENT_SOURCE_DIR}/madrinha_config.h.in ${CMAKE_CURRENT_BINARY_DIR}/madrinha_config.h)
include_directories(${CMAKE_CURRENT_BINARY_DIR})

add_library(${PROJECT_NAME}_lib ${PROJECT_NAME}.cpp ${PROJECT_NAME}.h)

target_include_directories(${PROJECT_NAME}_lib  PUBLIC ${Boost_INCLUDE_DIRS} 
					 ${CMAKE_CURRENT_SOURCE_DIR}/../client/cpp)

target_include_directories(${PROJECT_NAME}_lib INTERFACE ${CMAKE_CURRENT_SOURCE_DIR})


target_compile_features(${PROJECT_NAME}_lib PUBLIC cxx_std_14)

target_link_libraries(${PROJECT_NAME}_lib ${Boost_LIBRARIES} tioclient)

add_executable(${PROJECT_NAME})
target_sources(${PROJECT_NAME} PRIVATE
	main.cpp
	madrinha.cpp
	madrinha.h
)

target_link_libraries(${PROJECT_NAME} ${PROJECT_NAME}_lib)
