cmake_minimum_required(VERSION 2.8)

project(xmr-stak-config)

set(CMAKE_VERBOSE_MAKEFILE ON)
SET(CMAKE_CXX_FLAGS "-std=gnu++11")

include(FindPkgConfig)

if(DEFINED CMAKE_SETUP_SCRIPT)
	message("Loading Setup Script: ${CMAKE_SETUP_SCRIPT}")
	include(${CMAKE_SETUP_SCRIPT})
endif()

find_package(Boost REQUIRED COMPONENTS system filesystem date_time thread)

#pkg_check_modules(CORE REQUIRED jsoncpp)

include_directories(${CORE_INCLUDE_DIRS})

add_executable(xmr-stak-config xmr-stak-config.cpp)

target_link_libraries(xmr-stak-config ${Boost_LIBRARIES})
#target_link_libraries(xmr-stak-config ${CORE_LIBRARIES} ${Boost_LIBRARIES})
