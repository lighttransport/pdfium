cmake_minimum_required(VERSION 3.15)

project(PDFium 
        VERSION 1.0
        DESCRIPTION "PDFium - PDF rendering engine"
        LANGUAGES CXX C)

# Set C++20 as default
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# Build type if not specified
if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE Release)
endif()

# Options matching GN configuration
option(PDFIUM_ENABLE_V8 "Enable V8 JavaScript support" OFF)
option(PDFIUM_ENABLE_XFA "Enable XFA forms support" OFF)
option(PDFIUM_USE_SKIA "Use Skia rendering backend" OFF)
option(PDFIUM_ENABLE_FONTATIONS "Enable Fontations backend" OFF)
option(PDFIUM_IS_STANDALONE "Build PDFium in standalone mode" ON)

# Dependencies
include(FetchContent)

if(PDFIUM_ENABLE_V8)
    message(STATUS "V8 JavaScript support enabled")
    # TODO: Add V8 dependency
endif()

if(PDFIUM_USE_SKIA)
    message(STATUS "Skia rendering backend enabled")
    # TODO: Add Skia dependency
endif()

# Core library directories
add_subdirectory(core)
add_subdirectory(fpdfsdk)
add_subdirectory(constants)
add_subdirectory(public)

# Optional components
if(PDFIUM_ENABLE_XFA)
    add_subdirectory(xfa)
endif()

if(PDFIUM_ENABLE_V8)
    add_subdirectory(fxjs)
endif()

# Testing
include(CTest)
if(BUILD_TESTING)
    add_subdirectory(testing)
endif()