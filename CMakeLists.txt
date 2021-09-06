cmake_minimum_required(VERSION 3.12)

include(FetchContent)

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

if (NOT TARGET stlab::stlab)
    message(STATUS "ORC third-party: creating target 'stlab::stlab'...")
    FetchContent_Declare(
        stlab
        GIT_REPOSITORY https://github.com/stlab/libraries.git
        GIT_TAG        0a7232a4120c2daf8ddb6621ec13f313a029e495 # v1.6.2
    )
    FetchContent_MakeAvailable(stlab)
endif()

if (NOT TARGET tomlplusplus::tomlplusplus)
    message(STATUS "ORC third-party: creating target 'tomlplusplus::tomlplusplus'...")
    FetchContent_Declare(
        toml
        GIT_REPOSITORY https://github.com/marzer/tomlplusplus
        GIT_TAG        037bfdd21f794d7212616d5e6f4f8baab543c472 # v2.5.0
    )
    FetchContent_MakeAvailable(toml)
endif()

if (NOT TARGET TBB::tbb)
    message(STATUS "ORC third-party: creating target 'TBB::tbb'...")
    FetchContent_Declare(
        tbb
        GIT_REPOSITORY https://github.com/oneapi-src/oneTBB.git
        GIT_TAG        86fe3f04c1b319faecebdc8b642ecc896fbb2c3b # 2021 Aug 31
    )
    set(TBB_TEST FALSE) # See https://github.com/oneapi-src/oneTBB/blob/master/cmake/README.md
    FetchContent_MakeAvailable(tbb)
endif()

set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_XCODE_GENERATE_SCHEME OFF)

file(GLOB SRC_FILES CONFIGURE_DEPENDS ${PROJECT_SOURCE_DIR}/src/*.cpp)

add_executable(orc_orc ${SRC_FILES})
add_executable(orc::orc ALIAS orc_orc)

set_target_properties(orc_orc PROPERTIES OUTPUT_NAME "orc")

target_link_libraries(orc_orc
    PRIVATE
        stlab::stlab
        TBB::tbb
        tomlplusplus::tomlplusplus
)

target_include_directories(orc_orc
    PRIVATE
        ${PROJECT_SOURCE_DIR}/include
)

if (PROJECT_IS_TOP_LEVEL)
    target_compile_options(orc_orc PRIVATE -Wall -Werror)
    set_target_properties(orc_orc PROPERTIES XCODE_GENERATE_SCHEME ON)
endif()

# This is the end of the ORC executable definition

function(link_via_orc project)
    add_dependencies(${project} orc_orc)
    set_target_properties(${project}
                          PROPERTIES
                          XCODE_ATTRIBUTE_ALTERNATE_LINKER "$<TARGET_FILE:orc_orc>"
                          XCODE_ATTRIBUTE_LIBTOOL "$<TARGET_FILE:orc_orc>")
    set_target_properties(${project} PROPERTIES XCODE_GENERATE_SCHEME ON)
endfunction()

# These are example apps that uses ORC as its linker. We can add as many of these as necessary to
# test out the tool. ORC is specified as a dependency of the example apps so it is built and ready
# to use at link-time. Note that the configuration is linked between debug/release of ORC and the
# example app (e.g., the Debug ORC will test Debug example_size_t, and so for Release.)

if (ORC_BUILD_EXAMPLES)


    ##### example app: size_t

    add_executable(example_size_t
                   ${PROJECT_SOURCE_DIR}/examples/size_t/main.cpp
                   ${PROJECT_SOURCE_DIR}/examples/size_t/one.cpp
                   ${PROJECT_SOURCE_DIR}/examples/size_t/two.cpp
    )
    link_via_orc(example_size_t)

    ##### example app: vtable

    add_executable(example_vtable
                   ${PROJECT_SOURCE_DIR}/examples/vtable/main.cpp
                   ${PROJECT_SOURCE_DIR}/examples/vtable/one.cpp
                   ${PROJECT_SOURCE_DIR}/examples/vtable/two.cpp
                   ${PROJECT_SOURCE_DIR}/examples/vtable/object.cpp
                   ${PROJECT_SOURCE_DIR}/examples/vtable/object.hpp
    )
    link_via_orc(example_vtable)

    ##### example app: typedef

    add_executable(example_typedef
                   ${PROJECT_SOURCE_DIR}/examples/typedef/main.cpp
                   ${PROJECT_SOURCE_DIR}/examples/typedef/alt.cpp
    )
    link_via_orc(example_typedef)

    ##### example app: function

    add_executable(example_function
                   ${PROJECT_SOURCE_DIR}/examples/function/main.cpp
                   ${PROJECT_SOURCE_DIR}/examples/function/alt.cpp
    )
    link_via_orc(example_function)
endif()