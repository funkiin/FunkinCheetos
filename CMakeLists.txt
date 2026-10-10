cmake_minimum_required(VERSION 3.18)
project(FunkinCheetos VERSION 1.1 LANGUAGES C)

if(NOT WIN32 OR NOT CMAKE_SIZEOF_VOID_P EQUAL 8)
    message(FATAL_ERROR "Build this project for 64-bit Windows (MinGW-w64 x86_64 or MSVC x64).")
endif()

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)

set(CMAKE_RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin")

add_library(FunkinCheetos SHARED src/cheetos.c)
set_target_properties(FunkinCheetos PROPERTIES PREFIX "" OUTPUT_NAME "FunkinCheetos")

add_executable(FunkinLauncher src/launcher.c)
if(MINGW)
    target_link_options(FunkinLauncher PRIVATE -municode)
endif()

if(MINGW)
    target_link_options(FunkinCheetos PRIVATE -static-libgcc)
    target_link_options(FunkinLauncher PRIVATE -static-libgcc)
endif()
