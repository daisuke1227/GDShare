cmake_minimum_required(VERSION 3.21.0)
project(GDShare VERSION 1.0.1 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_VISIBILITY_PRESET hidden)

if (IOS OR "${CMAKE_SYSTEM_NAME}" STREQUAL "iOS")
    set(CMAKE_OSX_ARCHITECTURES "arm64")
else()
    set(CMAKE_OSX_ARCHITECTURES "arm64;x86_64")
endif()

if (NOT DEFINED ENV{GEODE_SDK})
    message(FATAL_ERROR "Please define GEODE_SDK to point to your Geode SDK checkout")
endif()

add_subdirectory($ENV{GEODE_SDK} ${CMAKE_BINARY_DIR}/geode)

file(GLOB_RECURSE MOD_SOURCES ${CMAKE_CURRENT_LIST_DIR}/src/*.cpp)
set(GMD_API_DIR ${CMAKE_CURRENT_LIST_DIR}/hjfod.gmd-api)
file(GLOB_RECURSE GMD_API_SOURCES ${GMD_API_DIR}/src/*.cpp)

add_library(${PROJECT_NAME} SHARED
    ${MOD_SOURCES}
    ${GMD_API_SOURCES}
)

target_compile_definitions(${PROJECT_NAME}
    PRIVATE
        GMDAPI_BUILD
)

target_include_directories(${PROJECT_NAME}
    PRIVATE
        ${GMD_API_DIR}/include
)

target_link_libraries(${PROJECT_NAME}
    geode-sdk
)

setup_geode_mod(${PROJECT_NAME}
    EXTERNALS
        geode.node-ids:1.12.0
        hjfod.gmd-api:1.4.0
)

install(TARGETS ${PROJECT_NAME}
    LIBRARY DESTINATION .
)