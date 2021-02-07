cmake_minimum_required (VERSION 3.13)
project (tio VERSION 1.3.2)

set(Boost_USE_STATIC_LIBS ON)
set(Boost_USE_MULTITHREADED      ON)

find_package(Boost 1.72.0 EXACT REQUIRED COMPONENTS date_time filesystem system thread program_options regex)
find_package(Threads)

add_executable(tio)

configure_file(${CMAKE_CURRENT_SOURCE_DIR}/tio_config.h.in ${CMAKE_CURRENT_BINARY_DIR}/tio_config.h)
include_directories(${CMAKE_CURRENT_BINARY_DIR})

target_compile_features(tio PUBLIC cxx_std_14)

if (WIN32)
  if ("${CMAKE_BUILD_TYPE}" STREQUAL "PROFILE_GEN")
    target_compile_options(tio PRIVATE "/GL")
    set_target_properties(tio PROPERTIES LINK_FLAGS " /LTCG /GENPROFILE")
  elseif ("${CMAKE_BUILD_TYPE}" STREQUAL "PROFILE_BUILD")
    target_compile_options(tio PRIVATE "/GL")
    set_target_properties(tio PROPERTIES LINK_FLAGS "/PGD:${PROJECT_BINARY_DIR}/${CMAKE_CFG_INTDIR}/tio.pgd /LTCG:PGOptimize")
  endif()
endif (WIN32)

if(Boost_FOUND)
  target_include_directories(tio PRIVATE ${Boost_INCLUDE_DIRS})
endif()

target_include_directories(tio PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})

target_sources(tio PRIVATE 
			auth.h
			buffer.h
			BinaryProtocolLogger.h
			Command.h
			tio_config.h
			Container.h
			ContainerManager.h
			FileSystemStorage.h
			HttpParser.h
			ListStorage.h
			logdb.h
			LogDbStorage.h
			MapStorage.h
			MemoryPropertyMap.h
			MemoryStorage.h
			pch.h
			resource.h
			TioPython.h
			TioTcpClient.h
			TioTcpProtocol.h
			TioTcpServer.h
			TioTcpSession.h
			VectorStorage.h


			pch.cpp
			tiomutex.cpp
			tio.cpp	
			Command.cpp
			ContainerManager.cpp
			TioTcpServer.cpp
			TioTcpSession.cpp
			../../submodules/http-parser/http_parser.c
			../../client/c/tioclient.c

            tio.rc
			)

#include_directories(${PROJECT_SOURCE_DIR}/server/tio)

# WARNING: CMAKE_ADDITIONAL_LIBS added to be used on Azure
target_link_libraries(tio PUBLIC ${Boost_LIBRARIES} ${CMAKE_THREAD_LIBS_INIT} ${CMAKE_ADDITIONAL_LIBS})
