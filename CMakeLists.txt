cmake_minimum_required(VERSION 3.12)

include(FetchContent)

project(orc)

FetchContent_Declare(
    stlab
    GIT_REPOSITORY https://github.com/stlab/libraries.git
    GIT_TAG        0a7232a4120c2daf8ddb6621ec13f313a029e495 # v1.6.2
)
FetchContent_Populate(stlab)

FetchContent_Declare(
    toml
    GIT_REPOSITORY https://github.com/marzer/tomlplusplus
    GIT_TAG        037bfdd21f794d7212616d5e6f4f8baab543c472 # v2.5.0
)
FetchContent_Populate(toml)

set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

file(GLOB SRC_FILES CONFIGURE_DEPENDS ${PROJECT_SOURCE_DIR}/src/*.cpp)

add_executable(orc ${SRC_FILES})

target_include_directories(orc PUBLIC
                           ${PROJECT_SOURCE_DIR}/include
                           ${stlab_SOURCE_DIR}
                           ${toml_SOURCE_DIR})

target_compile_options(orc PUBLIC -Wall -Werror)

# This is the end of the ORC executable definition

function(link_via_orc project)
    add_dependencies(${project} orc)
    set_target_properties(${project}
                          PROPERTIES
                          XCODE_ATTRIBUTE_ALTERNATE_LINKER "$<TARGET_FILE:orc>"
                          XCODE_ATTRIBUTE_LIBTOOL "$<TARGET_FILE:orc>")
endfunction()

# These are example apps that uses ORC as its linker. We can add as many of these as necessary to
# test out the tool. ORC is specified as a dependency of the example apps so it is built and ready
# to use at link-time. Note that the configuration is linked between debug/release of ORC and the
# example app (e.g., the Debug ORC will test Debug example_size_t, and so for Release.)

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
