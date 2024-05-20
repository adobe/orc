cmake_minimum_required(VERSION 3.14)

include(FetchContent)

set(CMAKE_OSX_ARCHITECTURES arm64;x86_64)

project(orc)

# Detects whether this is a top-level/root/standalone project
# This variable is already set by project() in cmake 3.21+
if (NOT DEFINED PROJECT_IS_TOP_LEVEL)
    get_directory_property(_has_parent PARENT_DIRECTORY)
    if (_has_parent)
        set(PROJECT_IS_TOP_LEVEL OFF)
    else()
        set(PROJECT_IS_TOP_LEVEL ON)
    endif()
    unset(_has_parent)
endif()

option(ORC_BUILD_EXAMPLES "Build ORC example programs" ${PROJECT_IS_TOP_LEVEL})

if (NOT ${CMAKE_SYSTEM_NAME} MATCHES "Darwin")
    set(ORC_BUILD_EXAMPLES FALSE)
endif()

####################################################################################################

if (NOT TARGET stlab::stlab)
    message(STATUS "ORC third-party: creating target 'stlab::stlab'...")
    FetchContent_Declare(
        stlab
        GIT_REPOSITORY https://github.com/stlab/libraries.git
        GIT_TAG        0a7232a4120c2daf8ddb6621ec13f313a029e495 # v1.6.2
    )
    FetchContent_MakeAvailable(stlab)
endif()

####################################################################################################

if (NOT TARGET tomlplusplus::tomlplusplus)
    message(STATUS "ORC third-party: creating target 'tomlplusplus::tomlplusplus'...")
    FetchContent_Declare(
        toml
        GIT_REPOSITORY https://github.com/marzer/tomlplusplus
        GIT_TAG        037bfdd21f794d7212616d5e6f4f8baab543c472 # v2.5.0
    )
    FetchContent_MakeAvailable(toml)
endif()

####################################################################################################
#
# Taken from https://json.nlohmann.me/integration/cmake/#fetchcontent
#

if (NOT TARGET nlohmann_json::nlohmann_json)
    message(STATUS "ORC third-party: creating target 'nlohmann_json::nlohmann_json'...")
    FetchContent_Declare(
        json
        URL
        https://github.com/nlohmann/json/releases/download/v3.11.3/json.tar.xz
    )
    FetchContent_MakeAvailable(json)
endif()

####################################################################################################
#
# Adds support for the Tracy profiler.
#

message(STATUS "ORC third-party: creating target 'TracyClient'...")

FetchContent_Declare(
    tracy
    GIT_REPOSITORY https://github.com/wolfpld/tracy
    GIT_TAG        37aff70dfa50cf6307b3fee6074d627dc2929143 # v0.10.0
)

if (${TRACY_ENABLE})
    message(STATUS "ORC profiling: ON")
else()
    message(STATUS "ORC profiling: OFF")
endif()

FetchContent_MakeAvailable(tracy)

####################################################################################################


if (NOT TARGET TBB::tbb)
    message(STATUS "ORC third-party: creating target 'TBB::tbb'...")
    FetchContent_Declare(
        tbb
        GIT_REPOSITORY https://github.com/oneapi-src/oneTBB.git
        GIT_TAG        3df08fe234f23e732a122809b40eb129ae22733f # 2021 Dec 22
    )
    set(TBB_TEST OFF) # See https://github.com/oneapi-src/oneTBB/blob/master/cmake/README.md
    # TBB generates `argument unused during compilation: '-mrtm'` when building for Apple Silicon
    # Turning strict off causes warnings to not be errors for the TBB sources, which works for us.
    set(TBB_STRICT OFF)
    set(BUILD_SHARED_LIBS OFF) # See https://github.com/oneapi-src/oneTBB/issues/297#issuecomment-772759422
    FetchContent_MakeAvailable(tbb)
    target_compile_options(tbb PUBLIC -Wno-error)
endif()

set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_XCODE_GENERATE_SCHEME OFF)
# This does not appear to be set via `CMAKE_CXX_STANDARD`; not sure why.
set(CMAKE_XCODE_ATTRIBUTE_CLANG_CXX_LANGUAGE_STANDARD "c++20")

