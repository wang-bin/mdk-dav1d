cmake_minimum_required(VERSION 3.15)

project(mdk-dav1d VERSION 0.22.0 DESCRIPTION "mdk dav1d plugin")
set(PROJECT_VERSION_TWEAK 0)

set(MDKSDK "${CMAKE_BINARY_DIR}/mdk-sdk" CACHE STRING "libmdk SDK dir")

if(NOT CMAKE_PROJECT_NAME STREQUAL mdk) # not build in source tree
    list(APPEND CMAKE_MODULE_PATH ${MDKSDK}/lib/cmake)
endif()
find_package(MDK)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

setup_mdk_plugin(${PROJECT_NAME} STATIC)

target_sources(${PROJECT_NAME} PRIVATE
    dav1d_api.cpp
    Dav1dDecoder.cpp
)

if(APPLE)
  if(CMAKE_SYSTEM_NAME STREQUAL Darwin) # link dylib first, then if link without loader(dav1d_api.cpp), dylib(not framework) will be used
    target_link_libraries(${PROJECT_NAME} PUBLIC -L${CMAKE_CURRENT_SOURCE_DIR} -weak-ldav1d)
  endif()
  target_link_libraries(${PROJECT_NAME} PUBLIC -L${CMAKE_CURRENT_SOURCE_DIR} "-weak_framework dav1d")
endif()
if(NOT WIN32)
  target_link_libraries(${PROJECT_NAME} PUBLIC dl)
endif()
