cmake_minimum_required (VERSION 3.0)
project(tioclient C)

set(CMAKE_CXX_STANDARD 11)

set(CMAKE_ARCHIVE_OUTPUT_DIRECTORY
    ../../bin
)

set(CMAKE_LIBRARY_OUTPUT_DIRECTORY
	../../bin
)

set(CMAKE_RUNTIME_OUTPUT_DIRECTORY
	../../bin
)

#add_compile_definitions(WIN32 _WINDOWS _USRDLL tioclientdll_EXPORTS)

add_library(tioclient STATIC 
	tioclient.c
	tioclient.h
	tioclient_internals.h
	tioclientdll.def 
)

