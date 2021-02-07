cmake_minimum_required(VERSION 3.12)

add_subdirectory("${PROJECT_SOURCE_DIR}/submodules/googletest" "submodules/googletest")

set(Boost_USE_STATIC_LIBS ON)
set(Boost_USE_MULTITHREADED ON)
find_package(Boost 1.72.0 EXACT REQUIRED COMPONENTS chrono program_options)

macro(package_add_test TESTNAME)
    # create an exectuable in which the tests will be stored
    add_executable(${TESTNAME} ${ARGN})
    # link the Google test infrastructure, mocking library, and a default main fuction to
    # the test executable.  Remove g_test_main if writing your own main function.
    target_link_libraries(${TESTNAME} gtest gmock gtest_main ${Boost_LIBRARIES})
    target_include_directories(${TESTNAME} PRIVATE ${Boost_INCLUDE_DIRS})
    # gtest_discover_tests replaces gtest_add_tests,
    # see https://cmake.org/cmake/help/v3.10/module/GoogleTest.html for more options to pass to it
    gtest_discover_tests(${TESTNAME}
        # set a working directory so your project root so that you can find test data via paths relative to the project root
        WORKING_DIRECTORY ${PROJECT_DIR}
        PROPERTIES VS_DEBUGGER_WORKING_DIRECTORY "${PROJECT_DIR}"
    )
    set_target_properties(${TESTNAME} PROPERTIES FOLDER tests)
endmacro()

if (WIN32)
    add_definitions(-DINCL_WINSOCK_API_PROTOTYPES=1) # 0: unit tests, 1: functional tests
    package_add_test(tioclient_test tioclient_unittest.cpp)
	add_library(tioclient_test_lib STATIC tioclient_mock.c ../../client/c/tioclient.c)
	target_link_libraries(tioclient_test tioclient_test_lib)
endif()

package_add_test(madrinha_tests madrinha_unittest.cpp)
package_add_test(template_test template_unittest.cpp)
add_library(template_test_lib STATIC template_lib.cpp)
target_link_libraries(template_test template_test_lib)
