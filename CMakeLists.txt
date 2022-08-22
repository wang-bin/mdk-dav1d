cmake_minimum_required(VERSION 3.1)
if(POLICY CMP0025)
# MUST call before project() and enable_language
  cmake_policy(SET CMP0025 NEW) # since 3.0, prefer AppleClang instead of Clang.
endif()
if(POLICY CMP0063) # visibility. since 3.3
  cmake_policy(SET CMP0063 NEW)
endif()

project(mdk-dav1d VERSION 0.11.0 DESCRIPTION "mdk dav1d plugin")
set(PROJECT_VERSION_TWEAK 0)
if(CMAKE_PROJECT_NAME STREQUAL mdk) # build in source tree
    include_directories(${CMAKE_SOURCE_DIR}/include ${CMAKE_SOURCE_DIR}/video/hwa ${CMAKE_SOURCE_DIR})
else()
  # TODO: FindMDKAbi
endif()
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

list(APPEND CMAKE_MODULE_PATH ${CMAKE_CURRENT_SOURCE_DIR}/cmake)

set(DEP_INCLUDE ${CMAKE_SOURCE_DIR}/external/include)
include_directories(${DEP_INCLUDE})

add_library(${PROJECT_NAME} OBJECT # STATIC: register decoder DCE
    dav1d_api.cpp
    Dav1dDecoder.cpp
)

target_compile_definitions(${PROJECT_NAME} PRIVATE BUILD_MDK_LIB)
if(APPLE)
  target_link_libraries(${PROJECT_NAME} PUBLIC -L${CMAKE_CURRENT_SOURCE_DIR} -weak-ldav1d)
endif()
if(NOT WIN32)
  target_link_libraries(${PROJECT_NAME} PUBLIC dl)
endif()
