# Copyright (C) 2018-2023 Intel Corporation
# SPDX-License-Identifier: Apache-2.0
#

set(TARGET_NAME "paddle_tests")

ov_add_test_target(
    NAME ${TARGET_NAME}
        ROOT ${CMAKE_CURRENT_SOURCE_DIR}
        DEPENDENCIES
            paddle_test_models
            openvino_paddle_frontend
            paddle_fe_standalone_build_test
        LINK_LIBRARIES
            cnpy
            frontend_shared_test_classes
            openvino_paddle_frontend
            openvino::runtime
            gtest_main_manifest
            func_test_utils
        ADD_CLANG_FORMAT
        LABELS
            OV
            PADDLE_FE
)

# Test model generating
set(PADDLE_REQ "${CMAKE_CURRENT_SOURCE_DIR}/requirements.txt")
if(PYTHONINTERP_FOUND)
    execute_process(
        COMMAND ${PYTHON_EXECUTABLE} "${CMAKE_CURRENT_SOURCE_DIR}/paddle_pip_check.py" ${PADDLE_REQ}
        RESULT_VARIABLE EXIT_CODE
        OUTPUT_VARIABLE OUTPUT_TEXT
        ERROR_VARIABLE ERROR_TEXT)
endif()

if(NOT EXIT_CODE EQUAL 0)
    set(paddlepaddle_FOUND OFF)
    message(WARNING "Python requirement file ${PADDLE_REQ} is not installed, PaddlePaddle frontend unit tests will be skipped")
else()
    set(paddlepaddle_FOUND ON)
endif()

set(TEST_PADDLE_MODELS_DIRNAME test_model_zoo/paddle_test_models)
target_compile_definitions(${TARGET_NAME} PRIVATE -D TEST_PADDLE_MODELS_DIRNAME=\"${TEST_PADDLE_MODELS_DIRNAME}/\")

set(PADDLEDET_OPS_URL "https://raw.githubusercontent.com/PaddlePaddle/PaddleDetection/release/2.1/ppdet/modeling/ops.py")
set(PADDLEDET_DIRNAME ${CMAKE_CURRENT_BINARY_DIR}/thirdparty/PaddleDetection/ppdet/modeling/)
set(PADDLEDET_OPS_SHA256 "5cc079eda295ed78b58fba8223c51d85a931a7069ecad51c6af5c2fd26b7a8cb")

DownloadAndCheck(${PADDLEDET_OPS_URL} ${PADDLEDET_DIRNAME}/ops.py PADDLEDET_FATAL PADDLEDET_RESULT ${PADDLEDET_OPS_SHA256})

# If 'paddlepaddle' is not found, code will still be compiled
# but models will not be generated and tests will fail
# This is done this way for 'code style' and check cases - cmake shall pass, but CI machine doesn't need to have
# 'paddlepaddle' installed to check code style
if(paddlepaddle_FOUND AND PADDLEDET_RESULT)
    set(TEST_PADDLE_MODELS ${CMAKE_RUNTIME_OUTPUT_DIRECTORY}/${TEST_PADDLE_MODELS_DIRNAME}/)

    if(WIN32)
        set(SETENV "set")
        set(PATHSEP ";")
    else()
        set(SETENV "export")
        set(PATHSEP ":")
    endif()

    file(GLOB_RECURSE PADDLE_ALL_SCRIPTS ${CMAKE_CURRENT_SOURCE_DIR}/*.py)
    set(OUT_FILE ${TEST_PADDLE_MODELS}/generate_done.txt)
    add_custom_command(OUTPUT ${OUT_FILE}
            COMMAND ${SETENV} PYTHONPATH=${PADDLEDET_DIRNAME}${PATHSEP}$ENV{PYTHONPATH} && ${PYTHON_EXECUTABLE}
            ${CMAKE_CURRENT_SOURCE_DIR}/test_models/gen_wrapper.py
            ${CMAKE_CURRENT_SOURCE_DIR}/test_models/gen_scripts
            ${TEST_PADDLE_MODELS}
            DEPENDS ${PADDLE_ALL_SCRIPTS}
            )
    add_custom_target(paddle_test_models DEPENDS ${OUT_FILE})

    install(DIRECTORY ${TEST_PADDLE_MODELS}
            DESTINATION tests/${TEST_PADDLE_MODELS_DIRNAME}
            COMPONENT tests
            EXCLUDE_FROM_ALL)
else()
    # Produce warning message at build time as well
    add_custom_command(OUTPUT unable_build_paddle_models.txt
            COMMAND ${CMAKE_COMMAND}
            -E cmake_echo_color --red "Warning: Unable to generate PaddlePaddle test models. Running '${TARGET_NAME}' will likely fail"
            )
    add_custom_target(paddle_test_models DEPENDS unable_build_paddle_models.txt)
endif()

# Fuzzy tests for PaddlePaddle use IE_CPU engine
if(ENABLE_INTEL_CPU)
    add_dependencies(${TARGET_NAME} openvino_intel_cpu_plugin)
endif()

add_subdirectory(standalone_build)