file(GLOB SRC_FILES CONFIGURE_DEPENDS ${PROJECT_SOURCE_DIR}/src/*.cpp)
file(GLOB HEADER_FILES CONFIGURE_DEPENDS ${PROJECT_SOURCE_DIR}/include/orc/*.hpp)

add_executable(orc_orc
    ${SRC_FILES}
    ${HEADER_FILES}
)
add_executable(orc::orc ALIAS orc_orc)
set_target_properties(orc_orc PROPERTIES OUTPUT_NAME "orc")

add_executable(orc_dogfood
    ${SRC_FILES}
    ${HEADER_FILES}
)
add_executable(orc::dogfood ALIAS orc_dogfood)
set_target_properties(orc_dogfood PROPERTIES OUTPUT_NAME "orc_dogfood")

foreach(_TARGET_NAME IN ITEMS orc_orc orc_dogfood)

    if (PROJECT_IS_TOP_LEVEL)
        target_compile_options(${_TARGET_NAME} PRIVATE -Wall -Werror)
        set_target_properties(${_TARGET_NAME} PROPERTIES XCODE_GENERATE_SCHEME ON)
    endif()

    target_link_libraries(${_TARGET_NAME}
        PRIVATE
            stlab::stlab
            TBB::tbb
            tomlplusplus::tomlplusplus
            Tracy::TracyClient
            nlohmann_json::nlohmann_json
    )

    target_include_directories(${_TARGET_NAME}
        PRIVATE
            ${PROJECT_SOURCE_DIR}/include
    )

endforeach()

# This is the end of the ORC executable(s) definition

file(GLOB TEST_SRC_FILES CONFIGURE_DEPENDS ${PROJECT_SOURCE_DIR}/test/src/*.cpp)
list(REMOVE_ITEM SRC_FILES ${PROJECT_SOURCE_DIR}/src/main.cpp)
add_executable(orc_test
    ${SRC_FILES}
    ${HEADER_FILES}
    ${TEST_SRC_FILES}
)
add_executable(orc::test ALIAS orc_test)
target_include_directories(orc_test
    PRIVATE
        ${PROJECT_SOURCE_DIR}/include
)
target_link_libraries(orc_test
    PRIVATE
        stlab::stlab
        TBB::tbb
        tomlplusplus::tomlplusplus
        Tracy::TracyClient
        nlohmann_json::nlohmann_json
)
if (PROJECT_IS_TOP_LEVEL)
    target_compile_options(orc_test PRIVATE -Wall -Werror)
    set_target_properties(orc_test PROPERTIES XCODE_GENERATE_SCHEME ON)
    if (CMAKE_GENERATOR MATCHES "Xcode")
        set_property(TARGET orc_test PROPERTY XCODE_SCHEME_ARGUMENTS ${PROJECT_SOURCE_DIR}/test/battery/)
    endif()
endif()

# This is the end of the ORC test suite app

# This variable can be used by parent projects to import the link_via_orc helper function with
# include(${ORC_HELPERS})
set(ORC_HELPERS ${PROJECT_SOURCE_DIR}/orc_helpers.cmake)
if (NOT PROJECT_IS_TOP_LEVEL)
    set(ORC_HELPERS ${ORC_HELPERS} PARENT_SCOPE)
endif()

include(${ORC_HELPERS})

# The dogfood target is specifically to run ORC over itself.
if (CMAKE_GENERATOR MATCHES "Xcode")
    link_via_orc(orc_dogfood)
endif()

# These are example apps that uses ORC as its linker. We can add as many of these as necessary to
# test out the tool.

if (ORC_BUILD_EXAMPLES)

    ##### example app: vtable

    add_executable(example_vtable
                   ${PROJECT_SOURCE_DIR}/examples/vtable/main.cpp
                   ${PROJECT_SOURCE_DIR}/examples/vtable/one.cpp
                   ${PROJECT_SOURCE_DIR}/examples/vtable/two.cpp
                   ${PROJECT_SOURCE_DIR}/examples/vtable/object.cpp
                   ${PROJECT_SOURCE_DIR}/examples/vtable/object.hpp
    )
    link_via_orc(example_vtable)

endif()
