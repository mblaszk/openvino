# Copyright (C) 2018-2023  Intel Corporation
# SPDX-License-Identifier: Apache-2.0
#

set(TARGET_NAME "openvino_builders")

file(GLOB_RECURSE LIBRARY_SRC ${CMAKE_CURRENT_SOURCE_DIR}/src/*.cpp)
file(GLOB_RECURSE PUBLIC_HEADERS ${CMAKE_CURRENT_SOURCE_DIR}/include/*.hpp)

set(BUILDER_INCLUDE_DIR ${CMAKE_CURRENT_SOURCE_DIR}/include/)

# Create named folders for the sources within the .vcproj
# Empty name lists them directly under the .vcproj

source_group("src" FILES ${LIBRARY_SRC})
source_group("include" FILES ${PUBLIC_HEADERS})

# Create static library
add_library(${TARGET_NAME} STATIC ${LIBRARY_SRC} ${PUBLIC_HEADERS})

add_library(openvino::builders ALIAS ${TARGET_NAME})
set_target_properties(${TARGET_NAME} PROPERTIES EXPORT_NAME builders)

ie_faster_build(${TARGET_NAME}
    UNITY
    PCH PRIVATE "src/precomp.hpp")

target_include_directories(${TARGET_NAME} PUBLIC
    $<BUILD_INTERFACE:${BUILDER_INCLUDE_DIR}>
    $<BUILD_INTERFACE:${OV_CORE_INCLUDE_PATH}>)

if(NOT BUILD_SHARED_LIBS)
    target_compile_definitions(${TARGET_NAME} PUBLIC OPENVINO_STATIC_LIBRARY)
endif()

add_clang_format_target(${TARGET_NAME}_clang FOR_TARGETS ${TARGET_NAME})

ov_install_static_lib(openvino_builders ${OV_CPACK_COMP_CORE})

# developer package

openvino_developer_export_targets(COMPONENT core TARGETS openvino::builders)
